#include "halikeydevice.h"
#include "logging/logcategories.h"
#include "logging/logsink.h"
#include "halikeymidiworker.h"
#include "halikeyv14worker.h"
#include "halikeyworkerbase.h"
#include "utils/threadshutdown.h"
#include <QDebug>
#include <QSerialPortInfo>
#include <RtMidi.h>
#include <chrono>

namespace {
// WHY 1 s: the serial worker sees its stop request within one 1 ms poll, and prepareShutdown() has
// already closed a MIDI port before the join. The rest is closing the port handle.
constexpr std::chrono::milliseconds kWorkerStopTimeout{1000};
} // namespace

HalikeyDevice::HalikeyDevice(int deviceType, QObject *parent)
    : QObject(parent), m_midiSink(std::make_shared<HaliKeyMidiSink>()), m_deviceType(deviceType) {}

void HalikeyDevice::setMidiSink(std::function<void(int, int, int, double)> sink) {
    m_midiSink->set(std::move(sink));
}

void HalikeyDevice::setStraightKey(bool straight) {
    if (m_straightKey.exchange(straight, std::memory_order_acq_rel) != straight)
        disarmKeying();
    if (m_worker)
        m_worker->setStraightKey(straight);
}

HalikeyDevice::~HalikeyDevice() {
    closePort();
}

bool HalikeyDevice::openPort(const QString &portName) {
    // Unconditionally, not only when connected: a worker still opening has not reported
    // portOpened yet, and replacing it without a teardown would orphan its thread.
    closePort();

    m_portName = portName;

    // Create worker based on injected device type (0 = V1.4, 1 = MIDI)
    if (m_deviceType == 1) {
        m_worker = new HaliKeyMidiWorker(portName, m_midiSink);
    } else {
        m_worker = new HaliKeyV14Worker(portName);
    }

    m_worker->setSafety(m_safety);
    m_workerThread = new QThread(this);
    m_workerThread->setObjectName(QStringLiteral("HaliKey"));
    Logging::markRealtimeOnStart(m_workerThread);
    m_worker->moveToThread(m_workerThread);

    // Straight through, on the worker thread: the keyer stamps the edge on arrival.
    connect(m_worker, &HaliKeyWorkerBase::paddleChanged, this, &HalikeyDevice::paddleChanged, Qt::DirectConnection);
    m_worker->setStraightKey(m_straightKey.load(std::memory_order_acquire));
    connect(m_worker, &HaliKeyWorkerBase::portOpened, this, [this]() {
        qCInfo(lcCwHalikey) << "HaliKey opened on" << m_portName << (m_deviceType == 1 ? "(MIDI)" : "(V1.4 serial)");
        m_connected = true;
        emit connected();
    });
    connect(m_worker, &HaliKeyWorkerBase::errorOccurred, this, [this](const QString &error) {
        qCWarning(lcCwHalikey) << "HalikeyDevice: Worker error -" << error;
        closePort();
        emit connectionError(error);
    });

    // Start worker when thread starts
    connect(m_workerThread, &QThread::started, m_worker, &HaliKeyWorkerBase::start);

    // Clean up worker when thread finishes
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);

    m_workerThread->start();
    return true;
}

void HalikeyDevice::closePort() {
    disarmKeying();
    // Disconnect worker signals FIRST — prevent stale queued events
    // from reaching slots after state reset
    if (m_worker) {
        disconnect(m_worker, nullptr, this, nullptr);
        m_worker->stop();
        m_worker->prepareShutdown();
    }

    // The worker goes with its thread (finished -> deleteLater), or stays on it if the thread is
    // disowned; its paddle line into this object was cut above either way.
    ThreadShutdown::retire(m_workerThread, QDeadlineTimer(kWorkerStopTimeout), lcCwHalikey());
    m_worker = nullptr;

    bool wasConnected = m_connected;
    m_connected = false;

    if (wasConnected) {
        emit disconnected();
    }
}

bool HalikeyDevice::isConnected() const {
    return m_connected;
}

QString HalikeyDevice::portName() const {
    return m_portName;
}

QStringList HalikeyDevice::availablePorts() {
    QStringList ports;
    const auto portInfos = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : portInfos) {
        ports.append(info.portName());
    }
    return ports;
}

QList<HaliKeyPortInfo> HalikeyDevice::availablePortsDetailed() {
    QList<HaliKeyPortInfo> ports;
    const auto portInfos = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : portInfos) {
        HaliKeyPortInfo pi;
        pi.portName = info.portName();
        ports.append(pi);
    }
    return ports;
}

QStringList HalikeyDevice::availableMidiDevices() {
    // System virtual MIDI devices to exclude from the list
    static const QStringList excludedPrefixes = {"IAC Driver"};

    QStringList devices;
    try {
        RtMidiIn midi;
        unsigned int portCount = midi.getPortCount();
        for (unsigned int i = 0; i < portCount; i++) {
            QString name = QString::fromStdString(midi.getPortName(i));
            bool excluded = false;
            for (const QString &prefix : excludedPrefixes) {
                if (name.startsWith(prefix, Qt::CaseInsensitive)) {
                    excluded = true;
                    break;
                }
            }
            if (!excluded) {
                devices.append(name);
            }
        }
    } catch (RtMidiError &error) {
        qCWarning(lcCwHalikey) << "HalikeyDevice: MIDI enumeration failed:"
                               << QString::fromStdString(error.getMessage());
    }
    return devices;
}
