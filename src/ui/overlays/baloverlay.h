#ifndef BALOVERLAY_H
#define BALOVERLAY_H

#include "ui/overlays/sidecontroloverlay.h"
#include <QLabel>
#include <QPushButton>

/**
 * @brief Sub AF balance overlay for SideControlPanel.
 *
 * Shows SUB AF balance mode (NOR/BAL) with MAIN and SUB values.
 * Emits balance requests; the owner turns them into BL.
 */
class BalOverlay : public SideControlOverlay {
    Q_OBJECT

public:
    explicit BalOverlay(QWidget *parent = nullptr);

    /**
     * @brief Set the balance state from radio.
     * @param mode 0=NOR, 1=BAL
     * @param offset -50 to +50 (MAIN = 50 - offset, SUB = 50 + offset)
     */
    void setBalance(int mode, int offset);

signals:
    /**
     * @brief Emitted when the user scrolls to change balance.
     * @param mode Current mode (0=NOR, 1=BAL)
     * @param offset New absolute offset (-50 to +50)
     */
    void balanceChangeRequested(int mode, int offset);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void setupUi();
    void updateDisplay();

    QPushButton *m_modeBtn;
    QLabel *m_mainLabel;
    QLabel *m_subLabel;

    int m_mode = 0;   // 0=NOR, 1=BAL
    int m_offset = 0; // -50 to +50
};

#endif // BALOVERLAY_H
