#include "halikeydevice.h"
#include "halikey_edge.h"
#include "halikeymidiworker.h"
#include "halikeyv14worker.h"
#include "halikeyworkerbase.h"
#include "vailmidiworker.h"
#include <QDebug>
#include <RtMidi.h>

Q_LOGGING_CATEGORY(hwHalikey, "hw.halikey")

// WHY: HalikeyDevice no longer runs a time-window debounce. Each worker is authoritative for
// clean edges — the V1.4 serial worker confirms across ≥2 reads (≥500 µs apart on Linux/macOS,
// ~1 ms apart on Windows, where the poll cadence sets the spacing) before emitting; the MIDI
// worker delivers already-debounced firmware events. The previous 3 ms
// processing-time gate silently dropped real MIDI releases on Windows when WinMM delivered
// press+release in a single burst (see docs/halikey-midi-windows-debounce-bug.md), and also
// harbored a latent bug where a rejected edge left `confirmed` stale. acceptEdge() (extracted
// into halikey_edge.h for isolated testing) now does just same-direction dedupe.
using HalikeyEdge::acceptEdge;

HalikeyDevice::HalikeyDevice(int deviceType, QObject *parent) : QObject(parent), m_deviceType(deviceType) {}

HalikeyDevice::~HalikeyDevice() {
    closePort();
}

void HalikeyDevice::onRawLines(bool dit, bool dah, bool ptt) {
    // Per-line same-direction dedupe is kept exactly as it was — it is the regression gate for the
    // Windows MIDI stuck-paddle bug (see docs/halikey-midi-windows-debounce-bug.md). What changed
    // is only that the confirmed lines leave here as ONE sample: a dedupe that drops a redundant
    // edge on one line must not also drop the other lines' fresh values.
    const bool ditNew = acceptEdge(dit, m_confirmedDitState);
    const bool dahNew = acceptEdge(dah, m_confirmedDahState);
    const bool pttNew = acceptEdge(ptt, m_confirmedPttState);
    if (!ditNew && !dahNew && !pttNew)
        return;

    emit lineStateChanged(m_confirmedDitState.load(std::memory_order_acquire),
                          m_confirmedDahState.load(std::memory_order_acquire),
                          m_confirmedPttState.load(std::memory_order_acquire));
}

bool HalikeyDevice::openPort(const QString &portName) {
    if (m_worker) {
        closePort();
    }

    m_portName = portName;
    m_confirmedDitState.store(false, std::memory_order_relaxed);
    m_confirmedDahState.store(false, std::memory_order_relaxed);
    m_confirmedPttState.store(false, std::memory_order_relaxed);

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
        m_worker = new HaliKeyMidiWorker(portName);
    } else {
        m_worker = new HaliKeyV14Worker(portName);
    }

    m_workerThread = new QThread(this);
    m_worker->moveToThread(m_workerThread);

    // DirectConnection: onRawLines runs on whatever worker thread delivered the event (RtMidi
    // callback for MIDI, monitor thread for V1.4). It updates the confirmed atomics and re-emits
    // from that thread. Downstream connections are independently safe (CwController's handler only
    // touches atomics and queues to the keyer thread).
    connect(m_worker, &HaliKeyWorkerBase::lineStateChanged, this, &HalikeyDevice::onRawLines, Qt::DirectConnection);
    const quint64 generation = ++m_workerGeneration;
    connect(m_worker, &HaliKeyWorkerBase::portOpened, this, [this, generation]() {
        if (generation != m_workerGeneration || !m_worker)
            return;
        m_connected = true;
        emit connected();
        updateVailRouting();
    });
    connect(m_worker, &HaliKeyWorkerBase::errorOccurred, this, [this, generation](const QString &error) {
        if (generation != m_workerGeneration || !m_worker)
            return;
        qCWarning(hwHalikey) << "HalikeyDevice: Worker error -" << error;
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
    ++m_workerGeneration; // disconnect() does not cancel already queued opened/error callbacks.
    const quint64 epoch = m_vailRouting->invalidate();
    // WHY: send the matching key-up while the worker-to-radio route is still connected.
    if (auto *vail = qobject_cast<VailMidiWorker *>(m_worker)) {
        const auto token = m_vailWorkerEpoch;
        QMetaObject::invokeMethod(
            vail,
            [vail, token, epoch]() {
                vail->setKeyingEnabled(false); // Cleanup carries the generation which keyed DOWN.
                *token = epoch;
            },
            Qt::BlockingQueuedConnection);
    }
    // Disconnect worker signals FIRST — prevent stale queued events
    // from reaching slots after state reset
    if (m_worker) {
        disconnect(m_worker, nullptr, this, nullptr);
        m_worker->stop();
        m_worker->prepareShutdown();
    }

    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait(2000);
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
    }

    m_worker = nullptr; // Deleted by QThread::finished -> deleteLater
    m_vailWorkerEpoch.reset();
    m_vailWorkerEnabled = false;

    bool wasConnected = m_connected;
    m_connected = false;
    m_confirmedDitState.store(false, std::memory_order_relaxed);
    m_confirmedDahState.store(false, std::memory_order_relaxed);
    m_confirmedPttState.store(false, std::memory_order_relaxed);

    if (wasConnected) {
        emit disconnected();
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
        QMetaObject::invokeMethod(
            vail,
            [vail, token, epoch, enabled]() {
                if (enabled) {
                    *token = epoch;
                    vail->setKeyingEnabled(true);
                } else {
                    vail->setKeyingEnabled(false);
                    *token = epoch;
                }
            },
            enabled ? Qt::QueuedConnection : Qt::BlockingQueuedConnection);
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
        qCWarning(hwHalikey) << "HalikeyDevice: MIDI enumeration failed:" << QString::fromStdString(error.getMessage());
    }
    return devices;
}
