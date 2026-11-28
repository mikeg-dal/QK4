// SPDX-License-Identifier: GPL-3.0-or-later
//
// OrbPtt (src/hardware/orbptt.h): when the ORB's PTT/KEY jack keys, and when it lets go.

#include "hardware/orbptt.h"

#include <QtTest/QtTest>

using OrbPtt::Action;

class TestOrbPtt : public QObject {
    Q_OBJECT

private slots:
    // Every mode on both VFOs, split or not: only voice on the transmit VFO, and never with the
    // keyer live on VFO A.
    void allowedSweep() {
        const RadioState::Mode modes[] = {RadioState::Unknown, RadioState::LSB,  RadioState::USB,
                                          RadioState::CW,      RadioState::FM,   RadioState::AM,
                                          RadioState::DATA,    RadioState::CW_R, RadioState::DATA_R};
        const QList<RadioState::Mode> voice{RadioState::LSB, RadioState::USB, RadioState::FM, RadioState::AM};
        for (auto a : modes) {
            for (auto b : modes) {
                for (bool split : {false, true}) {
                    const bool keyer = a == RadioState::CW || a == RadioState::CW_R;
                    const bool expected = !keyer && voice.contains(split ? b : a);
                    QCOMPARE(OrbPtt::allowed({true, true, a, b, split}), expected);
                    QVERIFY(!OrbPtt::allowed({false, true, a, b, split}));
                    QVERIFY(!OrbPtt::allowed({true, false, a, b, split}));
                }
            }
        }
    }

    void defaultsAllowNothing() { QVERIFY(!OrbPtt::allowed({})); }

    void pressAndRelease() {
        OrbPtt::Gate gate;
        QCOMPARE(gate.jack(true, true), Action::Engage);
        QCOMPARE(gate.jack(false, true), Action::Release);
        QCOMPARE(gate.jack(false, true), Action::None);
    }

    void pressWhileNotAllowedDoesNothing() {
        OrbPtt::Gate gate;
        QCOMPARE(gate.jack(true, false), Action::None);
        QCOMPARE(gate.jack(false, false), Action::None);
    }

    // A straight key held while the mode changes from CW to voice must not open the microphone.
    void heldIntoAllowedDoesNotKey() {
        OrbPtt::Gate gate;
        QCOMPARE(gate.jack(true, false), Action::None);
        QCOMPARE(gate.allowedChanged(true), Action::None);
        QCOMPARE(gate.jack(false, true), Action::None);
        QCOMPARE(gate.jack(true, true), Action::Engage); // a fresh press does
    }

    // Into CW, or the option off, while held: release once, and do not re-key on the way back.
    void losingPermissionReleasesAndStaysReleased() {
        OrbPtt::Gate gate;
        gate.jack(true, true);
        QCOMPARE(gate.allowedChanged(false), Action::Release);
        QCOMPARE(gate.allowedChanged(false), Action::None);
        QCOMPARE(gate.allowedChanged(true), Action::None);
        QCOMPARE(gate.jack(false, true), Action::None);
    }

    // A change that leaves it allowed (USB to LSB) must not cut the transmission.
    void stayingAllowedKeepsHolding() {
        OrbPtt::Gate gate;
        gate.jack(true, true);
        QCOMPARE(gate.allowedChanged(true), Action::None);
        QCOMPARE(gate.jack(false, true), Action::Release);
    }

    // A release always lets go, even if the permission went without being reported.
    void releaseIsNeverBlocked() {
        OrbPtt::Gate gate;
        gate.jack(true, true);
        QCOMPARE(gate.jack(false, false), Action::Release);
    }
};

QTEST_APPLESS_MAIN(TestOrbPtt)
#include "test_orbptt.moc"
