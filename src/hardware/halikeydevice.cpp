#include "halikeydevice.h"
#include "halikeymidiworker.h"
#include "halikeyv14worker.h"
#include "halikeyworkerbase.h"
#include "iambickeyer.h" // for cwChainMs()
#include "../settings/radiosettings.h"
#include <QDebug>
#include <RtMidi.h>

HalikeyDevice::HalikeyDevice(QObject *parent) : QObject(parent) {
    // Debounce timers — single-shot, fire once after DEBOUNCE_MS of stable state
    m_ditDebounceTimer = new QTimer(this);
    m_ditDebounceTimer->setSingleShot(true);
    m_ditDebounceTimer->setInterval(DEBOUNCE_MS);
    connect(m_ditDebounceTimer, &QTimer::timeout, this, [this]() {
        if (m_rawDitState != m_confirmedDitState) {
            m_confirmedDitState = m_rawDitState;
            qDebug("[CW %10.3f] DEBOUNCE dit=%s", cwChainMs(), m_confirmedDitState ? "DOWN" : "UP");
            emit ditStateChanged(m_confirmedDitState);
        }
    });

    m_dahDebounceTimer = new QTimer(this);
    m_dahDebounceTimer->setSingleShot(true);
    m_dahDebounceTimer->setInterval(DEBOUNCE_MS);
    connect(m_dahDebounceTimer, &QTimer::timeout, this, [this]() {
        if (m_rawDahState != m_confirmedDahState) {
            m_confirmedDahState = m_rawDahState;
            qDebug("[CW %10.3f] DEBOUNCE dah=%s", cwChainMs(), m_confirmedDahState ? "DOWN" : "UP");
            emit dahStateChanged(m_confirmedDahState);
        }
    });

    m_pttDebounceTimer = new QTimer(this);
    m_pttDebounceTimer->setSingleShot(true);
    m_pttDebounceTimer->setInterval(DEBOUNCE_MS);
    connect(m_pttDebounceTimer, &QTimer::timeout, this, [this]() {
        if (m_rawPttState != m_confirmedPttState) {
            m_confirmedPttState = m_rawPttState;
            emit pttStateChanged(m_confirmedPttState);
        }
    });
}

HalikeyDevice::~HalikeyDevice() {
    closePort();
}

void HalikeyDevice::onRawDit(bool pressed) {
    m_rawDitState = pressed;
    if (pressed && !m_confirmedDitState) {
        // Key down — emit immediately for zero latency (runs on RtMidi thread)
        m_confirmedDitState = true;
        QMetaObject::invokeMethod(m_ditDebounceTimer, "stop", Qt::QueuedConnection);
        qDebug("[CW %10.3f] HALIKEY dit=DOWN (immediate)", cwChainMs());
        emit ditStateChanged(true);
    } else {
        // Key up or redundant key down — post debounce start to main thread (QTimer not thread-safe)
        QMetaObject::invokeMethod(m_ditDebounceTimer, "start", Qt::QueuedConnection);
    }
}

void HalikeyDevice::onRawDah(bool pressed) {
    m_rawDahState = pressed;
    if (pressed && !m_confirmedDahState) {
        m_confirmedDahState = true;
        QMetaObject::invokeMethod(m_dahDebounceTimer, "stop", Qt::QueuedConnection);
        qDebug("[CW %10.3f] HALIKEY dah=DOWN (immediate)", cwChainMs());
        emit dahStateChanged(true);
    } else {
        QMetaObject::invokeMethod(m_dahDebounceTimer, "start", Qt::QueuedConnection);
    }
}

void HalikeyDevice::onRawPtt(bool pressed) {
    m_rawPttState = pressed;
    if (pressed && !m_confirmedPttState) {
        m_confirmedPttState = true;
        QMetaObject::invokeMethod(m_pttDebounceTimer, "stop", Qt::QueuedConnection);
        emit pttStateChanged(true);
    } else {
        QMetaObject::invokeMethod(m_pttDebounceTimer, "start", Qt::QueuedConnection);
    }
}

bool HalikeyDevice::openPort(const QString &portName) {
    if (m_connected) {
        closePort();
    }

    m_portName = portName;
    m_rawDitState = false;
    m_rawDahState = false;
    m_rawPttState = false;
    m_confirmedDitState = false;
    m_confirmedDahState = false;
    m_confirmedPttState = false;

    // Create worker based on configured device type
    int deviceType = RadioSettings::instance()->halikeyDeviceType();
    if (deviceType == 1) {
        m_worker = new HaliKeyMidiWorker(portName);
    } else {
        m_worker = new HaliKeyV14Worker(portName);
    }

    m_workerThread = new QThread(this);
    m_worker->moveToThread(m_workerThread);

    // Wire worker signals through debounce handlers
    // DirectConnection: onRaw* runs on RtMidi callback thread for zero key-down latency.
    // Thread safety: key-down emits immediately; key-up posts timer start to main thread via QueuedConnection.
    connect(m_worker, &HaliKeyWorkerBase::ditStateChanged, this, &HalikeyDevice::onRawDit, Qt::DirectConnection);
    connect(m_worker, &HaliKeyWorkerBase::dahStateChanged, this, &HalikeyDevice::onRawDah, Qt::DirectConnection);
    connect(m_worker, &HaliKeyWorkerBase::pttStateChanged, this, &HalikeyDevice::onRawPtt, Qt::DirectConnection);
    connect(m_worker, &HaliKeyWorkerBase::portOpened, this, [this]() {
        m_connected = true;
        emit connected();
    });
    connect(m_worker, &HaliKeyWorkerBase::errorOccurred, this, [this](const QString &error) {
        qWarning() << "HalikeyDevice: Worker error -" << error;
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

    // Stop any pending debounce timers
    m_ditDebounceTimer->stop();
    m_dahDebounceTimer->stop();
    m_pttDebounceTimer->stop();

    bool wasConnected = m_connected;
    m_connected = false;
    m_rawDitState = false;
    m_rawDahState = false;
    m_rawPttState = false;
    m_confirmedDitState = false;
    m_confirmedDahState = false;
    m_confirmedPttState = false;

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
        qWarning() << "HalikeyDevice: MIDI enumeration failed:" << QString::fromStdString(error.getMessage());
    }
    return devices;
}

bool HalikeyDevice::ditPressed() const {
    return m_confirmedDitState;
}

bool HalikeyDevice::dahPressed() const {
    return m_confirmedDahState;
}
