#ifndef HALIKEYMIDIWORKER_H
#define HALIKEYMIDIWORKER_H

#include "halikeyworkerbase.h"
#include <memory>

class RtMidiIn;

class HaliKeyMidiWorker : public HaliKeyWorkerBase {
    Q_OBJECT

public:
    explicit HaliKeyMidiWorker(const QString &deviceName, QObject *parent = nullptr);
    ~HaliKeyMidiWorker() override;

    // Closes MIDI port and stops RtMidi callback thread before QThread teardown.
    // Called from main thread — RtMidi::closePort() synchronizes internally.
    void prepareShutdown() override;

public slots:
    void start() override;

private:
    static void midiCallback(double deltaTime, std::vector<unsigned char> *message, void *userData);
    void handleMidiMessage(double deltaTime, const std::vector<unsigned char> &message);

    std::unique_ptr<RtMidiIn> m_midiIn;

    // MoMIDI protocol state
    bool m_momidiDetected = false;
    int m_pendingTimeMsb = 0;
};

#endif // HALIKEYMIDIWORKER_H
