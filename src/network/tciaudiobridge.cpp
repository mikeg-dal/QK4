#include "network/tciaudiobridge.h"

#include "network/tciserver.h"

TciAudioBridge::TciAudioBridge(TciServer *server, QObject *parent)
    : QObject(parent), m_server(server), m_upsampler(OUTPUT_RATE / INPUT_RATE, INPUT_RATE),
      m_upsamplerSub(OUTPUT_RATE / INPUT_RATE, INPUT_RATE) {}

void TciAudioBridge::setEnabled(bool enabled) {
    // Enabling is a stream discontinuity. While disabled onRxAudio returns before converting
    // anything, so the resampler still holds taps from before the gap - the same staleness
    // audio_start resets for, and the same reason reset() exists.
    //
    // EDGE-TRIGGERED, not level: a settings write can repeat the value already in force, and
    // resetting on every call would punch a hole in a stream that was running fine.
    if (enabled && !m_enabled) {
        reset();
    }
    m_enabled = enabled;
}

void TciAudioBridge::setSubReceiverEnabled(bool enabled) {
    // Edge-triggered, like setEnabled. The sub chain produces nothing while the Sub RX is off, so
    // switching it on is a stream discontinuity and its filter history is from before the gap.
    if (enabled != m_subEnabled) {
        reset();
    }
    m_subEnabled = enabled;
}

void TciAudioBridge::reset() {
    m_upsampler.reset();
    m_upsamplerSub.reset();
    m_mono12k.clear();
    m_mono48k.clear();
    m_sub12k.clear();
    m_sub48k.clear();
    m_stereo48k.clear();
}

const std::vector<float> &TciAudioBridge::convert(const QByteArray &pcm12kStereo) {
    const int floatCount = pcm12kStereo.size() / static_cast<int>(sizeof(float));
    // Interleaved stereo: an odd float count means a torn packet, not a partial frame we can use.
    if (floatCount < 2 || (floatCount % 2) != 0) {
        m_stereo48k.clear();
        return m_stereo48k;
    }
    const float *in = reinterpret_cast<const float *>(pcm12kStereo.constData());
    const int frames = floatCount / 2;

    // L is Main, R is Sub, as the K4 sends it.
    m_mono12k.resize(static_cast<size_t>(frames));
    for (int i = 0; i < frames; ++i) {
        m_mono12k[static_cast<size_t>(i)] = in[i * 2];
    }

    m_mono48k.clear();
    m_upsampler.process(m_mono12k.data(), frames, m_mono48k);

    if (!m_subEnabled) {
        // No Sub RX: duplicate Main into both channels, which is what a mono client expects.
        m_stereo48k.resize(m_mono48k.size() * 2);
        for (size_t i = 0; i < m_mono48k.size(); ++i) {
            const float s = m_mono48k[i];
            m_stereo48k[i * 2] = s;
            m_stereo48k[i * 2 + 1] = s;
        }
        return m_stereo48k;
    }

    m_sub12k.resize(static_cast<size_t>(frames));
    for (int i = 0; i < frames; ++i) {
        m_sub12k[static_cast<size_t>(i)] = in[i * 2 + 1];
    }
    m_sub48k.clear();
    m_upsamplerSub.process(m_sub12k.data(), frames, m_sub48k);

    // Both chains see the same input length and the same filter, so they produce the same count.
    // Pair defensively anyway rather than trusting that across future upsampler changes.
    const size_t pairs = m_mono48k.size() < m_sub48k.size() ? m_mono48k.size() : m_sub48k.size();
    m_stereo48k.resize(pairs * 2);
    for (size_t i = 0; i < pairs; ++i) {
        m_stereo48k[i * 2] = m_mono48k[i];
        m_stereo48k[i * 2 + 1] = m_sub48k[i];
    }
    return m_stereo48k;
}

void TciAudioBridge::onRxAudio(const QByteArray &pcm12kStereo) {
    if (!m_enabled || !m_server || m_server->audioClientCount() == 0) {
        // Nobody is listening. Skipping the work entirely also means the resampler keeps no state
        // across an idle period, which is why reset() exists for the resume.
        return;
    }
    const std::vector<float> &stereo = convert(pcm12kStereo);
    if (!stereo.empty()) {
        m_server->sendRxAudio(stereo, OUTPUT_RATE);
    }
}
