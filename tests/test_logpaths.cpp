#include <QtTest/QtTest>

#include "logging/logpaths.h"

using Logging::Platform;

namespace {

Logging::PathInputs inputs(Platform platform, const QString &home, const QHash<QString, QString> &env = {}) {
    Logging::PathInputs in;
    in.platform = platform;
    in.home = home;
    in.env = env;
    return in;
}

} // namespace

class LogPathsTests : public QObject {
    Q_OBJECT

private slots:
    void macOsUsesLibraryLogs() {
        QCOMPARE(Logging::logDirectory(inputs(Platform::MacOS, "/Users/op")), QString("/Users/op/Library/Logs/QK4"));
        // XDG variables mean nothing on macOS.
        QCOMPARE(Logging::logDirectory(inputs(Platform::MacOS, "/Users/op", {{"XDG_STATE_HOME", "/tmp/state"}})),
                 QString("/Users/op/Library/Logs/QK4"));
    }

    void windowsUsesLocalAppData() {
        QCOMPARE(Logging::logDirectory(
                     inputs(Platform::Windows, "C:/Users/op", {{"LOCALAPPDATA", "C:\\Users\\op\\AppData\\Local"}})),
                 QString("C:/Users/op/AppData/Local/QK4/Logs"));
        QCOMPARE(
            Logging::logDirectory(inputs(Platform::Windows, "C:/Users/op", {{"LOCALAPPDATA", "D:\\Profiles\\op\\"}})),
            QString("D:/Profiles/op/QK4/Logs"));
    }

    void windowsFallsBackToTheDefaultLocalAppData() {
        QCOMPARE(Logging::logDirectory(inputs(Platform::Windows, "C:\\Users\\op")),
                 QString("C:/Users/op/AppData/Local/QK4/Logs"));
        QCOMPARE(Logging::logDirectory(inputs(Platform::Windows, "C:/Users/op", {{"LOCALAPPDATA", ""}})),
                 QString("C:/Users/op/AppData/Local/QK4/Logs"));
    }

    void linuxDefaultsToLocalState() {
        QCOMPARE(Logging::logDirectory(inputs(Platform::Linux, "/home/op")), QString("/home/op/.local/state/QK4/logs"));
    }

    void linuxHonoursXdgStateHome() {
        QCOMPARE(Logging::logDirectory(inputs(Platform::Linux, "/home/op", {{"XDG_STATE_HOME", "/data/state/"}})),
                 QString("/data/state/QK4/logs"));
    }

    void linuxIgnoresAnEmptyOrRelativeXdgStateHome() {
        QCOMPARE(Logging::logDirectory(inputs(Platform::Linux, "/home/op", {{"XDG_STATE_HOME", ""}})),
                 QString("/home/op/.local/state/QK4/logs"));
        QCOMPARE(Logging::logDirectory(inputs(Platform::Linux, "/home/op", {{"XDG_STATE_HOME", "state"}})),
                 QString("/home/op/.local/state/QK4/logs"));
    }

    void flatpakUsesItsMappedStateHome() {
        const QString state = "/home/op/.var/app/io.github.mikeg_dal.QK4/.local/state";
        QCOMPARE(Logging::logDirectory(inputs(Platform::Linux, "/home/op",
                                              {{"FLATPAK_ID", "io.github.mikeg_dal.QK4"}, {"XDG_STATE_HOME", state}})),
                 state + "/QK4/logs");
    }

    void flatpakBefore113FallsBackToTheDataHome() {
        const QString data = "/home/op/.var/app/io.github.mikeg_dal.QK4/data";
        QCOMPARE(Logging::logDirectory(inputs(Platform::Linux, "/home/op",
                                              {{"FLATPAK_ID", "io.github.mikeg_dal.QK4"}, {"XDG_DATA_HOME", data}})),
                 data + "/QK4/logs");
        QCOMPARE(
            Logging::logDirectory(inputs(Platform::Linux, "/home/op", {{"FLATPAK_ID", "io.github.mikeg_dal.QK4"}})),
            QString("/home/op/.local/share/QK4/logs"));
    }

    void currentInputsMatchThisPlatform() {
        const Logging::PathInputs in = Logging::currentPathInputs();
        QVERIFY(!in.home.isEmpty());
#if defined(Q_OS_MACOS)
        QCOMPARE(in.platform, Platform::MacOS);
#elif defined(Q_OS_WIN)
        QCOMPARE(in.platform, Platform::Windows);
#else
        QCOMPARE(in.platform, Platform::Linux);
#endif
        QVERIFY(Logging::logDirectory(in).contains("QK4"));
    }
};

QTEST_GUILESS_MAIN(LogPathsTests)
#include "test_logpaths.moc"
