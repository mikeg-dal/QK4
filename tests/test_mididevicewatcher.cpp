#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

#include "hardware/mididevicewatcher.h"

// MidiDeviceWatcher's lifecycle. Device arrival needs a real MIDI device and is a bench check; what
// is tested here is the part that could hang or crash QK4 at quit: the watcher thread (macOS) starts
// with the object and stops within its deadline, stop() is safe to call twice, and destruction
// without stop() still ends the thread. Elsewhere the object is inert.
class TestMidiDeviceWatcher : public QObject {
    Q_OBJECT

private slots:
    void stopsPromptly() {
        MidiDeviceWatcher watcher;
        QTest::qWait(300); // let the thread create its client and enter its run loop
        QElapsedTimer timer;
        timer.start();
        watcher.stop();
        // The thread polls its stop flag every 250 ms; well under the 2 s deadline means it was
        // joined, not disowned.
        QVERIFY2(timer.elapsed() < 1500, qPrintable(QString::number(timer.elapsed())));
    }

    void stopTwiceIsSafe() {
        MidiDeviceWatcher watcher;
        watcher.stop();
        watcher.stop();
    }

    void destroyedWithoutStopEndsTheThread() {
        QElapsedTimer timer;
        timer.start();
        { MidiDeviceWatcher watcher; }
        QVERIFY(timer.elapsed() < 1500);
    }

    void nothingIsReportedAfterStop() {
        MidiDeviceWatcher watcher;
        QSignalSpy spy(&watcher, &MidiDeviceWatcher::devicesChanged);
        watcher.stop();
        QTest::qWait(100);
        QCOMPARE(spy.count(), 0);
    }
};

QTEST_MAIN(TestMidiDeviceWatcher)
#include "test_mididevicewatcher.moc"
