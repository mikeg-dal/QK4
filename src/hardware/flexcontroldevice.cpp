#include "flexcontroldevice.h"
#include "flexcontrolserialworker.h"
#include <QThread>

FlexControlDevice::FlexControlDevice(QObject *parent) : QObject(parent) {
    m_thread = new QThread(this);
    m_thread->setObjectName("FlexControl");
    m_worker = new FlexControlSerialWorker();
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::started, m_worker, &FlexControlSerialWorker::start);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    connect(m_worker, &FlexControlSerialWorker::deviceInfoReady, this, [this](FlexControlDeviceInfo info) {
        m_info = info;
        emit deviceInfoReady();
    });
    connect(m_worker, &FlexControlSerialWorker::deviceArrived, this, [this]() {
        m_polling = true;
        emit deviceConnected();
    });
    connect(m_worker, &FlexControlSerialWorker::deviceRemoved, this, [this]() {
        m_polling = false;
        emit deviceDisconnected();
    });
    connect(m_worker, &FlexControlSerialWorker::encoderRotated, this, &FlexControlDevice::encoderRotated);
    connect(m_worker, &FlexControlSerialWorker::buttonPressed, this, &FlexControlDevice::buttonPressed);
    connect(m_worker, &FlexControlSerialWorker::pollError, this, &FlexControlDevice::pollError);

    m_thread->start();
}

FlexControlDevice::~FlexControlDevice() {
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, "shutdown", Qt::BlockingQueuedConnection);
    }
    if (m_thread) {
        m_thread->quit();
        m_thread->wait(2000);
    }
}

bool FlexControlDevice::isDetected() const {
    return m_info.detected;
}

FlexControlDeviceInfo FlexControlDevice::deviceInfo() const {
    return m_info;
}

bool FlexControlDevice::isPolling() const {
    return m_polling;
}

bool FlexControlDevice::startPolling() {
    if (m_polling)
        return true;
    QMetaObject::invokeMethod(m_worker, "openDevice", Qt::QueuedConnection);
    return m_info.detected;
}

void FlexControlDevice::stopPolling() {
    QMetaObject::invokeMethod(m_worker, "closeDevice", Qt::QueuedConnection);
}
