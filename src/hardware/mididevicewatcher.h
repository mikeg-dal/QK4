#ifndef HARDWARE_MIDIDEVICEWATCHER_H
#define HARDWARE_MIDIDEVICEWATCHER_H

#include <QObject>

#include <memory>

class QThread;

// Keeps macOS's list of MIDI devices current while QK4 runs, and says when it changes.
//
// WHY it exists: CoreMIDI updates a process's view of its devices through notifications, which it
// delivers "on the same run loop that you created the client on" (Apple, MIDIClientCreate), and
// only while that run loop is running. QK4's first MIDI client used to be created by RtMidi on a
// port-scan thread with no run loop, which exits at once, so a HaliKey MIDI plugged in after that
// first scan never appeared: not on the HaliKey page, not after Refresh. This object owns a client
// on a thread whose CFRunLoop runs for the life of the app. Create it before anything else touches
// MIDI, so it is the process's first client.
//
// On Windows and Linux it does nothing: WinMM and ALSA enumerate the current devices on every call,
// and devicesChanged() is never emitted.
class MidiDeviceWatcher : public QObject {
    Q_OBJECT

public:
    explicit MidiDeviceWatcher(QObject *parent = nullptr);
    ~MidiDeviceWatcher() override;

    // Ends the watcher thread (bounded; see ThreadShutdown). Safe to call twice; the destructor
    // calls it too.
    void stop();

    // Implementation detail, shared with the watcher thread (mididevicewatcher.cpp).
    struct Shared;

signals:
    // A MIDI device or port appeared, disappeared or changed. Delivered on this object's thread.
    void devicesChanged();

private:
    std::shared_ptr<Shared> m_shared;
    QThread *m_thread = nullptr;
};

#endif // HARDWARE_MIDIDEVICEWATCHER_H
