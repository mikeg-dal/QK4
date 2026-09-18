#ifndef DIGITALMODESCONTROLLER_H
#define DIGITALMODESCONTROLLER_H

#include <QObject>
#include <QImage>
#include <QThread>
#include <QVector>
#include <memory>

#include "audio/digitaltxguard.h"
#include "ft8/ft8radiomode.h"
#include "ft8/ft8logbook.h"

class AudioController;
class ConnectionController;
class Ft8Receiver;
class FtxWindow;
class LogbookWindow;
class MainWindow;
class RadioState;
class SstvDecoder;
class SstvWindow;
class QTimer;
class WsjtBroadcaster;

class DigitalModesController final : public QObject {
    Q_OBJECT
public:
    DigitalModesController(ConnectionController *connection, AudioController *audio, RadioState *radioState,
                           MainWindow *mainWindow);
    ~DigitalModesController() override;

    void showFtx();
    void showSstv();
    void showLogbook();
    void shutdown();
    bool handleCtr2Knob(const QString &action, int value, bool absolute);
    bool handleCtr2Button(const QString &action);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum class ProgramKind { None, Ftx, Sstv, Calibration };
    enum class CalibrationPhase { None, ReadTest, EnableTest, Running };
    void ensureFtxWindow();
    void ensureSstvWindow();
    void ensureLogbookWindow();
    void updateWindows();
    void updateCapture();
    void updateFtxRadioMode();
    void setFrequency(qint64 frequencyHz);
    void setPower(double watts);
    void showTxSetup(int mode);
    void startCalibration(int mode);
    void processCatResponse(const QString &response);
    void serviceTxProtection();
    void handleGuardAction(DigitalTxGuard::Action action);
    void finishCalibration(bool success, const QString &status);
    std::optional<float> calibratedGain(int mode) const;
    void startFtxTransmit(const QString &message, int mode, int audioHz, qint64 slotUtc);
    void startSstvTransmit(const QImage &frame, int modeId, const QString &cwId, int cwWpm, const QString &fskId);
    void startPreparedProgramAudio();
    void stopProgramTransmit(const QString &reason = QStringLiteral("Transmission stopped"));
    void finishProgramTransmit(bool completed);
    void reviewContact(const AdifRecord &record);
    void saveContact(const AdifRecord &record);

    ConnectionController *m_connection;
    AudioController *m_audio;
    RadioState *m_radioState;
    FtxWindow *m_ftxWindow = nullptr;
    SstvWindow *m_sstvWindow = nullptr;
    WsjtBroadcaster *m_wsjt = nullptr;
    LogbookWindow *m_logbookWindow = nullptr;
    Ft8Receiver *m_ft8Receiver = nullptr;
    QThread m_ft8Thread;
    SstvDecoder *m_sstvDecoder = nullptr;
    QThread m_sstvThread;
    Ft8RadioMode m_ft8RadioMode;
    ProgramKind m_programKind = ProgramKind::None;
    QVector<qint16> m_preparedAudio;
    QTimer *m_startTimer = nullptr;
    QTimer *m_protectionTimer = nullptr;
    std::shared_ptr<DigitalTxControl> m_txControl = std::make_shared<DigitalTxControl>();
    DigitalTxGuard m_txGuard{m_txControl};
    quint64 m_txGeneration = 0;
    CalibrationPhase m_calibrationPhase = CalibrationPhase::None;
    int m_calibrationMode = 0;
    bool m_restoreTest = false;
    bool m_keyObserved = false;
    bool m_shuttingDown = false;
};

#endif // DIGITALMODESCONTROLLER_H
