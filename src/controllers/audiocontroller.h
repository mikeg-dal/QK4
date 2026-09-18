#ifndef AUDIOCONTROLLER_H
#define AUDIOCONTROLLER_H

#include <QObject>
#include <QThread>
#include <QVector>

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
    bool isProgramAudioActive() const;
    void startProgramAudio(const QVector<qint16> &samples, float gain = 0.12f);
    void setProgramAudioGain(float gain);
    void stopProgramAudio();

    // Volume/mix controls (atomic — safe from any thread)
    void setMainVolume(float vol);
    void setSubVolume(float vol);
    void setBalanceMode(int mode);
    void setBalanceOffset(int offset);
    void setAudioMix(int left, int right);
    void setSubMuted(bool muted);

    // Device selection + mic gain — used by Options dialog tabs. Each dispatches via
    // QMetaObject::invokeMethod to the audio thread internally. Task-level API only —
    // per CONVENTIONS.md Rule 2, callers do not get direct access to AudioEngine.
    void setMicDevice(const QString &deviceId);
    void setOutputDevice(const QString &deviceId);
    void setMicGain(float gain); // 0.0 to 1.0

signals:
    // Decoded K4 stereo Float32 PCM before playback volume and routing.
    void receivePcmAvailable(const QByteArray &stereoFloatPcm, qint64 receivedUtcMs);
    void programAudioStarted(int totalSamples);
    void programAudioProgress(int emittedSamples, int totalSamples);
    void programAudioFinished(bool completed);

private slots:
    void onStreamingLatencyChanged(int tier);

private:
    ConnectionController *m_connectionController;
    RadioState *m_radioState;

    AudioEngine *m_audioEngine;
    QThread *m_audioThread = nullptr;
    OpusDecoder *m_opusDecoder;
};

#endif // AUDIOCONTROLLER_H
