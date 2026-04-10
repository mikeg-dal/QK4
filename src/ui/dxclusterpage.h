#ifndef DXCLUSTERPAGE_H
#define DXCLUSTERPAGE_H

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QWidget>

class DxClusterController;

class DxClusterPage : public QWidget {
    Q_OBJECT

public:
    explicit DxClusterPage(DxClusterController *controller, QWidget *parent = nullptr);

    void refresh();

private:
    void populateClusterList();
    void updateListIndicators();
    void loadSelectedEntry();
    void enterAddMode();
    void saveEntry();
    void removeEntry();
    void updateStatus();
    void updateFormState();
    void switchConsole(int index);

    DxClusterController *m_controller;

    // Left: cluster list
    QListWidget *m_clusterList = nullptr;

    // Right: form fields (per-cluster)
    QLineEdit *m_hostEdit = nullptr;
    QLineEdit *m_portEdit = nullptr;
    QCheckBox *m_autoConnectCheck = nullptr;

    // Action buttons
    QPushButton *m_newBtn = nullptr;
    QPushButton *m_saveBtn = nullptr;
    QPushButton *m_removeBtn = nullptr;
    QPushButton *m_connectBtn = nullptr;
    QPushButton *m_disconnectBtn = nullptr;

    // Status
    QLabel *m_statusLabel = nullptr;
    QLabel *m_formTitleLabel = nullptr;

    // Cluster Settings (global)
    QLineEdit *m_callsignEdit = nullptr;
    QSlider *m_ageSlider = nullptr;
    QLabel *m_ageValueLabel = nullptr;

    // Console
    QPlainTextEdit *m_consoleOutput = nullptr;
    QLineEdit *m_consoleInput = nullptr;

    bool m_addMode = false;
};

#endif // DXCLUSTERPAGE_H
