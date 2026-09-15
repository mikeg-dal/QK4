#ifndef NETWORK_TCIAUDIOBRIDGE_H
#define NETWORK_TCIAUDIOBRIDGE_H

#include <QByteArray>
#include <QObject>

#include <vector>

#include "dsp/audioupsampler.h"

class TciServer;

// Converts K4 receive audio into TCI RX_AUDIO frames.
//
// In:  12 kHz stereo Float32, L = Main, R = Sub - exactly what OpusDecoder produces, tapped in
//      AudioController before the jitter buffer.
// Out: 48 kHz stereo Float32 handed to TciServer::sendRxAudio.
//
// WHY the rate changes: WSJT-X's TCI implementation ignores a declared audio_samplerate and always
// treats the stream as 48 kHz (audioSampleRate is assigned once in its constructor and
// Cmd_AudioSR has no dispatch case), so the 12 kHz stream has to be interpolated rather than
// announced. See docs/tci-server-design.md.
//
// WHY Main is duplicated into both channels: with trx_count:1 a client only addresses receiver 0,
// so there is no TCI address for the Sub receiver yet. Sending Sub in the right channel would
// deliver audio no client asked for and no client can identify. Revisit with the RX Two work.
//
// Thread affinity: lives on the TCI thread. onRxAudio is a slot, so a queued connection from the
// I/O thread lands it here and the resampling cost is paid off the I/O thread - which also carries
// the K4 control stream and must not stall.
class TciAudioBridge : public QObject {
    Q_OBJECT

public:
    static constexpr int INPUT_RATE = 12000;
    static constexpr int OUTPUT_RATE = 48000;

    explicit TciAudioBridge(TciServer *server, QObject *parent = nullptr);

    // Audio can be switched off while the CAT half keeps running - a legitimate configuration, and
    // audio is the expensive half. Enabling drops resampler history; see the definition.
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    // Drop resampler history. Call on stream discontinuity - a reconnect or a K4 audio restart -
    // so samples from before the gap cannot bleed across it.
    void reset();

    // Exposed for tests: the conversion with no server attached. Returns a reference to an
    // internal buffer, valid until the next call - a 10 ms audio callback must not allocate.
    const std::vector<float> &convert(const QByteArray &pcm12kStereo);

public slots:
    void onRxAudio(const QByteArray &pcm12kStereo);

private:
    TciServer *m_server;
    bool m_enabled = true;
    AudioUpsampler m_upsampler;
    // Scratch buffers kept as members so a 10 ms audio callback does not allocate.
    std::vector<float> m_mono12k;
    std::vector<float> m_mono48k;
    std::vector<float> m_stereo48k;
};

#endif // NETWORK_TCIAUDIOBRIDGE_H
