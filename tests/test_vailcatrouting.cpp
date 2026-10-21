#include "hardware/vail_cat_routing.h"
#include <QtTest>

class TestVailCatRouting : public QObject {
    Q_OBJECT

private slots:
    void ordinaryEdges() {
        VailCatRouting::Control control;
        VailCatRouting::Route route;
        const auto epoch = control.invalidate();
        control.enabled.store(true);
        QVERIFY(route.accept("KZD0000;", epoch, control, true));
        QVERIFY(route.accept("KZU0060;", epoch, control, true));
        QVERIFY(route.accept("KZD0060;", epoch, control, true));
        QVERIFY(route.accept("KZU0180;", epoch, control, true));
    }

    void queuedDownInvalidatedBeforeWorkerDisables() {
        VailCatRouting::Control control;
        VailCatRouting::Route route;
        const auto oldEpoch = control.invalidate();
        control.enabled.store(true);
        control.invalidate(); // GUI mode/transport/KPOD change while DOWN waits on I/O.
        QVERIFY(!route.accept("KZD0000;", oldEpoch, control, true));
        QVERIFY(!route.accept("KZU0060;", oldEpoch, control, true));
        QVERIFY(!route.accept("RX;", oldEpoch, control, true));
    }

    void acceptedDownGetsCleanupAfterGateCloses() {
        VailCatRouting::Control control;
        VailCatRouting::Route route;
        const auto oldEpoch = control.invalidate();
        control.enabled.store(true);
        QVERIFY(route.accept("KZD0000;", oldEpoch, control, true));
        control.invalidate();
        QVERIFY(route.accept("KZU0060;", oldEpoch, control, false));
        QVERIFY(route.accept("RX;", oldEpoch, control, false));
        QVERIFY(!route.accept("RX;", oldEpoch, control, false));
    }

    void reenableDoesNotResurrectQueuedDown() {
        VailCatRouting::Control control;
        VailCatRouting::Route route;
        const auto oldEpoch = control.invalidate();
        control.enabled.store(true);
        control.invalidate();
        const auto newEpoch = control.invalidate();
        control.enabled.store(true);
        QVERIFY(!route.accept("KZD0000;", oldEpoch, control, true));
        QVERIFY(route.accept("KZD0000;", newEpoch, control, true));
        QVERIFY(!route.accept("KZU0060;", oldEpoch, control, true));
        QVERIFY(!route.accept("RX;", oldEpoch, control, true));
        QVERIFY(route.accept("KZU0180;", newEpoch, control, true));
    }

    void droppedDownCannotReusePreviousReleaseForAbort() {
        VailCatRouting::Control control;
        VailCatRouting::Route route;
        const auto epoch = control.invalidate();
        control.enabled.store(true);
        QVERIFY(route.accept("KZD0000;", epoch, control, true));
        QVERIFY(route.accept("KZU0060;", epoch, control, true));
        control.invalidate();
        QVERIFY(!route.accept("KZD0060;", epoch, control, false));
        QVERIFY(!route.accept("KZU0060;", epoch, control, false));
        QVERIFY(!route.accept("RX;", epoch, control, false));
    }

    void reconnectRejectsOldCleanup() {
        VailCatRouting::Control control;
        VailCatRouting::Route route;
        const auto oldEpoch = control.invalidate();
        control.enabled.store(true);
        QVERIFY(route.accept("KZD0000;", oldEpoch, control, true));
        control.invalidate();
        route.reset();
        const auto newEpoch = control.invalidate();
        control.enabled.store(true);
        QVERIFY(!route.accept("KZU0060;", oldEpoch, control, true));
        QVERIFY(!route.accept("RX;", oldEpoch, control, true));
        QVERIFY(route.accept("KZD0000;", newEpoch, control, true));
    }

    void incompatibleOwnerRejectsStart() {
        VailCatRouting::Control control;
        VailCatRouting::Route route;
        const auto epoch = control.invalidate();
        control.enabled.store(true);
        QVERIFY(!route.accept("KZD0000;", epoch, control, false));
        QVERIFY(!route.accept("KZU0060;", epoch, control, false));
        QVERIFY(!route.accept("RX;", epoch, control, false));
    }
};

QTEST_APPLESS_MAIN(TestVailCatRouting)
#include "test_vailcatrouting.moc"
