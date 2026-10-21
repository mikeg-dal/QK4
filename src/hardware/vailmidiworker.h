#ifndef VAILMIDIWORKER_H
#define VAILMIDIWORKER_H

#include "halikeyworkerbase.h"
#include "vail_midi_protocol.h"
#include <QMutex>
#include <memory>

class QTimer;
class RtMidiIn;
class RtMidiOut;

class VailMidiWorker : public HaliKeyWorkerBase {
    Q_OBJECT

public:
    explicit VailMidiWorker(const QString &deviceName, int wpm = 20, int pitchHz = 600, int mode = 0,
                            QObject *parent = nullptr);
    ~VailMidiWorker() override;
    void prepareShutdown() override;

public slots:
    void start() override;
    void configure(int wpm, int pitchHz, int mode);
    void setKeyingEnabled(bool enabled);

signals:
    void timedKeyingCommand(const QString &command);
    void keyingStateChanged(bool down, qint64 timestampNs);
    void keyingAborted();

private:
    static void midiCallback(double deltaTime, std::vector<unsigned char> *message, void *userData);
    static std::int64_t nowNs();
    void handleMidiMessage(const std::vector<unsigned char> &message, std::int64_t timestampNs);
    void deliver(const VailMidiProtocol::Result &result, std::int64_t timestampNs);
    bool applyConfiguration();
    void checkPortPresence();
    void fail(const QString &message);

    std::unique_ptr<RtMidiIn> m_midiIn;
    std::unique_ptr<RtMidiOut> m_midiOut;
    // start(), configure(), polling and main-thread prepareShutdown() share these ports.
    // Never hold this mutex while emitting a signal or waiting for the worker event loop.
    QMutex m_midiMutex;
    bool m_shutdown = false;
    QString m_outputError; // RtMidiOut callbacks are synchronous under m_midiMutex.
    QTimer *m_presenceTimer = nullptr;
    QTimer *m_holdTimer = nullptr;
    int m_missedPolls = 0;
    VailMidiProtocol::Configuration m_configuration;
    VailMidiProtocol::Configuration m_appliedConfiguration;
    bool m_configurationApplied = false;
    VailMidiProtocol::Stream m_stream;
};

#endif // VAILMIDIWORKER_H
