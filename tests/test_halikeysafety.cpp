// SPDX-License-Identifier: GPL-3.0-or-later
#include "hardware/halikeysafety.h"
#include <QtTest/QtTest>

class TestHaliKeySafety : public QObject {
    Q_OBJECT
private slots:
    void noInputCanArmBeforeApproval() {
        HaliKeySafety guard;
        QVERIFY(!guard.serialSample(false, false));
        QVERIFY(!guard.midiMessage(0x90, 20));
        QVERIFY(!guard.midiMessage(0x80, 20));
        QVERIFY(!guard.midiMessage(0x80, 21));
        QVERIFY(!guard.armed());
    }
    void serialHeldAtConnection() {
        HaliKeySafety guard;
        guard.enable(false);
        QVERIFY(!guard.serialSample(true, false));
        QVERIFY(!guard.serialSample(false, true));
        QVERIFY(!guard.armed());
        QVERIFY(!guard.serialSample(false, false));
        QVERIFY(guard.armed());
        QVERIFY(guard.serialSample(true, false));
    }
    void midiBothPaddlesMustBeKnownReleased() {
        HaliKeySafety guard;
        guard.enable(false);
        QVERIFY(!guard.midiMessage(0x90, 20));
        QVERIFY(!guard.midiMessage(0x90, 21));
        QVERIFY(!guard.midiMessage(0x80, 20));
        QVERIFY(!guard.armed());
        QVERIFY(!guard.midiMessage(0x80, 21));
        QVERIFY(guard.armed());
        QVERIFY(guard.midiMessage(0x90, 20));
    }
    void midiHeldBeforeApproval() {
        HaliKeySafety guard;
        QVERIFY(!guard.midiMessage(0x90, 20));
        guard.enable(false);
        QVERIFY(!guard.midiMessage(0x80, 21));
        QVERIFY(!guard.armed());
        QVERIFY(!guard.midiMessage(0x80, 20));
        QVERIFY(guard.armed());
    }
    void anotherPressInvalidatesPreviousRelease() {
        HaliKeySafety guard;
        guard.enable(false);
        guard.midiMessage(0x80, 20);
        guard.midiMessage(0x90, 20);
        guard.midiMessage(0x80, 21);
        QVERIFY(!guard.armed());
        guard.midiMessage(0x80, 20);
        QVERIFY(guard.armed());
    }
    void unrelatedMidiDoesNotArm() {
        HaliKeySafety guard;
        guard.enable(false);
        for (const int status : {0x80, 0x90, 0xa0, 0xb0})
            QVERIFY(!guard.midiMessage(status, 31));
        guard.midiMessage(0xb0, 20);
        guard.midiMessage(0xa0, 21);
        QVERIFY(!guard.armed());
    }
    void straightKeyNeedsOnlyDit() {
        HaliKeySafety serial;
        serial.enable(true);
        QVERIFY(!serial.serialSample(false, true));
        QVERIFY(serial.armed());
        HaliKeySafety midi;
        midi.enable(true);
        midi.midiMessage(0x80, 21);
        QVERIFY(!midi.armed());
        QVERIFY(!midi.midiMessage(0x80, 20));
        QVERIFY(midi.armed());
    }
    void reconnectAndNewApprovalForgetReleases() {
        HaliKeySafety guard;
        guard.enable(false);
        guard.midiMessage(0x80, 20);
        guard.midiMessage(0x80, 21);
        QVERIFY(guard.armed());
        const auto previous = guard.epoch();
        guard.disarm();
        QVERIFY(guard.epoch() != previous);
        QVERIFY(!guard.armed());
        QVERIFY(!guard.midiMessage(0x90, 20));
        guard.enable(false);
        guard.midiMessage(0x80, 20);
        QVERIFY(!guard.armed());
        guard.midiMessage(0x80, 21);
        QVERIFY(guard.armed());
    }
};
QTEST_GUILESS_MAIN(TestHaliKeySafety)
#include "test_halikeysafety.moc"
