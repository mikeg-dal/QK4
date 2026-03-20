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

private slots:
    void onConnectClicked();
    void onNewClicked();
    void onSaveClicked();
    void onDeleteClicked();
    void onBackClicked();
    void onSelectionChanged();
    void onItemDoubleClicked(QListWidgetItem *item);
    void onTlsCheckboxToggled(bool checked);
    void refreshList();
    void onRadioFound(const K4RadioInfo &radio);
    void onDiscoveryFinished(int count);

private:
    void setupUi();
    void updateButtonStates();
    void clearFields();
    void populateFieldsFromSelection();
    void startDiscovery();
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

    int m_currentIndex;
    QString m_connectedHost; // Host of currently connected radio (empty if disconnected)

    K4Discovery *m_discovery;
    QList<K4RadioInfo> m_discoveredRadios; // Unconfigured discovered radios shown in list
};

#endif // RADIOMANAGERDIALOG_H
