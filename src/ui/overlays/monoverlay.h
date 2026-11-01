#ifndef MONOVERLAY_H
#define MONOVERLAY_H

#include "ui/overlays/sidecontroloverlay.h"
#include <QLabel>

/**
 * @brief Monitor level overlay for SideControlPanel.
 *
 * Shows "MON LEVEL" with the current monitor level value.
 * Mouse wheel adjusts the value (0-100).
 * The owner sets the ML mode group (setMode); level requests carry it.
 */
class MonOverlay : public SideControlOverlay {
    Q_OBJECT

public:
    explicit MonOverlay(QWidget *parent = nullptr);

    /**
     * @brief Set the displayed monitor level value.
     * @param value Level 0-100
     */
    void setValue(int value);

    /**
     * @brief Set the mode for the ML command.
     * @param mode 0=CW, 1=Data, 2=Voice
     */
    void setMode(int mode);

    /**
     * @brief Get the current mode code.
     */
    int mode() const { return m_mode; }

    /**
     * @brief Get the current value.
     */
    int value() const { return m_value; }

signals:
    /**
     * @brief Emitted when the user scrolls to change the level.
     * @param mode The mode code (0/1/2)
     * @param newLevel The requested new level (0-100)
     */
    void levelChangeRequested(int mode, int newLevel);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void setupUi();
    void updateValueDisplay();

    QLabel *m_titleLabel;
    QLabel *m_subtitleLabel;
    QLabel *m_valueLabel;

    int m_value = 0;
    int m_mode = 0; // 0=CW, 1=Data, 2=Voice
};

#endif // MONOVERLAY_H
