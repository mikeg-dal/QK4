// SPDX-License-Identifier: GPL-3.0-or-later
//
// CwGate: when QK4's keyer may key, and when the sidetone holds an output stream open. The stream
// (and its 1 ms pump) must be closed whenever no CW can be keyed: disconnected, not in CW, the
// KPOD+ owning CW, or no HaliKey open — the idle-CPU regression of #92. Swept over every case.

#include "hardware/cwgate.h"

#include <QtTest/QtTest>

class TestCwGate : public QObject {
    Q_OBJECT

private slots:
    void everyCase() {
        for (int bits = 0; bits < 16; ++bits) {
            CwGate::Inputs in;
            in.radioReady = bits & 1;
            in.inCwMode = bits & 2;
            in.kpodPlusOwnsCw = bits & 4;
            in.halikeyOpen = bits & 8;
            const bool keyable = in.radioReady && in.inCwMode && !in.kpodPlusOwnsCw;
            QCOMPARE(CwGate::keyerInhibited(in), !keyable);
            QCOMPARE(CwGate::sidetoneStreamWanted(in), keyable && in.halikeyOpen);
        }
    }

    // The idle states #92 was measured in: no stream, so no pump and no live audio device.
    void noStreamWhileIdle() {
        CwGate::Inputs idle; // disconnected, no HaliKey
        QVERIFY(!CwGate::sidetoneStreamWanted(idle));
        CwGate::Inputs connectedNoKey;
        connectedNoKey.radioReady = true;
        connectedNoKey.inCwMode = true;
        QVERIFY(!CwGate::sidetoneStreamWanted(connectedNoKey));
        CwGate::Inputs keyOpenDisconnected;
        keyOpenDisconnected.halikeyOpen = true;
        keyOpenDisconnected.inCwMode = true;
        QVERIFY(!CwGate::sidetoneStreamWanted(keyOpenDisconnected));
    }
};

QTEST_GUILESS_MAIN(TestCwGate)
#include "test_cwgate.moc"
