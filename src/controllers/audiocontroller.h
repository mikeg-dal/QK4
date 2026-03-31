#ifndef AUDIOCONTROLLER_H
#define AUDIOCONTROLLER_H

#include <QObject>
#include <QThread>

class AudioEngine;
class OpusDecoder;
class OpusEncoder;
class ConnectionController;
class RadioState;
class Protocol;

class AudioController : public QObject {
    Q_OBJECT

public:
    AudioController(ConnectionController *connController, RadioState *radioState, QObject *parent = nullptr);
    ~AudioController();

    // Audio lifecycle
    void startAudio(float mainVolume, float subVolume, float micGain);
    void stopAudio();

    // PTT control
    void setPttActive(bool active);
    bool isPttActive() const { return m_pttActive; }

    // Volume/mix controls (atomic — safe from any thread)
    void setMainVolume(float vol);
    void setSubVolume(float vol);
    void setBalanceMode(int mode);
    void setBalanceOffset(int offset);
    void setAudioMix(int left, int right);
    void setSubMuted(bool muted);

    // Accessor for OptionsDialog integration
    AudioEngine *audioEngine() const { return m_audioEngine; }

signals:
    void pttStateChanged(bool active);

private slots:
    void onMicrophoneFrame(const QByteArray &s16leData);

private:
    ConnectionController *m_connectionController;
    RadioState *m_radioState;

    AudioEngine *m_audioEngine;
    QThread *m_audioThread = nullptr;
    OpusDecoder *m_opusDecoder;
    OpusEncoder *m_opusEncoder;

    bool m_pttActive = false;
    quint8 m_txSequence = 0;
};

#endif // AUDIOCONTROLLER_H
