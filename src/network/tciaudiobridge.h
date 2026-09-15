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
// WHY the right channel depends on the Sub RX: TCI addresses the Sub receiver as CHANNEL 1 of
// receiver 0 (rx_channel_enable), and the audio stream is stereo, so the natural carrier for it is
// the right channel. With the Sub RX off there is nothing to put there and Main is duplicated into
// both, which is what a mono client expects to hear. With it on, L = Main and R = Sub.
//
// The alternative - always sending Sub on the right - delivers audio to clients that never asked
// for a second receiver and cannot tell the two apart, which is why it follows the radio.
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

    // Follows the radio's Sub RX. Off duplicates Main into both output channels; on puts Main in
    // the left and Sub in the right. Changing it drops resampler history for the same reason
    // setEnabled does - the sub chain has been running dry and its taps are stale.
    void setSubReceiverEnabled(bool enabled);
    bool subReceiverEnabled() const { return m_subEnabled; }

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
    bool m_subEnabled = false;

    // One upsampler per channel: the FIR carries history, so a single instance fed alternating
    // channels would filter each against the other's samples.
    AudioUpsampler m_upsampler;
    AudioUpsampler m_upsamplerSub;

    // Scratch buffers kept as members so a 10 ms audio callback does not allocate.
    std::vector<float> m_mono12k;
    std::vector<float> m_mono48k;
    std::vector<float> m_sub12k;
    std::vector<float> m_sub48k;
    std::vector<float> m_stereo48k;
};

#endif // NETWORK_TCIAUDIOBRIDGE_H
