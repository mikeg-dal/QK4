#include <QTest>

#include "network/dxclusterclient.h"

class TestDxCluster : public QObject {
    Q_OBJECT

private slots:
    void parseStandardCwSpot();
    void parseFt8Spot();
    void parseHighFreqSpot();
    void parseLowFreqSpot();
    void parseDecimalFreq();
    void parseQrpSuffix();
    void parseMissingMode();
    void parseAlignedSpot();
    void rejectLoginPrompt();
    void rejectBanner();
    void rejectBlankLine();
    void rejectPartialLine();
    void frequencyConversion();
};

void TestDxCluster::parseStandardCwSpot() {
    DxSpot spot;
    bool ok = DxClusterClient::parseSpotLine(
        "DX de K3GMQ-#:  14031.00  K0RX           CW    18 dB  24 WPM  CQ      1902Z", spot);
    QVERIFY(ok);
    QCOMPARE(spot.spotterCall, "K3GMQ-#");
    QCOMPARE(spot.frequencyHz, 14031000LL);
    QCOMPARE(spot.spottedCall, "K0RX");
    QCOMPARE(spot.mode, "CW");
    QCOMPARE(spot.timeUtc, "1902Z");
    QVERIFY(!spot.timestamp.isNull());
}

void TestDxCluster::parseFt8Spot() {
    DxSpot spot;
    bool ok = DxClusterClient::parseSpotLine(
        "DX de W3LPL:    14074.00  JA1ABC         FT8   -12 dB                  1903Z", spot);
    QVERIFY(ok);
    QCOMPARE(spot.frequencyHz, 14074000LL);
    QCOMPARE(spot.spottedCall, "JA1ABC");
    QCOMPARE(spot.mode, "FT8");
}

void TestDxCluster::parseHighFreqSpot() {
    DxSpot spot;
    bool ok = DxClusterClient::parseSpotLine(
        "DX de K2PO/7-#: 28039.00  XE1AY          CW     4 dB  24 WPM  CQ      1902Z", spot);
    QVERIFY(ok);
    QCOMPARE(spot.frequencyHz, 28039000LL);
    QCOMPARE(spot.spottedCall, "XE1AY");
    QCOMPARE(spot.spotterCall, "K2PO/7-#");
}

void TestDxCluster::parseLowFreqSpot() {
    DxSpot spot;
    bool ok = DxClusterClient::parseSpotLine(
        "DX de W1AW:      1825.00  K1TTT          CW    30 dB  18 WPM  CQ      2345Z", spot);
    QVERIFY(ok);
    QCOMPARE(spot.frequencyHz, 1825000LL);
    QCOMPARE(spot.spottedCall, "K1TTT");
}

void TestDxCluster::parseDecimalFreq() {
    DxSpot spot;
    bool ok = DxClusterClient::parseSpotLine(
        "DX de MM0ZBH-#:  7026.50  F4FSZ          CW     8 dB  25 WPM  CQ      1902Z", spot);
    QVERIFY(ok);
    QCOMPARE(spot.frequencyHz, 7026500LL);
}

void TestDxCluster::parseQrpSuffix() {
    DxSpot spot;
    bool ok = DxClusterClient::parseSpotLine(
        "DX de DK3WW-#:  14060.00  EA5EQ/QRP      CW    16 dB  15 WPM  CQ      1902Z", spot);
    QVERIFY(ok);
    QCOMPARE(spot.spottedCall, "EA5EQ/QRP");
    QCOMPARE(spot.frequencyHz, 14060000LL);
}

void TestDxCluster::parseMissingMode() {
    DxSpot spot;
    bool ok = DxClusterClient::parseSpotLine(
        "DX de W1AW:     14250.00  VK3ABC         some comment here             2100Z", spot);
    QVERIFY(ok);
    QCOMPARE(spot.frequencyHz, 14250000LL);
    QCOMPARE(spot.spottedCall, "VK3ABC");
    QVERIFY(spot.mode.isEmpty()); // No recognized mode in comment
}

void TestDxCluster::parseAlignedSpot() {
    // Test the exact format from RBN with extra whitespace alignment
    DxSpot spot;
    bool ok = DxClusterClient::parseSpotLine(
        "DX de SP5GQ-#:  10103.50  HB9CVQ         CW    39 dB  24 WPM  CQ      1902Z", spot);
    QVERIFY(ok);
    QCOMPARE(spot.frequencyHz, 10103500LL);
    QCOMPARE(spot.spottedCall, "HB9CVQ");
    QCOMPARE(spot.spotterCall, "SP5GQ-#");
    QCOMPARE(spot.mode, "CW");
}

void TestDxCluster::rejectLoginPrompt() {
    DxSpot spot;
    QVERIFY(!DxClusterClient::parseSpotLine("Please enter your call:", spot));
}

void TestDxCluster::rejectBanner() {
    DxSpot spot;
    QVERIFY(!DxClusterClient::parseSpotLine("Hello, AI5QK! Connected.", spot));
}

void TestDxCluster::rejectBlankLine() {
    DxSpot spot;
    QVERIFY(!DxClusterClient::parseSpotLine("", spot));
}

void TestDxCluster::rejectPartialLine() {
    DxSpot spot;
    QVERIFY(!DxClusterClient::parseSpotLine("DX de", spot));
    QVERIFY(!DxClusterClient::parseSpotLine("AI5QK de RELAY 06-Apr-2026 19:02Z >", spot));
}

void TestDxCluster::frequencyConversion() {
    DxSpot spot;
    // Verify kHz to Hz conversion precision
    DxClusterClient::parseSpotLine("DX de W1AW:     14074.10  JA1ABC         FT8   -12 dB                  1903Z",
                                   spot);
    QCOMPARE(spot.frequencyHz, 14074100LL);

    DxClusterClient::parseSpotLine("DX de W1AW:      3573.00  W2AAA          FT8   -10 dB                  1903Z",
                                   spot);
    QCOMPARE(spot.frequencyHz, 3573000LL);

    // Integer frequency (no decimal)
    DxClusterClient::parseSpotLine("DX de W1AW:     14074  JA1ABC         FT8   -12 dB                  1903Z", spot);
    QCOMPARE(spot.frequencyHz, 14074000LL);
}

QTEST_MAIN(TestDxCluster)

#include "test_dxcluster.moc"
