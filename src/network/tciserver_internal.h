#ifndef NETWORK_TCISERVER_INTERNAL_H
#define NETWORK_TCISERVER_INTERNAL_H

// Shared between tciserver.cpp and tciserverstate.cpp, which are two halves of one class:
// the dispatch/transport half and the state-reporting half. Not part of the public interface.

#include "network/tciaudioframe.h"
#include "network/tciprotocol.h"

namespace TciServerInternal {

using namespace TciProtocol;

// What QK4 claims to support. Sent verbatim as free text because it is legitimately
// comma-separated - scrubbing it would corrupt the value.
// What QK4 claims to support. nfm is deliberately ABSENT: the K4 has one FM mode and no narrow
// variant, so advertising nfm would offer a mode the radio cannot enter. A client that sends nfm
// anyway is still understood as FM - see k4ModeFor.
const char kModulationsList[] = "usb,lsb,cw,cwr,am,sam,fm,digu,digl,rtty";
// WSJT-X matches on this string; a mangled one makes it halve transmit amplitude.
const char kProtocolIdentity[] = "ExpertSDR3,1.5";

// The K4's tuning range.
constexpr qint64 kVfoLowHz = 100000;
constexpr qint64 kVfoHighHz = 54000000;
constexpr int kIfLowHz = -48000;
constexpr int kIfHighHz = 48000;

// WSJT-X always treats TCI audio as 48 kHz regardless of what a server declares, so declaring
// anything else is misleading at best. See docs/tci-server-design.md.
constexpr int kAudioSampleRate = 48000;
constexpr int kAudioStreamSamples = TciAudioFrame::CHRONO_FLOATS;

// One chrono asks for CHRONO_FLOATS floats = 1024 stereo frames = 21.333 ms at 48 kHz.
constexpr qint64 kChronoPeriodNs =
    static_cast<qint64>(TciAudioFrame::CHRONO_FLOATS / 2) * 1000000000LL / kAudioSampleRate;
constexpr int kChronoPollMs = 5;

// Audio blocks are far too frequent to log individually - roughly 47 a second each way. Summarise
// instead, so a log can answer "is audio actually moving" without drowning everything else.
// Sensor reporting interval. The spec allows 30..1000 ms and makes the argument optional; 200 ms
// is a readable meter without flooding the link, and is what the reference server defaults to.
constexpr int kSensorIntervalDefaultMs = 200;
constexpr int kSensorIntervalMinMs = 30;
constexpr int kSensorIntervalMaxMs = 1000;

constexpr int kRxSummaryEveryBlocks = 200; // ~4.3 s
constexpr int kTxSummaryEveryBlocks = 100; // ~2.1 s

// Client roster (the options page table).
//
// Announcing the roster is a queued signal carrying a copy of up to MAX_CLIENTS entries, so it
// cannot ride every frame: TX audio alone arrives ~47 times a second. Half a second is below what
// a table showing whole seconds can distinguish, so the throttle costs the operator nothing.
constexpr int kClientAnnounceMinMs = 500;

// A TCI message is tens of bytes, but nothing stops a client sending a long one, and the table is
// one line per row. Elided rather than wrapped so a row cannot push the rest of the page around.
constexpr int kClientMessageMaxChars = 80;

inline QString elideForDisplay(const QString &text) {
    if (text.size() <= kClientMessageMaxChars) {
        return text;
    }
    return text.left(kClientMessageMaxChars - 1) + QChar(0x2026); // horizontal ellipsis
}

} // namespace TciServerInternal

#endif // NETWORK_TCISERVER_INTERNAL_H
