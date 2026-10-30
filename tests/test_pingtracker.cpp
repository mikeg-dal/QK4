#include <QTest>
#include "network/pingtracker.h"

class TestPingTracker : public QObject {
    Q_OBJECT

private slots:
    void pongAnswersItsOwnPing() {
        PingTracker t;
        t.pingSent(1000, 0);
        QCOMPARE(t.onCatPayload(u"PONG1000;", 42), qint64(42));
        QCOMPARE(t.outstanding(), std::size_t(0));
    }

    // TRN-003: a ping goes out every second. A 1.5 s round trip must read 1.5 s, not the 0.5 s since
    // the next ping (the old timer was restarted on every ping).
    void roundTripLongerThanThePingIntervalIsNotWrapped() {
        PingTracker t;
        t.pingSent(1000, 0);
        t.pingSent(1001, 1000);
        QCOMPARE(t.onCatPayload(u"PONG1000;", 1500), qint64(1500));
        QCOMPARE(t.onCatPayload(u"PONG1001;", 2500), qint64(1500));
    }

    // TRN-003: the K4 bundles several replies in one CAT packet; a PONG that is not first counts.
    void pongBundledAfterOtherRepliesIsFound() {
        PingTracker t;
        t.pingSent(1000, 100);
        QCOMPARE(t.onCatPayload(u"FA00014074000;SM0012;PONG1000;MD2;", 130), qint64(30));
    }

    void payloadWithoutPongMeasuresNothing() {
        PingTracker t;
        t.pingSent(1000, 0);
        QCOMPARE(t.onCatPayload(u"FA00014074000;PO1;", 10), qint64(-1));
        QCOMPARE(t.outstanding(), std::size_t(1));
    }

    // Unverified on the radio whether the PONG echoes the key, so a bare one answers the oldest.
    void barePongAnswersTheOldestPing() {
        PingTracker t;
        t.pingSent(1000, 0);
        t.pingSent(1001, 1000);
        QCOMPARE(t.onCatPayload(u"PONG;", 1200), qint64(1200));
        QCOMPARE(t.onCatPayload(u"PONG", 1300), qint64(300));
        QCOMPARE(t.onCatPayload(u"PONG;", 1400), qint64(-1));
    }

    // The epoch second can repeat when the 1 s timer fires early; TCP keeps the replies in order.
    void repeatedKeyMatchesOldestFirst() {
        PingTracker t;
        t.pingSent(1000, 0);
        t.pingSent(1000, 950);
        QCOMPARE(t.onCatPayload(u"PONG1000;", 1000), qint64(1000));
        QCOMPARE(t.onCatPayload(u"PONG1000;", 1010), qint64(60));
    }

    void answeringANewerPingDropsOlderOnesAsLost() {
        PingTracker t;
        t.pingSent(1, 0);
        t.pingSent(2, 1000);
        t.pingSent(3, 2000);
        QCOMPARE(t.onCatPayload(u"PONG2;", 2100), qint64(1100));
        QCOMPARE(t.outstanding(), std::size_t(1));
        QCOMPARE(t.onCatPayload(u"PONG1;", 2200), qint64(-1));
    }

    void unknownOrMalformedKeyIsIgnored() {
        PingTracker t;
        t.pingSent(1000, 0);
        QCOMPARE(t.onCatPayload(u"PONG999;", 10), qint64(-1));
        QCOMPARE(t.onCatPayload(u"PONGx;", 10), qint64(-1));
        QCOMPARE(t.outstanding(), std::size_t(1));
    }

    void twoPongsInOnePayloadReportTheLast() {
        PingTracker t;
        t.pingSent(1, 0);
        t.pingSent(2, 1000);
        QCOMPARE(t.onCatPayload(u"PONG1;PONG2;", 1300), qint64(300));
        QCOMPARE(t.outstanding(), std::size_t(0));
    }

    void outstandingPingsAreBounded() {
        PingTracker t;
        for (qint64 i = 0; i < 100; ++i)
            t.pingSent(i, i * 1000);
        QCOMPARE(t.outstanding(), PingTracker::kMaxOutstanding);
        QCOMPARE(t.onCatPayload(u"PONG99;", 99'010), qint64(10));
        QCOMPARE(t.onCatPayload(u"PONG0;", 99'020), qint64(-1)); // long since forgotten
    }

    void resetForgetsEverything() {
        PingTracker t;
        t.pingSent(1000, 0);
        t.reset();
        QCOMPARE(t.onCatPayload(u"PONG1000;", 10), qint64(-1));
    }
};

QTEST_GUILESS_MAIN(TestPingTracker)
#include "test_pingtracker.moc"
