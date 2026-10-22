#include <QtTest>
#include <limits>

#include "dsp/autoreference.h"

class TestAutoReference : public QObject {
    Q_OBJECT

private slots:
    void manualReferenceAndInitialization() {
        AutoReference::State state;
        QCOMPARE(AutoReference::reference(state, true, -110.0f), -110.0f);
        QVERIFY(!AutoReference::update(state, -100.0f, false, 12, 10, 0));
        QVERIFY(!state.initialized);
        QVERIFY(AutoReference::update(state, -100.0f, true, 12, 10, 0));
        QCOMPARE(AutoReference::reference(state, true, -110.0f), -100.0f);
        QCOMPARE(AutoReference::reference(state, false, -120.0f), -120.0f);
    }

    void averagingContinuesBetweenDebouncedUpdates() {
        AutoReference::State state;
        AutoReference::update(state, -120.0f, true, 2, 2, 0);
        QVERIFY(!AutoReference::update(state, -100.0f, true, 2, 2, 0));
        QCOMPARE(state.level, -120.0f);
        QVERIFY(AutoReference::update(state, -100.0f, true, 2, 2, 0));
        QCOMPARE(state.level, -105.0f);
        QVERIFY(!AutoReference::update(state, -100.0f, true, 2, 2, 0));
        QCOMPARE(state.level, -105.0f);
    }

    void supportsAveragingThirty() {
        AutoReference::State state;
        AutoReference::update(state, -120.0f, true, 30, 1, 0);
        QVERIFY(AutoReference::update(state, -90.0f, true, 30, 1, 0));
        QVERIFY(std::abs(state.level + 119.0f) < 0.001f);
    }

    void offsetPositionsFloorRelativeToBottom() {
        for (int offset : {-8, 0, 8}) {
            AutoReference::State state;
            AutoReference::update(state, -110.0f, true, 1, 1, offset);
            QCOMPARE(-110.0f - state.level, static_cast<float>(offset));
            AutoReference::update(state, -100.0f, true, 1, 1, offset);
            QCOMPARE(-100.0f - state.level, static_cast<float>(offset));
        }
    }

    void invalidFloorsDoNotAdvanceDebounce() {
        AutoReference::State state;
        AutoReference::update(state, -120.0f, true, 1, 2, 0);
        for (float invalid :
             {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -201.0f, 51.0f}) {
            QVERIFY(!AutoReference::update(state, invalid, true, 1, 2, 0));
            QCOMPARE(state.level, -120.0f);
        }
        QVERIFY(!AutoReference::update(state, -100.0f, true, 1, 2, 0));
        QVERIFY(AutoReference::update(state, -100.0f, true, 1, 2, 0));
        QCOMPARE(state.level, -100.0f);
    }

    void resetDiscardsPreviousFloorAndPendingUpdate() {
        AutoReference::State state;
        AutoReference::update(state, -120.0f, true, 12, 10, 0);
        AutoReference::update(state, -100.0f, true, 12, 10, 0);
        state = {};
        QCOMPARE(AutoReference::reference(state, true, -110.0f), -110.0f);
        QVERIFY(AutoReference::update(state, -90.0f, true, 12, 10, 8));
        QCOMPARE(state.level, -98.0f);
        QCOMPARE(state.pendingFrames, 0);
    }
};

QTEST_GUILESS_MAIN(TestAutoReference)
#include "test_autoreference.moc"
