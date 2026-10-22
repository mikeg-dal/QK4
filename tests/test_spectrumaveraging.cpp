#include <QtTest>
#include <cmath>

#include "dsp/spectrumaveraging.h"

class TestSpectrumAveraging : public QObject {
    Q_OBJECT

private slots:
    void levelOneFollowsEveryFrame() {
        QCOMPARE(SpectrumAveraging::update(-110.0f, -70.0f, 1), -70.0f);
        QCOMPARE(SpectrumAveraging::update(-70.0f, -110.0f, 1), -110.0f);
    }

    void strongerAveragingReducesTransient() {
        const float light = SpectrumAveraging::update(-110.0f, -70.0f, 5);
        const float heavy = SpectrumAveraging::update(-110.0f, -70.0f, 20);
        QVERIFY(heavy < light);
        QVERIFY(heavy > -110.0f);
    }

    void fluctuatingNoiseRetainsItsMean() {
        for (int level = 1; level <= 20; ++level) {
            float value = -100.0f;
            double sum = 0.0;
            for (int frame = 0; frame < 2000; ++frame) {
                value = SpectrumAveraging::update(value, frame % 2 ? -90.0f : -110.0f, level);
                if (frame >= 1000)
                    sum += value;
            }
            QVERIFY(std::abs(sum / 1000.0 + 100.0) < 0.001);
        }
    }
};

QTEST_GUILESS_MAIN(TestSpectrumAveraging)
#include "test_spectrumaveraging.moc"
