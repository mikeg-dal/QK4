#include <QtTest>

#include "network/kpa1500antennas.h"

// KPA1500 sub-antenna numbering. Enable-map inputs are real ^AEbbALL payloads: one read from Mike's
// amp (only antennas 1 and 2 enabled) and the example in the V3 Programming Reference.
class TestKpa1500Antennas : public QObject {
    Q_OBJECT

private slots:
    void onlyAntennasOneAndTwoWhenNoSubAntennasAreEnabled() {
        const QString map = QStringLiteral("12") + QString(30, QLatin1Char('D'));
        const QVector<int> connectors = Kpa1500Antennas::parseEnableMap(map);
        QCOMPARE(connectors.size(), 32);
        QCOMPARE(connectors[0], 1);
        QCOMPARE(connectors[1], 2);
        for (int i = 2; i < 32; ++i)
            QCOMPARE(connectors[i], 0);
    }

    void theReferenceExampleMapsEachAntennaToItsConnector() {
        const QVector<int> connectors =
            Kpa1500Antennas::parseEnableMap(QStringLiteral("12DDDDD1111122222DDDDD1111122222"));
        QCOMPARE(connectors.size(), 32);
        for (int antenna = 3; antenna <= 7; ++antenna)
            QCOMPARE(connectors[antenna - 1], 0);
        for (int antenna = 8; antenna <= 12; ++antenna)
            QCOMPARE(connectors[antenna - 1], 1);
        for (int antenna = 13; antenna <= 17; ++antenna)
            QCOMPARE(connectors[antenna - 1], 2);
        QCOMPARE(connectors[31], 2);
    }

    void aMalformedMapIsRejected() {
        QVERIFY(Kpa1500Antennas::parseEnableMap(QString()).isEmpty());
        QVERIFY(Kpa1500Antennas::parseEnableMap(QStringLiteral("12DDD")).isEmpty());
        QVERIFY(Kpa1500Antennas::parseEnableMap(QStringLiteral("12") + QString(31, QLatin1Char('D'))).isEmpty());
        QVERIFY(
            Kpa1500Antennas::parseEnableMap(QStringLiteral("12") + QString(29, QLatin1Char('D')) + QStringLiteral("3"))
                .isEmpty());
    }

    void antennasOneAndTwoAreLabelledByConnectorOnly() {
        QCOMPARE(Kpa1500Antennas::label(1, 1), QStringLiteral("ANT1"));
        QCOMPARE(Kpa1500Antennas::label(2, 2), QStringLiteral("ANT2"));
        QCOMPARE(Kpa1500Antennas::label(1, 0), QStringLiteral("ANT1"));
    }

    void aSubAntennaIsLabelledConnectorColonNumber() {
        QCOMPARE(Kpa1500Antennas::label(5, 1), QStringLiteral("ANT1:5"));
        QCOMPARE(Kpa1500Antennas::label(17, 2), QStringLiteral("ANT2:17"));
        QCOMPARE(Kpa1500Antennas::label(32, 2), QStringLiteral("ANT2:32"));
    }

    void aSubAntennaWithUnknownConnectorShowsItsNumber() {
        QCOMPARE(Kpa1500Antennas::label(5, 0), QStringLiteral("ANT:5"));
    }

    void anOutOfRangeAntennaHasNoLabel() {
        QVERIFY(Kpa1500Antennas::label(0, 1).isEmpty());
        QVERIFY(Kpa1500Antennas::label(33, 1).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestKpa1500Antennas)
#include "test_kpa1500antennas.moc"
