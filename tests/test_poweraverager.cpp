#include <QTest>
#include <cmath>

#include "dsp/poweraverager.h"

namespace {
QVector<float> frame(std::initializer_list<float> dbm) {
    return QVector<float>(dbm);
}
} // namespace

class TestPowerAverager : public QObject {
    Q_OBJECT

private slots:
    // #AVG 1: every frame drawn as it arrives, as on the K4.
    void lengthOnePassesFramesThrough() {
        PowerAverager a;
        QVector<float> out;
        a.add(frame({-100.0f, -60.0f}), out);
        QCOMPARE(out, frame({-100.0f, -60.0f}));
        a.add(frame({-90.0f, -120.0f}), out);
        QCOMPARE(out, frame({-90.0f, -120.0f}));
    }

    // Power, not dB: the mean of 1e-10 mW and 1e-9 mW is 5.5e-10 mW = -92.6 dBm, not the dB mean -95.
    // Averaging dB values drew the noise grass ~2.5 dB low and flat (bench 2026-10-30, #AVG 3).
    void averagesPowerNotDb() {
        PowerAverager a;
        a.setLength(2);
        QVector<float> out;
        a.add(frame({-100.0f}), out);
        a.add(frame({-90.0f}), out);
        QVERIFY(std::abs(out[0] - (-92.596f)) < 0.01f);
    }

    // The regression this replaces: a running power blend let a stopped 60 dB carrier fade for over a
    // second at #AVG 3, a tail on the trace and in the waterfall. Over the last n frames it is gone
    // after exactly n frames.
    void stoppedSignalIsGoneAfterNFrames() {
        for (int n : {2, 3, 5, 20}) {
            PowerAverager a;
            a.setLength(n);
            QVector<float> out;
            for (int i = 0; i < 2 * n; ++i)
                a.add(frame({-40.0f}), out); // a strong carrier
            for (int i = 0; i < n; ++i) {
                a.add(frame({-110.0f}), out); // it stops; noise only
                if (i < n - 1)
                    QVERIFY2(out[0] > -100.0f, qPrintable(QString("n %1 frame %2: %3").arg(n).arg(i).arg(out[0])));
            }
            QVERIFY2(std::abs(out[0] - (-110.0f)) < 1e-3f, qPrintable(QString("n %1: %2").arg(n).arg(out[0])));
        }
    }

    void steadyLevelStaysPut() {
        PowerAverager a;
        a.setLength(7);
        QVector<float> out;
        for (int i = 0; i < 3000; ++i) // past the periodic re-sum
            a.add(frame({-123.4f, -60.0f}), out);
        QVERIFY(std::abs(out[0] + 123.4f) < 1e-3f);
        QVERIFY(std::abs(out[1] + 60.0f) < 1e-3f);
    }

    // A span or tier change alters the bin count: the average restarts instead of mixing resolutions.
    void binCountChangeRestarts() {
        PowerAverager a;
        a.setLength(4);
        QVector<float> out;
        a.add(frame({-40.0f, -40.0f}), out);
        a.add(frame({-100.0f, -100.0f, -100.0f}), out);
        QCOMPARE(a.framesHeld(), 1);
        QCOMPARE(out, frame({-100.0f, -100.0f, -100.0f}));
    }

    void lengthIsClampedAndChangingItRestarts() {
        PowerAverager a;
        a.setLength(0);
        QCOMPARE(a.length(), 1);
        a.setLength(99);
        QCOMPARE(a.length(), PowerAverager::MaxLength);
        QVector<float> out;
        a.add(frame({-50.0f}), out);
        a.setLength(3);
        QCOMPARE(a.framesHeld(), 0);
        a.add(frame({-80.0f}), out);
        QCOMPARE(out[0], -80.0f);
    }
};

QTEST_APPLESS_MAIN(TestPowerAverager)
#include "test_poweraverager.moc"
