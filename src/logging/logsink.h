#ifndef LOGGING_LOGSINK_H
#define LOGGING_LOGSINK_H

#include "logging/logqueue.h"

#include <QFile>
#include <QLockFile>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

class QThread;

namespace Logging {

// Receives every Qt log message and writes it from its own normal-priority thread, so no thread
// that logs ever formats a line, touches a file or waits on the console.
//
// The handler side (post) only copies the message into a preallocated LogQueue. The writer thread
// wakes on its own tick (and early for a warning from a non-real-time thread), formats, writes the
// optional log file with rotation, and mirrors to the previous message handler (Qt's console
// output) when asked to.
class LogSink {
public:
    static constexpr qint64 DefaultMaxBytes = 10 * 1024 * 1024;
    static constexpr int DefaultFileCount = 5;

    struct FileOptions {
        QString directory;  // empty: no log file
        QStringList header; // written after the first line of each new file
        qint64 maxBytes = DefaultMaxBytes;
        int fileCount = DefaultFileCount; // qk4.log plus qk4.1.log ... qk4.(fileCount-1).log
    };

    explicit LogSink(std::size_t capacity = 8192, int tickMs = 200);
    ~LogSink();
    LogSink(const LogSink &) = delete;
    LogSink &operator=(const LogSink &) = delete;

    // Where the console mirror goes; nullptr discards it. Set before start().
    void setConsoleHandler(QtMessageHandler handler) { m_console = handler; }
    void setConsoleEnabled(bool enabled) { m_consoleEnabled.store(enabled, std::memory_order_relaxed); }
    bool consoleEnabled() const { return m_consoleEnabled.load(std::memory_order_relaxed); }

    // Starts the writer. Records posted before this or before the first setFile() are kept (up to
    // the queue's capacity) and land in the file.
    void start();

    // Opens a log file in options.directory, rotating the previous one, or closes the current file
    // when the directory is empty. Returns false if the file cannot be opened. Synchronous: when it
    // returns, the file exists (or is closed).
    bool setFile(const FileOptions &options);
    QString currentFile() const;

    // Writes everything queued so far, then the footer, and stops the writer. Idempotent.
    void stop();

    // Writes everything posted so far, on the calling thread. For tests and the fatal path.
    void flush();

    // The handler body. Safe from any thread, at any time.
    void post(QtMsgType type, const QMessageLogContext &context, const QString &message);

    // Every later occurrence of `secret` is written as "<redacted>", in the file and on the console.
    // Strings shorter than MinSecretLength are ignored: redacting "K4" would wreck every line.
    static constexpr int MinSecretLength = 4;
    void registerSecret(const QString &secret);

    // Real-time producers never wait for the queue lock and never wake the writer.
    static void markRealtimeThread();
    static bool isRealtimeThread();

    static constexpr const char *CleanFooter = "=== log closed: session ended cleanly";
    static constexpr const char *SwitchedOffFooter = "=== log closed: file logging switched off";

private:
    void run();
    void drain();
    void drainLocked();
    void writeLine(const QString &line);
    void writeRaw(const QByteArray &bytes);
    void openFileLocked(bool rotate);
    void closeFileLocked(const char *footer);
    void rotateLocked();
    QString filePathLocked(int index) const;
    void mirror(const LogRecord &record);
    QString format(const LogRecord &record) const;

    QString redact(const QString &message) const;

    mutable std::mutex m_secretMutex;
    QStringList m_secrets;

    LogQueue m_queue;
    std::vector<LogRecord> m_spare;
    const int m_tickMs;
    QtMessageHandler m_console = nullptr;
    std::atomic<bool> m_consoleEnabled{true};

    std::thread m_thread;
    std::mutex m_wakeMutex;
    std::condition_variable m_wake;
    bool m_wakeRequested = false;
    bool m_stopRequested = false;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopped{false};

    // Held while a batch is written and while the file is opened, rotated or closed.
    mutable std::mutex m_fileMutex;
    FileOptions m_fileOptions;
    QFile m_file;
    QString m_baseName;
    qint64 m_written = 0;
    std::unique_ptr<QLockFile> m_lock;
};

// The process-wide sink behind qInstallMessageHandler. install() is the first line of main();
// messages from then on are queued, and written once start() runs.
LogSink *install();
LogSink *sink();
// Stops the sink and restores the previous handler, so messages during static destruction still
// reach the console.
void shutdown();
void markRealtimeThread();
// Registers with the installed sink; does nothing before install().
void registerSecret(const QString &secret);
// Marks `thread` real-time as it starts, before anything connected to started() later runs.
// Connect it before any started() connection that enters a loop.
void markRealtimeOnStart(QThread *thread);

} // namespace Logging

#endif // LOGGING_LOGSINK_H
