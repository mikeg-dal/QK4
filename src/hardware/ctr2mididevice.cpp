#include "ctr2mididevice.h"

#include <QMetaObject>
#include <RtMidi.h>

Ctr2MidiDevice::Ctr2MidiDevice(QObject *parent) : QObject(parent) {}
Ctr2MidiDevice::~Ctr2MidiDevice() {
    closePort();
}

QStringList Ctr2MidiDevice::availableMidiDevices() {
    QStringList result;
    try {
        RtMidiIn input;
        for (unsigned int index = 0; index < input.getPortCount(); ++index)
            result.append(QString::fromStdString(input.getPortName(index)));
    } catch (const RtMidiError &) {
    }
    return result;
}

bool Ctr2MidiDevice::openPort(const QString &portName) {
    closePort();
    try {
        m_input = std::make_unique<RtMidiIn>();
        int match = -1;
        for (unsigned int index = 0; index < m_input->getPortCount(); ++index) {
            if (QString::fromStdString(m_input->getPortName(index)) == portName) {
                match = int(index);
                break;
            }
        }
        if (match < 0)
            throw std::runtime_error("The selected MIDI input is not attached");
        m_input->ignoreTypes(false, false, false);
        m_input->setCallback(&Ctr2MidiDevice::midiCallback, this);
        m_input->openPort(unsigned(match), "QK4 CTR2 input");
        m_portName = portName;
        m_connected = true;
        m_status = QStringLiteral("Connected to %1").arg(portName);
        emit connected();
        return true;
    } catch (const std::exception &error) {
        m_input.reset();
        m_connected = false;
        m_status = QString::fromUtf8(error.what());
        emit connectionError(m_status);
        return false;
    }
}

void Ctr2MidiDevice::closePort() {
    const bool wasConnected = m_connected;
    if (m_input) {
        try {
            m_input->cancelCallback();
            m_input->closePort();
        } catch (...) {
        }
        m_input.reset();
    }
    m_connected = false;
    m_status = QStringLiteral("Not connected");
    if (wasConnected)
        emit disconnected();
}

void Ctr2MidiDevice::midiCallback(double, std::vector<unsigned char> *message, void *userData) {
    auto *device = static_cast<Ctr2MidiDevice *>(userData);
    if (!device || !message || message->size() < 2)
        return;
    const int status = (*message)[0];
    const int data1 = (*message)[1] & 0x7f;
    const int data2 = message->size() > 2 ? (*message)[2] & 0x7f : 0;
    QMetaObject::invokeMethod(
        device,
        [device, status, data1, data2] {
            if (device->m_connected)
                emit device->rawMidiEvent(status, data1, data2);
        },
        Qt::QueuedConnection);
}
