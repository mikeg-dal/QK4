// Nothing should wake the GUI thread while QK4 sits idle and disconnected (#92). Each case pins a
// widget whose timer used to run whether or not it had work. Rendered offscreen.

#include "ui/widgets/txmeterwidget.h"

#include <QTest>
#include <QTimer>

class IdleWakeupTests : public QObject {
    Q_OBJECT

private:
    static QTimer *decayTimer(TxMeterWidget &meter) {
        const auto timers = meter.findChildren<QTimer *>();
        return timers.size() == 1 ? timers.first() : nullptr;
    }

private slots:
    void txMeterDecayTimerIsIdleUntilAReadingArrives() {
        TxMeterWidget meter;
        QTimer *timer = decayTimer(meter);
        QVERIFY(timer);
        QVERIFY(!timer->isActive());

        // With nothing ever received there is nothing to decay, however long it sits.
        QTest::qWait(3 * timer->interval());
        QVERIFY(!timer->isActive());
    }

    void txMeterDecayTimerStopsOnceTheBarsSettle() {
        TxMeterWidget meter;
        QTimer *timer = decayTimer(meter);
        QVERIFY(timer);

        meter.setSMeter(9.0);
        meter.setSMeter(0.0);
        QVERIFY(timer->isActive()); // the bar and its held peak still have to fall

        // Full scale falls in ~0.5 s and the peak holds for 0.5 s before it falls too.
        QTRY_VERIFY_WITH_TIMEOUT(!timer->isActive(), 5000);
        QTest::qWait(3 * timer->interval());
        QVERIFY(!timer->isActive());
    }

    void txMeterReadingAtRestDoesNotKeepTheTimerRunning() {
        TxMeterWidget meter;
        QTimer *timer = decayTimer(meter);
        QVERIFY(timer);

        // The radio repeats an all-zero TX meter frame; with nothing above its target the timer
        // makes one check and stops.
        meter.setTxMeters(0, 0, 0.0, 1.0);
        meter.setCurrent(0.0);
        QTRY_VERIFY_WITH_TIMEOUT(!timer->isActive(), 4 * timer->interval());
    }
};

QTEST_MAIN(IdleWakeupTests)
#include "test_idlewakeups.moc"
