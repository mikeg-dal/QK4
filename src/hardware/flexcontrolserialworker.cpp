#include "flexcontrolserialworker.h"
#include <QLoggingCategory>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QTimer>

Q_LOGGING_CATEGORY(hwFlex, "hw.flexcontrol")

// =============================================================================
// Token decoder (pure logic)
// =============================================================================

bool FlexControlSerialWorker::decodeToken(const QByteArray &token, Event &out) {
    out = Event{};
    if (token.isEmpty())
        return false;

    const char c0 = token.at(0);

    // Encoder: U / D optionally followed by a decimal detent count.
    if (c0 == 'U' || c0 == 'D') {
        int count = 1;
        if (token.size() > 1) {
            bool ok = false;
            int parsed = token.mid(1).toInt(&ok);
            if (ok && parsed > 0)
                count = parsed;
        }
        out.type = Event::Encoder;
        out.ticks = (c0 == 'U') ? count : -count;
        return true;
    }

    // AUX buttons: X<n><type>, e.g. "X1S", "X2C", "X3L".
    if (c0 == 'X' && token.size() >= 3) {
        const char bn = token.at(1);
        const char ty = token.at(2);
        if (bn < '1' || bn > '3')
            return false;
        out.type = Event::Button;
        out.buttonNumber = bn - '0';
        switch (ty) {
        case 'S':
            out.pressType = 0;
            break;
        case 'C':
            out.pressType = 1;
            break;
        case 'L':
            out.pressType = 2;
            break;
        default:
            return false;
        }
        return true;
    }

    // The switch built into the center knob reports bare S / C / L tokens. It
    // is a fourth control, distinct from the three X-prefixed AUX buttons.
    if (token.size() == 1 && (c0 == 'S' || c0 == 'C' || c0 == 'L')) {
        out.type = Event::Button;
        out.buttonNumber = 0;
        out.pressType = (c0 == 'S') ? 0 : (c0 == 'C') ? 1 : 2;
        return true;
    }

    // Reset banner ("F0304") and anything else — ignore.
    return false;
}

QByteArray FlexControlSerialWorker::encodeLedCommand(bool aux1, bool aux2, bool aux3) {
    QByteArray command("I000;");
    command[1] = aux1 ? '1' : '0';
    command[2] = aux2 ? '1' : '0';
    command[3] = aux3 ? '1' : '0';
    return command;
}

// =============================================================================
// Lifecycle
// =============================================================================

FlexControlSerialWorker::FlexControlSerialWorker(QObject *parent) : QObject(parent) {}

FlexControlSerialWorker::~FlexControlSerialWorker() {
    releaseHandle();
}

void FlexControlSerialWorker::start() {
    m_presenceTimer = new QTimer(this);
    m_presenceTimer->setInterval(PRESENCE_CHECK_INTERVAL_MS);
    connect(m_presenceTimer, &QTimer::timeout, this, &FlexControlSerialWorker::onPresenceTimer);
    m_presenceTimer->start();

    m_info = detectDeviceInfo();
    m_devicePresent = m_info.detected;
    emit deviceInfoReady(m_info);
}

void FlexControlSerialWorker::shutdown() {
    if (m_presenceTimer)
        m_presenceTimer->stop();
    releaseHandle();
}

// =============================================================================
// Open / close
// =============================================================================

bool FlexControlSerialWorker::openHandle() {
    if (m_port && m_port->isOpen())
        return true;
    if (m_info.portName.isEmpty()) {
        qCWarning(hwFlex) << "openHandle: no port name";
        return false;
    }
    if (!m_port) {
        m_port = new QSerialPort(this);
        connect(m_port, &QSerialPort::readyRead, this, &FlexControlSerialWorker::onReadyRead);
    }
    m_port->setPortName(m_info.portName);
    // CDC-ACM: line settings are nominal (the USB endpoint ignores them), but
    // Qt requires them set. 9600 8N1 matches the documented FlexControl config.
    m_port->setBaudRate(QSerialPort::Baud9600);
    m_port->setDataBits(QSerialPort::Data8);
    m_port->setParity(QSerialPort::NoParity);
    m_port->setStopBits(QSerialPort::OneStop);
    m_port->setFlowControl(QSerialPort::NoFlowControl);
    if (!m_port->open(QIODevice::ReadWrite)) {
        qCWarning(hwFlex) << "openHandle: open failed:" << m_port->errorString();
        return false;
    }
    m_rxBuffer.clear();
    return true;
}

void FlexControlSerialWorker::releaseHandle() {
    if (m_port) {
        if (m_port->isOpen())
            m_port->close();
        m_port->deleteLater();
        m_port = nullptr;
    }
    m_rxBuffer.clear();
}

void FlexControlSerialWorker::openDevice() {
    if (m_port && m_port->isOpen())
        return;
    if (!openHandle()) {
        emit pollError(QStringLiteral("Failed to open FlexControl port"));
        return;
    }
    emit deviceArrived();
}

void FlexControlSerialWorker::closeDevice() {
    const bool wasOpen = (m_port && m_port->isOpen());
    releaseHandle();
    if (wasOpen)
        emit deviceRemoved();
}

void FlexControlSerialWorker::setLeds(bool aux1, bool aux2, bool aux3) {
    if (!m_port || !m_port->isOpen())
        return;

    const QByteArray command = encodeLedCommand(aux1, aux2, aux3);
    if (m_port->write(command) != command.size())
        emit pollError(QStringLiteral("Failed to update FlexControl LEDs"));
}

// =============================================================================
// Reading
// =============================================================================

void FlexControlSerialWorker::onReadyRead() {
    if (!m_port)
        return;
    m_rxBuffer.append(m_port->readAll());
    processBuffer();
}

void FlexControlSerialWorker::processBuffer() {
    int idx;
    while ((idx = m_rxBuffer.indexOf(';')) >= 0) {
        QByteArray token = m_rxBuffer.left(idx).trimmed();
        m_rxBuffer.remove(0, idx + 1);
        Event ev;
        if (!decodeToken(token, ev))
            continue;
        if (ev.type == Event::Encoder)
            emit encoderRotated(ev.ticks);
        else if (ev.type == Event::Button)
            emit buttonPressed(ev.buttonNumber, ev.pressType);
    }
    // Guard against an unbounded buffer if a ';' never arrives.
    if (m_rxBuffer.size() > 256)
        m_rxBuffer.clear();
}

// =============================================================================
// Presence / hotplug
// =============================================================================

void FlexControlSerialWorker::onPresenceTimer() {
    FlexControlDeviceInfo info = detectDeviceInfo();
    const bool now = info.detected;

    if (!m_devicePresent && now) {
        m_info = info;
        m_devicePresent = true;
        emit deviceInfoReady(m_info);
    } else if (m_devicePresent && !now) {
        m_devicePresent = false;
        if (m_port && m_port->isOpen()) {
            releaseHandle();
            emit deviceRemoved();
        }
        m_info = FlexControlDeviceInfo{};
        emit deviceInfoReady(m_info);
    } else if (m_devicePresent && now && m_port && m_port->isOpen() &&
               !m_port->errorString().isEmpty() && m_port->error() != QSerialPort::NoError) {
        // Port went into an error state (e.g. yanked while open) — recover.
        releaseHandle();
        m_devicePresent = false;
        emit deviceRemoved();
    }
}

// =============================================================================
// Detection
// =============================================================================

FlexControlDeviceInfo FlexControlSerialWorker::detectDeviceInfo() {
    FlexControlDeviceInfo info;
    const auto ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &pi : ports) {
        if (pi.hasVendorIdentifier() && pi.hasProductIdentifier() && pi.vendorIdentifier() == VENDOR_ID &&
            pi.productIdentifier() == PRODUCT_ID) {
            info.detected = true;
            info.vendorId = pi.vendorIdentifier();
            info.productId = pi.productIdentifier();
            info.portName = pi.portName();
            info.productName = pi.description();
            info.manufacturer = pi.manufacturer();
            break;
        }
    }
    return info;
}
