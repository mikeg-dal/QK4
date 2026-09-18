#ifndef DIGITALMODEWINDOWS_H
#define DIGITALMODEWINDOWS_H

#include <QImage>
#include <QMainWindow>
#include <QVector>

#include "ft8/ft8logbook.h"
#include "ft8/ft8session.h"
#include "sstv/sstvstorage.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QDialog;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTabWidget;
class QTimer;
class FtxWaterfallWidget;

class FtxWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit FtxWindow(QWidget *parent = nullptr);
    Ft8::Mode mode() const;
    int rxTone() const { return m_session.rxHz; }
    int txTone() const { return m_session.txHz; }
    bool receiving() const;
    void setRadioState(bool connected, qint64 frequencyHz, const QString &radioMode, bool transmitting, double watts);
    void addDecodes(const QVector<Ft8::Decode> &decodes);
    void addSpectrum(const QVector<float> &db, double firstHz, double binHz);
    void setReceiveStatus(const QString &status);
    void setTransmitState(bool active, const QString &status);
    void setTransmitProgress(int emitted, int total);
    void finishTransmit(bool success, const QString &status);
    void setTransmitProtection(const QString &status, bool fault = false);
    void adjustSelectedTone(int steps);
    void switchSelectedTone();

signals:
    void captureChanged();
    void frequencyRequested(qint64 frequencyHz);
    void powerRequested(double watts);
    void transmitRequested(const QString &message, int mode, int audioHz, qint64 slotUtc);
    void stopRequested();
    void logContactRequested(const AdifRecord &record);
    void autoLogContactRequested(const AdifRecord &record);
    void logbookRequested();
    void txSetupRequested(int mode);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void showOptions();
    void setDecodeRows(int rows);
    void setTone(bool transmit, int hz);
    void refreshSession();
    void chooseBand(int index);
    void selectDecode(int row, bool callNow);
    void armCq();
    void armSelected();
    void halt();
    void tick();
    void queueNextTransmit();
    AdifRecord contactRecord() const;

    Ft8Session m_session;
    bool m_connected = false;
    bool m_radioTx = false;
    bool m_liveTx = false;
    qint64 m_frequencyHz = 0;
    qint64 m_lastQueuedSlot = -1;
    QVector<Ft8::Decode> m_decodes;
    QComboBox *m_mode = nullptr;
    QComboBox *m_band = nullptr;
    QLineEdit *m_frequency = nullptr;
    QLineEdit *m_call = nullptr;
    QLineEdit *m_grid = nullptr;
    QSpinBox *m_rxAudio = nullptr;
    QSpinBox *m_txAudio = nullptr;
    QLineEdit *m_message = nullptr;
    QDialog *m_optionsDialog = nullptr;
    QCheckBox *m_even = nullptr;
    QCheckBox *m_auto = nullptr;
    QDoubleSpinBox *m_power = nullptr;
    QLabel *m_radio = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_tones = nullptr;
    QLabel *m_protection = nullptr;
    QLabel *m_partner = nullptr;
    QProgressBar *m_cycle = nullptr;
    QProgressBar *m_txProgress = nullptr;
    QTableWidget *m_activity = nullptr;
    FtxWaterfallWidget *m_waterfall = nullptr;
    QPushButton *m_callButton = nullptr;
    QPushButton *m_cqButton = nullptr;
    QPushButton *m_haltButton = nullptr;
    QTimer *m_timer = nullptr;
    int m_decodeRows = 2;
    bool m_selectedTxTone = false;
    bool m_autoLog = true;
    bool m_completionHandled = false;
};

class SstvWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit SstvWindow(QWidget *parent = nullptr);
    void setRadioState(bool connected, qint64 rxFrequencyHz, const QString &rxMode, qint64 txFrequencyHz,
                       const QString &txMode, double watts);
    void setReceiveStatus(const QString &status);
    void setReceiveLevel(int percent);
    void setReceiveImage(const QImage &image, int completedRows, int totalRows, const QString &slantStatus);
    void completeReceiveImage(const QImage &image, int modeId, const QString &slantStatus, qint64 frequencyHz);
    void receiveCallsign(const QString &callsign, const QString &source, int confidence);
    void setTransmitState(bool active, const QString &status);
    void setTransmitProgress(int emitted, int total);
    void setTransmitProtection(const QString &status, bool fault = false);
    bool receiving() const;

signals:
    void resetReceiveRequested();
    void frequencyRequested(qint64 frequencyHz);
    void powerRequested(double watts);
    void transmitRequested(const QImage &image, int modeId, const QString &cwId, int cwWpm, const QString &fskId);
    void stopRequested();
    void logContactRequested(const AdifRecord &record);
    void logbookRequested();
    void txSetupRequested(int mode);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void chooseImage();
    void refreshPreview();
    void refreshHistory();
    void selectHistory(int index);
    AdifRecord contactRecord() const;

    SstvStorage m_storage;
    QVector<SstvRxRecord> m_history;
    QImage m_sourceImage;
    QImage m_receiveImageValue;
    qint64 m_rxFrequency = 0;
    qint64 m_txFrequency = 0;
    bool m_connected = false;
    bool m_programTx = false;
    QTabWidget *m_tabs = nullptr;
    QLabel *m_radio = nullptr;
    QLabel *m_rxStatus = nullptr;
    QLabel *m_rxImage = nullptr;
    QLabel *m_txImage = nullptr;
    QLabel *m_txStatus = nullptr;
    QLabel *m_txProtection = nullptr;
    QLabel *m_modeInfo = nullptr;
    QProgressBar *m_rxLevel = nullptr;
    QProgressBar *m_txProgress = nullptr;
    QComboBox *m_historyCombo = nullptr;
    QComboBox *m_mode = nullptr;
    QLineEdit *m_myCall = nullptr;
    QLineEdit *m_rxCall = nullptr;
    QLineEdit *m_toCall = nullptr;
    QCheckBox *m_fskId = nullptr;
    QCheckBox *m_cwId = nullptr;
    QComboBox *m_cwWpm = nullptr;
    QDoubleSpinBox *m_power = nullptr;
    QPushButton *m_send = nullptr;
    QPushButton *m_stop = nullptr;
};

class LogbookWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit LogbookWindow(QWidget *parent = nullptr);
    bool addContact(const AdifRecord &record, bool review = true, QString *error = nullptr);
    void setDefaults(const AdifRecord &record);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void buildUi();
    void reload();
    void populate();
    bool editRecord(AdifRecord record, int index = -1);
    void importAdif();
    void exportAdif();

    Ft8Logbook m_logbook;
    AdifRecord m_defaults;
    QLineEdit *m_search = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_status = nullptr;
};

#endif // DIGITALMODEWINDOWS_H
