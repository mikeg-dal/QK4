#include "logging/logsink.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QThread>
#include <chrono>

namespace Logging {

namespace {

thread_local bool t_realtime = false;
// Set while this thread is writing a batch. A message raised by the writing itself (a QFile
// warning) goes straight to the console instead of back into the queue.
thread_local bool t_draining = false;

std::thread::id g_mainThread;

const QString &currentThreadName() {
    thread_local QString name;
    thread_local bool known = false;
    if (!known) {
        known = true;
        if (std::this_thread::get_id() == g_mainThread) {
            name = QStringLiteral("main");
        } else {
            QThread *thread = QThread::currentThread();
            name = thread ? thread->objectName() : QString();
            if (name.isEmpty())
                name = QStringLiteral("t%1").arg(quintptr(QThread::currentThreadId()), 0, 16);
        }
    }
    return name;
}

// WHY: QtMsgType is not ordered by severity - QtInfoMsg was added last and is the largest value.
bool isWarningOrWorse(QtMsgType type) {
    return type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg;
}

char typeLetter(QtMsgType type) {
    switch (type) {
    case QtDebugMsg:
        return 'D';
    case QtInfoMsg:
        return 'I';
    case QtWarningMsg:
        return 'W';
    case QtCriticalMsg:
        return 'C';
    case QtFatalMsg:
        return 'F';
    }
    return '?';
}

QString timestamp(qint64 wallMs) {
    const QDateTime when = QDateTime::fromMSecsSinceEpoch(wallMs);
    const int offset = when.offsetFromUtc();
    const int minutes = qAbs(offset) / 60;
    return when.toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzz")) + QLatin1Char(offset < 0 ? '-' : '+') +
           QStringLiteral("%1:%2")
               .arg(minutes / 60, 2, 10, QLatin1Char('0'))
               .arg(minutes % 60, 2, 10, QLatin1Char('0'));
}

const char *baseName(const char *path) {
    if (!path)
        return nullptr;
    const char *base = path;
    for (const char *p = path; *p; ++p) {
        if (*p == '/' || *p == '\\')
            base = p + 1;
    }
    return base;
}

// WHY: kqueue cannot report exceptional conditions, so Qt's own socket code warns about it on
// macOS for every QSocketNotifier of that type. It is noise, and it was already filtered before
// this sink existed.
bool isKnownNoise(const QString &message) {
#ifdef Q_OS_MACOS
    return message.contains(QLatin1String("QSocketNotifier::Exception is not supported"));
#else
    Q_UNUSED(message);
    return false;
#endif
}

} // namespace

LogSink::LogSink(std::size_t capacity, int tickMs) : m_queue(capacity), m_spare(capacity), m_tickMs(tickMs) {}

LogSink::~LogSink() {
    stop();
}

void LogSink::markRealtimeThread() {
    t_realtime = true;
}

bool LogSink::isRealtimeThread() {
    return t_realtime;
}

void LogSink::post(QtMsgType type, const QMessageLogContext &context, const QString &message) {
    if (t_draining || m_stopped.load(std::memory_order_acquire)) {
        if (m_console)
            m_console(type, context, message);
        return;
    }

    LogRecord record;
    record.wallMs = QDateTime::currentMSecsSinceEpoch();
    record.type = type;
    record.category = context.category;
    record.file = context.file;
    record.function = context.function;
    record.line = context.line;
    record.thread = currentThreadName();
    record.message = message;

    const bool realtime = t_realtime;
    m_queue.push(std::move(record), realtime);

    if (type == QtFatalMsg) {
        // Qt aborts as soon as this returns: write everything now, on this thread.
        drain();
        if (m_console && !consoleEnabled())
            m_console(type, context, redact(message));
        return;
    }
    // WHY: a real-time thread never wakes the writer. notify_one can enter the kernel; the writer's
    // own tick collects the record within m_tickMs instead.
    if (!realtime && isWarningOrWorse(type) && m_running.load(std::memory_order_acquire)) {
        {
            std::lock_guard<std::mutex> lock(m_wakeMutex);
            m_wakeRequested = true;
        }
        m_wake.notify_one();
    }
}

void LogSink::start() {
    if (m_running.exchange(true))
        return;
    m_thread = std::thread([this] { run(); });
}

void LogSink::run() {
    std::unique_lock<std::mutex> lock(m_wakeMutex);
    for (;;) {
        m_wake.wait_for(lock, std::chrono::milliseconds(m_tickMs),
                        [this] { return m_wakeRequested || m_stopRequested; });
        m_wakeRequested = false;
        const bool stopping = m_stopRequested;
        lock.unlock();
        drain();
        if (stopping)
            return;
        lock.lock();
    }
}

void LogSink::stop() {
    if (m_stopped.load(std::memory_order_acquire))
        return;
    if (m_running.load(std::memory_order_acquire) && m_thread.joinable()) {
        {
            std::lock_guard<std::mutex> lock(m_wakeMutex);
            m_stopRequested = true;
        }
        m_wake.notify_one();
        m_thread.join();
    }
    drain();
    {
        std::lock_guard<std::mutex> lock(m_fileMutex);
        closeFileLocked(CleanFooter);
    }
    m_stopped.store(true, std::memory_order_release);
}

void LogSink::flush() {
    drain();
}

void LogSink::drain() {
    std::lock_guard<std::mutex> fileLock(m_fileMutex);
    drainLocked();
}

void LogSink::drainLocked() {
    const bool wasDraining = t_draining;
    t_draining = true;

    const std::size_t n = m_queue.take(m_spare);
    const quint64 dropped = m_queue.takeDropped();
    std::unique_lock<std::mutex> secretLock(m_secretMutex);
    const bool redacting = !m_secrets.isEmpty();
    secretLock.unlock();
    for (std::size_t i = 0; i < n; ++i) {
        LogRecord &record = m_spare[i];
        if (isKnownNoise(record.message))
            continue;
        if (redacting)
            record.message = redact(record.message);
        if (m_file.isOpen())
            writeLine(format(record));
        mirror(record);
    }
    LogQueue::release(m_spare, n);
    if (dropped > 0) {
        const QString line = QStringLiteral("%1 W [logging] %2 messages dropped (queue full or contended)")
                                 .arg(timestamp(QDateTime::currentMSecsSinceEpoch()))
                                 .arg(dropped);
        if (m_file.isOpen())
            writeLine(line);
    }
    if (m_file.isOpen())
        m_file.flush();

    t_draining = wasDraining;
}

void LogSink::registerSecret(const QString &secret) {
    if (secret.size() < MinSecretLength)
        return;
    std::lock_guard<std::mutex> lock(m_secretMutex);
    if (!m_secrets.contains(secret))
        m_secrets << secret;
}

// WHY in the writer and not the producer: redaction is a search per secret per line, which is
// exactly the work the handler must not do on the thread that logged.
QString LogSink::redact(const QString &message) const {
    std::lock_guard<std::mutex> lock(m_secretMutex);
    QString out = message;
    for (const QString &secret : m_secrets)
        out.replace(secret, QStringLiteral("<redacted>"));
    return out;
}

QString LogSink::format(const LogRecord &record) const {
    QString line = timestamp(record.wallMs);
    line += u' ';
    line += QLatin1Char(typeLetter(record.type));
    line += QStringLiteral(" [");
    line += record.thread;
    line += QStringLiteral("] ");
    line += QLatin1String(record.category ? record.category : "default");
    line += QStringLiteral(": ");
    line += record.message;
    if (isWarningOrWorse(record.type) && record.file && record.line > 0)
        line += QStringLiteral(" (%1:%2)").arg(QLatin1String(baseName(record.file))).arg(record.line);
    return line;
}

void LogSink::mirror(const LogRecord &record) {
    if (!m_console || !consoleEnabled())
        return;
    const QMessageLogContext context(record.file, record.line, record.function, record.category);
    m_console(record.type, context, record.message);
}

void LogSink::writeLine(const QString &line) {
    QByteArray bytes = line.toUtf8();
    bytes += '\n';
    writeRaw(bytes);
}

void LogSink::writeRaw(const QByteArray &bytes) {
    if (!m_file.isOpen())
        return;
    if (m_written > 0 && m_written + bytes.size() > m_fileOptions.maxBytes && m_fileOptions.fileCount > 1) {
        m_file.write(QByteArray("=== continued in the next file\n"));
        m_file.close();
        rotateLocked();
        if (m_file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            m_written = 0;
            const QByteArray header =
                QStringLiteral("=== log continued from %1\n").arg(filePathLocked(1).section(u'/', -1)).toUtf8();
            m_written += m_file.write(header);
        }
    }
    const qint64 n = m_file.write(bytes);
    if (n > 0)
        m_written += n;
}

QString LogSink::filePathLocked(int index) const {
    const QString name =
        index == 0 ? m_baseName + QStringLiteral(".log") : QStringLiteral("%1.%2.log").arg(m_baseName).arg(index);
    return m_fileOptions.directory + u'/' + name;
}

void LogSink::rotateLocked() {
    const int last = qMax(1, m_fileOptions.fileCount - 1);
    QFile::remove(filePathLocked(last));
    for (int i = last - 1; i >= 0; --i) {
        if (QFile::exists(filePathLocked(i)))
            QFile::rename(filePathLocked(i), filePathLocked(i + 1));
    }
}

bool LogSink::setFile(const FileOptions &options) {
    std::lock_guard<std::mutex> lock(m_fileMutex);
    // Everything queued so far belongs with the file as it was. With no file open yet it is kept
    // for the new one, so the lines from startup land in it.
    if (m_file.isOpen())
        drainLocked();
    const bool wasDraining = t_draining;
    t_draining = true;

    closeFileLocked(SwitchedOffFooter);
    m_fileOptions = options;
    bool ok = true;
    if (!options.directory.isEmpty()) {
        openFileLocked(true);
        ok = m_file.isOpen();
    }

    t_draining = wasDraining;
    return ok;
}

QString LogSink::currentFile() const {
    std::lock_guard<std::mutex> lock(m_fileMutex);
    return m_file.isOpen() ? m_file.fileName() : QString();
}

void LogSink::openFileLocked(bool rotate) {
    if (!QDir().mkpath(m_fileOptions.directory))
        return;

    // WHY: a second QK4 running at the same time must not rotate the first one's open file away.
    // It writes its own qk4-<pid>.log instead. staleLockTime 0 because the lock is held for the
    // whole session; a crashed owner is detected by its process id.
    m_lock = std::make_unique<QLockFile>(m_fileOptions.directory + QStringLiteral("/.qk4.lock"));
    m_lock->setStaleLockTime(0);
    const bool owner = m_lock->tryLock(0);
    if (!owner)
        m_lock.reset();
    m_baseName = owner ? QStringLiteral("qk4") : QStringLiteral("qk4-%1").arg(QCoreApplication::applicationPid());

    bool previousEndedCleanly = true;
    if (owner && rotate && QFileInfo(filePathLocked(0)).size() > 0) {
        rotateLocked();
        QFile previous(filePathLocked(1));
        if (previous.open(QIODevice::ReadOnly)) {
            const qint64 tail = qMin<qint64>(previous.size(), 4096);
            previous.seek(previous.size() - tail);
            previousEndedCleanly = previous.read(tail).contains("=== log closed");
        }
    }

    m_file.setFileName(filePathLocked(0));
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        m_lock.reset();
        return;
    }
    m_written = 0;
    writeLine(QStringLiteral("=== QK4 log opened %1").arg(timestamp(QDateTime::currentMSecsSinceEpoch())));
    for (const QString &line : std::as_const(m_fileOptions.header))
        writeLine(line);
    if (!previousEndedCleanly) {
        QString line =
            QStringLiteral("previous session did not end cleanly - see %1").arg(filePathLocked(1).section(u'/', -1));
#ifdef Q_OS_MACOS
        line += QStringLiteral(" and ~/Library/Logs/DiagnosticReports");
#endif
        writeLine(line);
    }
    m_file.flush();
}

void LogSink::closeFileLocked(const char *footer) {
    if (m_file.isOpen()) {
        writeLine(QStringLiteral("%1 %2").arg(QLatin1String(footer), timestamp(QDateTime::currentMSecsSinceEpoch())));
        m_file.close();
    }
    m_lock.reset();
}

// ---------------------------------------------------------------------------------------------

namespace {
LogSink *g_sink = nullptr;
QtMessageHandler g_previous = nullptr;

void handler(QtMsgType type, const QMessageLogContext &context, const QString &message) {
    g_sink->post(type, context, message);
}
} // namespace

LogSink *install() {
    if (g_sink)
        return g_sink;
    g_mainThread = std::this_thread::get_id();
    g_sink = new LogSink();
    g_previous = qInstallMessageHandler(handler);
    g_sink->setConsoleHandler(g_previous);
    return g_sink;
}

LogSink *sink() {
    return g_sink;
}

void shutdown() {
    if (!g_sink)
        return;
    g_sink->stop();
    qInstallMessageHandler(g_previous);
    // WHY: deliberately leaked. A thread still logging during static destruction may hold the
    // pointer it read before the handler was swapped back; a stopped sink forwards to the console.
}

void markRealtimeThread() {
    LogSink::markRealtimeThread();
}

void registerSecret(const QString &secret) {
    if (g_sink)
        g_sink->registerSecret(secret);
}

void markRealtimeOnStart(QThread *thread) {
    QObject::connect(thread, &QThread::started, thread, [] { LogSink::markRealtimeThread(); }, Qt::DirectConnection);
}

} // namespace Logging
