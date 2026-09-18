#ifndef NETWORK_TCIAUDIOFRAME_H
#define NETWORK_TCIAUDIOFRAME_H

#include <QByteArray>
#include <QtGlobal>

#include <vector>

// TCI binary audio frames: a 64-byte header followed by samples.
//
// Header layout per the ExpertSDR3 TCI v2.0 Stream struct, confirmed byte-for-byte against a
// captured AetherSDR <-> WSJT-X session. All fields are little-endian quint32.
//
// The two directions do NOT share a payload rule, which is the single most important thing in this
// file. See docs/tci-server-design.md.
namespace TciAudioFrame {

constexpr int HEADER_BYTES = 64;

enum Type : quint32 {
    TypeIq = 0,
    TypeRxAudio = 1,
    TypeTxAudio = 2,
    TypeTxChrono = 3,
};

enum Format : quint32 {
    FormatInt16 = 0,
    FormatFloat32 = 3,
};

// One TX_CHRONO asks the client for this many floats, which is 1024 stereo frames = 21.333 ms at
// 48 kHz. Matches the audio_stream_samples the reference server announces.
constexpr int CHRONO_FLOATS = 2048;

struct Header {
    quint32 receiver = 0;
    quint32 sampleRate = 48000;
    quint32 format = FormatFloat32;
    quint32 codec = 0;
    quint32 crc = 0;
    quint32 length = 0; // floats in the VALID region, not necessarily the payload size
    quint32 type = TypeRxAudio;
    quint32 channels = 2;
};

// Read a header. Returns false if the payload is too short to contain one.
bool parseHeader(const QByteArray &payload, Header *out);

// Build a frame from interleaved stereo float samples.
//
// `length` is set to the total float count, and the payload holds exactly that many - the shape
// AetherSDR sends and WSJT-X accepts (payload floats / length == 1.0).
QByteArray encodeRxAudio(int receiver, int sampleRate, const float *interleavedStereo, int floatCount);

// Header-only frame, no payload. This is what pulls TX audio from the client: WSJT-X sends nothing
// until asked, and answers exactly one TX_AUDIO block per chrono.
QByteArray encodeTxChrono(int receiver, int sampleRate = 48000);

// Extract mono samples from an inbound TX_AUDIO frame.
//
// Three measured rules, all of which a naive reader gets wrong:
//  - Only the first header.length floats are valid. The payload is twice that size and the tail is
//    81.77% non-zero stale buffer, so sizing from the frame appends garbage audio.
//  - Those floats are duplicated stereo pairs (L == R), measured at 409600/409600 = 100.00%.
//    Treating them as true mono doubles the duration and drops every tone an octave.
//  - header.channels is unusable: eight distinct values appeared across 762 captured frames,
//    including 1818781545, 4098833031 and 0. Never read it.
//
// Returns false if the frame is not TX_AUDIO, is malformed, or carries a format we do not decode.
bool decodeTxAudioToMono(const QByteArray &payload, std::vector<float> *out, Header *headerOut = nullptr);

// Whether the first `count` floats look like duplicated stereo pairs. Exposed for tests; callers
// should use decodeTxAudioToMono, which applies it.
bool looksLikeDuplicatedStereo(const float *samples, int count);

} // namespace TciAudioFrame

#endif // NETWORK_TCIAUDIOFRAME_H
