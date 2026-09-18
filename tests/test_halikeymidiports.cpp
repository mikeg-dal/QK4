#include "hardware/halikey_midi_ports.h"

#include <QtTest>

// Port-name matching for the MIDI HaliKey. Pure logic, so no RtMidi or MIDI subsystem is needed —
// same shape as test_halikeydevice.cpp, which covers the edge-dedupe helper.
//
// This decides both which port is opened and whether the opened port has disappeared, so a wrong
// answer either fails to connect or reports a device that is still plugged in as removed.
class TestHalikeyMidiPorts : public QObject {
    Q_OBJECT

private slots:
    void findsAnExactName();
    void findsByContainmentDespiteWinmmIndexSuffix();
    void survivesIndexRenumberingWhenAnotherDeviceIsRemoved();
    void isCaseInsensitive();
    void missingPortReturnsMinusOne();
    void emptyEnumerationReturnsMinusOne();
    void emptyNeedleNeverMatches();
};

void TestHalikeyMidiPorts::findsAnExactName() {
    const QStringList ports{"IAC Driver Bus 1", "HaliKey MIDI", "Launchpad"};
    QCOMPARE(HalikeyMidiPorts::findPort(ports, "HaliKey MIDI"), 1);
}

void TestHalikeyMidiPorts::findsByContainmentDespiteWinmmIndexSuffix() {
    // WinMM appends the port index to the name (RTMIDI_DO_NOT_ENSURE_UNIQUE_PORTNAMES is not
    // defined), so an equality match would never find it on Windows.
    const QStringList ports{"Microsoft GS Wavetable Synth 0", "HaliKey MIDI 1"};
    QCOMPARE(HalikeyMidiPorts::findPort(ports, "HaliKey MIDI"), 1);
}

void TestHalikeyMidiPorts::survivesIndexRenumberingWhenAnotherDeviceIsRemoved() {
    // Windows compacts indices when any MIDI device is removed, so our port is renamed by the
    // unplugging of something else entirely. It must still be found, or the presence poll would
    // report our device gone because a different one was.
    const QStringList before{"Keystation 0", "HaliKey MIDI 1", "Launchpad 2"};
    const QStringList afterOtherRemoved{"Keystation 0", "HaliKey MIDI 1"};
    const QStringList afterFirstRemoved{"HaliKey MIDI 0", "Launchpad 1"};

    QVERIFY(HalikeyMidiPorts::findPort(before, "HaliKey MIDI") >= 0);
    QVERIFY(HalikeyMidiPorts::findPort(afterOtherRemoved, "HaliKey MIDI") >= 0);
    QCOMPARE(HalikeyMidiPorts::findPort(afterFirstRemoved, "HaliKey MIDI"), 0);
}

void TestHalikeyMidiPorts::isCaseInsensitive() {
    const QStringList ports{"halikey midi"};
    QCOMPARE(HalikeyMidiPorts::findPort(ports, "HaliKey MIDI"), 0);
}

void TestHalikeyMidiPorts::missingPortReturnsMinusOne() {
    // The removal case: the device is unplugged and no longer enumerated.
    const QStringList ports{"IAC Driver Bus 1", "Launchpad"};
    QCOMPARE(HalikeyMidiPorts::findPort(ports, "HaliKey MIDI"), -1);
}

void TestHalikeyMidiPorts::emptyEnumerationReturnsMinusOne() {
    QCOMPARE(HalikeyMidiPorts::findPort({}, "HaliKey MIDI"), -1);
}

void TestHalikeyMidiPorts::emptyNeedleNeverMatches() {
    // QString::contains("") is true for every string, which would make an unconfigured device name
    // match the first port on the system and open something at random.
    const QStringList ports{"IAC Driver Bus 1", "HaliKey MIDI"};
    QCOMPARE(HalikeyMidiPorts::findPort(ports, QString()), -1);
    QCOMPARE(HalikeyMidiPorts::findPort(ports, ""), -1);
}

QTEST_APPLESS_MAIN(TestHalikeyMidiPorts)
#include "test_halikeymidiports.moc"
