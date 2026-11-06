// SPDX-License-Identifier: GPL-3.0-or-later
//
// RtScheduling: each OS's fallback chain, driven through fake OS calls so all three are checked
// on every platform; the report table the threads write and the main thread reads; and the real
// promotion on this machine, which must return something sane whether or not the OS grants it
// (CI runners usually have no real-time rights).

#include "utils/rtscheduling.h"

#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QThread>
#include <QtTest/QtTest>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace RtScheduling;

namespace {

constexpr int kEACCES = 13;

// Answers each call with the next code queued for it, and records the call.
struct FakeOs {
    int qos = 0, timeConstraint = 0;
    int mmcss = 0, timeCritical = 0, highest = 0;
    int fifo = 0, nice = 0;
    int niceFloor = -20; // what RLIMIT_NICE allows; setNice refuses anything lower with EACCES
    std::vector<std::string> calls;

    static FakeOs &of(void *c) { return *static_cast<FakeOs *>(c); }

    MacOps macOps() {
        return MacOps{this,
                      [](void *c) {
                          of(c).calls.push_back("qos");
                          return of(c).qos;
                      },
                      [](void *c) {
                          of(c).calls.push_back("time-constraint");
                          return of(c).timeConstraint;
                      }};
    }
    WindowsOps windowsOps() {
        return WindowsOps{this,
                          [](void *c) {
                              of(c).calls.push_back("mmcss");
                              return of(c).mmcss;
                          },
                          [](void *c, WindowsPriority p) {
                              const bool critical = p == WindowsPriority::TimeCritical;
                              of(c).calls.push_back(critical ? "time-critical" : "highest");
                              return critical ? of(c).timeCritical : of(c).highest;
                          }};
    }
    LinuxOps linuxOps() {
        return LinuxOps{this,
                        [](void *c, int priority) {
                            of(c).calls.push_back("fifo " + std::to_string(priority));
                            return of(c).fifo;
                        },
                        [](void *c, int nice) {
                            of(c).calls.push_back("nice " + std::to_string(nice));
                            if (of(c).nice != 0)
                                return of(c).nice;
                            return nice < of(c).niceFloor ? kEACCES : 0;
                        },
                        [](void *c, unsigned long ns) {
                            of(c).calls.push_back("slack " + std::to_string(ns));
                            return 0;
                        },
                        [](void *c) { return of(c).niceFloor; }};
    }
};

using Calls = std::vector<std::string>;
constexpr int kEPERM = 1;

// Every non-comment line of every C++ source under src/, as (relative path, line).
std::vector<std::pair<QString, QString>> sourceLines() {
    std::vector<std::pair<QString, QString>> out;
    const QString root = QStringLiteral(QK4_SOURCE_DIR);
    QDirIterator it(root, {"*.cpp", "*.h", "*.mm"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        const QString rel = path.mid(root.size() + 1);
        for (const QString &line : QString::fromUtf8(f.readAll()).split('\n')) {
            if (!line.trimmed().startsWith(QLatin1String("//")))
                out.push_back({rel, line});
        }
    }
    return out;
}

} // namespace

class TestRtScheduling : public QObject {
    Q_OBJECT

private slots:
    // ---- macOS ----

    void macRealTimeGetsTheTimeConstraintPolicy() {
        FakeOs os;
        const Result r = applyMac(Class::RealTime, os.macOps());
        QCOMPARE(r.outcome, Outcome::RealTime);
        QCOMPARE(r.error, 0);
        QCOMPARE(os.calls, (Calls{"qos", "time-constraint"}));
    }

    void macRealTimeFallsBackToQos() {
        FakeOs os;
        os.timeConstraint = 5; // KERN_FAILURE
        const Result r = applyMac(Class::RealTime, os.macOps());
        QCOMPARE(r.outcome, Outcome::Elevated);
        QCOMPARE(r.error, 5);
        QVERIFY(std::strstr(r.mechanism, "QoS user-interactive"));
    }

    void macEverythingRefusedLeavesTheThreadAlone() {
        FakeOs os;
        os.timeConstraint = 5;
        os.qos = kEPERM;
        const Result r = applyMac(Class::RealTime, os.macOps());
        QCOMPARE(r.outcome, Outcome::Unchanged);
        QCOMPARE(r.error, 5); // the first refusal
    }

    void macHighIsQosOnly() {
        FakeOs os;
        const Result r = applyMac(Class::High, os.macOps());
        QCOMPARE(r.outcome, Outcome::Elevated);
        QCOMPARE(os.calls, Calls{"qos"});
    }

    // ---- Windows ----

    void windowsRealTimeJoinsMmcss() {
        FakeOs os;
        const Result r = applyWindows(Class::RealTime, os.windowsOps());
        QCOMPARE(r.outcome, Outcome::RealTime);
        QCOMPARE(os.calls, Calls{"mmcss"});
    }

    void windowsRealTimeFallsBackToTimeCritical() {
        FakeOs os;
        os.mmcss = 1060; // ERROR_SERVICE_DOES_NOT_EXIST
        const Result r = applyWindows(Class::RealTime, os.windowsOps());
        QCOMPARE(r.outcome, Outcome::Elevated);
        QCOMPARE(r.error, 1060);
        QCOMPARE(os.calls, (Calls{"mmcss", "time-critical"}));
    }

    void windowsRealTimeFallsBackToHighest() {
        FakeOs os;
        os.mmcss = 1060;
        os.timeCritical = 5;
        const Result r = applyWindows(Class::RealTime, os.windowsOps());
        QCOMPARE(r.outcome, Outcome::Elevated);
        QCOMPARE(os.calls, (Calls{"mmcss", "time-critical", "highest"}));
        QVERIFY(std::strstr(r.mechanism, "HIGHEST"));
    }

    void windowsHighNeverTouchesMmcss() {
        FakeOs os;
        const Result r = applyWindows(Class::High, os.windowsOps());
        QCOMPARE(r.outcome, Outcome::Elevated);
        QCOMPARE(os.calls, Calls{"highest"});
    }

    // ---- Linux ----

    void linuxRealTimeGetsFifoBelowPipeWire() {
        FakeOs os;
        const Result r = applyLinux(Class::RealTime, os.linuxOps());
        QCOMPARE(r.outcome, Outcome::RealTime);
        QCOMPARE(os.calls, (Calls{"slack 1", "fifo 15"}));
        QVERIFY(kLinuxFifoPriority < 88); // PipeWire's default rt.prio
    }

    // The ordinary desktop and CI case: no RLIMIT_RTPRIO, no CAP_SYS_NICE.
    void linuxWithoutRtprioFallsBackQuietly() {
        FakeOs os;
        os.fifo = kEPERM;
        os.nice = kEPERM;
        const Result r = applyLinux(Class::RealTime, os.linuxOps());
        QCOMPARE(r.outcome, Outcome::Unchanged);
        QCOMPARE(r.error, kEPERM);
        QCOMPARE(os.calls, (Calls{"slack 1", "fifo 15", "nice -10"}));
        QVERIFY(std::strstr(r.mechanism, "timer slack 1 ns"));
    }

    void linuxFifoRefusedNiceGranted() {
        FakeOs os;
        os.fifo = kEPERM;
        const Result r = applyLinux(Class::RealTime, os.linuxOps());
        QCOMPARE(r.outcome, Outcome::Elevated);
        QCOMPARE(r.error, kEPERM);
    }

    void linuxHighIsNiceOnly() {
        FakeOs os;
        const Result r = applyLinux(Class::High, os.linuxOps());
        QCOMPARE(r.outcome, Outcome::Elevated);
        QCOMPARE(r.nice, kLinuxHighNice);
        QCOMPARE(r.error, 0);
        QCOMPARE(os.calls, (Calls{"slack 1", "nice -10"}));
    }

    // RLIMIT_NICE 25 (limits.conf "nice -5"): -10 is refused, -5 is not, and -5 is what it gets.
    void linuxHighTakesWhatRlimitNiceAllows() {
        FakeOs os;
        os.niceFloor = -5;
        const Result r = applyLinux(Class::High, os.linuxOps());
        QCOMPARE(r.outcome, Outcome::Elevated);
        QCOMPARE(r.nice, -5);
        QCOMPARE(r.error, kEACCES);
        QCOMPARE(os.calls, (Calls{"slack 1", "nice -10", "nice -5"}));
        QVERIFY(std::strstr(r.mechanism, "RLIMIT_NICE"));
    }

    // RLIMIT_NICE 0, the systemd and limits.conf default: no lower nice at all, and no second try.
    void linuxHighWithoutRlimitNiceLeavesTheThreadAlone() {
        FakeOs os;
        os.niceFloor = 20;
        const Result r = applyLinux(Class::High, os.linuxOps());
        QCOMPARE(r.outcome, Outcome::Unchanged);
        QCOMPARE(r.nice, 0);
        QCOMPARE(r.error, kEACCES);
        QCOMPARE(os.calls, (Calls{"slack 1", "nice -10"}));
    }

    void linuxRealTimeFallbackAlsoTakesWhatRlimitNiceAllows() {
        FakeOs os;
        os.fifo = kEPERM;
        os.niceFloor = -3;
        const Result r = applyLinux(Class::RealTime, os.linuxOps());
        QCOMPARE(r.outcome, Outcome::Elevated);
        QCOMPARE(r.nice, -3);
        QCOMPARE(r.error, kEPERM); // the first refusal: SCHED_FIFO
        QCOMPARE(os.calls, (Calls{"slack 1", "fifo 15", "nice -10", "nice -3"}));
    }

    // ---- every OS ----

    void normalTouchesNothing() {
        FakeOs os;
        QCOMPARE(applyMac(Class::Normal, os.macOps()).outcome, Outcome::Unchanged);
        QCOMPARE(applyWindows(Class::Normal, os.windowsOps()).outcome, Outcome::Unchanged);
        QCOMPARE(applyLinux(Class::Normal, os.linuxOps()).outcome, Outcome::Unchanged);
        QVERIFY(os.calls.empty());
    }

    // ---- this machine, and the report table ----

    void promotionOnThisMachineIsReportedOnce() {
        takeUnreported(); // whatever an earlier case left
        Result got;
        std::thread t([&got] { got = promoteCurrentThread(Class::RealTime, "TestThread"); });
        t.join();
        qInfo("this machine: real-time request -> %s, %s (error %d)", outcomeName(got.outcome), got.mechanism,
              got.error);
        QVERIFY(got.mechanism != nullptr);
        QCOMPARE(got.requested, Class::RealTime);
        const auto reports = takeUnreported();
        QCOMPARE(reports.size(), size_t(1));
        QCOMPARE(QByteArray(reports[0].thread), QByteArray("TestThread"));
        QCOMPARE(reports[0].result.outcome, got.outcome);
        QVERIFY(takeUnreported().empty());
    }

    // ---- the thread roles ----

    void cwThreadsAreRealTimeAndIoAndAudioOnlyHigh() {
        for (const ThreadRole &role : {Threads::kKeyer, Threads::kSidetone, Threads::kHaliKey, Threads::kKpodPlusEp02})
            QCOMPARE(role.schedulingClass, Class::RealTime);
        for (const ThreadRole &role : {Threads::kIo, Threads::kAudioRx, Threads::kAudioTx})
            QCOMPARE(role.schedulingClass, Class::High);
    }

    // Each role is applied to the thread it names, by its owner, and nothing bypasses the table.
    void eachRoleIsAppliedWhereItBelongs() {
        struct Expected {
            const char *role, *file, *call;
            int seen = 0;
        };
        std::vector<Expected> expected{
            {"kIo", "controllers/connectioncontroller.cpp", "promoteOnStart(m_ioThread, RtScheduling::Threads::kIo)"},
            {"kAudioRx", "controllers/audiocontroller.cpp",
             "promoteOnStart(m_audioThread, RtScheduling::Threads::kAudioRx)"},
            {"kAudioTx", "controllers/audiocontroller.cpp",
             "promoteOnStart(m_txThread, RtScheduling::Threads::kAudioTx)"},
            {"kKeyer", "controllers/hardwarecontroller.cpp", "promoteCurrentThread(RtScheduling::Threads::kKeyer)"},
            {"kSidetone", "audio/sidetonegenerator.cpp", "promoteCurrentThread(RtScheduling::Threads::kSidetone)"},
            {"kHaliKey", "hardware/halikeyv14worker.cpp", "promoteCurrentThread(RtScheduling::Threads::kHaliKey)"},
            {"kKpodPlusEp02", "hardware/kpodplususbworker.cpp",
             "promoteCurrentThread(RtScheduling::Threads::kKpodPlusEp02)"},
        };
        // No QThread::Priority anywhere (setPriority, start(priority)): a no-op on Linux, and on macOS
        // the scheduling parameters they set opt the thread out of QoS.
        static const QRegularExpression bypass(
            QStringLiteral(R"(promoteCurrentThread\(\s*RtScheduling::Class::|QThread::\w+Priority\b)"));
        QStringList stray;
        for (const auto &[file, line] : sourceLines()) {
            if (file.startsWith(QLatin1String("utils/rtscheduling")))
                continue;
            if (bypass.match(line).hasMatch())
                stray << file + ": " + line.trimmed();
            for (Expected &e : expected) {
                if (!line.contains(QLatin1String("Threads::") + QLatin1String(e.role) + QLatin1Char(')')) &&
                    !line.contains(QLatin1String("Threads::") + QLatin1String(e.role) + QLatin1Char(',')))
                    continue;
                if (file == QLatin1String(e.file) && line.contains(QLatin1String(e.call)))
                    ++e.seen;
                else
                    stray << file + ": " + line.trimmed();
            }
        }
        QVERIFY2(stray.isEmpty(), qPrintable(stray.join('\n')));
        for (const Expected &e : expected)
            QVERIFY2(e.seen == 1, e.role);
    }

    void promoteOnStartRunsOnTheThreadEachTimeItStarts() {
        takeUnreported();
        QThread thread;
        promoteOnStart(&thread, Threads::kAudioTx);
        QThread *ranOn = nullptr;
        QObject::connect(
            &thread, &QThread::started, &thread, [&ranOn] { ranOn = QThread::currentThread(); }, Qt::DirectConnection);
        for (int run = 0; run < 2; ++run) {
            ranOn = nullptr;
            thread.start();
            thread.quit();
            QVERIFY(thread.wait(QDeadlineTimer(5000)));
            QCOMPARE(ranOn, &thread);
            const auto reports = takeUnreported();
            QCOMPARE(reports.size(), size_t(1));
            QCOMPARE(QByteArray(reports[0].thread), QByteArray("AudioTX"));
            QCOMPARE(reports[0].result.requested, Class::High);
            QVERIFY(reports[0].result.outcome != Outcome::RealTime);
        }
    }

    void reportsComeBackInOrderFromManyThreads() {
        takeUnreported();
        for (const char *name : {"A", "B", "C"}) {
            std::thread t([name] { promoteCurrentThread(Class::Normal, name); });
            t.join();
        }
        const auto reports = takeUnreported();
        QCOMPARE(reports.size(), size_t(3));
        QCOMPARE(QByteArray(reports[0].thread), QByteArray("A"));
        QCOMPARE(QByteArray(reports[2].thread), QByteArray("C"));
    }
};

QTEST_GUILESS_MAIN(TestRtScheduling)
#include "test_rtscheduling.moc"
