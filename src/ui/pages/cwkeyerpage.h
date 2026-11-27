#ifndef CWKEYERPAGE_H
#define CWKEYERPAGE_H

#include <QWidget>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTimer>

class HalikeyDevice;
class HaliKeyPortScan;
class RadioState;
class CwController;
class QFormLayout;

/**
 * @brief OptionsDialog "CW Keyer" tab: HaliKey or Vail device, port and connect, keyer settings,
 *        sidetone source and volume, and the K4's key-down delay (KZL, via CwController). Settings
 *        persist in RadioSettings.
 */
class CwKeyerPage : public QWidget {
    Q_OBJECT

public:
    CwKeyerPage(HalikeyDevice *halikeyDevice, RadioState *radioState, CwController *cwController,
                QWidget *parent = nullptr);

    void refresh();

    // A device may have come or gone: rescan without the "Scanning…" placeholder, and leave the
    // list alone if nothing changed.
    void rescanInBackground();

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private slots:
    void onCwKeyerConnectClicked();
    void onCwKeyerRefreshClicked();
    void updateCwKeyerStatus();

private:
    void populateCwKeyerPorts();
    void startScan(int deviceType);
    void applyCwKeyerPorts(int deviceType, const QStringList &ports);
    void updateCwKeyerDescription();
    void showKeyDownDelay(int ms);
    void sendKeyDownDelay();
    void updateSidetoneControls();

    HalikeyDevice *m_halikeyDevice;
    QComboBox *m_cwKeyerDeviceTypeCombo = nullptr;
    QLabel *m_cwKeyerDescLabel = nullptr;
    QComboBox *m_cwKeyerPortCombo = nullptr;
    QPushButton *m_cwKeyerRefreshBtn = nullptr;
    QPushButton *m_cwKeyerConnectBtn = nullptr;
    QLabel *m_cwKeyerStatusLabel = nullptr;
    QSlider *m_sidetoneVolumeSlider = nullptr;
    QLabel *m_sidetoneVolumeValueLabel = nullptr;
    QWidget *m_halikeyKeyerSettings = nullptr;
    RadioState *m_radioState;
    CwController *m_cwController;
    QSlider *m_keyDownDelaySlider = nullptr;
    QLabel *m_keyDownDelayValueLabel = nullptr;
    QWidget *m_sidetoneSourceRow = nullptr;
    QComboBox *m_sidetoneSourceCombo = nullptr;
    QWidget *m_sidetoneVolumeRow = nullptr;
    QLabel *m_sidetoneHelpLabel = nullptr;
    QFormLayout *m_sidetoneForm = nullptr;
    HaliKeyPortScan *m_portScan = nullptr;
    int m_scanType = -1; // device type of the list being shown or scanned
    // WHY a poll: macOS, Windows and Linux announce no serial port arrivals to Qt (QSerialPortInfo
    // only lists), and WinMM/ALSA MIDI likewise. Only while the page is visible.
    QTimer m_rescanTimer;
};

#endif // CWKEYERPAGE_H
