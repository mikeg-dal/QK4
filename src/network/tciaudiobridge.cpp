#include "network/tciaudiobridge.h"

#include "network/tciserver.h"

TciAudioBridge::TciAudioBridge(TciServer *server, QObject *parent)
    : QObject(parent), m_server(server), m_upsampler(OUTPUT_RATE / INPUT_RATE, INPUT_RATE) {}

void TciAudioBridge::reset() {
    m_upsampler.reset();
    m_mono12k.clear();
    m_mono48k.clear();
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

    // Take the left channel only: L is Main, and Sub has no TCI address while trx_count is 1.
    m_mono12k.resize(static_cast<size_t>(frames));
    for (int i = 0; i < frames; ++i) {
        m_mono12k[static_cast<size_t>(i)] = in[i * 2];
    }

    m_mono48k.clear();
    m_upsampler.process(m_mono12k.data(), frames, m_mono48k);

    m_stereo48k.resize(m_mono48k.size() * 2);
    for (size_t i = 0; i < m_mono48k.size(); ++i) {
        const float s = m_mono48k[i];
        m_stereo48k[i * 2] = s;
        m_stereo48k[i * 2 + 1] = s;
    }
    return m_stereo48k;
}

void TciAudioBridge::onRxAudio(const QByteArray &pcm12kStereo) {
    if (!m_server || m_server->audioClientCount() == 0) {
        // Nobody is listening. Skipping the work entirely also means the resampler keeps no state
        // across an idle period, which is why reset() exists for the resume.
        return;
    }
    const std::vector<float> &stereo = convert(pcm12kStereo);
    if (!stereo.empty()) {
        m_server->sendRxAudio(stereo, OUTPUT_RATE);
    }
}
