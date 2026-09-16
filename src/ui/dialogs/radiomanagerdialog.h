#ifndef RADIOMANAGERDIALOG_H
#define RADIOMANAGERDIALOG_H

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include "settings/radiosettings.h"
#include "network/k4discovery.h"

/**
 * @brief Dialog for managing saved K4 connections. Shows the stored RadioEntry list, exposes the
 *        host/port/password/TLS-PSK fields, embeds a mDNS discovery list (K4Discovery), and the
 *        per-entry encode-mode + streaming-latency selectors. Emits connectRequested /
 *        disconnectRequested; also emits streamingLatencyChanged so MainWindow can re-send SL
 *        without reconnecting.
 */
class RadioManagerDialog : public QDialog {
    Q_OBJECT

public:
    explicit RadioManagerDialog(QWidget *parent = nullptr);

    RadioEntry selectedRadio() const;
    bool hasSelection() const;

    // Set the currently connected radio host (empty string if disconnected)
    void setConnectedHost(const QString &host);

signals:
    void connectRequested(const RadioEntry &radio);
    void disconnectRequested();
    void streamingLatencyChanged(int tier); // Emitted on save while connected

private slots:
    void onConnectClicked();
    void onNewClicked();
    void onSaveClicked();
    void onDeleteClicked();
    void onBackClicked();
    void onSelectionChanged();
    void onItemDoubleClicked(QListWidgetItem *item);
    void onStartupCheckChanged(QListWidgetItem *item);
    void onTlsCheckboxToggled(bool checked);
    void refreshList();
    void onRadioFound(const K4RadioInfo &radio);
    void onDiscoveryFinished(int count);

private:
    void setupUi();
    static QString lineEditStyle(const QString &borderColor);
    void updateButtonStates();
    void updateHostFieldStyle();
    void clearFields();
    void populateFieldsFromSelection();
    void startDiscovery();
    void addDiscoveredItem(const K4RadioInfo &radio);

    // The stored auto-connect flag of the entry being edited. Saving carries it through rather
    // than editing it, so a save cannot silently disarm the radio the list armed.
    bool startupFlagOfCurrentEntry() const;

    // Set while the list is being rebuilt. Populating check states emits itemChanged, which would
    // otherwise be read as the operator clicking every box in turn.
    bool m_populatingList = false;
    bool isAlreadyConfigured(const K4RadioInfo &radio) const;

    QListWidget *m_radioList;
    QLineEdit *m_nameEdit;
    QLineEdit *m_hostEdit;
    QLineEdit *m_portEdit;
    QLineEdit *m_passwordEdit;
    QCheckBox *m_tlsCheckbox;
    QLineEdit *m_identityEdit;
    QLabel *m_identityLabel;
    QComboBox *m_encodeModeCombo;
    QComboBox *m_streamingLatencyCombo;

    QPushButton *m_connectButton;
    QPushButton *m_newButton;
    QPushButton *m_saveButton;
    QPushButton *m_deleteButton;
    QPushButton *m_backButton;
    QPushButton *m_scanButton;
    QTimer *m_scanCountdown = nullptr;
    int m_scanSecondsLeft = 0;

    QString m_hostNormalStyle; // m_hostEdit style with normal border
    QString m_hostErrorStyle;  // m_hostEdit style with red border (invalid host)

    int m_currentIndex;
    QString m_connectedHost; // Host of currently connected radio (empty if disconnected)

    K4Discovery *m_discovery;
    QList<K4RadioInfo> m_discoveredRadios; // Unconfigured discovered radios shown in list
};

#endif // RADIOMANAGERDIALOG_H
