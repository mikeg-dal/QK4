#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include "logging/logcategories.h"
#include "logging/logsink.h"

using Logging::LogSink;

namespace {

QString readAll(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(f.readAll());
}

QStringList consoleLines;

void captureConsole(QtMsgType, const QMessageLogContext &context, const QString &message) {
    consoleLines << QStringLiteral("%1: %2").arg(QLatin1String(context.category ? context.category : "?"), message);
}

void post(LogSink &sink, QtMsgType type, const QString &message, const char *category = "qk4.test",
          const char *file = "/src/dir/file.cpp", int line = 42) {
    const QMessageLogContext context(file, line, "void f()", category);
    sink.post(type, context, message);
}

LogSink::FileOptions fileIn(const QString &dir, int count = 5, qint64 maxBytes = LogSink::DefaultMaxBytes) {
    LogSink::FileOptions options;
    options.directory = dir;
    options.header = {QStringLiteral("QK4 test header")};
    options.fileCount = count;
    options.maxBytes = maxBytes;
    return options;
}

} // namespace

class LogSinkTests : public QObject {
    Q_OBJECT

private slots:
    void init() { consoleLines.clear(); }

    void writesHeaderLinesAndFooter() {
        QTemporaryDir dir;
        LogSink sink;
        QVERIFY(sink.setFile(fileIn(dir.path())));
        sink.start();
        post(sink, QtInfoMsg, QStringLiteral("hello"));
        post(sink, QtWarningMsg, QStringLiteral("careful"));
        sink.stop();

        const QString text = readAll(dir.path() + "/qk4.log");
        QVERIFY(text.startsWith(QStringLiteral("=== QK4 log opened ")));
        QVERIFY(text.contains(QStringLiteral("QK4 test header\n")));
        QVERIFY(text.contains(QRegularExpression(
            QStringLiteral(R"(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{3}[+-]\d\d:\d\d I \[[^\]]+\] qk4\.test: hello\n)"))));
        // Warnings carry their source file's base name and line; info does not.
        QVERIFY(text.contains(QStringLiteral("qk4.test: careful (file.cpp:42)\n")));
        QVERIFY(!text.contains(QStringLiteral("hello (file.cpp")));
        QVERIFY(text.trimmed().split('\n').last().startsWith(QLatin1String(LogSink::CleanFooter)));
        QVERIFY(!text.contains(QStringLiteral("did not end cleanly")));
    }

    void messagesBeforeTheFileOpensLandInIt() {
        QTemporaryDir dir;
        LogSink sink;
        post(sink, QtInfoMsg, QStringLiteral("early"));
        QVERIFY(sink.setFile(fileIn(dir.path())));
        sink.start();
        sink.stop();
        QVERIFY(readAll(dir.path() + "/qk4.log").contains("qk4.test: early"));
    }

    void eachLaunchRotatesAndKeepsTheCount() {
        QTemporaryDir dir;
        for (int session = 0; session < 7; ++session) {
            LogSink sink;
            QVERIFY(sink.setFile(fileIn(dir.path(), 3)));
            post(sink, QtInfoMsg, QStringLiteral("session %1").arg(session));
            sink.stop();
        }
        QVERIFY(readAll(dir.path() + "/qk4.log").contains("session 6"));
        QVERIFY(readAll(dir.path() + "/qk4.1.log").contains("session 5"));
        QVERIFY(readAll(dir.path() + "/qk4.2.log").contains("session 4"));
        QVERIFY(!QFile::exists(dir.path() + "/qk4.3.log"));
    }

    void sizeRotationStartsANewFile() {
        QTemporaryDir dir;
        LogSink sink;
        QVERIFY(sink.setFile(fileIn(dir.path(), 3, 2000)));
        for (int i = 0; i < 100; ++i)
            post(sink, QtInfoMsg, QStringLiteral("line %1 ").arg(i) + QString(40, u'x'));
        sink.stop();
        const QString current = readAll(dir.path() + "/qk4.log");
        const QString previous = readAll(dir.path() + "/qk4.1.log");
        QVERIFY(current.contains("line 99"));
        QVERIFY(current.startsWith("=== log continued from qk4.1.log"));
        QVERIFY(previous.trimmed().endsWith("=== continued in the next file"));
        QVERIFY(QFileInfo(dir.path() + "/qk4.1.log").size() <= 2100);
    }

    void uncleanPreviousSessionIsReported() {
        QTemporaryDir dir;
        {
            QFile crashed(dir.path() + "/qk4.log");
            QVERIFY(crashed.open(QIODevice::WriteOnly));
            crashed.write("=== QK4 log opened 2026-10-29T10:00:00.000+00:00\nsomething happened\n");
        }
        LogSink sink;
        QVERIFY(sink.setFile(fileIn(dir.path())));
        sink.stop();
        QVERIFY(readAll(dir.path() + "/qk4.log").contains("previous session did not end cleanly - see qk4.1.log"));
        QVERIFY(readAll(dir.path() + "/qk4.1.log").contains("something happened"));
    }

    void switchingTheFileOffWritesItsOwnFooter() {
        QTemporaryDir dir;
        LogSink sink;
        QVERIFY(sink.setFile(fileIn(dir.path())));
        QCOMPARE(sink.currentFile(), dir.path() + "/qk4.log");
        post(sink, QtInfoMsg, QStringLiteral("on"));
        QVERIFY(sink.setFile(LogSink::FileOptions()));
        QVERIFY(sink.currentFile().isEmpty());
        post(sink, QtInfoMsg, QStringLiteral("off"));
        sink.stop();
        const QString text = readAll(dir.path() + "/qk4.log");
        QVERIFY(text.contains("qk4.test: on"));
        QVERIFY(!text.contains("qk4.test: off"));
        QVERIFY(text.contains(QLatin1String(LogSink::SwitchedOffFooter)));
    }

    void secondInstanceWritesItsOwnFile() {
        QTemporaryDir dir;
        LogSink first;
        LogSink second;
        QVERIFY(first.setFile(fileIn(dir.path())));
        QVERIFY(second.setFile(fileIn(dir.path())));
        QVERIFY(second.currentFile() != first.currentFile());
        QVERIFY(second.currentFile().contains(QRegularExpression("/qk4-\\d+\\.log$")));
        second.stop();
        first.stop();
    }

    void registeredSecretsAreRedactedEverywhere() {
        QTemporaryDir dir;
        LogSink sink(8192);
        sink.setConsoleHandler(captureConsole);
        sink.registerSecret(QStringLiteral("hunter22"));
        sink.registerSecret(QStringLiteral("K4")); // too short to redact safely
        QVERIFY(sink.setFile(fileIn(dir.path(), 3, 600)));
        for (int i = 0; i < 20; ++i)
            post(sink, QtWarningMsg, QStringLiteral("K4 password hunter22 try %1").arg(i));
        sink.stop();
        const QString all =
            readAll(dir.path() + "/qk4.log") + readAll(dir.path() + "/qk4.1.log") + readAll(dir.path() + "/qk4.2.log");
        QVERIFY(QFile::exists(dir.path() + "/qk4.1.log")); // the rotation was crossed
        QVERIFY(!all.contains("hunter22"));
        QVERIFY(all.contains("K4 password <redacted> try 19"));
        QVERIFY(!consoleLines.join('\n').contains("hunter22"));
    }

    void dropsAreReported() {
        QTemporaryDir dir;
        LogSink sink(4);
        QVERIFY(sink.setFile(fileIn(dir.path())));
        for (int i = 0; i < 10; ++i)
            post(sink, QtInfoMsg, QStringLiteral("m%1").arg(i));
        sink.stop();
        QVERIFY(readAll(dir.path() + "/qk4.log").contains("[logging] 6 messages dropped"));
    }

    void consoleMirrorFollowsTheSwitch() {
        LogSink sink;
        sink.setConsoleHandler(captureConsole);
        post(sink, QtInfoMsg, QStringLiteral("shown"));
        sink.flush();
        sink.setConsoleEnabled(false);
        post(sink, QtInfoMsg, QStringLiteral("hidden"));
        sink.stop();
        QCOMPARE(consoleLines, QStringList{QStringLiteral("qk4.test: shown")});
    }

    void afterStopMessagesGoStraightToTheConsole() {
        LogSink sink;
        sink.setConsoleHandler(captureConsole);
        sink.stop();
        post(sink, QtWarningMsg, QStringLiteral("late"));
        QCOMPARE(consoleLines, QStringList{QStringLiteral("qk4.test: late")});
    }

    void writerThreadDeliversWithoutAFlush() {
        QTemporaryDir dir;
        LogSink sink(8192, 20);
        QVERIFY(sink.setFile(fileIn(dir.path())));
        sink.start();
        post(sink, QtDebugMsg, QStringLiteral("ticked"));
        QTRY_VERIFY_WITH_TIMEOUT(readAll(dir.path() + "/qk4.log").contains("ticked"), 2000);
        sink.stop();
    }

    void realtimeThreadsAreMarkedPerThread() {
        QVERIFY(!LogSink::isRealtimeThread());
        bool other = false;
        std::thread t([&] {
            LogSink::markRealtimeThread();
            other = LogSink::isRealtimeThread();
        });
        t.join();
        QVERIFY(other);
        QVERIFY(!LogSink::isRealtimeThread());
    }

    void installedHandlerCarriesRealCategories() {
        QTemporaryDir dir;
        // The app installs once in main(); here it is installed and removed within one test.
        LogSink *sink = Logging::install();
        QVERIFY(sink->setFile(fileIn(dir.path())));
        qCWarning(lcApp) << "through the handler";
        Logging::shutdown();
        QVERIFY(readAll(dir.path() + "/qk4.log").contains("qk4.app: through the handler"));
    }
};

QTEST_GUILESS_MAIN(LogSinkTests)
#include "test_logsink.moc"
