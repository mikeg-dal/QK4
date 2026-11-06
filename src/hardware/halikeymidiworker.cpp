#include "halikeymidiworker.h"
#include "logging/logcategories.h"
#include "logging/logsink.h"
#include "halikey_midi_ports.h"
#include <RtMidi.h>
#include <QLoggingCategory>
#include <QTimer>

namespace {
// How often to check the port is still enumerated, and how many consecutive misses count as gone.
// 500 ms x 2 bounds detection at ~1 s, which is the longest a lever held at unplug may keep the
// radio keyed. A single miss is not enough: the enumeration can blink during a CoreMIDI or driver
// restart while the device is still attached.
constexpr int kPresencePollMs = 500;
constexpr int kMissesBeforeGone = 2;
} // namespace

HaliKeyMidiWorker::HaliKeyMidiWorker(const QString &deviceName, std::shared_ptr<HaliKeyMidiSink> sink, QObject *parent)
    : HaliKeyWorkerBase(deviceName, parent), m_sink(std::move(sink)) {}

HaliKeyMidiWorker::~HaliKeyMidiWorker() {
    QMutexLocker lock(&m_midiMutex);
    m_midiIn.reset();
}

void HaliKeyMidiWorker::prepareShutdown() {
    // Close the MIDI port and stop RtMidi's internal callback thread BEFORE
    // the QThread is torn down. RtMidi::closePort() blocks until any in-progress
    // callback finishes, so after this returns no more callbacks can fire.
    //
    // Called from the MAIN thread. RtMidi synchronises its own callback thread, but nothing in it
    // stops this destroying the RtMidiIn while the presence poll is calling getPortCount() on the
    // worker thread — hence the mutex. The poll re-checks m_midiIn under the same lock, so it sees
    // the null rather than a dangling object.
    QMutexLocker lock(&m_midiMutex);
    m_midiIn.reset();
    qCDebug(lcCwMidi) << "HaliKeyMidiWorker: MIDI port closed during shutdown";
}

void HaliKeyMidiWorker::checkPortPresence() {
    if (stopRequested()) {
        return;
    }

    if (!m_momidiReported && m_momidiDetected.load(std::memory_order_acquire)) {
        m_momidiReported = true;
        qCDebug(lcCwMidi) << "HaliKeyMidiWorker: MoMIDI detected, version"
                          << m_momidiVersion.load(std::memory_order_relaxed);
    }

    QStringList names;
    {
        QMutexLocker lock(&m_midiMutex);
        if (!m_midiIn) {
            return; // shutting down
        }
        try {
            const unsigned int count = m_midiIn->getPortCount();
            names.reserve(static_cast<int>(count));
            for (unsigned int i = 0; i < count; ++i) {
                names << QString::fromStdString(m_midiIn->getPortName(i));
            }
        } catch (RtMidiError &error) {
            // Enumeration itself failed — treat as a miss rather than a removal, and let the
            // repeat count decide. Throwing out of a Qt slot would terminate.
            qCWarning(lcCwMidi) << "HaliKeyMidiWorker: port enumeration failed:"
                                << QString::fromStdString(error.getMessage());
            names.clear();
        }
    }

    if (HalikeyMidiPorts::findPort(names, m_portName) >= 0) {
        m_missedPolls = 0;
        return;
    }

    if (++m_missedPolls < kMissesBeforeGone) {
        qCDebug(lcCwMidi) << "HaliKeyMidiWorker: port" << m_portName << "missing from enumeration, miss"
                          << m_missedPolls;
        return;
    }

    // WHY this matters rather than being cosmetic: MIDI has no equivalent of the serial worker's
    // failing read, and none of RtMidi's backends report removal — CoreMIDI's client here has a
    // null notify proc, WinMM's input callback drops MIM_CLOSE, and ALSA logs the unsubscribe and
    // carries on. So an unplug with a lever held left the keyer believing it was still down and
    // sending forever. (It also left a held footswitch asserting PTT, back when note 31 still
    // reached PTT — that path is gone, but the held-lever case remains.) The enumeration
    // does drop the device (verified on macOS), which is what makes this poll work.
    //
    // errorOccurred is the existing route: HalikeyDevice closes the port, which emits disconnected,
    // and CwController releases the keyer's paddles on that.
    m_presenceTimer->stop();
    const QString message = QString("MIDI device '%1' removed").arg(m_portName);
    qCWarning(lcCwMidi) << "HaliKeyMidiWorker:" << message;
    emit errorOccurred(message);
}

void HaliKeyMidiWorker::start() {
    if (stopRequested())
        return;

    // Built in a local and published under m_midiMutex: prepareShutdown() resets m_midiIn from the
    // main thread and may run while this is still opening the port.
    std::unique_ptr<RtMidiIn> midiIn;
    try {
        midiIn = std::make_unique<RtMidiIn>();
    } catch (RtMidiError &error) {
        QString msg = QString("Failed to create MIDI input: %1").arg(QString::fromStdString(error.getMessage()));
        qCWarning(lcCwMidi) << "HaliKeyMidiWorker:" << msg;
        emit errorOccurred(msg);
        return;
    }

    // Find the MIDI port matching our device name. The same matcher the presence poll uses, so
    // "the port we opened" and "the port that disappeared" can never be decided differently.
    unsigned int portCount = midiIn->getPortCount();
    qCDebug(lcCwMidi) << "HaliKeyMidiWorker: searching for device" << m_portName << "among" << portCount
                      << "MIDI ports";
    QStringList portNames;
    portNames.reserve(static_cast<int>(portCount));
    for (unsigned int i = 0; i < portCount; i++) {
        portNames << QString::fromStdString(midiIn->getPortName(i));
        qCDebug(lcCwMidi) << "HaliKeyMidiWorker: MIDI port" << i << ":" << portNames.last();
    }
    const int foundPort = HalikeyMidiPorts::findPort(portNames, m_portName);

    if (foundPort < 0) {
        QString msg = QString("MIDI device '%1' not found (%2 ports available)").arg(m_portName).arg(portCount);
        qCWarning(lcCwMidi) << "HaliKeyMidiWorker:" << msg;
        emit errorOccurred(msg);
        return;
    }

    try {
        midiIn->openPort(static_cast<unsigned int>(foundPort));
    } catch (RtMidiError &error) {
        QString msg = QString("Failed to open MIDI port: %1").arg(QString::fromStdString(error.getMessage()));
        qCWarning(lcCwMidi) << "HaliKeyMidiWorker:" << msg;
        emit errorOccurred(msg);
        return;
    }

    // Ignore sysex, timing, and active sensing messages
    midiIn->ignoreTypes(true, true, true);

    // Set callback — RtMidi calls this from its internal thread
    midiIn->setCallback(&HaliKeyMidiWorker::midiCallback, this);

    {
        QMutexLocker lock(&m_midiMutex);
        if (stopRequested())
            return; // shutdown began while opening; the local closes the port
        m_midiIn = std::move(midiIn);
    }

    // Presence poll. Created here because start() runs ON the worker thread, so the timer belongs
    // to that thread's event loop and fires there — the same thread that owns m_midiIn — and dies
    // with the worker. The main thread never touches it.
    m_missedPolls = 0;
    m_presenceTimer = new QTimer(this);
    m_presenceTimer->setInterval(kPresencePollMs);
    connect(m_presenceTimer, &QTimer::timeout, this, &HaliKeyMidiWorker::checkPortPresence);
    m_presenceTimer->start();
    qCDebug(lcCwMidi) << "HaliKeyMidiWorker: opened MIDI port" << foundPort << "for device" << m_portName;
    emit portOpened();
}

void HaliKeyMidiWorker::midiCallback(double deltaTime, std::vector<unsigned char> *message, void *userData) noexcept {
    {
        // WHY allowed: it sets a thread_local flag. The platform may allocate a thread's TLS block
        // on its first access, once per thread, which RTSan would report.
        RtSan::Allow allow;
        Logging::markRealtimeThread();
    }
    auto *self = static_cast<HaliKeyMidiWorker *>(userData);
    if (self->stopRequested())
        return;
    if (!message || message->size() < 3)
        return;
    int status = (*message)[0];
    const int note = (*message)[1];
    const int velocity = (*message)[2];
    const int kind = status & 0xF0;
    if (status == 0xB0 && !self->m_momidiDetected.load(std::memory_order_relaxed)) {
        self->m_momidiVersion.store(velocity, std::memory_order_relaxed);
        self->m_momidiDetected.store(true, std::memory_order_release);
    }
    if (kind == 0x90 || kind == 0x80) {
        // WHY normalise the channel: QK4 has always taken notes on any channel; the keyer
        // matches 0x90/0x80 exactly.
        status = kind;
        // WHY translate only for non-MoMIDI devices: MoMIDI's note-on is always key-down, its
        // velocity carrying time. Ordinary MIDI sends note-on velocity 0 for a release, which
        // left as key-down would hold the key down.
        if (kind == 0x90 && velocity == 0 && !self->m_momidiDetected.load(std::memory_order_relaxed))
            status = 0x80;
    }
    // Raw to the keyer, on this thread, with RtMidi's own delta: the paddle edge is stamped on
    // arrival (PaddleKeyer::midiMessage, ported from K4-Companion). No log line per note: this is a
    // real-time thread (CONVENTIONS Rule 16). The HaliKey MIDI user guide's notes are 20 (left
    // paddle), 21 (right paddle) and 31 (footswitch); the keyer maps 20/21 and ignores the rest,
    // which keeps footswitch PTT withdrawn (see cwcontroller.h).
    if (self->m_sink)
        self->m_sink->call(status, note, velocity, deltaTime);
}
