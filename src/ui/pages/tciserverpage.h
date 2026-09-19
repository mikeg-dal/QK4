#ifndef TCISERVERPAGE_H
#define TCISERVERPAGE_H

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QSlider>
#include <QTableWidget>
#include <QVector>
#include <QWidget>

#include "network/tciclientinfo.h"

class AudioController;
class TciController;

/**
 * @brief OptionsDialog "TCI Server" tab. Enables/disables the TCI WebSocket server, configures its
 *        listen port, switches the audio half on and off independently of CAT, and shows live
 *        status and client count.
 *
 * Deliberately mirrors RigControlPage's CAT Server panel: same status/clients/port/enable shape,
 * because the two servers do the same job for different clients and should not look different.
 */
class TciServerPage : public QWidget {
    Q_OBJECT

public:
    // audioController is needed for the TCI transmit level, which reaches the encoder the same way
    // Mic Gain does from the Audio Input page: write the setting, then tell the controller.
    explicit TciServerPage(TciController *tciController, AudioController *audioController, QWidget *parent = nullptr);

    void refresh();

private:
    void updateStatus();
    void onTciTxGainChanged(int value);

    // Repaints the client table from a roster. Takes the roster by argument rather than pulling it
    // from the controller so the signal path and the manual path are the same code.
    void updateClients(const QVector<TciClientInfo> &clients);

    TciController *m_tciController;
    AudioController *m_audioController;
    QCheckBox *m_enableCheckbox = nullptr;
    QCheckBox *m_audioCheckbox = nullptr;
    QLineEdit *m_portEdit = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_clientsLabel = nullptr;
    QTableWidget *m_clientsTable = nullptr;
    QSlider *m_txGainSlider = nullptr;
    QLabel *m_txGainValueLabel = nullptr;
};

#endif // TCISERVERPAGE_H
