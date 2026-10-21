#include "vailmidiworker.h"
#include <RtMidi.h>
#include <QLoggingCategory>
#include <QTimer>
#include <chrono>

Q_LOGGING_CATEGORY(hwVail, "hw.vail")

namespace {
constexpr int kPresencePollMs = 500;
constexpr int kMissesBeforeGone = 2;

template <typename Port> QStringList portNames(Port &port) {
    QStringList names;
    const unsigned int count = port.getPortCount();
    for (unsigned int index = 0; index < count; ++index) {
        names.append(QString::fromStdString(port.getPortName(index)));
    }
    return names;
}

template <typename Port> int selectedPort(Port &port, const QString &selected) {
    const auto naming = port.getCurrentApi() == RtMidi::WINDOWS_MM ? VailMidiProtocol::PortNaming::WinmmIndexed
                                                                   : VailMidiProtocol::PortNaming::Exact;
    return VailMidiProtocol::uniquePort(portNames(port), selected, naming);
}
} // namespace

VailMidiWorker::VailMidiWorker(const QString &deviceName, int wpm, int pitchHz, int mode, QObject *parent)
    : HaliKeyWorkerBase(deviceName, parent), m_configuration{wpm, pitchHz, mode} {}

VailMidiWorker::~VailMidiWorker() {
    prepareShutdown();
}

std::int64_t VailMidiWorker::nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void VailMidiWorker::prepareShutdown() {
    m_running.store(false, std::memory_order_release);
    QMutexLocker lock(&m_midiMutex);
    m_shutdown = true;
    m_running.store(false, std::memory_order_release);
    // Destroy input first: RtMidi joins its callback thread before this returns. The callback
    // never takes m_midiMutex, so closing cannot deadlock against an in-flight callback.
    m_midiIn.reset();
    m_midiOut.reset();
}

void VailMidiWorker::start() {
    QString failure;
    {
        QMutexLocker lock(&m_midiMutex);
        if (m_shutdown) {
            return;
        }
        if (!m_configuration.valid()) {
            failure = QStringLiteral("Invalid Vail keyer configuration");
        } else {
            try {
                m_midiIn = std::make_unique<RtMidiIn>();
                m_midiOut = std::make_unique<RtMidiOut>();
                // RtMidi reports some send failures as WARNING and merely prints to stderr.
                // Capture all output errors so configuration cannot silently appear successful.
                m_midiOut->setErrorCallback(
                    [](RtMidiError::Type type, const std::string &error, void *context) {
                        if (type != RtMidiError::DEBUG_WARNING) {
                            *static_cast<QString *>(context) = QString::fromStdString(error);
                        }
                    },
                    &m_outputError);
                m_midiIn->setErrorCallback(
                    [](RtMidiError::Type type, const std::string &error, void *context) {
                        if (type == RtMidiError::DEBUG_WARNING) {
                            return;
                        }
                        auto *worker = static_cast<VailMidiWorker *>(context);
                        const auto message = QString::fromStdString(error);
                        QMetaObject::invokeMethod(
                            worker,
                            [worker, message]() {
                                if (worker->m_running.load(std::memory_order_acquire)) {
                                    worker->fail(QStringLiteral("Vail MIDI input failed: %1").arg(message));
                                }
                            },
                            Qt::QueuedConnection);
                    },
                    this);
                const int input = selectedPort(*m_midiIn, m_portName);
                const int output = selectedPort(*m_midiOut, m_portName);
                if (input < 0 || output < 0) {
                    failure = QStringLiteral("Vail requires a unique matching MIDI input and output for '%1'. "
                                             "If multiple adapters have the same name, connect only one.")
                                  .arg(m_portName);
                } else {
                    m_midiOut->openPort(static_cast<unsigned int>(output), "QK4 Vail configuration");
                    m_midiIn->ignoreTypes(true, true, true);
                    m_midiIn->setCallback(&VailMidiWorker::midiCallback, this);
                    m_midiIn->openPort(static_cast<unsigned int>(input), "QK4 Vail keying");
                    if (!m_outputError.isEmpty()) {
                        failure = QStringLiteral("Failed to open Vail MIDI output: %1").arg(m_outputError);
                    } else if (!m_midiIn->isPortOpen() || !m_midiOut->isPortOpen()) {
                        failure = QStringLiteral("Failed to open both Vail MIDI ports");
                    }
                }
            } catch (const RtMidiError &error) {
                failure =
                    QStringLiteral("Failed to open Vail MIDI: %1").arg(QString::fromStdString(error.getMessage()));
            }
        }
        if (!failure.isEmpty()) {
            m_midiIn.reset();
            m_midiOut.reset();
        } else {
            m_running.store(true, std::memory_order_release);
        }
    }
    if (!failure.isEmpty()) {
        fail(failure);
        return;
    }
    if (!applyConfiguration()) {
        return;
    }
    m_presenceTimer = new QTimer(this);
    m_presenceTimer->setInterval(kPresencePollMs);
    connect(m_presenceTimer, &QTimer::timeout, this, &VailMidiWorker::checkPortPresence);
    m_presenceTimer->start();

    m_holdTimer = new QTimer(this);
    m_holdTimer->setInterval(25);
    m_holdTimer->setTimerType(Qt::PreciseTimer);
    connect(m_holdTimer, &QTimer::timeout, this, [this]() {
        if (m_running.load(std::memory_order_acquire)) {
            const auto timestampNs = nowNs();
            const auto result = m_stream.checkTimeout(timestampNs);
            if (result.aborted) {
                qCWarning(hwVail) << "Vail key hold exceeded safety timeout; release all keys to rearm";
            }
            deliver(result, timestampNs);
        }
    });
    m_holdTimer->start();
    qCDebug(hwVail) << "Opened Vail MIDI input/output" << m_portName;
    emit portOpened();
}

void VailMidiWorker::configure(int wpm, int pitchHz, int mode) {
    const VailMidiProtocol::Configuration configuration{wpm, pitchHz, mode};
    if (!configuration.valid()) {
        fail(
            QStringLiteral("Invalid Vail configuration: WPM %1, pitch %2 Hz, mode %3").arg(wpm).arg(pitchHz).arg(mode));
        return;
    }
    m_configuration = configuration;
    if (m_running.load(std::memory_order_acquire) && !m_stream.notesDown()) {
        applyConfiguration();
    }
}

bool VailMidiWorker::applyConfiguration() {
    QString failure;
    {
        QMutexLocker lock(&m_midiMutex);
        if (m_shutdown || !m_midiOut || !m_running.load(std::memory_order_acquire)) {
            return false;
        }
        try {
            const auto messages = VailMidiProtocol::configurationMessages(
                m_configuration, m_configurationApplied ? &m_appliedConfiguration : nullptr);
            for (const auto &message : messages) {
                m_outputError.clear();
                m_midiOut->sendMessage(&message);
                if (!m_outputError.isEmpty()) {
                    failure = QStringLiteral("Failed to configure Vail MIDI output: %1").arg(m_outputError);
                    break;
                }
            }
            if (failure.isEmpty()) {
                m_appliedConfiguration = m_configuration;
                m_configurationApplied = true;
            }
        } catch (const RtMidiError &error) {
            failure = QStringLiteral("Failed to configure Vail MIDI output: %1")
                          .arg(QString::fromStdString(error.getMessage()));
        }
    }
    if (!failure.isEmpty()) {
        fail(failure);
        return false;
    }
    return true;
}

void VailMidiWorker::setKeyingEnabled(bool enabled) {
    const auto timestampNs = nowNs();
    deliver(m_stream.setEnabled(enabled, timestampNs), timestampNs);
}

void VailMidiWorker::midiCallback(double, std::vector<unsigned char> *message, void *userData) {
    auto *self = static_cast<VailMidiWorker *>(userData);
    if (!self->m_running.load(std::memory_order_acquire) || !message || message->empty()) {
        return;
    }
    // Capture arrival time on RtMidi's callback thread BEFORE any Qt queue. GUI/worker queue
    // stalls then delay delivery but cannot stretch a dit or shorten an inter-element gap.
    // RtMidi's CoreMIDI backend repeats one packet's delta for each contained message, so
    // accumulating its deltaTime can manufacture elapsed time for alias notes in that packet.
    const auto timestampNs = nowNs();
    auto copy = *message;
    QMetaObject::invokeMethod(
        self,
        [self, timestampNs, message = std::move(copy)]() {
            if (self->m_running.load(std::memory_order_acquire)) {
                self->handleMidiMessage(message, timestampNs);
            }
        },
        Qt::QueuedConnection);
}

void VailMidiWorker::handleMidiMessage(const std::vector<unsigned char> &message, std::int64_t timestampNs) {
    deliver(m_stream.accept(message, timestampNs), timestampNs);
    if (!m_stream.notesDown()) {
        applyConfiguration();
    }
}

void VailMidiWorker::deliver(const VailMidiProtocol::Result &result, std::int64_t timestampNs) {
    if (!result.command.isEmpty()) {
        emit timedKeyingCommand(result.command);
    }
    if (result.stateChanged) {
        emit keyingStateChanged(result.down, static_cast<qint64>(timestampNs));
    }
    if (result.aborted) {
        emit keyingAborted();
    }
}

void VailMidiWorker::checkPortPresence() {
    if (!m_running.load(std::memory_order_acquire)) {
        return;
    }
    bool present = false;
    {
        QMutexLocker lock(&m_midiMutex);
        if (m_shutdown || !m_midiIn || !m_midiOut) {
            return;
        }
        try {
            present = selectedPort(*m_midiIn, m_portName) >= 0 && selectedPort(*m_midiOut, m_portName) >= 0;
        } catch (const RtMidiError &error) {
            qCWarning(hwVail) << "Vail port enumeration failed:" << QString::fromStdString(error.getMessage());
        }
    }
    if (present) {
        m_missedPolls = 0;
    } else if (++m_missedPolls >= kMissesBeforeGone) {
        fail(QStringLiteral("Vail MIDI input or output '%1' was removed").arg(m_portName));
    }
}

void VailMidiWorker::fail(const QString &message) {
    const auto timestampNs = nowNs();
    deliver(m_stream.setEnabled(false, timestampNs), timestampNs);
    m_running.store(false, std::memory_order_release);
    if (m_presenceTimer) {
        m_presenceTimer->stop();
    }
    if (m_holdTimer) {
        m_holdTimer->stop();
    }
    qCWarning(hwVail) << message;
    emit errorOccurred(message);
}
