#ifndef HALIKEYMIDIWORKER_H
#define HALIKEYMIDIWORKER_H

#include "halikeyworkerbase.h"
#include <memory>

class RtMidiIn;

/**
 * @brief HaliKey worker for the MIDI variant of the paddle device. Uses RtMidi with a callback
 *        thread (separate from QThread's event loop) that writes raw atomic state into
 *        HalikeyDevice. Implements the MoMIDI extended protocol for time-stamped paddle events.
 *        `prepareShutdown()` synchronously closes the MIDI port so RtMidi's callback thread
 *        quits before QThread tears down.
 */
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

    // Last-known state of each line. MIDI reports one line per message, so these carry the other
    // two forward when a note arrives — see the switch in handleMidiMessage().
    bool m_ditState = false;
    bool m_dahState = false;
    bool m_pttState = false;

    // MoMIDI protocol state
    bool m_momidiDetected = false;
    int m_pendingTimeMsb = 0;
};

#endif // HALIKEYMIDIWORKER_H
