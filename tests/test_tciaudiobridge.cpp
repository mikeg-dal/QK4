#include <QtTest>

#include <cmath>
#include <vector>

#include "network/tciaudiobridge.h"

namespace {

constexpr double kPi = 3.14159265358979323846;

// One K4 audio packet as OpusDecoder produces it: 12 kHz stereo Float32, L = Main, R = Sub.
QByteArray k4Packet(const std::vector<float> &main, const std::vector<float> &sub) {
    QByteArray out;
    out.resize(static_cast<int>(main.size() * 2 * sizeof(float)));
    float *p = reinterpret_cast<float *>(out.data());
    for (size_t i = 0; i < main.size(); ++i) {
        p[i * 2] = main[i];
        p[i * 2 + 1] = (i < sub.size()) ? sub[i] : 0.0f;
    }
    return out;
}

std::vector<float> tone(double freqHz, double rateHz, int count, float amp = 0.5f) {
    std::vector<float> v(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        v[static_cast<size_t>(i)] = static_cast<float>(amp * std::sin(2.0 * kPi * freqHz * i / rateHz));
    }
    return v;
}

// Amplitude at one frequency, Hann-windowed single-bin DFT.
double amplitudeAt(const std::vector<float> &x, double freqHz, double rateHz, int skip) {
    const int n = static_cast<int>(x.size()) - skip;
    if (n <= 1) {
        return 0.0;
    }
    double re = 0.0, im = 0.0, win = 0.0;
    for (int i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * i / (n - 1));
        const double a = 2.0 * kPi * freqHz * i / rateHz;
        re += x[skip + i] * w * std::cos(a);
        im -= x[skip + i] * w * std::sin(a);
        win += w;
    }
    return 2.0 * std::sqrt(re * re + im * im) / win;
}

// Every other float, i.e. one channel of an interleaved pair.
std::vector<float> channel(const std::vector<float> &interleaved, int index) {
    std::vector<float> out(interleaved.size() / 2);
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] = interleaved[i * 2 + index];
    }
    return out;
}

} // namespace

// K4 receive audio -> TCI RX_AUDIO samples.
//
// 12 kHz stereo in (L = Main, R = Sub), 48 kHz stereo out with Main duplicated to both channels.
// See docs/tci-server-design.md.
class TestTciAudioBridge : public QObject {
    Q_OBJECT

private slots:
    void quadruplesTheFrameCount() {
        // 12 kHz in, 48 kHz out: the rate WSJT-X always assumes regardless of what is declared.
        TciAudioBridge bridge(nullptr);
        const std::vector<float> main(256, 0.0f);
        const std::vector<float> out = bridge.convert(k4Packet(main, main));
        QCOMPARE(out.size(), size_t(256 * 4 * 2)); // 4x rate, still stereo
    }

    void duplicatesMainIntoBothChannels() {
        // With trx_count:1 there is no TCI address for Sub, so sending it would deliver audio no
        // client asked for and none can identify.
        TciAudioBridge bridge(nullptr);
        const std::vector<float> main = tone(1000.0, 12000.0, 512);
        const std::vector<float> sub = tone(2500.0, 12000.0, 512);
        const std::vector<float> out = bridge.convert(k4Packet(main, sub));

        const std::vector<float> left = channel(out, 0);
        const std::vector<float> right = channel(out, 1);
        QCOMPARE(left.size(), right.size());
        for (size_t i = 0; i < left.size(); ++i) {
            QCOMPARE(left[i], right[i]);
        }
    }

    void carriesMainAndDiscardsSub() {
        // The Sub tone must be absent, not merely quieter - otherwise a client decodes a receiver
        // it never subscribed to.
        TciAudioBridge bridge(nullptr);
        const std::vector<float> main = tone(1000.0, 12000.0, 4096);
        const std::vector<float> sub = tone(2500.0, 12000.0, 4096);
        const std::vector<float> out = bridge.convert(k4Packet(main, sub));

        const std::vector<float> left = channel(out, 0);
        const double wanted = amplitudeAt(left, 1000.0, 48000.0, 1024);
        const double unwanted = amplitudeAt(left, 2500.0, 48000.0, 1024);

        QVERIFY2(wanted > 0.4, qPrintable(QString("main amplitude %1").arg(wanted)));
        const double dBc = 20.0 * std::log10(std::max(unwanted / wanted, 1e-12));
        QVERIFY2(dBc < -60.0, qPrintable(QString("sub leaked at %1 dBc").arg(dBc)));
    }

    void preservesToneFrequencyAndAmplitude() {
        // The whole point: a 1000 Hz tone at 12 kHz must still be 1000 Hz at 48 kHz, at the same
        // level. Getting this wrong is how a rate error manifests.
        TciAudioBridge bridge(nullptr);
        const std::vector<float> main = tone(1000.0, 12000.0, 4096);
        const std::vector<float> out = bridge.convert(k4Packet(main, {}));

        const std::vector<float> left = channel(out, 0);
        const double amp = amplitudeAt(left, 1000.0, 48000.0, 1024);
        QVERIFY2(std::fabs(amp - 0.5) < 0.01, qPrintable(QString("amplitude %1, expected 0.5").arg(amp)));
    }

    void isContinuousAcrossPacketBoundaries() {
        // K4 audio arrives in packets; a discontinuity at each boundary would be an audible click
        // and would smear a digital signal.
        const std::vector<float> main = tone(1000.0, 12000.0, 1024);

        TciAudioBridge whole(nullptr);
        const std::vector<float> reference = whole.convert(k4Packet(main, {}));

        TciAudioBridge split(nullptr);
        const std::vector<float> firstHalf(main.begin(), main.begin() + 512);
        const std::vector<float> secondHalf(main.begin() + 512, main.end());
        std::vector<float> joined = split.convert(k4Packet(firstHalf, {}));
        const std::vector<float> &tail = split.convert(k4Packet(secondHalf, {}));
        joined.insert(joined.end(), tail.begin(), tail.end());

        QCOMPARE(joined.size(), reference.size());
        for (size_t i = 0; i < reference.size(); ++i) {
            QVERIFY2(std::fabs(joined[i] - reference[i]) < 1e-6f, qPrintable(QString("sample %1 differs").arg(i)));
        }
    }

    void resetMakesTheNextPacketIdenticalToAFreshStart() {
        TciAudioBridge bridge(nullptr);
        const std::vector<float> main = tone(1000.0, 12000.0, 256);

        const std::vector<float> first = bridge.convert(k4Packet(main, {}));
        bridge.convert(k4Packet(main, {})); // dirty the filter history
        bridge.reset();
        const std::vector<float> afterReset = bridge.convert(k4Packet(main, {}));

        QCOMPARE(afterReset.size(), first.size());
        for (size_t i = 0; i < first.size(); ++i) {
            QCOMPARE(afterReset[i], first[i]);
        }
    }

    void rejectsATornPacket() {
        // An odd float count cannot be interleaved stereo. Treating it as such would swap the
        // channels for every subsequent packet.
        TciAudioBridge bridge(nullptr);
        QByteArray torn(3 * static_cast<int>(sizeof(float)), '\0');
        QVERIFY(bridge.convert(torn).empty());
    }

    void handlesAnEmptyPacket() {
        TciAudioBridge bridge(nullptr);
        QVERIFY(bridge.convert(QByteArray()).empty());
    }

    void doesNothingWhenNobodyIsListening() {
        // A null server stands in for "no subscribers": the slot must not crash or do work.
        TciAudioBridge bridge(nullptr);
        const std::vector<float> main = tone(1000.0, 12000.0, 256);
        bridge.onRxAudio(k4Packet(main, {}));
    }
};

QTEST_MAIN(TestTciAudioBridge)
#include "test_tciaudiobridge.moc"
