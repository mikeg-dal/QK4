#include "hardware/cwmonitormute.h"
#include "hardware/vail_cat_routing.h"
#include <QtTest>

class TestCwMonitorMute : public QObject {
    Q_OBJECT

    static void configure(CwMonitorMute::State &state) {
        state.setEnabled(true, 0);
        state.observeLevel(25, 0);
        state.setKeyDownDelay(80, 0);
    }

private slots:
    void restoresOnceAfterRadioTail() {
        CwMonitorMute::State state;
        configure(state);
        const auto down = state.accept("KZD0000;", 100);
        QCOMPARE(down.command, QStringLiteral("ML0000;"));
        QCOMPARE(down.restoreAtMs, -1);
        const auto up = state.accept("KZU0060;", 160);
        QVERIFY(up.command.isEmpty());
        QCOMPARE(up.restoreAtMs, 740);
        QVERIFY(state.pollRestore(up.generation, 739).command.isEmpty());
        QCOMPARE(state.pollRestore(up.generation, 740).command, QStringLiteral("ML0025;"));
        QVERIFY(state.pollRestore(up.generation, 741).command.isEmpty());
    }

    void earlyWakeRetainsDeadlineAndRestoresExactlyOnce() {
        CwMonitorMute::State state;
        configure(state);
        state.accept("KZD0000;", 0);
        const auto pending = state.accept("KZU0060;", 60);
        for (qint64 nowMs : {qint64{61}, pending.restoreAtMs - 1, pending.restoreAtMs - 1}) {
            const auto early = state.pollRestore(pending.generation, nowMs);
            QVERIFY(early.command.isEmpty());
            QCOMPARE(early.restoreAtMs, pending.restoreAtMs);
            QCOMPARE(early.generation, pending.generation);
        }
        const auto due = state.pollRestore(pending.generation, pending.restoreAtMs);
        QCOMPARE(due.command, QStringLiteral("ML0025;"));
        QCOMPARE(due.restoreAtMs, -1);
        const auto repeated = state.pollRestore(pending.generation, pending.restoreAtMs + 1);
        QVERIFY(repeated.command.isEmpty());
        QCOMPARE(repeated.restoreAtMs, -1);
    }

    void staleWakeCannotRearmExtendedDeadline() {
        CwMonitorMute::State state;
        configure(state);
        state.accept("KZD0000;", 0);
        const auto pending = state.accept("KZU0060;", 60);
        const auto extended = state.setKeyDownDelay(100, 100);
        QVERIFY(extended.generation != pending.generation);
        const auto stale = state.pollRestore(pending.generation, pending.restoreAtMs - 1);
        QVERIFY(stale.command.isEmpty());
        QCOMPARE(stale.restoreAtMs, -1);
        const auto early = state.pollRestore(extended.generation, extended.restoreAtMs - 1);
        QVERIFY(early.command.isEmpty());
        QCOMPARE(early.restoreAtMs, extended.restoreAtMs);
        QCOMPARE(state.pollRestore(extended.generation, extended.restoreAtMs).command, QStringLiteral("ML0025;"));
    }

    void heldKeyCancelsEarlyWakeUntilItsRelease() {
        CwMonitorMute::State state;
        configure(state);
        state.accept("KZD0000;", 0);
        const auto pending = state.accept("KZU0060;", 60);
        QCOMPARE(state.pollRestore(pending.generation, 100).restoreAtMs, pending.restoreAtMs);
        const auto held = state.accept("KZD0060;", 120);
        const auto stale = state.pollRestore(pending.generation, 200);
        QVERIFY(stale.command.isEmpty());
        QCOMPARE(stale.restoreAtMs, -1);
        const auto noRelease = state.pollRestore(held.generation, 200);
        QVERIFY(noRelease.command.isEmpty());
        QCOMPARE(noRelease.restoreAtMs, -1);
        const auto released = state.accept("KZU0180;", 300);
        QCOMPARE(state.pollRestore(released.generation, released.restoreAtMs - 1).restoreAtMs, released.restoreAtMs);
        QCOMPARE(state.pollRestore(released.generation, released.restoreAtMs).command, QStringLiteral("ML0025;"));
    }

    void disconnectCancelsAnEarlyWakeRetry() {
        CwMonitorMute::State state;
        configure(state);
        state.accept("KZD0000;", 0);
        const auto pending = state.accept("KZU0060;", 60);
        QCOMPARE(state.pollRestore(pending.generation, pending.restoreAtMs - 1).restoreAtMs, pending.restoreAtMs);
        QCOMPARE(state.restoreBeforeDisconnect(), QStringLiteral("ML0025;"));
        const auto disconnected = state.pollRestore(pending.generation, pending.restoreAtMs);
        QVERIFY(disconnected.command.isEmpty());
        QCOMPARE(disconnected.restoreAtMs, -1);
        const auto unknown = state.accept("KZD0000;", 1000);
        const auto nextSession = state.pollRestore(unknown.generation, 2000);
        QVERIFY(nextSession.command.isEmpty());
        QCOMPARE(nextSession.restoreAtMs, -1);
    }

    void compressedBurstPreservesEveryGapAndMark() {
        CwMonitorMute::State state;
        configure(state);
        QCOMPARE(state.accept("KZD0000;", 0).command, QStringLiteral("ML0000;"));
        QCOMPARE(state.accept("KZU0060;", 0).restoreAtMs, 640);
        QVERIFY(state.accept("KZD0060;", 0).command.isEmpty());
        const auto up = state.accept("KZU0180;", 0);
        QCOMPARE(up.restoreAtMs, 880);
        QVERIFY(state.pollRestore(up.generation, 879).command.isEmpty());
        QCOMPARE(state.pollRestore(up.generation, 880).command, QStringLiteral("ML0025;"));
    }

    void longQueuedBurstOutlastsSafetyMargin() {
        CwMonitorMute::State state;
        configure(state);
        CwMonitorMute::Action up;
        for (int index = 0; index < 10; ++index) {
            state.accept(index == 0 ? "KZD0000;" : "KZD0060;", 0);
            up = state.accept("KZU0060;", 0);
        }
        QCOMPARE(up.restoreAtMs, 1720);
        QVERIFY(state.pollRestore(up.generation, 1000).command.isEmpty());
        QCOMPARE(state.pollRestore(up.generation, 1720).command, QStringLiteral("ML0025;"));
    }

    void lateDeliveryReanchorsWithoutSchedulingInPast() {
        CwMonitorMute::State state;
        configure(state);
        state.accept("KZD0000;", 0);
        const auto lateUp = state.accept("KZU0060;", 1000);
        QCOMPARE(lateUp.restoreAtMs, 1500);
        state.accept("KZD0060;", 2000);
        const auto nextUp = state.accept("KZU0060;", 2060);
        QCOMPARE(nextUp.restoreAtMs, 2640);
    }

    void sourceHandoffInvalidatesPreviousRestore() {
        CwMonitorMute::State state;
        configure(state);
        VailCatRouting::Route route;
        VailCatRouting::Control control;
        const auto epoch = control.invalidate();
        control.enabled.store(true);
        QVERIFY(route.acceptSoftware("KZD0000;", 1, true));
        state.accept("KZD0000;", 0);
        QVERIFY(route.acceptSoftware("KZU0060;", 1, false));
        const auto oldUp = state.accept("KZU0060;", 60);
        QVERIFY(route.accept("KZD0060;", epoch, control, true));
        const auto down = state.accept("KZD0060;", 120);
        QVERIFY(down.command.isEmpty());
        QCOMPARE(down.restoreAtMs, -1);
        QVERIFY(state.pollRestore(oldUp.generation, 2000).command.isEmpty());
        QVERIFY(route.accept("KZU0180;", epoch, control, true));
        const auto up = state.accept("KZU0180;", 300);
        QCOMPARE(up.restoreAtMs, 880);
        QCOMPARE(state.pollRestore(up.generation, 880).command, QStringLiteral("ML0025;"));
    }

    void rejectedStartsCannotMute() {
        CwMonitorMute::State state;
        configure(state);
        VailCatRouting::Route route;
        VailCatRouting::Control control;
        const auto epoch = control.invalidate();
        control.enabled.store(true);
        control.invalidate();
        if (route.accept("KZD0000;", epoch, control, true))
            QFAIL("Invalidated DOWN unexpectedly accepted");
        QVERIFY(!route.accept("KZU0060;", epoch, control, true));
        const auto action = state.setEnabled(false, 100);
        QVERIFY(action.command.isEmpty());
        QCOMPARE(action.restoreAtMs, -1);
    }

    void abortReleaseKeepsConservativeTail_data() {
        QTest::addColumn<int>("heldMs");
        QTest::newRow("Escape") << 90;
        QTest::newRow("device-disconnect") << 180;
        QTest::newRow("mode-change") << 250;
        QTest::newRow("hold-timeout") << 2400;
    }

    void abortReleaseKeepsConservativeTail() {
        QFETCH(int, heldMs);
        CwMonitorMute::State state;
        configure(state);
        state.accept("KZD0000;", 0);
        const auto up = state.accept(QStringLiteral("KZU%1;").arg(heldMs, 4, 10, QLatin1Char('0')), heldMs);
        const auto rx = state.accept("RX;", heldMs);
        QCOMPARE(rx.generation, up.generation);
        QCOMPARE(rx.restoreAtMs, up.restoreAtMs);
        QCOMPARE(rx.restoreAtMs, heldMs + 580);
        QVERIFY(state.pollRestore(rx.generation, heldMs).command.isEmpty());
        QCOMPARE(state.pollRestore(rx.generation, rx.restoreAtMs).command, QStringLiteral("ML0025;"));
    }

    void disableWhileHeldRestoresImmediatelyOnce() {
        CwMonitorMute::State state;
        configure(state);
        const auto down = state.accept("KZD0000;", 0);
        const auto disabled = state.setEnabled(false, 20);
        QCOMPARE(disabled.command, QStringLiteral("ML0025;"));
        QCOMPARE(disabled.restoreAtMs, -1);
        QVERIFY(disabled.generation != down.generation);
        QVERIFY(state.setEnabled(false, 30).command.isEmpty());
        const auto up = state.accept("KZU0060;", 60);
        QCOMPARE(up.restoreAtMs, -1);
        QVERIFY(state.pollRestore(down.generation, 5000).command.isEmpty());
    }

    void disableCancelsPendingRestoreAndReenableMutesRemainingTail() {
        CwMonitorMute::State state;
        configure(state);
        state.accept("KZD0000;", 0);
        const auto oldUp = state.accept("KZU0060;", 60);
        QCOMPARE(state.setEnabled(false, 100).command, QStringLiteral("ML0025;"));
        const auto enabled = state.setEnabled(true, 200);
        QCOMPARE(enabled.command, QStringLiteral("ML0000;"));
        QCOMPARE(enabled.restoreAtMs, oldUp.restoreAtMs);
        QVERIFY(state.pollRestore(oldUp.generation, 1000).command.isEmpty());
        QCOMPARE(state.pollRestore(enabled.generation, enabled.restoreAtMs).command, QStringLiteral("ML0025;"));
    }

    void enableWhileHeldMutesImmediately() {
        CwMonitorMute::State state;
        state.observeLevel(37, 0);
        state.setKeyDownDelay(80, 0);
        QVERIFY(state.accept("KZD0000;", 0).command.isEmpty());
        QCOMPARE(state.setEnabled(true, 20).command, QStringLiteral("ML0000;"));
        const auto up = state.accept("KZU0060;", 60);
        QCOMPARE(state.pollRestore(up.generation, up.restoreAtMs).command, QStringLiteral("ML0037;"));
    }

    void ownMuteEchoCannotOverwriteSavedLevel() {
        CwMonitorMute::State state;
        configure(state);
        state.accept("KZD0000;", 0);
        const auto echo = state.observeLevel(0, 1);
        QVERIFY(echo.command.isEmpty());
        const auto up = state.accept("KZU0060;", 60);
        state.observeLevel(0, 600);
        QCOMPARE(state.pollRestore(up.generation, up.restoreAtMs).command, QStringLiteral("ML0025;"));
    }

    void operatorLevelChangesRemuteAndBecomeRestoreLevel() {
        CwMonitorMute::State state;
        configure(state);
        state.accept("KZD0000;", 0);
        QCOMPARE(state.observeLevel(42, 20).command, QStringLiteral("ML0000;"));
        const auto up = state.accept("KZU0060;", 60);
        const auto changed = state.observeLevel(63, 100);
        QCOMPARE(changed.command, QStringLiteral("ML0000;"));
        QCOMPARE(changed.restoreAtMs, up.restoreAtMs);
        QCOMPARE(state.pollRestore(changed.generation, changed.restoreAtMs).command, QStringLiteral("ML0063;"));
    }

    void unknownAndAlreadyZeroLevelsDoNotNeedRestoration() {
        CwMonitorMute::State state;
        state.setEnabled(true, 0);
        QVERIFY(state.accept("KZD0000;", 0).command.isEmpty());
        const auto up = state.accept("KZU0060;", 60);
        QCOMPARE(up.restoreAtMs, -1);
        QVERIFY(state.pollRestore(up.generation, 5000).command.isEmpty());
        state.observeLevel(0, 5000);
        QVERIFY(state.accept("KZD0000;", 5000).command.isEmpty());
        QCOMPARE(state.observeLevel(30, 5010).command, QStringLiteral("ML0000;"));
        const auto nextUp = state.accept("KZU0060;", 5060);
        QCOMPARE(state.pollRestore(nextUp.generation, nextUp.restoreAtMs).command, QStringLiteral("ML0030;"));
    }

    void unknownLevelCanBeLearnedWhileHeld() {
        CwMonitorMute::State state;
        state.setEnabled(true, 0);
        state.accept("KZD0000;", 0);
        QCOMPARE(state.observeLevel(20, 10).command, QStringLiteral("ML0000;"));
        const auto up = state.accept("KZU0060;", 60);
        QCOMPARE(state.pollRestore(up.generation, up.restoreAtMs).command, QStringLiteral("ML0020;"));
    }

    void unknownDelayUsesMaximumAndChangesCannotShortenQueuedTail() {
        CwMonitorMute::State state;
        state.setEnabled(true, 0);
        state.observeLevel(25, 0);
        state.accept("KZD0000;", 0);
        const auto up = state.accept("KZU0060;", 60);
        QCOMPARE(up.restoreAtMs, 1560);
        const auto lower = state.setKeyDownDelay(80, 100);
        QCOMPARE(lower.restoreAtMs, up.restoreAtMs);
        const auto higher = state.setKeyDownDelay(100, 110);
        QCOMPARE(higher.restoreAtMs, 1580);
        QVERIFY(higher.generation != up.generation);
        QVERIFY(state.pollRestore(up.generation, 2000).command.isEmpty());
        QCOMPARE(state.pollRestore(higher.generation, higher.restoreAtMs).command, QStringLiteral("ML0025;"));
    }

    void resetDropsOldSocketTimerAndRequiresNewLevel() {
        CwMonitorMute::State state;
        configure(state);
        state.accept("KZD0000;", 0);
        const auto oldUp = state.accept("KZU0060;", 60);
        state.reset();
        QVERIFY(state.pollRestore(oldUp.generation, 5000).command.isEmpty());
        QVERIFY(state.accept("KZD0000;", 1000).command.isEmpty());
        QCOMPARE(state.observeLevel(44, 1010).command, QStringLiteral("ML0000;"));
        const auto up = state.accept("KZU0060;", 1060);
        QCOMPARE(up.restoreAtMs, 2560);
        QCOMPARE(state.pollRestore(up.generation, up.restoreAtMs).command, QStringLiteral("ML0044;"));
    }

    void parsesRadioLevelAndDelayResponsesInOrder() {
        CwMonitorMute::State state;
        state.setEnabled(true, 0);
        state.observeResponse("KS020;ML0025;KZL0080;", 0);
        QCOMPARE(state.accept("KZD0000;", 0).command, QStringLiteral("ML0000;"));
        const auto observed = state.observeResponse("ML0000;ML0042;ML0063;", 20);
        QCOMPARE(observed.command, QStringLiteral("ML0000;ML0000;"));
        const auto up = state.accept("KZU0060;", 60);
        QCOMPARE(up.restoreAtMs, 640);
        QCOMPARE(state.pollRestore(up.generation, up.restoreAtMs).command, QStringLiteral("ML0063;"));
    }

    void acceptsPaddedAndUnpaddedDelayResponses_data() {
        QTest::addColumn<QString>("response");
        QTest::addColumn<int>("delayMs");
        QTest::newRow("zero") << QStringLiteral("KZL0;") << 0;
        QTest::newRow("sixty") << QStringLiteral("KZL60;") << 60;
        QTest::newRow("five-hundred") << QStringLiteral("KZL500;") << 500;
        QTest::newRow("maximum") << QStringLiteral("KZL1000;") << 1000;
        QTest::newRow("padded-zero") << QStringLiteral("KZL0000;") << 0;
        QTest::newRow("padded-sixty") << QStringLiteral("KZL0060;") << 60;
        QTest::newRow("partly-padded") << QStringLiteral("KZL060;") << 60;
    }

    void acceptsPaddedAndUnpaddedDelayResponses() {
        QFETCH(QString, response);
        QFETCH(int, delayMs);
        CwMonitorMute::State state;
        state.setEnabled(true, 0);
        state.observeLevel(25, 0);
        state.observeResponse(response, 0);
        state.accept("KZD0000;", 0);
        const auto up = state.accept("KZU0060;", 60);
        QCOMPARE(up.restoreAtMs, delayMs + 560);
        QCOMPARE(state.pollRestore(up.generation, up.restoreAtMs).command, QStringLiteral("ML0025;"));
    }

    void acceptsPaddedAndUnpaddedMonitorResponses_data() {
        QTest::addColumn<QString>("response");
        QTest::addColumn<int>("level");
        QTest::newRow("zero") << QStringLiteral("ML00;") << 0;
        QTest::newRow("five") << QStringLiteral("ML05;") << 5;
        QTest::newRow("twenty-five") << QStringLiteral("ML025;") << 25;
        QTest::newRow("maximum") << QStringLiteral("ML0100;") << 100;
        QTest::newRow("padded-zero") << QStringLiteral("ML0000;") << 0;
        QTest::newRow("padded-five") << QStringLiteral("ML0005;") << 5;
        QTest::newRow("padded-twenty-five") << QStringLiteral("ML0025;") << 25;
        QTest::newRow("partly-padded") << QStringLiteral("ML005;") << 5;
    }

    void acceptsPaddedAndUnpaddedMonitorResponses() {
        QFETCH(QString, response);
        QFETCH(int, level);
        CwMonitorMute::State state;
        state.setEnabled(true, 0);
        state.observeResponse(response, 0);
        state.setKeyDownDelay(80, 0);
        const auto down = state.accept("KZD0000;", 0);
        const auto up = state.accept("KZU0060;", 60);
        if (level == 0) {
            QVERIFY(down.command.isEmpty());
            QCOMPARE(up.restoreAtMs, -1);
        } else {
            QCOMPARE(down.command, QStringLiteral("ML0000;"));
            QCOMPARE(state.pollRestore(up.generation, up.restoreAtMs).command,
                     QStringLiteral("ML0%1;").arg(level, 3, 10, QLatin1Char('0')));
        }
    }

    void parserIgnoresOtherModesMalformedAndUnterminatedResponses() {
        CwMonitorMute::State state;
        configure(state);
        state.accept("KZD0000;", 0);
        const auto up = state.accept("KZU0060;", 60);
        const QStringList responses{"ML1063;", "ML2063;",   "ML0101;",  "ML0-01;", "ML0+25;",   "ML0;",     "ML00025;",
                                    "ML00a5;", "ML0025x;",  " ML0040;", "ML0040",  "KZL1001;",  "KZL-080;", "KZL+80;",
                                    "KZL;",    "KZL00080;", "KZL00a0;", "KZL1000", "KZL1000x;", ";;;"};
        for (const auto &response : responses) {
            const auto action = state.observeResponse(response, 100);
            QVERIFY(action.command.isEmpty());
            QCOMPARE(action.generation, up.generation);
            QCOMPARE(action.restoreAtMs, up.restoreAtMs);
        }
        QCOMPARE(state.pollRestore(up.generation, up.restoreAtMs).command, QStringLiteral("ML0025;"));
    }

    void delayResponseExtendsPendingTimer() {
        CwMonitorMute::State state;
        configure(state);
        state.accept("KZD0000;", 0);
        const auto up = state.accept("KZU0060;", 60);
        const auto observed = state.observeResponse("KZL0100;ML0000;", 100);
        QCOMPARE(observed.restoreAtMs, 660);
        QVERIFY(state.pollRestore(up.generation, 1000).command.isEmpty());
        QCOMPARE(state.pollRestore(observed.generation, observed.restoreAtMs).command, QStringLiteral("ML0025;"));
    }

    void gracefulDisconnectRestoresOwnedLevelAndClearsSocketState() {
        CwMonitorMute::State state;
        configure(state);
        state.accept("KZD0000;", 0);
        const auto up = state.accept("KZU0060;", 60);
        QCOMPARE(state.restoreBeforeDisconnect(), QStringLiteral("ML0025;"));
        QVERIFY(state.pollRestore(up.generation, 5000).command.isEmpty());
        QVERIFY(state.restoreBeforeDisconnect().isEmpty());
        QVERIFY(state.accept("KZD0000;", 1000).command.isEmpty());
        state.observeResponse("ML0044;KZL0080;", 1010);
        const auto nextUp = state.accept("KZU0060;", 1060);
        QCOMPARE(state.pollRestore(nextUp.generation, nextUp.restoreAtMs).command, QStringLiteral("ML0044;"));
    }

    void gracefulDisconnectWhileHeldRestoresAndDropsHeldState() {
        CwMonitorMute::State state;
        configure(state);
        state.accept("KZD0000;", 0);
        QCOMPARE(state.restoreBeforeDisconnect(), QStringLiteral("ML0025;"));
        state.observeResponse("ML0037;KZL0080;", 1000);
        QCOMPARE(state.accept("KZD0000;", 1000).command, QStringLiteral("ML0000;"));
        const auto up = state.accept("KZU0060;", 1060);
        QCOMPARE(up.restoreAtMs, 1640);
        QCOMPARE(state.pollRestore(up.generation, up.restoreAtMs).command, QStringLiteral("ML0037;"));
    }

    void unrelatedAndMalformedCommandsCannotChangeTimer() {
        CwMonitorMute::State state;
        configure(state);
        state.accept("KZD0000;", 0);
        const auto up = state.accept("KZU0060;", 60);
        const QStringList commands{"RX;",     "ML0000;",   "KZD;",     "KZU006;",  "KZD+060;",
                                   "KZD0060", "KZD0060;;", "KZX0060;", "KZD006a;", "KZD0000;KZU0060;"};
        for (const auto &command : commands) {
            const auto action = state.accept(command, 100);
            QVERIFY(action.command.isEmpty());
            QCOMPARE(action.generation, up.generation);
            QCOMPARE(action.restoreAtMs, up.restoreAtMs);
        }
        QCOMPARE(state.pollRestore(up.generation, up.restoreAtMs).command, QStringLiteral("ML0025;"));
    }

    void invalidConfigurationDoesNotChangeTimer() {
        CwMonitorMute::State state;
        configure(state);
        state.accept("KZD0000;", 0);
        const auto up = state.accept("KZU0060;", 60);
        QCOMPARE(state.observeLevel(-1, 100).generation, up.generation);
        QCOMPARE(state.observeLevel(101, 100).generation, up.generation);
        QCOMPARE(state.setKeyDownDelay(-1, 100).restoreAtMs, up.restoreAtMs);
        QCOMPARE(state.setKeyDownDelay(1001, 100).restoreAtMs, up.restoreAtMs);
        QCOMPARE(state.pollRestore(up.generation, up.restoreAtMs).command, QStringLiteral("ML0025;"));
    }

    void timeArithmeticSaturatesWithoutOverflow() {
        CwMonitorMute::State state;
        configure(state);
        const auto maximum = std::numeric_limits<qint64>::max();
        state.accept("KZD0000;", maximum - 10);
        const auto up = state.accept("KZU5000;", maximum - 5);
        QCOMPARE(up.restoreAtMs, maximum);
        QVERIFY(state.pollRestore(up.generation, maximum - 1).command.isEmpty());
        QCOMPARE(state.pollRestore(up.generation, maximum).command, QStringLiteral("ML0025;"));
    }
};

QTEST_APPLESS_MAIN(TestCwMonitorMute)
#include "test_cwmonitormute.moc"
