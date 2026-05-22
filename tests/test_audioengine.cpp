#include <QTest>
#include "audio/audioengine.h"

// Uses the friend class declaration in audioengine.h to access private resampler methods.
class TestAudioEngine : public QObject {
    Q_OBJECT

    // Helper: build a QByteArray of Float32 samples from a std::initializer_list
    static QByteArray makeFloat32(std::initializer_list<float> values) {
        QByteArray buf;
        buf.reserve(static_cast<int>(values.size() * sizeof(float)));
        for (float v : values)
            buf.append(reinterpret_cast<const char *>(&v), sizeof(float));
        return buf;
    }

    // Helper: read the k-th Float32 sample from a QByteArray
    static float sample(const QByteArray &buf, int k) {
        float v;
        memcpy(&v, buf.constData() + k * sizeof(float), sizeof(float));
        return v;
    }

private slots:

    // --- resample48kTo12k ---

    void testResample48kTo12k_outputCount() {
        // 16 input samples → 4 output samples (4:1 decimation)
        AudioEngine engine;
        QByteArray in = makeFloat32({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 1, 2, 3, 4});
        QByteArray out = engine.resample48kTo12k(in);
        QCOMPARE(out.size() / static_cast<int>(sizeof(float)), 4);
    }

    void testResample48kTo12k_averaging() {
        // Each group of 4 equal samples should average to that value
        AudioEngine engine;
        QByteArray in = makeFloat32({0.4f, 0.4f, 0.4f, 0.4f, 0.8f, 0.8f, 0.8f, 0.8f});
        QByteArray out = engine.resample48kTo12k(in);
        QCOMPARE(out.size() / static_cast<int>(sizeof(float)), 2);
        QVERIFY(qAbs(sample(out, 0) - 0.4f) < 1e-5f);
        QVERIFY(qAbs(sample(out, 1) - 0.8f) < 1e-5f);
    }

    // --- resample16kTo12k ---

    void testResample16kTo12k_outputCount() {
        // 8 input samples → 6 output samples (4:3 ratio)
        AudioEngine engine;
        QByteArray in = makeFloat32({0, 1, 2, 3, 4, 5, 6, 7});
        QByteArray out = engine.resample16kTo12k(in);
        QCOMPARE(out.size() / static_cast<int>(sizeof(float)), 6);
    }

    void testResample16kTo12k_firstSamplePassthrough() {
        // Output sample 0 = input sample 0 (position 0 * 4/3 = 0, exact)
        AudioEngine engine;
        QByteArray in = makeFloat32({0.5f, 0.0f, 0.0f, 0.0f});
        QByteArray out = engine.resample16kTo12k(in);
        QVERIFY(qAbs(sample(out, 0) - 0.5f) < 1e-5f);
    }

    void testResample16kTo12k_dcSignal() {
        // A constant signal must pass through unchanged (no ripple from interpolation)
        AudioEngine engine;
        QByteArray in = makeFloat32({0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f});
        QByteArray out = engine.resample16kTo12k(in);
        int outCount = out.size() / static_cast<int>(sizeof(float));
        for (int i = 0; i < outCount; i++)
            QVERIFY(qAbs(sample(out, i) - 0.6f) < 1e-5f);
    }

    // --- resample8kTo12k ---

    void testResample8kTo12k_outputCount() {
        // 4 input samples → 6 output samples (2:3 ratio)
        AudioEngine engine;
        QByteArray in = makeFloat32({0, 1, 2, 3});
        QByteArray out = engine.resample8kTo12k(in);
        QCOMPARE(out.size() / static_cast<int>(sizeof(float)), 6);
    }

    void testResample8kTo12k_firstSamplePassthrough() {
        // Output sample 0 = input sample 0 (position 0 * 2/3 = 0, exact)
        AudioEngine engine;
        QByteArray in = makeFloat32({0.7f, 0.0f, 0.0f, 0.0f});
        QByteArray out = engine.resample8kTo12k(in);
        QVERIFY(qAbs(sample(out, 0) - 0.7f) < 1e-5f);
    }

    void testResample8kTo12k_dcSignal() {
        // A constant signal must pass through unchanged
        AudioEngine engine;
        QByteArray in = makeFloat32({0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f});
        QByteArray out = engine.resample8kTo12k(in);
        int outCount = out.size() / static_cast<int>(sizeof(float));
        for (int i = 0; i < outCount; i++)
            QVERIFY(qAbs(sample(out, i) - 0.3f) < 1e-5f);
    }
};

QTEST_MAIN(TestAudioEngine)
#include "test_audioengine.moc"
