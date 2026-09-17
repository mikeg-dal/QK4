#include "halikeymidiworker.h"
#include <RtMidi.h>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(hwMidi, "hw.midi")

// MIDI note assignments from HaliKey MIDI user guide
static constexpr unsigned char NOTE_LEFT_PADDLE = 20;
static constexpr unsigned char NOTE_RIGHT_PADDLE = 21;
static constexpr unsigned char NOTE_PTT = 31;

HaliKeyMidiWorker::HaliKeyMidiWorker(const QString &deviceName, QObject *parent)
    : HaliKeyWorkerBase(deviceName, parent) {}

HaliKeyMidiWorker::~HaliKeyMidiWorker() {
    m_midiIn.reset();
}

void HaliKeyMidiWorker::prepareShutdown() {
    // Close the MIDI port and stop RtMidi's internal callback thread BEFORE
    // the QThread is torn down. RtMidi::closePort() blocks until any in-progress
    // callback finishes, so after this returns no more callbacks can fire.
    // This is safe to call from the main thread — RtMidi synchronizes internally.
    m_midiIn.reset();
    qCDebug(hwMidi) << "HaliKeyMidiWorker: MIDI port closed during shutdown";
}

void HaliKeyMidiWorker::start() {
    try {
        m_midiIn = std::make_unique<RtMidiIn>();
    } catch (RtMidiError &error) {
        QString msg = QString("Failed to create MIDI input: %1").arg(QString::fromStdString(error.getMessage()));
        qWarning() << "HaliKeyMidiWorker:" << msg;
        emit errorOccurred(msg);
        return;
    }

    // Find the MIDI port matching our device name
    unsigned int portCount = m_midiIn->getPortCount();
    qCDebug(hwMidi) << "HaliKeyMidiWorker: searching for device" << m_portName << "among" << portCount << "MIDI ports";
    int foundPort = -1;

    for (unsigned int i = 0; i < portCount; i++) {
        std::string name = m_midiIn->getPortName(i);
        QString portName = QString::fromStdString(name);
        qCDebug(hwMidi) << "HaliKeyMidiWorker: MIDI port" << i << ":" << portName;
        if (portName.contains(m_portName, Qt::CaseInsensitive)) {
            foundPort = static_cast<int>(i);
            break;
        }
    }

    if (foundPort < 0) {
        QString msg = QString("MIDI device '%1' not found (%2 ports available)").arg(m_portName).arg(portCount);
        qWarning() << "HaliKeyMidiWorker:" << msg;
        emit errorOccurred(msg);
        m_midiIn.reset();
        return;
    }

    try {
        m_midiIn->openPort(static_cast<unsigned int>(foundPort));
    } catch (RtMidiError &error) {
        QString msg = QString("Failed to open MIDI port: %1").arg(QString::fromStdString(error.getMessage()));
        qWarning() << "HaliKeyMidiWorker:" << msg;
        emit errorOccurred(msg);
        m_midiIn.reset();
        return;
    }

    // Ignore sysex, timing, and active sensing messages
    m_midiIn->ignoreTypes(true, true, true);

    // Set callback — RtMidi calls this from its internal thread
    m_midiIn->setCallback(&HaliKeyMidiWorker::midiCallback, this);

    m_running = true;
    qCDebug(hwMidi) << "HaliKeyMidiWorker: opened MIDI port" << foundPort << "for device" << m_portName;
    emit portOpened();
}

void HaliKeyMidiWorker::midiCallback(double deltaTime, std::vector<unsigned char> *message, void *userData) {
    auto *self = static_cast<HaliKeyMidiWorker *>(userData);
    if (!self->m_running)
        return;
    if (!message || message->empty())
        return;
    // WHY marshal to the worker thread before doing anything Qt-related:
    // RtMidi invokes this callback from its OWN internal thread (not a QThread).
    // Emitting Qt signals from a non-Qt thread is undefined — Qt's event-queue
    // machinery isn't guaranteed to work on threads it doesn't know about. Instead
    // we copy the message bytes by value into a lambda and post it back to this
    // worker (which IS a managed QThread). handleMidiMessage then runs on the
    // worker thread and emits signals safely.
    //
    // Cost: one Qt-event-queue hop (~10-50 µs typical). Negligible vs. CW timing.
    std::vector<unsigned char> copy = *message;
    QMetaObject::invokeMethod(
        self, [self, dt = deltaTime, msg = std::move(copy)]() { self->handleMidiMessage(dt, msg); },
        Qt::QueuedConnection);
}

void HaliKeyMidiWorker::handleMidiMessage(double deltaTime, const std::vector<unsigned char> &message) {
    if (message.size() < 3)
        return;

    unsigned char status = message[0] & 0xF0;
    unsigned char channel = message[0] & 0x0F;
    unsigned char data1 = message[1];
    unsigned char data2 = message[2];

    // --- CC events: MoMIDI version detection and timing MSB ---
    if (status == 0xB0) {
        if (channel == 0 && !m_momidiDetected) {
            m_momidiDetected = true;
            qCDebug(hwMidi) << "HaliKeyMidiWorker: MoMIDI detected, version" << data2;
        } else if (channel != 0) {
            m_pendingTimeMsb = data2;
        }
        return;
    }

    // --- Note events ---
    bool pressed = false;
    if (status == 0x90) {
        if (m_momidiDetected) {
            // MoMIDI: Note On is ALWAYS key down; velocity carries timing LSB
            pressed = true;
            m_pendingTimeMsb = 0;
        } else {
            // Traditional MIDI: velocity 0 on Note On = Note Off
            pressed = (data2 > 0);
        }
    } else if (status == 0x80) {
        pressed = false;
        m_pendingTimeMsb = 0;
    } else {
        return;
    }

    // WHY the worker holds all three lines: MIDI gives one note per message, so unlike the serial
    // variant there is no joint hardware sample to forward. Carrying the last-known value of the
    // lines that did not change still hands the keyer a coherent pair on every event, and keeps one
    // interface for both transports. It cannot make two genuinely separate messages simultaneous —
    // that limitation is the transport's, not ours.
    switch (data1) {
    case NOTE_LEFT_PADDLE:
        qCDebug(hwMidi) << "HaliKeyMidiWorker: dit (note 20)" << (pressed ? "down" : "up");
        m_ditState = pressed;
        emit lineStateChanged(m_ditState, m_dahState, m_pttState);
        break;
    case NOTE_RIGHT_PADDLE:
        qCDebug(hwMidi) << "HaliKeyMidiWorker: dah (note 21)" << (pressed ? "down" : "up");
        m_dahState = pressed;
        emit lineStateChanged(m_ditState, m_dahState, m_pttState);
        break;
    case NOTE_PTT:
        qCDebug(hwMidi) << "HaliKeyMidiWorker: ptt (note 31)" << (pressed ? "down" : "up");
        m_pttState = pressed;
        emit lineStateChanged(m_ditState, m_dahState, m_pttState);
        break;
    default:
        // Log unrecognized notes so a HaliKey MIDI firmware using different note numbers
        // than 20/21/31 is immediately visible in the trace rather than silently dropped.
        qCDebug(hwMidi) << "HaliKeyMidiWorker: unhandled note" << data1 << (pressed ? "down" : "up")
                        << "status=" << Qt::hex << status;
        break;
    }
}
