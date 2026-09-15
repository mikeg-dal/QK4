#include "network/tciaudioframe.h"

#include <QtEndian>

#include <cmath>
#include <cstring>

namespace {

constexpr int kFieldCount = 8; // named fields; the rest of the 64 bytes is reserved
constexpr int kReservedBytes = 32;

// Enough pairs to be confident without scanning a whole block.
constexpr int kStereoProbePairs = 128;
// The reference server accepts 90%; our own capture measured 100.00%, so this is slack, not a
// tolerance we rely on.
constexpr int kStereoMatchPercent = 90;
constexpr float kStereoEpsilon = 1.0e-6f;

// Below this a window carries no usable audio. Sits far above the denormal dust seen in unused
// buffers (~7e-15) and far below anything a transmitting client actually sends.
constexpr float kAudioFloor = 1.0e-7f;

// Where the live samples actually start inside a TX_AUDIO payload.
//
// WSJT-X allocates twice the length it was asked for (TCITransceiver.cpp:871 — `AudioHeaderSize +
// pStream->length * sizeof(float) * 2`) and the valid window lands in EITHER half. Measured across
// two captures of the same client:
//
//   against AetherSDR : offset 0    in 689 frames, 2048 in 34, 2049 in 18
//   against QK4       : offset 2048 in all 581 frames carrying signal
//
// So the offset cannot be assumed. Prefer the first window, which is what the reference server
// reads and what the client does most of the time; fall back to the second only when the first is
// entirely silent. Both windows are the same size, so this picks between "the audio" and "zeros" -
// never between two different signals. Genuine silence resolves to the first window, which is
// correct either way.
// A window is plausible audio only if it is non-silent, in range, and shaped like the duplicated
// stereo this client sends. "Non-zero" alone is NOT enough: the unused window holds stale buffer
// which is also non-zero, and picking it produced peaks around 5e35 - it has no pair structure and
// no amplitude bound.
bool looksLikeLiveAudio(const float *f, int count) {
    if (count < 2 || (count % 2) != 0) {
        return false;
    }

    // WHY a magnitude floor and not `!= 0.0f`: the unused window is not reliably all zeros. It has
    // been observed holding denormal dust around 7e-15, which is non-zero but inaudible. Treating
    // that as signal selected the wrong window and fed the radio silence - the meters moved a
    // little and nothing decoded. Real audio is orders of magnitude above this.
    float peak = 0.0f;
    for (int i = 0; i < count; ++i) {
        const float v = std::fabs(f[i]);
        // Audio is normalised; anything this large is reinterpreted memory, not samples.
        if (!(v <= 4.0f)) {
            return false;
        }
        if (v > peak) {
            peak = v;
        }
    }
    if (peak < kAudioFloor) {
        return false;
    }

    const int probe = (count / 2 < kStereoProbePairs) ? count / 2 : kStereoProbePairs;
    int matched = 0;
    for (int i = 0; i < probe; ++i) {
        if (std::fabs(f[i * 2] - f[i * 2 + 1]) < kStereoEpsilon) {
            ++matched;
        }
    }
    return matched >= (probe * kStereoMatchPercent) / 100;
}

int liveWindowOffset(const char *body, int availableFloats, int validFloats) {
    if (availableFloats < validFloats * 2) {
        return 0; // no second window to choose from
    }
    const float *f = reinterpret_cast<const float *>(body);
    if (looksLikeLiveAudio(f, validFloats)) {
        return 0;
    }
    if (looksLikeLiveAudio(f + validFloats, validFloats)) {
        return validFloats;
    }
    // Neither window carries audio: genuine silence, or a layout we have not seen. The first window
    // is what the reference server reads, so it is the safe default.
    return 0;
}

void appendU32(QByteArray &out, quint32 v) {
    char le[4];
    qToLittleEndian<quint32>(v, le);
    out.append(le, 4);
}

QByteArray buildHeader(const TciAudioFrame::Header &h) {
    QByteArray out;
    out.reserve(TciAudioFrame::HEADER_BYTES);
    appendU32(out, h.receiver);
    appendU32(out, h.sampleRate);
    appendU32(out, h.format);
    appendU32(out, h.codec);
    appendU32(out, h.crc);
    appendU32(out, h.length);
    appendU32(out, h.type);
    appendU32(out, h.channels);
    out.append(kReservedBytes, '\0');
    return out;
}

} // namespace

namespace TciAudioFrame {

bool parseHeader(const QByteArray &payload, Header *out) {
    if (payload.size() < HEADER_BYTES || !out) {
        return false;
    }
    quint32 fields[kFieldCount];
    for (int i = 0; i < kFieldCount; ++i) {
        fields[i] = qFromLittleEndian<quint32>(payload.constData() + i * 4);
    }
    out->receiver = fields[0];
    out->sampleRate = fields[1];
    out->format = fields[2];
    out->codec = fields[3];
    out->crc = fields[4];
    out->length = fields[5];
    out->type = fields[6];
    out->channels = fields[7];
    return true;
}

QByteArray encodeRxAudio(int receiver, int sampleRate, const float *interleavedStereo, int floatCount) {
    Header h;
    h.receiver = static_cast<quint32>(receiver);
    h.sampleRate = static_cast<quint32>(sampleRate);
    h.format = FormatFloat32;
    h.length = static_cast<quint32>(floatCount);
    h.type = TypeRxAudio;
    h.channels = 2;

    QByteArray out = buildHeader(h);
    if (interleavedStereo && floatCount > 0) {
        out.append(reinterpret_cast<const char *>(interleavedStereo), floatCount * static_cast<int>(sizeof(float)));
    }
    return out;
}

QByteArray encodeTxChrono(int receiver, int sampleRate) {
    Header h;
    h.receiver = static_cast<quint32>(receiver);
    h.sampleRate = static_cast<quint32>(sampleRate);
    h.format = FormatFloat32;
    h.length = CHRONO_FLOATS;
    h.type = TypeTxChrono;
    h.channels = 2;
    return buildHeader(h); // header only, no payload
}

bool looksLikeDuplicatedStereo(const float *samples, int count) {
    if (!samples || count < 2 || (count % 2) != 0) {
        return false;
    }
    const int pairs = count / 2;
    const int probe = pairs < kStereoProbePairs ? pairs : kStereoProbePairs;
    int matched = 0;
    for (int i = 0; i < probe; ++i) {
        if (std::fabs(samples[i * 2] - samples[i * 2 + 1]) < kStereoEpsilon) {
            ++matched;
        }
    }
    return matched >= (probe * kStereoMatchPercent) / 100;
}

bool decodeTxAudioToMono(const QByteArray &payload, std::vector<float> *out, Header *headerOut) {
    if (!out) {
        return false;
    }
    Header h;
    if (!parseHeader(payload, &h)) {
        return false;
    }
    if (headerOut) {
        *headerOut = h;
    }
    if (h.type != TypeTxAudio) {
        return false;
    }

    const int payloadBytes = payload.size() - HEADER_BYTES;
    if (payloadBytes <= 0 || h.length == 0) {
        return false;
    }
    const char *body = payload.constData() + HEADER_BYTES;

    // Honour header.length as the COUNT, never the frame size: the payload is twice as large and
    // only `length` floats are live.
    if (h.format == FormatFloat32) {
        const int available = payloadBytes / static_cast<int>(sizeof(float));
        int valid = static_cast<int>(h.length);
        if (valid > available) {
            valid = available;
        }
        if (valid <= 0) {
            return false;
        }
        const float *window = reinterpret_cast<const float *>(body) + liveWindowOffset(body, available, valid);

        std::vector<float> samples(static_cast<size_t>(valid));
        std::memcpy(samples.data(), window, static_cast<size_t>(valid) * sizeof(float));

        if (looksLikeDuplicatedStereo(samples.data(), valid)) {
            out->resize(static_cast<size_t>(valid) / 2);
            for (size_t i = 0; i < out->size(); ++i) {
                (*out)[i] = samples[i * 2];
            }
        } else {
            *out = std::move(samples);
        }
        return true;
    }

    if (h.format == FormatInt16) {
        const int available = payloadBytes / static_cast<int>(sizeof(qint16));
        int valid = static_cast<int>(h.length);
        if (valid > available) {
            valid = available;
        }
        if (valid <= 0) {
            return false;
        }
        std::vector<float> samples(static_cast<size_t>(valid));
        for (int i = 0; i < valid; ++i) {
            const qint16 s = qFromLittleEndian<qint16>(body + i * 2);
            samples[static_cast<size_t>(i)] = s / 32768.0f;
        }
        if (looksLikeDuplicatedStereo(samples.data(), valid)) {
            out->resize(static_cast<size_t>(valid) / 2);
            for (size_t i = 0; i < out->size(); ++i) {
                (*out)[i] = samples[i * 2];
            }
        } else {
            *out = std::move(samples);
        }
        return true;
    }

    return false; // int24 / int32 are legal in the spec but no observed client sends them
}

} // namespace TciAudioFrame
