#ifndef HALIKEYMIDIWORKER_H
#define HALIKEYMIDIWORKER_H

#include "halikeyworkerbase.h"
#include <QMutex>
#include <memory>

class QTimer;
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
    // Called from the MAIN thread, while the presence poll below may be using m_midiIn on the
    // worker thread — which is why m_midiIn is guarded by m_midiMutex. RtMidi synchronises its own
    // callback thread, but nothing in RtMidi protects the RtMidiIn object from being destroyed
    // while another thread is calling into it.
    void prepareShutdown() override;

public slots:
    void start() override;

private:
    static void midiCallback(double deltaTime, std::vector<unsigned char> *message, void *userData);
    void handleMidiMessage(double deltaTime, const std::vector<unsigned char> &message);

    // Has the port we opened disappeared from the enumeration? Runs on the worker thread.
    void checkPortPresence();

    std::unique_ptr<RtMidiIn> m_midiIn;

    // Guards m_midiIn against the main thread's prepareShutdown()/destructor racing the presence
    // poll on the worker thread. Never held while emitting.
    QMutex m_midiMutex;

    // Presence poll. Created on the worker thread in start(), so it is owned by that thread's
    // event loop and dies with it; the main thread never touches it.
    QTimer *m_presenceTimer = nullptr;

    // Consecutive polls that did not find the port. Two are required before reporting removal, so
    // a momentary enumeration glitch (a MIDIServer restart, a driver reload) cannot disconnect a
    // device that is still plugged in.
    int m_missedPolls = 0;

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
