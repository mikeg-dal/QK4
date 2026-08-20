#include "rc28device.h"
#include "rc28hidworker.h"
#include <QThread>

Rc28Device::Rc28Device(QObject *parent) : QObject(parent) {
    m_hidThread = new QThread(this);
    m_hidThread->setObjectName("Rc28Hid");
    m_hidWorker = new Rc28HidWorker();
    m_hidWorker->moveToThread(m_hidThread);
    connect(m_hidThread, &QThread::started, m_hidWorker, &Rc28HidWorker::start);
    connect(m_hidThread, &QThread::finished, m_hidWorker, &QObject::deleteLater);

    // Cache + re-emit worker signals.
    connect(m_hidWorker, &Rc28HidWorker::deviceInfoReady, this, [this](Rc28DeviceInfo info) {
        m_info = info;
        emit deviceInfoReady();
    });
    connect(m_hidWorker, &Rc28HidWorker::deviceArrived, this, [this]() {
        m_polling = true;
        emit deviceConnected();
    });
    connect(m_hidWorker, &Rc28HidWorker::deviceRemoved, this, [this]() {
        m_polling = false;
        emit deviceDisconnected();
    });
    connect(m_hidWorker, &Rc28HidWorker::encoderRotated, this, &Rc28Device::encoderRotated);
    connect(m_hidWorker, &Rc28HidWorker::buttonTapped, this, &Rc28Device::buttonTapped);
    connect(m_hidWorker, &Rc28HidWorker::buttonHeld, this, &Rc28Device::buttonHeld);
    connect(m_hidWorker, &Rc28HidWorker::pollError, this, &Rc28Device::pollError);

    m_hidThread->start();
}

Rc28Device::~Rc28Device() {
    if (m_hidWorker) {
        // Synchronously drain the worker (stop timers, close handle, hid_exit) before
        // tearing the thread down — mirrors KpodDevice's shutdown pattern.
        QMetaObject::invokeMethod(m_hidWorker, "shutdown", Qt::BlockingQueuedConnection);
    }
    if (m_hidThread) {
        m_hidThread->quit();
        m_hidThread->wait(2000);
    }
}

bool Rc28Device::isDetected() const {
    return m_info.detected;
}

Rc28DeviceInfo Rc28Device::deviceInfo() const {
    return m_info;
}

bool Rc28Device::isPolling() const {
    return m_polling;
}

bool Rc28Device::startPolling() {
    if (m_polling)
        return true;
    QMetaObject::invokeMethod(m_hidWorker, "openDevice", Qt::QueuedConnection);
    // Result is asynchronous; isPolling() becomes true on the deviceArrived signal.
    return m_info.detected;
}

void Rc28Device::stopPolling() {
    QMetaObject::invokeMethod(m_hidWorker, "closeDevice", Qt::QueuedConnection);
}

void Rc28Device::setLeds(bool f1, bool f2, bool tx) {
    QMetaObject::invokeMethod(m_hidWorker, "setLeds", Qt::QueuedConnection, Q_ARG(bool, f1), Q_ARG(bool, f2),
                              Q_ARG(bool, tx));
}
