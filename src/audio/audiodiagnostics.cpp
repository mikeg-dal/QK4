#include "audio/audiodiagnostics.h"
#include "audio/rawaudioformat.h" // EM0_FULL_SCALE
#include "logging/logcategories.h"

#include <cmath>

namespace AudioDiagnostics {
namespace {
double dbfs(double v) {
    return v > 0.0 ? 20.0 * std::log10(v) : -999.0;
}
} // namespace

void logRxLevels(const RxLevelReport &r) {
    qCDebug(lcAudioRx,
            "RX 1s: main peak %.1f rms %.1f dBFS | sub peak %.1f rms %.1f dBFS | out peak %.1f dBFS over-FS %d | "
            "sliders main %.2f sub %.2f boost %+.0f dB (%s)",
            dbfs(r.peakMain), dbfs(r.rmsMain), dbfs(r.peakSub), dbfs(r.rmsSub), dbfs(r.peakOut), r.clipped,
            static_cast<double>(r.mainSlider), static_cast<double>(r.subSlider), dbfs(r.boost),
            r.boost > 1.0f ? "soft-limited" : "clamped");
}

void logRxPlayout(const RxPlayoutReport &r) {
    qCDebug(lcAudioRx,
            "RX playout: jitter %d ms -> target %d ms over %d ms packets | device period %d ms%s | buffered %d ms | "
            "since reset: %d silence inserts, %d shrink skips, %d backlog trims, %d stalls%s",
            r.jitterMs, r.targetMs, r.packetMs, r.devicePeriodMs, r.devicePeriodMeasured ? "" : " (assumed)",
            r.bufferedMs, r.silenceInserts, r.shrinkSkips, r.backlogTrims, r.stalls, r.inStall ? " | IN STALL" : "");
}

void logTxFrame(const TxFrameReport &r) {
    // dBFS rather than raw counts: the TX path is float end to end, and a level in dB is comparable
    // across encode modes whose containers have different full scales.
    //
    // The peak is measured AFTER the gain, so a quiet frame is otherwise ambiguous between a quiet
    // room and a turned-down slider - an ambiguity that cost a bench cycle on 2026-09-19. Report
    // both the effective gain and the slider position it came from: both setters cube the slider,
    // so the cube root recovers exactly what the operator set. Which gain is named follows the
    // source, so a TCI bench log does not describe the microphone's control.
    const double gain = static_cast<double>(r.gain);
    qCDebug(lcAudioTx,
            "TX frame: EM%d seq=%u frameSamples=%d peak=%.1f dBFS peak-since-key=%.1f dBFS "
            "(%s slider %.0f%% -> gain %.4f) wire-payload=%d",
            r.encodeMode, r.sequence, r.frameSamples, dbfs(r.framePeak), dbfs(r.peakSinceKey),
            r.fromTci ? "TCI TX" : "mic", std::cbrt(gain) * 100.0, gain, r.wirePayloadBytes);

    if (r.em0WirePeak < 0)
        return;
    // WHY: report the EM0 payload against the format's known full scale. AUD-003 survived because
    // nothing QK4 can hear tells a wrong container apart from a dead mic - the bytes are only wrong
    // at the radio. A number here makes a future format mistake obvious in a bench log.
    const double fullScale = static_cast<double>(RawAudioFormat::EM0_FULL_SCALE);
    qCDebug(lcAudioTx, "  EM0 wire peak=%lld S32LE = %.4f of the %.0f full scale",
            static_cast<long long>(r.em0WirePeak), static_cast<double>(r.em0WirePeak) / fullScale, fullScale);
}

} // namespace AudioDiagnostics
