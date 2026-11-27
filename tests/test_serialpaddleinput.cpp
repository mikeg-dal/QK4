// SPDX-License-Identifier: GPL-3.0-or-later
//
// SerialPaddleInput (src/hardware/serialpaddleinput.h): which modem line is which contact on each
// serial paddle device, and switching between paddles and a straight key with contacts closed.

#include "hardware/serialpaddleinput.h"

#include <QtTest/QtTest>

class TestSerialPaddleInput : public QObject {
    Q_OBJECT

private slots:
    void lineMap_data() {
        QTest::addColumn<int>("type");
        QTest::addColumn<bool>("straight");
        QTest::addColumn<int>("lines"); // bit 0 = CTS, 1 = DSR, 2 = DCD
        QTest::addColumn<bool>("dit");
        QTest::addColumn<bool>("dah");

        for (int lines = 0; lines < 8; ++lines) {
            const bool cts = lines & 1;
            const bool dsr = lines & 2;
            const bool dcd = lines & 4;
            for (const bool straight : {false, true}) {
                const char *mode = straight ? "straight" : "paddle";
                QTest::addRow("V1.4 %s %d", mode, lines)
                    << HaliKeyType::kSerialV14 << straight << lines << cts << (dsr || dcd);
                QTest::addRow("ORB %s %d", mode, lines)
                    << HaliKeyType::kOrb << straight << lines << (straight ? cts : dsr) << (!straight && dcd);
            }
        }
    }

    void lineMap() {
        QFETCH(int, type);
        QFETCH(bool, straight);
        QFETCH(int, lines);
        QFETCH(bool, dit);
        QFETCH(bool, dah);
        const ModemLines raw{(lines & 1) != 0, (lines & 2) != 0, (lines & 4) != 0};
        const auto contacts = SerialPaddleInput::map(type, raw, straight);
        QCOMPARE(contacts.dit, dit);
        QCOMPARE(contacts.dah, dah);
    }

    void orbPaddleReportsDitAndDahAndIgnoresPttJack() {
        SerialPaddleInput input(HaliKeyType::kOrb);
        input.start({}, false);
        QVERIFY(input.poll({true, false, false}, false, 1.000).empty()); // CTS: the PTT/KEY jack

        auto c = input.poll({false, true, false}, false, 1.001);
        QCOMPARE(c.size(), size_t(1));
        QCOMPARE(c[0].signal, SerialPaddleInput::kDit);
        QCOMPARE(c[0].asserted, true);

        c = input.poll({false, true, true}, false, 1.002);
        QCOMPARE(c.size(), size_t(1));
        QCOMPARE(c[0].signal, SerialPaddleInput::kDah);
        QCOMPARE(c[0].asserted, true);
    }

    void orbStraightKeyUsesPttJackAndIgnoresPaddleJack() {
        SerialPaddleInput input(HaliKeyType::kOrb);
        input.start({}, true);
        QVERIFY(input.poll({false, true, true}, true, 1.000).empty());
        QVERIFY(input.poll({false, true, true}, true, 1.100).empty());

        QVERIFY(input.poll({true, false, false}, true, 2.000).empty()); // debouncing
        const auto c = input.poll({true, false, false}, true, 2.025);
        QCOMPARE(c.size(), size_t(1));
        QCOMPARE(c[0].signal, SerialPaddleInput::kDit);
        QCOMPARE(c[0].asserted, true);
    }

    void modeChangeTakesHeldContactsAsRest_data() {
        QTest::addColumn<int>("type");
        QTest::addColumn<bool>("toStraight");
        QTest::newRow("ORB to straight") << HaliKeyType::kOrb << true;
        QTest::newRow("ORB to paddle") << HaliKeyType::kOrb << false;
        QTest::newRow("V1.4 to straight") << HaliKeyType::kSerialV14 << true;
        QTest::newRow("V1.4 to paddle") << HaliKeyType::kSerialV14 << false;
    }

    // Every contact held closed across the switch: neither the switching poll nor any later one may
    // report an edge.
    void modeChangeTakesHeldContactsAsRest() {
        QFETCH(int, type);
        QFETCH(bool, toStraight);
        const ModemLines held{true, true, true};
        SerialPaddleInput input(type);
        input.start(held, !toStraight);
        QVERIFY(input.poll(held, !toStraight, 1.000).empty());
        QVERIFY(input.poll(held, toStraight, 1.001).empty());
        QVERIFY(input.poll(held, toStraight, 1.002).empty());
        QVERIFY(input.poll(held, toStraight, 1.100).empty());
    }

    // The ORB's two jacks differ: with only the paddle closed, switching to straight key must not
    // report the open PTT/KEY jack as a release, and vice versa.
    void orbModeChangeDoesNotCarryOneJackOntoTheOther() {
        const ModemLines paddleHeld{false, true, false};
        SerialPaddleInput input(HaliKeyType::kOrb);
        input.start(paddleHeld, false);
        QVERIFY(input.poll(paddleHeld, true, 1.000).empty());
        QVERIFY(input.poll(paddleHeld, true, 1.100).empty());
        QVERIFY(input.poll(paddleHeld, false, 1.200).empty());
        QVERIFY(input.poll(paddleHeld, false, 1.300).empty());
    }
};

QTEST_GUILESS_MAIN(TestSerialPaddleInput)
#include "test_serialpaddleinput.moc"
