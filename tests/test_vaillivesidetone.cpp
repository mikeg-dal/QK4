#include "audio/livesidetone.h"

#include <QtTest>
#include <limits>

namespace {
constexpr qint64 ms(int value) {
    return qint64(value) * 1'000'000;
}

QVector<qint16> render(LiveSidetone &tone, int milliseconds) {
    QVector<qint16> samples(LiveSidetone::kSampleRate * milliseconds / 1000);
    const auto bytes = samples.size() * sizeof(qint16);
    const auto written = tone.render(reinterpret_cast<char *>(samples.data()), bytes);
    if (written != static_cast<qint64>(bytes)) {
        qFatal("Incomplete sidetone render");
    }
    return samples;
}

bool silent(const QVector<qint16> &samples) {
    return std::all_of(samples.begin(), samples.end(), [](qint16 sample) { return sample == 0; });
}

int peak(const QVector<qint16> &samples) {
    int result = 0;
    for (const auto sample : samples) {
        result = std::max(result, std::abs(static_cast<int>(sample)));
    }
    return result;
}

int positiveCrossings(const QVector<qint16> &samples) {
    int count = 0;
    for (qsizetype index = 1; index < samples.size(); ++index) {
        if (samples[index - 1] <= 0 && samples[index] > 0) {
            ++count;
        }
    }
    return count;
}
} // namespace

class TestVailLiveSidetone : public QObject {
    Q_OBJECT

private slots:
    void startsSilent();
    void keyedToneHasBoundedAmplitudeAndAttack();
    void releaseFinishesWithinThreeMilliseconds();
    void shortTapBetweenReadsIsPreserved();
    void queuedEdgesPreserveToneAndGapDurations();
    void duplicateDownDoesNotExtendTone();
    void volumeChangesApplyWhileHeld();
    void frequencyChangesApplyWhileHeld();
    void invalidControlsRemainBounded();
    void stopResetClearsHeldAndQueuedTone();
    void repeatedResetDoesNotResurrectTone();
    void queueOverflowFailsSilent();
    void readsWholeFramesAndHonorsBounds();
    void pacedFloatBlocksPreserveCapturedTiming_data();
    void pacedFloatBlocksPreserveCapturedTiming();
    void floatRenderHonorsBounds();
};

void TestVailLiveSidetone::startsSilent() {
    LiveSidetone tone;
    QVERIFY(silent(render(tone, 100)));
}

void TestVailLiveSidetone::keyedToneHasBoundedAmplitudeAndAttack() {
    LiveSidetone tone;
    tone.setVolume(0.5f);
    tone.setKeyDown(true, 0);
    const auto attack = render(tone, 1);
    const auto steady = render(tone, 20);
    QCOMPARE(attack.first(), 0);
    QVERIFY(peak(attack) < 5000);
    QVERIFY(peak(steady) > 16000);
    QVERIFY(peak(steady) <= 16384);
}

void TestVailLiveSidetone::releaseFinishesWithinThreeMilliseconds() {
    LiveSidetone tone;
    tone.setKeyDown(true, 0);
    render(tone, 60);
    tone.setKeyDown(false, ms(60));
    const auto release = render(tone, 3);
    QVERIFY(!silent(release));
    QVERIFY(silent(render(tone, 100)));
}

void TestVailLiveSidetone::shortTapBetweenReadsIsPreserved() {
    LiveSidetone tone;
    // Both edges arrive before the next 20 ms audio read. A lone atomic level would lose this tap.
    tone.setKeyDown(true, ms(100));
    tone.setKeyDown(false, ms(112));
    const auto block = render(tone, 20);
    QVERIFY(!silent(block.mid(0, 12 * 48)));
    QVERIFY(silent(block.mid(15 * 48)));
}

void TestVailLiveSidetone::queuedEdgesPreserveToneAndGapDurations() {
    LiveSidetone tone;
    tone.setKeyDown(true, ms(100));
    tone.setKeyDown(false, ms(160));
    tone.setKeyDown(true, ms(220));
    tone.setKeyDown(false, ms(280));
    const auto samples = render(tone, 240);
    QVERIFY(!silent(samples.mid(10 * 48, 40 * 48)));
    QVERIFY(silent(samples.mid(70 * 48, 45 * 48)));
    QVERIFY(!silent(samples.mid(130 * 48, 40 * 48)));
    QVERIFY(silent(samples.mid(190 * 48)));
}

void TestVailLiveSidetone::duplicateDownDoesNotExtendTone() {
    LiveSidetone tone;
    tone.setKeyDown(true, ms(100));
    tone.setKeyDown(true, ms(5000));
    tone.setKeyDown(false, ms(160));
    const auto samples = render(tone, 100);
    QVERIFY(!silent(samples.mid(10 * 48, 40 * 48)));
    QVERIFY(silent(samples.mid(65 * 48)));
}

void TestVailLiveSidetone::volumeChangesApplyWhileHeld() {
    LiveSidetone tone;
    tone.setKeyDown(true, 0);
    render(tone, 10);
    tone.setVolume(0.2f);
    const auto quiet = render(tone, 20);
    tone.setVolume(0.8f);
    const auto loud = render(tone, 20);
    QVERIFY(peak(quiet) > 6000);
    QVERIFY(peak(quiet) < 6600);
    QVERIFY(peak(loud) > peak(quiet) * 3);
    tone.setVolume(0);
    QVERIFY(silent(render(tone, 20)));
}

void TestVailLiveSidetone::frequencyChangesApplyWhileHeld() {
    LiveSidetone tone;
    tone.setFrequency(600);
    tone.setKeyDown(true, 0);
    render(tone, 10);
    const int low = positiveCrossings(render(tone, 100));
    tone.setFrequency(1000);
    const int high = positiveCrossings(render(tone, 100));
    QVERIFY(low >= 59 && low <= 61);
    QVERIFY(high >= 99 && high <= 101);
}

void TestVailLiveSidetone::invalidControlsRemainBounded() {
    LiveSidetone tone;
    tone.setKeyDown(true, 0);
    tone.setVolume(std::numeric_limits<float>::quiet_NaN());
    QVERIFY(silent(render(tone, 10)));
    tone.setVolume(std::numeric_limits<float>::infinity());
    QVERIFY(silent(render(tone, 10)));
    tone.setVolume(-1);
    QVERIFY(silent(render(tone, 10)));
    tone.setVolume(4);
    tone.setFrequency(std::numeric_limits<int>::max());
    const auto loud = render(tone, 100);
    QVERIFY(peak(loud) <= 32767);
    const int high = positiveCrossings(loud);
    QVERIFY(high >= 199 && high <= 201);
    tone.setFrequency(-1);
    const int low = positiveCrossings(render(tone, 100));
    QVERIFY(low >= 9 && low <= 11);
}

void TestVailLiveSidetone::stopResetClearsHeldAndQueuedTone() {
    LiveSidetone tone;
    tone.setKeyDown(true, 0);
    QVERIFY(!silent(render(tone, 10)));
    tone.setKeyDown(false, ms(100));
    tone.setKeyDown(true, ms(200));
    tone.reset();
    QVERIFY(silent(render(tone, 300)));
    // Device restart starts silent, but a fresh key edge works.
    tone.setKeyDown(true, ms(1000));
    QVERIFY(!silent(render(tone, 10)));
}

void TestVailLiveSidetone::repeatedResetDoesNotResurrectTone() {
    LiveSidetone tone;
    tone.setKeyDown(true, 0);
    tone.reset();
    tone.reset();
    tone.setKeyDown(false, ms(100));
    QVERIFY(silent(render(tone, 100)));
}

void TestVailLiveSidetone::queueOverflowFailsSilent() {
    LiveSidetone tone;
    for (int index = 0; index < LiveSidetone::kMaximumQueuedEdges; ++index) {
        QVERIFY(tone.setKeyDown(index % 2 == 0, ms(index)));
    }
    QVERIFY(!tone.setKeyDown(true, ms(300)));
    QVERIFY(silent(render(tone, 500)));
}

void TestVailLiveSidetone::readsWholeFramesAndHonorsBounds() {
    LiveSidetone tone;
    char bytes[5]{1, 1, 1, 1, 1};
    QCOMPARE(tone.render(bytes, 5), 4);
    QCOMPARE(bytes[4], 1);
    QCOMPARE(tone.render(bytes, 0), 0);
    QCOMPARE(tone.render(bytes, -1), 0);
    QCOMPARE(tone.render(nullptr, 4), 0);
}

void TestVailLiveSidetone::pacedFloatBlocksPreserveCapturedTiming_data() {
    QTest::addColumn<int>("sampleRate");
    QTest::newRow("44100") << 44100;
    QTest::newRow("48000") << 48000;
    QTest::newRow("96000") << 96000;
}

void TestVailLiveSidetone::pacedFloatBlocksPreserveCapturedTiming() {
    QFETCH(int, sampleRate);
    LiveSidetone tone;
    tone.setVolume(0.5f);
    tone.setKeyDown(true, ms(100));
    tone.setKeyDown(false, ms(160));
    tone.setKeyDown(true, ms(220));
    tone.setKeyDown(false, ms(280));
    QVector<float> samples(sampleRate * 240 / 1000);
    const int blockFrames = sampleRate * 4 / 1000;
    for (qsizetype offset = 0; offset < samples.size();) {
        const auto frames = std::min<qsizetype>(blockFrames, samples.size() - offset);
        QCOMPARE(tone.renderFloat(samples.data() + offset, frames, sampleRate), frames);
        offset += frames;
    }
    const auto isSilent = [&samples, sampleRate](int startMs, int endMs) {
        return std::all_of(samples.begin() + sampleRate * startMs / 1000, samples.begin() + sampleRate * endMs / 1000,
                           [](float value) { return value == 0.0f; });
    };
    QVERIFY(!isSilent(10, 50));
    QVERIFY(isSilent(70, 115));
    QVERIFY(!isSilent(130, 170));
    QVERIFY(isSilent(190, 240));
    QVERIFY(std::all_of(samples.begin(), samples.end(),
                        [](float value) { return std::isfinite(value) && std::abs(value) <= 0.5f; }));
}

void TestVailLiveSidetone::floatRenderHonorsBounds() {
    LiveSidetone tone;
    float samples[5]{1, 1, 1, 1, 1};
    QCOMPARE(tone.renderFloat(samples, 4, 44100), 4);
    QCOMPARE(samples[4], 1.0f);
    QCOMPARE(tone.renderFloat(nullptr, 4, 44100), 0);
    QCOMPARE(tone.renderFloat(samples, 0, 44100), 0);
    QCOMPARE(tone.renderFloat(samples, -1, 44100), 0);
    QCOMPARE(tone.renderFloat(samples, 4, 0), 0);
    QCOMPARE(tone.renderFloat(samples, 4, -1), 0);
}

QTEST_APPLESS_MAIN(TestVailLiveSidetone)
#include "test_vaillivesidetone.moc"
