#ifndef MODELS_RADIOSTATE_AUDIOEFFECTSSTATE_H
#define MODELS_RADIOSTATE_AUDIOEFFECTSSTATE_H

#include <QString>
#include <QVector>

class RadioState;

// Audio pipeline state — VOX, audio effects, audio peak filter, ESSB,
// equalizers, line in/out levels, mic setup, monitor level, audio mix
// routing, and balance.
//
// Covers CAT commands: FX (audio effects), AP/AP$ (audio peak filter),
// VX (VOX enable), VG (VOX gain), VI (anti-VOX), ES (ESSB + SSB TX BW),
// RE/TE (RX/TX graphic EQ), LO (line out), LI (line in), MI (mic
// input), MS (mic setup), ML (monitor level), MX (audio mix routing),
// BL (balance).
//
// These fields all relate to the audio signal path — the chain from
// DDS out → speaker/line out, or mic/line in → modulator — so they
// cluster naturally.
struct AudioEffectsState {
    // FX — Audio Effects: 0=off, 1=delay, 2=pitch-map
    int afxMode = 0;

    // AP / AP$ — Audio Peak Filter (CW only); bandwidth 0=30Hz, 1=50Hz, 2=150Hz
    bool apfEnabled = false;
    int apfBandwidth = 0;
    bool apfEnabledB = false;
    int apfBandwidthB = 0;

    // VX — VOX enable flags per mode class
    bool voxCW = false;
    bool voxVoice = false;
    bool voxData = false;

    // VG — VOX gain per mode class (0-60)
    int voxGainVoice = -1;
    int voxGainData = -1;

    // VI — Anti-VOX (0-60)
    int antiVox = -1;

    // ES — ESSB enable + TX bandwidth (30-45, i.e. 3.0-4.5 kHz in 100Hz units)
    bool essbEnabled = false;
    int ssbTxBw = -1;

    // RE / TE — graphic EQ: 8 bands (100, 200, 400, 800, 1200, 1600, 2400,
    // 3200 Hz), range -16..+16 dB. Main RX and Sub RX share RE bands.
    int rxEqBands[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    int txEqBands[8] = {0, 0, 0, 0, 0, 0, 0, 0};

    // LO — Line Out (0-40 per channel). rightEqualsLeft is the "mode" flag.
    int lineOutLeft = -1;
    int lineOutRight = -1;
    bool lineOutRightEqualsLeft = false;

    // LI — Line In (0-250 per source). lineInSource: 0=soundcard, 1=jack.
    int lineInSoundCard = -1;
    int lineInJack = -1;
    int lineInSource = -1;

    // MI — Mic input: 0=front, 1=rear, 2=line in, 3=front+line, 4=rear+line
    int micInput = -1;

    // MS — Mic setup (front preamp 0/1/2=0dB/10dB/20dB, bias/buttons 0/1,
    // rear preamp 0/1=0dB/14dB, rear bias 0/1).
    int micFrontPreamp = -1;
    int micFrontBias = -1;
    int micFrontButtons = -1;
    int micRearPreamp = -1;
    int micRearBias = -1;

    // ML — Monitor level per mode class (0=CW, 1=AF data, 2=voice); range 0-100.
    int monitorLevelCW = -1;
    int monitorLevelData = -1;
    int monitorLevelVoice = -1;

    // MX — Audio mix routing (MixSource enum: 0=A, 1=B, 2=AB, 3=-A)
    int audioMixLeft = -1;
    int audioMixRight = -1;

    // BL — Balance. mode: 0=NOR, 1=BAL. offset: -50..+50.
    int balanceMode = -1;
    int balanceOffset = -99; // sentinel outside valid range

    void reset();
};

namespace AudioEffectsHandlers {

// CAT inbound handlers — mirror K4 radio echoes.
void handleFX(AudioEffectsState &state, RadioState &owner, const QString &cmd);
void handleAP(AudioEffectsState &state, RadioState &owner, const QString &cmd);
void handleAPSub(AudioEffectsState &state, RadioState &owner, const QString &cmd);
void handleVX(AudioEffectsState &state, RadioState &owner, const QString &cmd);
void handleVG(AudioEffectsState &state, RadioState &owner, const QString &cmd);
void handleVI(AudioEffectsState &state, RadioState &owner, const QString &cmd);
void handleLO(AudioEffectsState &state, RadioState &owner, const QString &cmd);
void handleLI(AudioEffectsState &state, RadioState &owner, const QString &cmd);
void handleMI(AudioEffectsState &state, RadioState &owner, const QString &cmd);
void handleMS(AudioEffectsState &state, RadioState &owner, const QString &cmd);
void handleES(AudioEffectsState &state, RadioState &owner, const QString &cmd);
void handleRE(AudioEffectsState &state, RadioState &owner, const QString &cmd);
void handleTE(AudioEffectsState &state, RadioState &owner, const QString &cmd);
void handleML(AudioEffectsState &state, RadioState &owner, const QString &cmd);
void handleBL(AudioEffectsState &state, RadioState &owner, const QString &cmd);
void handleMX(AudioEffectsState &state, RadioState &owner, const QString &cmd);

// Optimistic setters (radio does not echo most of these).
void setAudioMix(AudioEffectsState &state, RadioState &owner, int left, int right);
void setBalance(AudioEffectsState &state, RadioState &owner, int mode, int offset);
void setMonitorLevel(AudioEffectsState &state, RadioState &owner, int mode, int level);
void setRxEqBand(AudioEffectsState &state, RadioState &owner, int index, int dB);
void setRxEqBands(AudioEffectsState &state, RadioState &owner, const QVector<int> &bands);
void setTxEqBand(AudioEffectsState &state, RadioState &owner, int index, int dB);
void setTxEqBands(AudioEffectsState &state, RadioState &owner, const QVector<int> &bands);
void setLineOutLeft(AudioEffectsState &state, RadioState &owner, int level);
void setLineOutRight(AudioEffectsState &state, RadioState &owner, int level);
void setLineOutRightEqualsLeft(AudioEffectsState &state, RadioState &owner, bool enabled);
void setLineInSoundCard(AudioEffectsState &state, RadioState &owner, int level);
void setLineInJack(AudioEffectsState &state, RadioState &owner, int level);
void setLineInSource(AudioEffectsState &state, RadioState &owner, int source);
void setMicInput(AudioEffectsState &state, RadioState &owner, int input);
void setMicFrontPreamp(AudioEffectsState &state, RadioState &owner, int preamp);
void setMicFrontBias(AudioEffectsState &state, RadioState &owner, int bias);
void setMicFrontButtons(AudioEffectsState &state, RadioState &owner, int buttons);
void setMicRearPreamp(AudioEffectsState &state, RadioState &owner, int preamp);
void setMicRearBias(AudioEffectsState &state, RadioState &owner, int bias);
void setVoxGainVoice(AudioEffectsState &state, RadioState &owner, int gain);
void setVoxGainData(AudioEffectsState &state, RadioState &owner, int gain);
void setAntiVox(AudioEffectsState &state, RadioState &owner, int level);
void setEssbEnabled(AudioEffectsState &state, RadioState &owner, bool enabled);
void setSsbTxBw(AudioEffectsState &state, RadioState &owner, int bw);

} // namespace AudioEffectsHandlers

#endif // MODELS_RADIOSTATE_AUDIOEFFECTSSTATE_H
