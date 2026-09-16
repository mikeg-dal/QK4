#ifndef NETWORK_TCIRADIOSTATE_H
#define NETWORK_TCIRADIOSTATE_H

#include <QString>

// The radio model the TCI server answers from.
//
// WHY a snapshot rather than a RadioState pointer: RadioState is main-thread-only and CI-enforced
// (CONVENTIONS.md rule 4), and its getters carry no locking. The TCI server runs on its own
// thread, so it keeps its own copy, fed by queued signals. Building the init burst from one pass
// over this struct also keeps it self-consistent - RadioState emits per-field signals with no
// batch boundary, so live reads could seed a client with a frequency and mode that never
// coexisted.
//
// SPLIT INTO TWO RECEIVERS, which is the whole point of this file existing separately.
//
// TCI has two axes and they carry different things:
//
//   * CHANNEL (A/B within a receiver) carries ONLY frequency and audio - vfo, rx_channel_enable,
//     rx_volume, rx_balance, vfo_lock, rx_channel_sensors.
//   * TRX (receiver) carries everything else - modulation, rx_filter_band, agc_mode, rit/xit,
//     sql, the DSP flags, lock, drive.
//
// The K4's Sub RX has its own mode, filter, AGC, RIT and S-meter, so it is a RECEIVER in TCI's
// sense, not a channel. Modelling it as channel 1 (the first attempt) left every one of those
// settings with nowhere to live: `modulation` has no channel argument, so VFO B's mode could not
// be reported at all. ExpertSDR3's channels A/B are two tuning points inside one receiver's
// passband, which is a different thing from a second receiver.
//
// Channel 1 of receiver 0 keeps its other job - the split transmit VFO. On a K4 that is the same
// VFO B the Sub RX tunes, so both mappings point at one piece of hardware, which is correct.
namespace TciRadio {

// How many receivers QK4 advertises: Main and Sub.
constexpr int RECEIVER_COUNT = 2;
constexpr int MAIN_RECEIVER = 0;
constexpr int SUB_RECEIVER = 1;

// Channels within a receiver. Channel A is always on; channel B of the main receiver is the split
// transmit VFO.
constexpr int CHANNEL_A = 0;
constexpr int CHANNEL_B = 1;

} // namespace TciRadio

// Everything TCI addresses per receiver.
struct TciReceiverState {
    qint64 vfoHz = 14074000;
    QString modulation = QStringLiteral("usb");

    // Receiver 0 is always on. Receiver 1 follows the K4's Sub RX.
    bool enabled = true;

    bool rit = false;
    bool xit = false;

    // ONE offset, matching the radio. The K4 has a single RO register shared by RIT and XIT, with
    // RT and XT as independent enables, and RadioState models it the same way (ritXitOffset(),
    // ritXitChanged(rit, xit, offset)). TCI defines RIT_OFFSET and XIT_OFFSET as two values; they
    // are reported from this one and can never be set independently.
    // See docs/tci-command-coverage.md section 4.1.
    int ritXitOffsetHz = 0;

    int filterLowHz = 100;
    int filterHighHz = 2800;

    // TCI defines exactly three AGC modes: normal, fast, off. Anything else is unparseable to a
    // client matching the documented vocabulary.
    QString agcMode = QStringLiteral("normal");

    bool sqlEnabled = false;
    // TCI squelch is an ABSOLUTE threshold in dBm, range -140..0. QK4 does not know the K4's
    // squelch threshold in dBm - the radio reports SQ as an arbitrary integer scale - so no
    // truthful conversion exists. -140 is "opens on anything", which is both in range and the
    // least misleading thing to claim. See docs/tci-command-coverage.md.
    int sqlLevelDbm = -140;

    // Receiver audio level in dB, the protocol's -60..0 with -60 silent.
    //
    // The K4's AG runs 0-60 with 0 silent, so the two scales have the SAME WIDTH and matching
    // endpoints and the conversion is dB = AG - 60. That correspondence is why this can be
    // reported at all, where squelch cannot: SQ is 0-40 against a -140..0 dBm field, which is a
    // calibration guess rather than a mapping. See docs/tci-command-coverage.md.
    int volumeDb = 0;

    bool noiseBlanker = false;
    bool noiseReduction = false;
    bool autoNotch = false;
    bool apf = false;
    // The K4's manual notch. TCI calls the module RX_NF_ENABLE; RX_ANF_ENABLE is the automatic
    // one, which is autoNotch above. Two different filters, so two different flags.
    bool notchFilter = false;
    bool lock = false;
};

struct TciRadioSnapshot {
    TciRadioSnapshot() {
        // Receiver 0 is always on; the Sub RX is off until the radio says otherwise. Leaving it
        // at TciReceiverState's default of enabled made a client's "turn the sub receiver on"
        // look like a no-op, because the server already believed it was on.
        rx[TciRadio::SUB_RECEIVER].enabled = false;
    }

    TciReceiverState rx[TciRadio::RECEIVER_COUNT];

    bool split = false;
    bool transmitting = false;

    // The power QK4 displays, reported as-is: watts in QRP and QRO, mW in XVTR. Not rescaled to a
    // percentage, so the K4 front panel, QK4's PWR button and a TCI client all show the same
    // number. Clamped to the protocol's 0..100, which only bites in the 101-110 W QRO headroom.
    // See docs/tci-command-coverage.md.
    int drive = 100;

    // The K4 has no separate tune-power setting, so this tracks drive. Reporting an independent
    // value would invent a control the radio does not have.
    int tuneDrive = 100;
    int micLevel = 50;

    // CW keyer speed in WPM. A property of the transmitter, not of a receiver, so it sits here
    // rather than in TciReceiverState - TCI's CW_KEYER_SPEED carries no receiver index either.
    int cwKeyerSpeedWpm = 20;

    bool validReceiver(int index) const { return index >= 0 && index < TciRadio::RECEIVER_COUNT; }

    // Channel 1 of the MAIN receiver is the transmit VFO, and on a K4 that is VFO B - the same
    // frequency the Sub RX tunes. With split off it must still report something coherent, the
    // receive frequency, rather than the 0 a blank VFO B holds and a client would tune to.
    qint64 txChannelHz() const {
        const qint64 vfoB = rx[TciRadio::SUB_RECEIVER].vfoHz;
        return (split && vfoB > 0) ? vfoB : rx[TciRadio::MAIN_RECEIVER].vfoHz;
    }
};

// Fast-moving telemetry, kept OUT of the snapshot on purpose.
//
// The snapshot is slow state and is broadcast on change. Sensors move continuously - the S-meter
// updates several times a second - so running them through the same diff would either flood every
// client or need a change threshold nobody agreed on. They are stored instead, and emitted on a
// timer at the interval the client asked for, only to clients that asked.
struct TciSensorReadings {
    // Absolute signal level in the RX filter bandwidth, per receiver. dBm, as the protocol wants.
    double sMeterDbm[TciRadio::RECEIVER_COUNT] = {-140.0, -140.0};

    // Transmit. QK4 measures forward power and SWR; see micLevelDbm for the one field it cannot
    // fill honestly.
    double forwardPowerW = 0.0;
    double peakPowerW = 0.0;
    double swr = 1.0;

    // The protocol wants microphone signal level in dBm. The K4 reports ALC deflection, which is a
    // drive indicator and not a calibrated microphone level, so there is no truthful conversion.
    // This stays at the floor rather than passing off a scaled ALC reading as a measurement.
    // See docs/tci-command-coverage.md.
    double micLevelDbm = -60.0;
};

#endif // NETWORK_TCIRADIOSTATE_H
