#ifndef AUDIOCONTROLLER_H
#define AUDIOCONTROLLER_H

#include <QObject>
#include <QString>
#include <QThread>

class AudioEngine;
class OpusDecoder;
class ConnectionController;
class RadioState;

/**
 * @brief Owns the audio thread + AudioEngine + Opus decoder. Task-level API over the RX/TX paths:
 *        startAudio/stopAudio, PTT toggle, atomic volume/mix/balance setters. Connects to
 *        RadioState.streamingLatencyChanged to resize TX Opus frames in step with the K4's SL tier.
 *
 * Threading:
 *   - AudioEngine is moved to `m_audioThread` (single moveToThread at construction).
 *   - OpusDecoder keeps main-thread affinity but is only called from the IO-thread lambda
 *     wired to Protocol::audioDataReady — effectively single-threaded on the IO thread.
 *   - The TX encode pipeline (OpusEncoder + packet framing) lives on AudioEngine and runs
 *     fully on the audio thread (PR 12). AudioController only forwards PTT toggles and SL
 *     tier changes; the audio thread emits txPacketReady straight to TcpClient::sendRaw.
 *   Public AudioController methods are safe to call from the main thread (they dispatch via
 *   QMetaObject::invokeMethod / atomics).
 */
class AudioController : public QObject {
    Q_OBJECT

public:
    AudioController(ConnectionController *connController, RadioState *radioState, QObject *parent = nullptr);
    ~AudioController();

    // Audio lifecycle
    void startAudio(float mainVolume, float subVolume, float micGain);
    void stopAudio();
    void shutdown();

    // PTT control — forwards to AudioEngine (runs on the audio thread).
    void setPttActive(bool active);
    bool isPttActive() const;

    // Volume/mix controls (atomic — safe from any thread)
    static QString audioMixCommand(int left, int right) {
        auto component = [](int value) -> QString {
            switch (value) {
            case 0:
                return QStringLiteral("A");
            case 1:
                return QStringLiteral("B");
            case 2:
                return QStringLiteral("AB");
            case 3:
                return QStringLiteral("-A");
            }
            return QStringLiteral("A");
        };

        return QStringLiteral("MX%1.%2;").arg(component(left), component(right));
    }
    static QString monoMixCommand(bool enabled) { return enabled ? QStringLiteral("MXAB.AB;") : audioMixCommand(0, 1); }
    void setMainVolume(float vol);
    void setSubVolume(float vol);
    void setBalanceMode(int mode);
    void setBalanceOffset(int offset);
    void setAudioMix(int left, int right);
    void setSubMuted(bool muted);
    void setMonoMixEnabled(bool enabled);

    // Device selection + mic gain — used by Options dialog tabs. Each dispatches via
    // QMetaObject::invokeMethod to the audio thread internally. Task-level API only —
    // per CONVENTIONS.md Rule 2, callers do not get direct access to AudioEngine.
    void setMicDevice(const QString &deviceId);
    void setOutputDevice(const QString &deviceId);
    void setMicGain(float gain); // 0.0 to 1.0

private slots:
    void onStreamingLatencyChanged(int tier);

private:
    ConnectionController *m_connectionController;
    RadioState *m_radioState;

    AudioEngine *m_audioEngine;
    QThread *m_audioThread = nullptr;
    OpusDecoder *m_opusDecoder;
    int m_restoreMixLeft = -1;
    int m_restoreMixRight = -1;
};

#endif // AUDIOCONTROLLER_H
