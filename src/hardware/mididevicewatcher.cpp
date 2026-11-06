#include "hardware/mididevicewatcher.h"

#include "logging/logcategories.h"
#include "utils/threadshutdown.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QLoggingCategory>
#include <QPointer>
#include <QThread>

#include <atomic>

#ifdef Q_OS_MACOS
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/CoreMIDI.h>
#endif

namespace {
constexpr int kStopTimeoutMs = 2000;
} // namespace

// Shared with the watcher thread, so a thread that has to be disowned at quit never reaches a
// deleted MidiDeviceWatcher: it only posts to the application object, and the posted call finds
// the watcher through a QPointer read on the main thread.
struct MidiDeviceWatcher::Shared {
    std::atomic<bool> stop{false};
    QPointer<MidiDeviceWatcher> watcher; // read and written on the watcher object's thread only
};

#ifdef Q_OS_MACOS
namespace {

// How often the watcher thread looks at its stop flag, which bounds how long stop() waits for it.
constexpr double kStopPollSeconds = 0.25;

void postDevicesChanged(const std::shared_ptr<MidiDeviceWatcher::Shared> &shared) {
    QCoreApplication *app = QCoreApplication::instance();
    if (!app || shared->stop.load(std::memory_order_acquire))
        return;
    QMetaObject::invokeMethod(
        app,
        [shared]() {
            if (MidiDeviceWatcher *watcher = shared->watcher.data())
                emit watcher->devicesChanged();
        },
        Qt::QueuedConnection);
}

struct NotifyContext {
    std::shared_ptr<MidiDeviceWatcher::Shared> shared;
};

// Runs on the watcher thread, from its run loop.
void onMidiNotification(const MIDINotification *message, void *refCon) {
    // kMIDIMsgSetupChanged follows every add, remove or property change, so it alone is enough.
    if (message && message->messageID == kMIDIMsgSetupChanged)
        postDevicesChanged(static_cast<NotifyContext *>(refCon)->shared);
}

void keepAlive(CFRunLoopTimerRef, void *) {}

} // namespace
#endif

MidiDeviceWatcher::MidiDeviceWatcher(QObject *parent) : QObject(parent), m_shared(std::make_shared<Shared>()) {
    m_shared->watcher = this;
#ifdef Q_OS_MACOS
    m_thread = QThread::create([shared = m_shared]() {
        NotifyContext context{shared};
        MIDIClientRef client = 0;
        const OSStatus status =
            MIDIClientCreate(CFSTR("QK4 MIDI device watcher"), onMidiNotification, &context, &client);
        if (status != noErr) {
            qCWarning(lcHardware) << "MIDI device watcher: MIDIClientCreate failed, OSStatus" << status
                                  << "- MIDI devices plugged in later will not be seen until QK4 restarts";
            return;
        }
        // WHY a timer: CFRunLoopRunInMode returns at once when the loop has no source, which would
        // turn the stop-flag loop below into a busy spin. A far-off repeating timer is a source that
        // never does anything.
        CFRunLoopTimerRef timer = CFRunLoopTimerCreate(kCFAllocatorDefault, CFAbsoluteTimeGetCurrent() + 3600.0, 3600.0,
                                                       0, 0, keepAlive, nullptr);
        CFRunLoopAddTimer(CFRunLoopGetCurrent(), timer, kCFRunLoopDefaultMode);
        while (!shared->stop.load(std::memory_order_acquire))
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, kStopPollSeconds, false);
        CFRunLoopTimerInvalidate(timer);
        CFRelease(timer);
        MIDIClientDispose(client);
    });
    m_thread->setObjectName(QStringLiteral("MidiDeviceWatcher"));
    m_thread->start();
#endif
}

MidiDeviceWatcher::~MidiDeviceWatcher() {
    stop();
}

void MidiDeviceWatcher::stop() {
    m_shared->stop.store(true, std::memory_order_release);
    m_shared->watcher = nullptr;
    if (m_thread)
        ThreadShutdown::join(m_thread, QDeadlineTimer(kStopTimeoutMs), lcHardware());
}
