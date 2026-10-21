#include "halikeydevice.h"
#include "logging/logcategories.h"
#include "logging/logsink.h"
#include "halikeymidiworker.h"
#include "halikeyv14worker.h"
#include "halikeyworkerbase.h"
#include "utils/threadshutdown.h"
#include "vailmidiworker.h"
#include <QDebug>
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
    m_straightKey.store(straight, std::memory_order_release);
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

    if (m_deviceType == Vail) {
        auto *vail = new VailMidiWorker(portName, m_vailWpm, m_vailPitchHz, m_vailMode);
        m_worker = vail;
        m_vailWorkerEpoch = std::make_shared<quint64>(m_vailRouting->epoch.load(std::memory_order_acquire));
        const auto token = m_vailWorkerEpoch;
        connect(
            vail, &VailMidiWorker::timedKeyingCommand, this,
            [this, token](const QString &command) {
                emit timedKeyingCommand(command);
                emit routedVailKeyingCommand(command, *token);
            },
            Qt::DirectConnection);
        connect(
            vail, &VailMidiWorker::keyingStateChanged, this,
            [this, token](bool down, qint64 timestampNs) {
                emit routedVailKeyingStateChanged(down, *token, timestampNs);
            },
            Qt::DirectConnection);
        connect(
            vail, &VailMidiWorker::keyingAborted, this,
            [this, token]() {
                emit timedKeyingCommand(QStringLiteral("RX;"));
                emit routedVailKeyingCommand(QStringLiteral("RX;"), *token);
            },
            Qt::DirectConnection);
    } else if (m_deviceType == Midi) {
        m_worker = new HaliKeyMidiWorker(portName, m_midiSink);
    } else {
        m_worker = new HaliKeyV14Worker(portName);
    }

    m_workerThread = new QThread(this);
    m_workerThread->setObjectName(QStringLiteral("HaliKey"));
    Logging::markRealtimeOnStart(m_workerThread);
    m_worker->moveToThread(m_workerThread);

    // Straight through, on the worker thread: the keyer stamps the edge on arrival.
    connect(m_worker, &HaliKeyWorkerBase::paddleChanged, this, &HalikeyDevice::paddleChanged, Qt::DirectConnection);
    m_worker->setStraightKey(m_straightKey.load(std::memory_order_acquire));
    const quint64 generation = ++m_workerGeneration;
    connect(m_worker, &HaliKeyWorkerBase::portOpened, this, [this, generation]() {
        if (generation != m_workerGeneration || !m_worker)
            return;
        const char *transport = m_deviceType == Vail   ? "(Vail MIDI)"
                                : m_deviceType == Midi ? "(MIDI)"
                                                       : "(V1.4 serial)";
        qCInfo(lcCwHalikey) << "CW keyer opened on" << m_portName << transport;
        m_connected = true;
        emit connected();
        updateVailRouting();
    });
    connect(m_worker, &HaliKeyWorkerBase::errorOccurred, this, [this, generation](const QString &error) {
        if (generation != m_workerGeneration || !m_worker)
            return;
        qCWarning(lcCwHalikey) << "HalikeyDevice: Worker error -" << error;
        closePort();
        emit connectionError(error);
    });

    // Start worker when thread starts
    connect(m_workerThread, &QThread::started, m_worker, &HaliKeyWorkerBase::start);

    // Clean up worker when thread finishes
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);

    m_workerThread->start();
    updateVailRouting();
    return true;
}

void HalikeyDevice::setDeviceType(int deviceType) {
    if (deviceType == m_deviceType)
        return;
    closePort();
    m_deviceType = deviceType;
}

void HalikeyDevice::closePort() {
    closeWorker(true);
}

void HalikeyDevice::closeWorker(bool cleanupVail) {
    const QDeadlineTimer deadline(kWorkerStopTimeout);
    ++m_workerGeneration; // disconnect() does not cancel already queued opened/error callbacks.
    const quint64 epoch = m_vailRouting->invalidate();
    auto *vail = qobject_cast<VailMidiWorker *>(m_worker);
    bool cleanupFailed = vail && !cleanupVail;
    // WHY: send the matching key-up while the worker-to-radio route is still connected.
    if (vail && cleanupVail) {
        const auto token = m_vailWorkerEpoch;
        cleanupFailed = !ThreadShutdown::callBlocking(
            vail,
            [vail, token, epoch]() {
                vail->setKeyingEnabled(false); // Cleanup carries the generation which keyed DOWN.
                *token = epoch;
            },
            deadline);
    }
    // Disconnect worker signals FIRST — prevent stale queued events
    // from reaching slots after state reset
    if (m_worker) {
        disconnect(m_worker, nullptr, this, nullptr);
        m_worker->stop();
        if (vail) {
            ThreadShutdown::callBlocking(vail, [vail]() { vail->prepareShutdown(); }, deadline);
        } else {
            m_worker->prepareShutdown();
        }
    }

    // The worker goes with its thread (finished -> deleteLater), or stays on it if the thread is
    // disowned; its paddle line into this object was cut above either way.
    ThreadShutdown::retire(m_workerThread, deadline, lcCwHalikey());
    m_worker = nullptr;
    m_vailWorkerEpoch.reset();
    m_vailWorkerEnabled = false;

    bool wasConnected = m_connected;
    m_connected = false;

    if (wasConnected) {
        emit disconnected();
    }
    if (cleanupFailed) {
        qCWarning(lcCwHalikey) << "Vail keying cleanup timed out; disconnecting the radio for safety";
        emit vailCleanupFailed();
    }
}

void HalikeyDevice::configureVailKeyer(int wpm, int pitchHz, int mode) {
    m_vailWpm = wpm;
    m_vailPitchHz = pitchHz;
    m_vailMode = mode;
    if (auto *vail = qobject_cast<VailMidiWorker *>(m_worker)) {
        QMetaObject::invokeMethod(vail, "configure", Qt::QueuedConnection, Q_ARG(int, wpm), Q_ARG(int, pitchHz),
                                  Q_ARG(int, mode));
    }
}

void HalikeyDevice::setVailKeyingEnabled(bool enabled) {
    m_vailKeyingEnabled = enabled;
    updateVailRouting();
}

void HalikeyDevice::updateVailRouting() {
    const bool enabled = m_vailKeyingEnabled && m_connected && m_deviceType == Vail;
    if (enabled == m_vailRouting->enabled.load(std::memory_order_acquire) && enabled == m_vailWorkerEnabled)
        return;
    const quint64 epoch = m_vailRouting->invalidate();
    m_vailRouting->enabled.store(enabled, std::memory_order_release);
    m_vailWorkerEnabled = enabled;
    if (auto *vail = qobject_cast<VailMidiWorker *>(m_worker)) {
        const auto token = m_vailWorkerEpoch;
        // A takeover can start the KPOD+ immediately after this returns. Queue Vail
        // cleanup to I/O first, so it cannot arrive after the replacement keyer.
        const auto update = [vail, token, epoch, enabled]() {
            if (enabled) {
                *token = epoch;
                vail->setKeyingEnabled(true);
            } else {
                vail->setKeyingEnabled(false);
                *token = epoch;
            }
        };
        if (enabled) {
            QMetaObject::invokeMethod(vail, update, Qt::QueuedConnection);
        } else if (!ThreadShutdown::callBlocking(vail, update, QDeadlineTimer(kWorkerStopTimeout))) {
            closeWorker(false);
            emit connectionError(QStringLiteral("Vail did not finish keying cleanup in time. The radio was "
                                                "disconnected; reconnect before keying again."));
        }
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
