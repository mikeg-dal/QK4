#include "halikeymidiworker.h"
#include "iambickeyer.h" // for cwChainMs()
#include <RtMidi.h>
#include <QDebug>

// MIDI note assignments from HaliKey MIDI user guide
static constexpr unsigned char NOTE_LEFT_PADDLE = 20;
static constexpr unsigned char NOTE_RIGHT_PADDLE = 21;
static constexpr unsigned char NOTE_STRAIGHT_KEY = 30;
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
    qDebug() << "HaliKeyMidiWorker: MIDI port closed during shutdown";
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
    qDebug() << "HaliKeyMidiWorker: searching for device" << m_portName << "among" << portCount << "MIDI ports";
    int foundPort = -1;

    for (unsigned int i = 0; i < portCount; i++) {
        std::string name = m_midiIn->getPortName(i);
        QString portName = QString::fromStdString(name);
        qDebug() << "HaliKeyMidiWorker: MIDI port" << i << ":" << portName;
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
    qDebug() << "HaliKeyMidiWorker: opened MIDI port" << foundPort << "for device" << m_portName;
    emit portOpened();
}

void HaliKeyMidiWorker::midiCallback(double deltaTime, std::vector<unsigned char> *message, void *userData) {
    auto *self = static_cast<HaliKeyMidiWorker *>(userData);
    if (!self->m_running)
        return;
    if (message && !message->empty()) {
        self->handleMidiMessage(deltaTime, *message);
    }
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
            m_momidiVersion = data2;
            qDebug() << "HaliKeyMidiWorker: MoMIDI detected, version" << m_momidiVersion;
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
            if (data2 > 0 && data2 < 127) {
                int timeDeltaMs = (data2 - 1) + m_pendingTimeMsb * 126;
                qDebug("[CW %10.3f] MOMIDI timeDelta=%dms (msb=%d lsb=%d)", cwChainMs(), timeDeltaMs, m_pendingTimeMsb,
                       data2);
            }
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

    switch (data1) {
    case NOTE_LEFT_PADDLE:
        qDebug("[CW %10.3f] MIDI dit=%s (note=%d vel=%d dt=%.1fms)", cwChainMs(), pressed ? "DOWN" : "UP", data1, data2,
               deltaTime * 1000.0);
        emit ditStateChanged(pressed);
        break;
    case NOTE_RIGHT_PADDLE:
        qDebug("[CW %10.3f] MIDI dah=%s (note=%d vel=%d dt=%.1fms)", cwChainMs(), pressed ? "DOWN" : "UP", data1, data2,
               deltaTime * 1000.0);
        emit dahStateChanged(pressed);
        break;
    case NOTE_PTT:
        emit pttStateChanged(pressed);
        break;
    default:
        break;
    }
}
