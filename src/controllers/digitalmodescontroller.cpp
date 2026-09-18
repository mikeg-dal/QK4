#include "digitalmodescontroller.h"

#include "controllers/audiocontroller.h"
#include "controllers/connectioncontroller.h"
#include "ft8/ft8receiver.h"
#include "ft8/ft8transmitter.h"
#include "ft8/wsjtbroadcaster.h"
#include "mainwindow.h"
#include "models/radiostate.h"
#include "sstv/sstvdecoder.h"
#include "sstv/sstvencoder.h"
#include "ui/dialogs/digitalmodewindows.h"

#include <QDateTime>
#include <QEvent>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTimer>

#include <cmath>

namespace {
AdifRecord defaultContact(const QString &mode, qint64 frequencyHz, const QString &operatorCall) {
    const auto now = QDateTime::currentDateTimeUtc();
    AdifRecord record{{"QSO_DATE", now.toString("yyyyMMdd")},
                      {"TIME_ON", now.toString("HHmmss")},
                      {"MODE", mode},
                      {"STATION_CALLSIGN", operatorCall.trimmed().toUpper()}};
    if (frequencyHz > 0) {
        record["FREQ"] = QString::number(double(frequencyHz) / 1e6, 'f', 6);
        record["BAND"] = Ft8::bandFor(frequencyHz);
    }
    return record;
}
} // namespace

DigitalModesController::DigitalModesController(ConnectionController *connection, AudioController *audio,
                                               RadioState *radioState, MainWindow *mainWindow)
    : QObject(mainWindow), m_connection(connection), m_audio(audio), m_radioState(radioState) {
    qRegisterMetaType<Ft8::Decode>();
    qRegisterMetaType<QVector<Ft8::Decode>>();
    m_wsjt = new WsjtBroadcaster(this);

    m_ft8Thread.setObjectName(QStringLiteral("FT8 FT4 decoder"));
    m_ft8Receiver = new Ft8Receiver;
    m_ft8Receiver->moveToThread(&m_ft8Thread);
    m_ft8Thread.start();

    m_sstvThread.setObjectName(QStringLiteral("SSTV decoder"));
    m_sstvDecoder = new SstvDecoder;
    m_sstvDecoder->moveToThread(&m_sstvThread);
    m_sstvThread.start();

    m_startTimer = new QTimer(this);
    m_startTimer->setSingleShot(true);
    m_startTimer->setTimerType(Qt::PreciseTimer);
    connect(m_startTimer, &QTimer::timeout, this, &DigitalModesController::startPreparedProgramAudio);
    m_protectionTimer = new QTimer(this);
    m_protectionTimer->setInterval(250);
    connect(m_protectionTimer, &QTimer::timeout, this, &DigitalModesController::serviceTxProtection);
    connect(m_connection, &ConnectionController::catResponseReceived, this,
            &DigitalModesController::processCatResponse);

    connect(m_audio, &AudioController::receivePcmAvailable, this, [this](const QByteArray &pcm, qint64 utcMs) {
        if (m_ftxWindow && m_ftxWindow->receiving())
            m_ft8Receiver->enqueue(pcm, utcMs);
        if (m_sstvWindow && m_sstvWindow->receiving())
            QMetaObject::invokeMethod(m_sstvDecoder, "consumeStereoFloat", Qt::QueuedConnection,
                                      Q_ARG(QByteArray, pcm));
    });
    connect(m_audio, &AudioController::programAudioProgress, this, [this](int emitted, int total) {
        m_txGuard.audioAccepted(QDateTime::currentMSecsSinceEpoch());
        if (m_programKind == ProgramKind::Ftx && m_ftxWindow)
            m_ftxWindow->setTransmitProgress(emitted, total);
        else if (m_programKind == ProgramKind::Sstv && m_sstvWindow)
            m_sstvWindow->setTransmitProgress(emitted, total);
    });
    connect(m_audio, &AudioController::programAudioFinished, this, &DigitalModesController::finishProgramTransmit);

    const auto update = [this] {
        updateWindows();
        updateFtxRadioMode();
        updateCapture();
    };
    connect(m_radioState, &RadioState::frequencyChanged, this, update);
    connect(m_radioState, &RadioState::frequencyBChanged, this, update);
    connect(m_radioState, &RadioState::modeChanged, this, update);
    connect(m_radioState, &RadioState::modeBChanged, this, update);
    connect(m_radioState, &RadioState::dataSubModeChanged, this, update);
    connect(m_radioState, &RadioState::dataSubModeBChanged, this, update);
    connect(m_radioState, &RadioState::splitChanged, this, update);
    connect(m_radioState, &RadioState::transmitStateChanged, this, update);
    connect(m_radioState, &RadioState::transmitStateChanged, this, [this](bool transmitting) {
        if (m_programKind == ProgramKind::None)
            return;
        if (transmitting) {
            m_keyObserved = true;
        } else if (m_keyObserved) {
            stopProgramTransmit(QStringLiteral("K4 returned to RX · transmission stopped"));
        }
    });
    connect(m_radioState, &RadioState::rfPowerChanged, this, [update](double, LevelsState::PowerRange) { update(); });
    connect(m_connection, &ConnectionController::connectionStateChanged, this,
            [this, update](TcpClient::ConnectionState state) {
                if (state == TcpClient::Disconnected) {
                    stopProgramTransmit(QStringLiteral("Connection lost"));
                    m_ft8RadioMode.connectionLost();
                }
                update();
            });
}

DigitalModesController::~DigitalModesController() {
    shutdown();
    delete m_ftxWindow;
    delete m_sstvWindow;
    delete m_logbookWindow;
}

void DigitalModesController::shutdown() {
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;
    stopProgramTransmit(QStringLiteral("Application closing"));
    if (m_ft8Receiver && m_ft8Thread.isRunning()) {
        QMetaObject::invokeMethod(
            m_ft8Receiver,
            [this] {
                delete m_ft8Receiver;
                m_ft8Receiver = nullptr;
            },
            Qt::BlockingQueuedConnection);
        m_ft8Thread.quit();
        m_ft8Thread.wait(2000);
    }
    if (m_sstvDecoder && m_sstvThread.isRunning()) {
        QMetaObject::invokeMethod(
            m_sstvDecoder,
            [this] {
                delete m_sstvDecoder;
                m_sstvDecoder = nullptr;
            },
            Qt::BlockingQueuedConnection);
        m_sstvThread.quit();
        m_sstvThread.wait(2000);
    }
}

void DigitalModesController::ensureFtxWindow() {
    if (m_ftxWindow)
        return;
    m_ftxWindow = new FtxWindow;
    m_ftxWindow->installEventFilter(this);
    connect(m_ftxWindow, &FtxWindow::captureChanged, this, &DigitalModesController::updateCapture);
    connect(m_ftxWindow, &FtxWindow::frequencyRequested, this, &DigitalModesController::setFrequency);
    connect(m_ftxWindow, &FtxWindow::powerRequested, this, &DigitalModesController::setPower);
    connect(m_ftxWindow, &FtxWindow::transmitRequested, this, &DigitalModesController::startFtxTransmit);
    connect(m_ftxWindow, &FtxWindow::stopRequested, this,
            [this] { stopProgramTransmit(QStringLiteral("FT transmission halted")); });
    connect(m_ftxWindow, &FtxWindow::logContactRequested, this, &DigitalModesController::reviewContact);
    connect(m_ftxWindow, &FtxWindow::autoLogContactRequested, this, &DigitalModesController::saveContact);
    connect(m_ftxWindow, &FtxWindow::logbookRequested, this, &DigitalModesController::showLogbook);
    connect(m_ftxWindow, &FtxWindow::txSetupRequested, this, &DigitalModesController::showTxSetup);
    const bool calibrated = calibratedGain(int(m_ftxWindow->mode())).has_value();
    m_ftxWindow->setTransmitProtection(
        calibrated ? QStringLiteral("TX protection active · calibrated for ALC 3–5")
                   : QStringLiteral("TX audio not calibrated · open Options before transmitting"),
        !calibrated);
    connect(m_ft8Receiver, &Ft8Receiver::decoded, m_ftxWindow,
            [this](const QVector<Ft8::Decode> &decodes, quint64 generation) {
                if (m_ftxWindow && generation == m_ft8Receiver->generation() && m_ftxWindow->receiving())
                    m_ftxWindow->addDecodes(decodes);
                if (generation == m_ft8Receiver->generation())
                    for (const auto &decode : decodes)
                        m_wsjt->sendDecode(decode);
            });
    connect(m_ft8Receiver, &Ft8Receiver::spectrum, m_ftxWindow,
            [this](const QVector<float> &db, double firstHz, double binHz, quint64 generation) {
                if (m_ftxWindow && generation == m_ft8Receiver->generation() && m_ftxWindow->receiving())
                    m_ftxWindow->addSpectrum(db, firstHz, binHz);
            });
    connect(m_ft8Receiver, &Ft8Receiver::streamStatus, m_ftxWindow, [this](const QString &status, quint64 generation) {
        if (m_ftxWindow && generation == m_ft8Receiver->generation())
            m_ftxWindow->setReceiveStatus(status);
    });
}

void DigitalModesController::ensureSstvWindow() {
    if (m_sstvWindow)
        return;
    m_sstvWindow = new SstvWindow;
    m_sstvWindow->installEventFilter(this);
    connect(m_sstvWindow, &SstvWindow::resetReceiveRequested, this,
            [this] { QMetaObject::invokeMethod(m_sstvDecoder, "resetAuto", Qt::QueuedConnection); });
    connect(m_sstvWindow, &SstvWindow::frequencyRequested, this, &DigitalModesController::setFrequency);
    connect(m_sstvWindow, &SstvWindow::powerRequested, this, &DigitalModesController::setPower);
    connect(m_sstvWindow, &SstvWindow::transmitRequested, this, &DigitalModesController::startSstvTransmit);
    connect(m_sstvWindow, &SstvWindow::stopRequested, this,
            [this] { stopProgramTransmit(QStringLiteral("SSTV transmission stopped")); });
    connect(m_sstvWindow, &SstvWindow::logContactRequested, this, &DigitalModesController::reviewContact);
    connect(m_sstvWindow, &SstvWindow::logbookRequested, this, &DigitalModesController::showLogbook);
    connect(m_sstvWindow, &SstvWindow::txSetupRequested, this, &DigitalModesController::showTxSetup);
    const bool calibrated = calibratedGain(int(DigitalTxGuard::Mode::Sstv)).has_value();
    m_sstvWindow->setTransmitProtection(calibrated ? QStringLiteral("TX protection active · SSTV audio calibrated")
                                                   : QStringLiteral("SSTV TX audio not calibrated"),
                                        !calibrated);
    connect(m_sstvDecoder, &SstvDecoder::statusChanged, m_sstvWindow, &SstvWindow::setReceiveStatus);
    connect(m_sstvDecoder, &SstvDecoder::inputLevelChanged, m_sstvWindow, &SstvWindow::setReceiveLevel);
    connect(m_sstvDecoder, &SstvDecoder::imageUpdated, m_sstvWindow, &SstvWindow::setReceiveImage);
    connect(m_sstvDecoder, &SstvDecoder::imageCompleted, m_sstvWindow,
            [this](const QImage &image, int modeId, const QString &slant) {
                if (m_sstvWindow)
                    m_sstvWindow->completeReceiveImage(image, modeId, slant, qint64(m_radioState->vfoA()));
            });
    connect(m_sstvDecoder, &SstvDecoder::callsignDetected, m_sstvWindow, &SstvWindow::receiveCallsign);
}

void DigitalModesController::ensureLogbookWindow() {
    if (!m_logbookWindow)
        m_logbookWindow = new LogbookWindow;
}

void DigitalModesController::showFtx() {
    ensureFtxWindow();
    m_ft8RadioMode.enter();
    updateFtxRadioMode();
    updateWindows();
    m_ftxWindow->show();
    m_ftxWindow->raise();
    m_ftxWindow->activateWindow();
    updateCapture();
}

void DigitalModesController::showSstv() {
    ensureSstvWindow();
    updateWindows();
    m_sstvWindow->show();
    m_sstvWindow->raise();
    m_sstvWindow->activateWindow();
    QMetaObject::invokeMethod(m_sstvDecoder, "resetAuto", Qt::QueuedConnection);
    updateCapture();
}

void DigitalModesController::showLogbook() {
    ensureLogbookWindow();
    QSettings settings;
    auto defaults = defaultContact(m_radioState->modeStringFull(), qint64(m_radioState->vfoA()),
                                   settings.value("digital/myCall").toString());
    m_logbookWindow->setDefaults(defaults);
    m_logbookWindow->show();
    m_logbookWindow->raise();
    m_logbookWindow->activateWindow();
}

bool DigitalModesController::handleCtr2Knob(const QString &action, int value, bool absolute) {
    if (!m_ftxWindow || !m_ftxWindow->isVisible())
        return false;
    if (action == QStringLiteral("selected_adjustment") || action == QStringLiteral("active_vfo_frequency") ||
        action == QStringLiteral("other_vfo_frequency")) {
        if (!absolute)
            m_ftxWindow->adjustSelectedTone(value);
        return true;
    }
    return false;
}

bool DigitalModesController::handleCtr2Button(const QString &action) {
    if (action == QStringLiteral("adjust_ft8_rx_tx")) {
        showFtx();
        m_ftxWindow->switchSelectedTone();
        return true;
    }
    if (m_ftxWindow && m_ftxWindow->isVisible() && action == QStringLiteral("set_ft8_frequency"))
        return true;
    return false;
}

bool DigitalModesController::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::Hide || event->type() == QEvent::Show) {
        QTimer::singleShot(0, this, [this, watched] {
            if (watched == m_ftxWindow && m_ftxWindow && !m_ftxWindow->isVisible()) {
                m_ft8RadioMode.leave();
                updateFtxRadioMode();
            }
            updateCapture();
        });
    }
    return QObject::eventFilter(watched, event);
}

void DigitalModesController::updateWindows() {
    const bool connected = m_connection->isConnected();
    if (m_ftxWindow)
        m_ftxWindow->setRadioState(connected, qint64(m_radioState->vfoA()), m_radioState->modeStringFull(),
                                   m_radioState->isTransmitting(), m_radioState->rfPower());
    if (m_ftxWindow) {
        QSettings settings;
        m_wsjt->sendStatus(qint64(m_radioState->vfoA()), Ft8::modeName(m_ftxWindow->mode()),
                           m_radioState->isTransmitting(), m_ftxWindow->rxTone(), m_ftxWindow->txTone(),
                           settings.value(QStringLiteral("digital/myCall")).toString(),
                           settings.value(QStringLiteral("digital/myGrid")).toString());
    }
    if (m_sstvWindow) {
        const bool split = m_radioState->splitEnabled();
        m_sstvWindow->setRadioState(connected, qint64(m_radioState->vfoA()), m_radioState->modeStringFull(),
                                    qint64(split ? m_radioState->vfoB() : m_radioState->vfoA()),
                                    split ? m_radioState->modeStringFullB() : m_radioState->modeStringFull(),
                                    m_radioState->rfPower());
    }
}

void DigitalModesController::updateCapture() {
    if (!m_ft8Receiver)
        return;
    const bool enabled = m_ftxWindow && m_ftxWindow->receiving() && m_connection->isConnected() &&
                         !m_radioState->isTransmitting() && m_programKind == ProgramKind::None;
    m_ft8Receiver->setCapture(enabled, m_ftxWindow ? m_ftxWindow->mode() : Ft8::Mode::FT8);
}

void DigitalModesController::updateFtxRadioMode() {
    if (!m_ftxWindow)
        return;
    const bool busy = m_radioState->isTransmitting() || m_audio->isPttActive() || m_programKind != ProgramKind::None;
    const QString command = m_ft8RadioMode.update(m_connection->isConnected(), int(m_radioState->mode()),
                                                  m_radioState->dataSubMode(), busy);
    if (!command.isEmpty())
        m_connection->sendCAT(command);
}

void DigitalModesController::setFrequency(qint64 frequencyHz) {
    if (!m_connection->isConnected() || frequencyHz <= 0 || m_programKind != ProgramKind::None)
        return;
    const QString command = QStringLiteral("FA%1;").arg(quint64(frequencyHz), 11, 10, QLatin1Char('0'));
    m_connection->sendCAT(command + QStringLiteral("FA;"));
    // Match every other desktop tuning surface: update the shared state
    // optimistically, then let the K4 readback confirm/correct it.
    m_radioState->parseCATCommand(command);
}

void DigitalModesController::setPower(double watts) {
    if (!m_connection->isConnected() || m_programKind != ProgramKind::None)
        return;
    const double power = qBound(1.0, watts, 110.0);
    m_connection->sendCAT(power <= 10.0 ? QStringLiteral("PC%1L;").arg(qRound(power * 10), 3, 10, QLatin1Char('0'))
                                        : QStringLiteral("PC%1H;").arg(qRound(power), 3, 10, QLatin1Char('0')));
}

std::optional<float> DigitalModesController::calibratedGain(int mode) const {
    QSettings settings;
    return DigitalTxCalibration::load(settings, mode);
}

void DigitalModesController::showTxSetup(int mode) {
    const auto saved = calibratedGain(mode);
    QWidget *parent = mode == int(DigitalTxGuard::Mode::Sstv) ? static_cast<QWidget *>(m_sstvWindow)
                                                              : static_cast<QWidget *>(m_ftxWindow);
    QMessageBox box(parent);
    box.setWindowTitle(mode == int(DigitalTxGuard::Mode::Sstv) ? QStringLiteral("SSTV TX audio setup")
                                                               : QStringLiteral("FT8 / FT4 TX audio setup"));
    box.setIcon(QMessageBox::Information);
    box.setText(saved ? QStringLiteral("A protected TX audio level is saved.")
                      : QStringLiteral("TX audio has not been calibrated."));
    box.setInformativeText(
        QStringLiteral("Calibration switches the K4 to TEST, sends a tone, and automatically finds a level "
                       "for raw ALC 3–5. It will stop on RF output, compression, excessive ALC, missing meter "
                       "data, or missing audio. FT8 and FT4 share this calibration.\n\n"
                       "Disconnect or bypass external amplifiers before continuing."));
    auto *start = box.addButton(QStringLiteral("Start calibration"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() == start)
        startCalibration(mode);
}

void DigitalModesController::startCalibration(int mode) {
    if ((!m_ftxWindow && !m_sstvWindow) || m_programKind != ProgramKind::None || !m_connection->isConnected() ||
        m_radioState->isTransmitting() || m_audio->isPttActive()) {
        if (mode == int(DigitalTxGuard::Mode::Sstv) && m_sstvWindow)
            m_sstvWindow->setTransmitProtection(QStringLiteral("Calibration unavailable: connect and return to RX."),
                                                true);
        else if (m_ftxWindow)
            m_ftxWindow->setTransmitProtection(QStringLiteral("Calibration unavailable: connect and return to RX."),
                                               true);
        return;
    }
    if (m_radioState->mode() != RadioState::DATA || m_radioState->dataSubMode() != 0) {
        if (mode == int(DigitalTxGuard::Mode::Sstv) && m_sstvWindow)
            m_sstvWindow->setTransmitProtection(QStringLiteral("Calibration needs DATA-A."), true);
        else if (m_ftxWindow)
            m_ftxWindow->setTransmitProtection(QStringLiteral("Calibration needs DATA-A. Select DATA-A and try again."),
                                               true);
        updateFtxRadioMode();
        return;
    }
    m_txGuard.acknowledge();
    m_programKind = ProgramKind::Calibration;
    m_calibrationMode = mode;
    m_calibrationPhase = CalibrationPhase::ReadTest;
    m_restoreTest = false;
    if (mode == int(DigitalTxGuard::Mode::Sstv) && m_sstvWindow) {
        m_sstvWindow->setTransmitState(true, QStringLiteral("Calibration: checking K4 TEST mode…"));
        m_sstvWindow->setTransmitProtection(QStringLiteral("Calibration starting · TEST confirmation required"));
    } else if (m_ftxWindow) {
        m_ftxWindow->setTransmitState(true, QStringLiteral("Calibration: checking K4 TEST mode…"));
        m_ftxWindow->setTransmitProtection(QStringLiteral("Calibration starting · TEST mode confirmation required"));
    }
    m_connection->sendCAT(QStringLiteral("TS;"));
    QTimer::singleShot(3000, this, [this] {
        if (m_calibrationPhase == CalibrationPhase::ReadTest || m_calibrationPhase == CalibrationPhase::EnableTest)
            finishCalibration(false, QStringLiteral("Calibration stopped: K4 TEST mode was not confirmed."));
    });
}

void DigitalModesController::processCatResponse(const QString &response) {
    for (const QString &raw : response.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const QString command = raw.trimmed();
        if (m_calibrationPhase == CalibrationPhase::ReadTest &&
            (command == QStringLiteral("TS0") || command == QStringLiteral("TS1"))) {
            m_restoreTest = command == QStringLiteral("TS0");
            m_calibrationPhase = CalibrationPhase::EnableTest;
            m_connection->sendCAT(QStringLiteral("TS1;TS;"));
            if (m_calibrationMode == int(DigitalTxGuard::Mode::Sstv) && m_sstvWindow)
                m_sstvWindow->setTransmitProtection(QStringLiteral("Calibration: enabling K4 TEST mode…"));
            else if (m_ftxWindow)
                m_ftxWindow->setTransmitProtection(QStringLiteral("Calibration: enabling K4 TEST mode…"));
            continue;
        }
        if (m_calibrationPhase == CalibrationPhase::EnableTest && command == QStringLiteral("TS1")) {
            m_calibrationPhase = CalibrationPhase::Running;
            ++m_txGeneration;
            m_txGuard.acknowledge();
            m_txGuard.begin(DigitalTxGuard::Mode(m_calibrationMode), m_txGeneration,
                            QDateTime::currentMSecsSinceEpoch(), true);
            m_preparedAudio.resize(12000 * 20);
            for (int i = 0; i < m_preparedAudio.size(); ++i)
                m_preparedAudio[i] = qint16(qRound(26000.0 * std::sin(2.0 * M_PI * 1500.0 * i / 12000.0)));
            m_connection->sendCAT(QStringLiteral("TM1;TM;TX;"));
            m_startTimer->start(350);
            m_protectionTimer->start();
            if (m_calibrationMode == int(DigitalTxGuard::Mode::Sstv) && m_sstvWindow)
                m_sstvWindow->setTransmitProtection(QStringLiteral("Calibrating in TEST · target ALC 3–5"));
            else if (m_ftxWindow)
                m_ftxWindow->setTransmitProtection(QStringLiteral("Calibrating in TEST mode · target raw ALC 3–5"));
            continue;
        }
        if (m_txGuard.active() && command.startsWith(QStringLiteral("TM"))) {
            const auto action = m_txGuard.meter(command, QDateTime::currentMSecsSinceEpoch());
            m_audio->setProgramAudioGain(m_txGuard.gain());
            handleGuardAction(action);
            if (m_programKind == ProgramKind::Sstv && m_sstvWindow && m_txGuard.active())
                m_sstvWindow->setTransmitProtection(
                    QStringLiteral("TX protection active · %1").arg(m_txGuard.meterDiagnostic()));
            else if (m_ftxWindow && m_txGuard.active())
                m_ftxWindow->setTransmitProtection(
                    QStringLiteral("TX protection active · %1").arg(m_txGuard.meterDiagnostic()));
        }
    }
}

void DigitalModesController::serviceTxProtection() {
    if (!m_txGuard.active()) {
        m_protectionTimer->stop();
        return;
    }
    handleGuardAction(m_txGuard.tick(QDateTime::currentMSecsSinceEpoch()));
    if (m_txGuard.active())
        m_connection->sendCAT(QStringLiteral("TM;"));
}

void DigitalModesController::handleGuardAction(DigitalTxGuard::Action action) {
    if (action == DigitalTxGuard::Action::None)
        return;
    if (action == DigitalTxGuard::Action::Reduced) {
        if (m_programKind == ProgramKind::Sstv && m_sstvWindow)
            m_sstvWindow->setTransmitProtection(
                QStringLiteral("Audio drive reduced automatically · protection active"));
        else if (m_ftxWindow)
            m_ftxWindow->setTransmitProtection(QStringLiteral("Audio drive reduced automatically · protection active"));
    } else if (action == DigitalTxGuard::Action::Calibrated) {
        QSettings settings;
        const bool saved = DigitalTxCalibration::save(settings, m_calibrationMode, m_txGuard.gain());
        finishCalibration(saved, saved ? QStringLiteral("TX audio calibrated for ALC 3–5 · protection active")
                                       : QStringLiteral("Calibration completed, but the level could not be saved."));
    } else if (action == DigitalTxGuard::Action::Tripped) {
        const QString reason = m_txGuard.reason();
        if (m_calibrationPhase != CalibrationPhase::None)
            finishCalibration(false, reason);
        else
            stopProgramTransmit(reason);
    }
}

void DigitalModesController::finishCalibration(bool success, const QString &status) {
    m_calibrationPhase = CalibrationPhase::None;
    m_protectionTimer->stop();
    m_txGuard.stop();
    m_audio->stopProgramAudio();
    m_startTimer->stop();
    m_preparedAudio.clear();
    m_programKind = ProgramKind::None;
    m_keyObserved = false;
    if (m_connection->isConnected())
        m_connection->sendCAT(QStringLiteral("RX;") + (m_restoreTest ? QStringLiteral("TS0;TS;") : QString()));
    if (m_calibrationMode == int(DigitalTxGuard::Mode::Sstv) && m_sstvWindow) {
        m_sstvWindow->setTransmitState(false, status);
        m_sstvWindow->setTransmitProtection(status, !success);
    } else if (m_ftxWindow) {
        m_ftxWindow->setTransmitState(false, status);
        m_ftxWindow->setTransmitProtection(status, !success);
    }
    updateCapture();
}

void DigitalModesController::startFtxTransmit(const QString &message, int mode, int audioHz, qint64 slotUtc) {
    if (!m_ftxWindow || m_programKind != ProgramKind::None)
        return;
    if (!m_connection->isConnected() || m_radioState->isTransmitting() || m_audio->isPttActive() ||
        m_radioState->testMode()) {
        m_ftxWindow->finishTransmit(false, QStringLiteral("TX unavailable: connect, return to RX, and turn TEST off."));
        return;
    }
    if (m_radioState->mode() != RadioState::DATA || m_radioState->dataSubMode() != 0) {
        m_ftxWindow->finishTransmit(false, QStringLiteral("TX unavailable until the K4 confirms DATA-A."));
        updateFtxRadioMode();
        return;
    }
    const auto ftxMode = Ft8::Mode(mode);
    const auto gain = calibratedGain(mode);
    if (!gain) {
        m_ftxWindow->finishTransmit(false, QStringLiteral("TX blocked: calibrate TX audio in Options first."));
        m_ftxWindow->setTransmitProtection(QStringLiteral("TX blocked · calibration required to prevent excessive ALC"),
                                           true);
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (mode < int(Ft8::Mode::FT8) || mode > int(Ft8::Mode::FT4) ||
        now - slotUtc >= Ft8Transmitter::latestStartMs(ftxMode)) {
        m_ftxWindow->finishTransmit(false, QStringLiteral("TX stopped: transmit start window has elapsed."));
        return;
    }
    QString error;
    m_preparedAudio = ft8TransmitWaveform(message, ftxMode, audioHz, &error);
    if (m_preparedAudio.isEmpty()) {
        m_ftxWindow->finishTransmit(false, error);
        return;
    }
    const qint64 startUtc = slotUtc + Ft8Transmitter::audioOffsetMs(ftxMode);
    const int skippedSamples = int(qMax<qint64>(0, now - startUtc) * 12);
    if (skippedSamples >= m_preparedAudio.size()) {
        m_preparedAudio.clear();
        m_ftxWindow->finishTransmit(false, QStringLiteral("TX stopped: transmit audio window has elapsed."));
        return;
    }
    if (skippedSamples > 0) {
        m_preparedAudio.remove(0, skippedSamples);
        const int ramp = qMin(60, m_preparedAudio.size());
        for (int i = 0; i < ramp; ++i)
            m_preparedAudio[i] = qRound(m_preparedAudio[i] * .5 * (1 - std::cos(M_PI * i / ramp)));
    }
    m_programKind = ProgramKind::Ftx;
    ++m_txGeneration;
    m_txGuard.acknowledge();
    m_txControl->gain.store(*gain, std::memory_order_release);
    m_txControl->scheduledGeneration.store(m_txGeneration, std::memory_order_release);
    if (!m_txGuard.begin(DigitalTxGuard::Mode(mode), m_txGeneration, now)) {
        m_programKind = ProgramKind::None;
        m_ftxWindow->finishTransmit(false, QStringLiteral("TX protection is latched; retry from Options."));
        return;
    }
    m_protectionTimer->start();
    m_keyObserved = false;
    m_ftxWindow->setTransmitState(
        true, QStringLiteral("Keying K4 for timed %1 transmission…").arg(Ft8::modeName(Ft8::Mode(mode))));
    updateCapture();
    m_connection->sendCAT(QStringLiteral("TM1;TM;TX;"));
    m_startTimer->start(int(qMax<qint64>(0, startUtc - QDateTime::currentMSecsSinceEpoch())));
}

void DigitalModesController::startSstvTransmit(const QImage &frame, int modeId, const QString &cwId, int cwWpm,
                                               const QString &fskId) {
    if (!m_sstvWindow || m_programKind != ProgramKind::None)
        return;
    if (!m_connection->isConnected() || m_radioState->isTransmitting() || m_audio->isPttActive()) {
        m_sstvWindow->setTransmitState(false, QStringLiteral("TX unavailable: connect and return the K4 to RX."));
        return;
    }
    if (m_radioState->mode() != RadioState::DATA || m_radioState->dataSubMode() != 0) {
        m_sstvWindow->setTransmitState(false, QStringLiteral("TX unavailable until the K4 confirms DATA-A."));
        return;
    }
    const auto gain = calibratedGain(int(DigitalTxGuard::Mode::Sstv));
    if (!gain) {
        m_sstvWindow->setTransmitProtection(QStringLiteral("TX blocked · SSTV audio calibration required"), true);
        return;
    }
    if (QMessageBox::question(m_sstvWindow, QStringLiteral("Transmit SSTV"),
                              QStringLiteral("This will key the K4 and transmit the displayed image. Continue?")) !=
        QMessageBox::Yes)
        return;

    SstvEncoder encoder;
    QString error;
    if (!encoder.begin(frame, SstvModeId(modeId), &error, cwId, cwWpm, fskId, 0, 300)) {
        m_sstvWindow->setTransmitState(false, error);
        return;
    }
    m_preparedAudio.clear();
    m_preparedAudio.reserve(encoder.totalSamples());
    while (!encoder.isComplete())
        m_preparedAudio += encoder.nextSamples(12000);
    if (m_preparedAudio.isEmpty()) {
        m_sstvWindow->setTransmitState(false, QStringLiteral("SSTV waveform generation failed."));
        return;
    }
    m_programKind = ProgramKind::Sstv;
    ++m_txGeneration;
    m_txGuard.acknowledge();
    m_txControl->gain.store(*gain, std::memory_order_release);
    if (!m_txGuard.begin(DigitalTxGuard::Mode::Sstv, m_txGeneration, QDateTime::currentMSecsSinceEpoch())) {
        m_programKind = ProgramKind::None;
        m_sstvWindow->setTransmitProtection(QStringLiteral("TX protection is latched; run setup again."), true);
        return;
    }
    m_protectionTimer->start();
    m_keyObserved = false;
    m_sstvWindow->setTransmitState(true, QStringLiteral("Keying K4 · SSTV program audio ready"));
    updateCapture();
    m_connection->sendCAT(QStringLiteral("TM1;TM;TX;"));
    m_startTimer->start(500);
}

void DigitalModesController::startPreparedProgramAudio() {
    if (m_programKind == ProgramKind::None || m_preparedAudio.isEmpty())
        return;
    if (!m_connection->isConnected()) {
        stopProgramTransmit(QStringLiteral("Connection lost before audio start"));
        return;
    }
    if (m_programKind == ProgramKind::Ftx && m_ftxWindow)
        m_ftxWindow->setTransmitState(true, QStringLiteral("Transmitting timed digital audio"));
    else if (m_programKind == ProgramKind::Sstv && m_sstvWindow)
        m_sstvWindow->setTransmitState(true, QStringLiteral("Transmitting SSTV"));
    const float gain = m_txGuard.active()
                           ? m_txGuard.gain()
                           : qBound(0.02f, QSettings().value("digital/programAudioGain", 0.12).toFloat(), 0.30f);
    m_audio->startProgramAudio(m_preparedAudio, gain);
}

void DigitalModesController::stopProgramTransmit(const QString &reason) {
    if (m_programKind == ProgramKind::None)
        return;
    if (m_programKind == ProgramKind::Calibration) {
        finishCalibration(false, reason);
        return;
    }
    const ProgramKind kind = m_programKind;
    m_programKind = ProgramKind::None;
    m_keyObserved = false;
    m_startTimer->stop();
    m_preparedAudio.clear();
    m_audio->stopProgramAudio();
    m_txGuard.stop();
    m_protectionTimer->stop();
    if (m_connection->isConnected())
        m_connection->sendCAT(QStringLiteral("RX;"));
    if (kind == ProgramKind::Ftx && m_ftxWindow)
        m_ftxWindow->finishTransmit(false, reason);
    else if (kind == ProgramKind::Sstv && m_sstvWindow)
        m_sstvWindow->setTransmitState(false, reason);
    updateCapture();
}

void DigitalModesController::finishProgramTransmit(bool completed) {
    if (m_programKind == ProgramKind::None)
        return;
    if (m_programKind == ProgramKind::Calibration) {
        finishCalibration(false, QStringLiteral("Calibration tone ended before a stable level was found."));
        return;
    }
    const ProgramKind kind = m_programKind;
    m_programKind = ProgramKind::None;
    m_keyObserved = false;
    m_preparedAudio.clear();
    m_txGuard.stop();
    m_protectionTimer->stop();
    QTimer::singleShot(250, this, [this] {
        if (m_connection->isConnected())
            m_connection->sendCAT(QStringLiteral("RX;"));
        updateCapture();
    });
    if (kind == ProgramKind::Ftx && m_ftxWindow)
        m_ftxWindow->finishTransmit(completed, completed ? QStringLiteral("TX complete · listening")
                                                         : QStringLiteral("Digital audio stopped"));
    else if (kind == ProgramKind::Sstv && m_sstvWindow)
        m_sstvWindow->setTransmitState(false, completed ? QStringLiteral("SSTV transmission complete")
                                                        : QStringLiteral("SSTV audio stopped"));
}

void DigitalModesController::reviewContact(const AdifRecord &record) {
    ensureLogbookWindow();
    if (m_logbookWindow->addContact(record, true))
        m_wsjt->sendQsoLogged(record);
}

void DigitalModesController::saveContact(const AdifRecord &record) {
    ensureLogbookWindow();
    QString error;
    if (m_logbookWindow->addContact(record, false, &error)) {
        m_wsjt->sendQsoLogged(record);
        if (m_ftxWindow)
            m_ftxWindow->setReceiveStatus(QStringLiteral("QSO complete · contact saved automatically"));
    } else if (m_ftxWindow) {
        m_ftxWindow->setReceiveStatus(
            QStringLiteral("QSO complete, but the contact could not be saved · %1").arg(error));
    }
}
