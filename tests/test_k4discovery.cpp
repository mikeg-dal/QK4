#include <QTest>
#include "network/k4discovery.h"

// K4 discovery replies (K4Discovery::parseResponse): the address in a reply becomes the host QK4
// connects to, so a reply is accepted only when every field is well formed.
class TestK4Discovery : public QObject {
    Q_OBJECT

private slots:
    void wellFormedReplyIsRead() {
        const std::optional<K4RadioInfo> info = K4Discovery::parseResponse("k4:0:192.168.1.100:278");
        QVERIFY(info.has_value());
        QCOMPARE(info->rigType, QStringLiteral("k4"));
        QCOMPARE(info->rigIndex, 0);
        QCOMPARE(info->ipAddress, QStringLiteral("192.168.1.100"));
        QCOMPARE(info->serialNumber, QStringLiteral("278"));
        QCOMPARE(info->hostname(), QStringLiteral("K4-SN00278.local"));
        QVERIFY(!info->isK4Zero());
    }

    void k4ZeroReplyIsRead() {
        const std::optional<K4RadioInfo> info = K4Discovery::parseResponse("k4z:0:10.0.0.7:1641");
        QVERIFY(info.has_value());
        QVERIFY(info->isK4Zero());
        QCOMPARE(info->hostname(), QStringLiteral("K4Z-SN01641.local"));
    }

    // A terminator or line ending on the reply must not end up in the host name.
    void trailingWhitespaceAndNulsAreIgnored() {
        const std::optional<K4RadioInfo> info =
            K4Discovery::parseResponse(QByteArray("k4:0:192.168.1.100:278\r\n\0\0", 26));
        QVERIFY(info.has_value());
        QCOMPARE(info->ipAddress, QStringLiteral("192.168.1.100"));
        QCOMPARE(info->serialNumber, QStringLiteral("278"));

        const std::optional<K4RadioInfo> spaced = K4Discovery::parseResponse("k4:0: 192.168.1.100 :278");
        QVERIFY(spaced.has_value());
        QCOMPARE(spaced->ipAddress, QStringLiteral("192.168.1.100"));
    }

    void malformedRepliesAreRefused_data() {
        QTest::addColumn<QByteArray>("reply");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("our own probe") << QByteArray("findk4");
        QTest::newRow("other device") << QByteArray("kx3:0:192.168.1.100:278");
        QTest::newRow("too few fields") << QByteArray("k4:0:192.168.1.100");
        QTest::newRow("too many fields") << QByteArray("k4:0:192.168.1.100:278:9");
        QTest::newRow("hostname, not an address") << QByteArray("k4:0:evil.example.com:278");
        QTest::newRow("partial address") << QByteArray("k4:0:192.168.1:278");
        QTest::newRow("octet out of range") << QByteArray("k4:0:192.168.1.300:278");
        QTest::newRow("leading-zero octet") << QByteArray("k4:0:192.168.01.100:278");
        QTest::newRow("empty address") << QByteArray("k4:0::278");
        QTest::newRow("non-numeric index") << QByteArray("k4:x:192.168.1.100:278");
        QTest::newRow("non-numeric serial") << QByteArray("k4:0:192.168.1.100:27a");
        QTest::newRow("empty serial") << QByteArray("k4:0:192.168.1.100:");
    }
    void malformedRepliesAreRefused() {
        QFETCH(QByteArray, reply);
        QVERIFY(!K4Discovery::parseResponse(reply).has_value());
    }
};

QTEST_GUILESS_MAIN(TestK4Discovery)
#include "test_k4discovery.moc"
