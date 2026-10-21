#include <QtTest>
#include "hardware/vail_sidetone_routing.h"

class TestVailSidetoneRouting : public QObject {
    Q_OBJECT

private slots:
    void currentSessionFollowsDownAndUp() {
        VailCatRouting::Control control;
        control.epoch.store(7);
        control.enabled.store(true);
        VailSidetoneRouting::Route route;
        QVERIFY(route.accept(true, 7, 3, 3, control, true));
        QVERIFY(route.accept(false, 7, 3, 3, control, true));
        QVERIFY(!route.accept(false, 7, 3, 3, control, true));
    }

    void downRequiresEveryGateAndCurrentGeneration() {
        VailCatRouting::Control control;
        control.epoch.store(7);
        control.enabled.store(true);
        VailSidetoneRouting::Route route;
        QVERIFY(!route.accept(true, 7, 3, 3, control, false));
        QVERIFY(!route.accept(true, 6, 3, 3, control, true));
        QVERIFY(!route.accept(true, 7, 2, 3, control, true));
        control.enabled.store(false);
        QVERIFY(!route.accept(true, 7, 3, 3, control, true));
        QVERIFY(!route.accept(false, 7, 3, 3, control, false));
    }

    void matchingCleanupReleasesAfterGateInvalidation() {
        VailCatRouting::Control control;
        control.epoch.store(7);
        control.enabled.store(true);
        VailSidetoneRouting::Route route;
        QVERIFY(route.accept(true, 7, 3, 3, control, true));
        control.invalidate();
        QVERIFY(route.accept(false, 7, 4, 4, control, false));
        QVERIFY(!route.accept(false, 7, 4, 4, control, false));
    }

    void oldSourceEventsCannotReviveOrSilenceNewTone() {
        VailCatRouting::Control control;
        control.epoch.store(7);
        control.enabled.store(true);
        VailSidetoneRouting::Route route;
        QVERIFY(route.accept(true, 7, 3, 3, control, true));
        route.reset();
        QVERIFY(!route.accept(true, 7, 3, 5, control, true));
        QVERIFY(route.accept(true, 7, 5, 5, control, true));
        QVERIFY(!route.accept(false, 7, 3, 5, control, true));
        QVERIFY(route.accept(false, 7, 5, 5, control, true));
    }

    void oldWorkerCleanupCannotSilenceReplacement() {
        VailCatRouting::Control control;
        control.epoch.store(7);
        control.enabled.store(true);
        VailSidetoneRouting::Route route;
        QVERIFY(route.accept(true, 7, 3, 3, control, true));
        route.reset();
        control.invalidate();
        control.enabled.store(true);
        QVERIFY(route.accept(true, 8, 4, 4, control, true));
        QVERIFY(!route.accept(false, 7, 3, 4, control, false));
        QVERIFY(route.accept(false, 8, 4, 4, control, true));
    }

    void staleLifecycleResetCannotStopNewTone() {
        VailCatRouting::Control control;
        control.epoch.store(7);
        control.enabled.store(true);
        VailSidetoneRouting::Route route;
        QVERIFY(route.accept(true, 7, 5, 5, control, true));
        QVERIFY(!route.resetIfCurrent(3, 5));
        QVERIFY(route.accept(false, 7, 5, 5, control, true));
        QVERIFY(route.accept(true, 7, 5, 5, control, true));
        QVERIFY(route.resetIfCurrent(5, 5));
        QVERIFY(!route.accept(false, 7, 5, 5, control, true));
    }
};

QTEST_APPLESS_MAIN(TestVailSidetoneRouting)
#include "test_vailsidetonerouting.moc"
