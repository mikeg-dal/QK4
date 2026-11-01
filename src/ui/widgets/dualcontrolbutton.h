#ifndef DUALCONTROLBUTTON_H
#define DUALCONTROLBUTTON_H

#include <QWidget>
#include <QString>
#include <QColor>
#include "utils/wheelaccumulator.h"

/**
 * DualControlButton - A dual-function control button for QK4
 *
 * Features:
 * - Primary value (white text) - changes on scroll/drag when active
 * - Alternate function (yellow/amber text) - shown below, clicking swaps them
 * - Colored indicator bar on left edge - only shown when button is active in group
 *
 * Behavior:
 * - Click: Swaps primary and alternate labels/values
 * - Scroll (when active): Changes the primary value
 * - Only one button per group shows the indicator bar (is "active")
 *
 * Usage:
 *   auto *wpmBtn = new DualControlButton(parent);
 *   wpmBtn->setPrimaryLabel("WPM");
 *   wpmBtn->setPrimaryValue("15");
 *   wpmBtn->setAlternateLabel("PTCH");
 *   wpmBtn->setAlternateValue("600");
 *   wpmBtn->setContext(DualControlButton::Global);
 *   wpmBtn->setShowIndicator(true);  // Only one per group
 */
/**
 * @brief Side-panel dual-function control button (see docstring above for behavior). Carries a
 *        Context-colored indicator bar (Global/MainRx/SubRx) so users can see which receiver the
 *        button's active function targets.
 */
class DualControlButton : public QWidget {
    Q_OBJECT

public:
    enum Context {
        Global, // Orange bar - global settings like WPM, PWR
        MainRx, // Cyan bar - Main receiver settings
        SubRx   // Green bar - Sub receiver settings
    };

    explicit DualControlButton(QWidget *parent = nullptr);
    ~DualControlButton() = default;

    // Two lines of text, measured in the fonts they are painted in, plus the knob insets and line gap.
    static int measuredHeight(const QWidget *context);

    // Primary function (white text - top line)
    void setPrimaryLabel(const QString &label);
    void setPrimaryValue(const QString &value);
    QString primaryLabel() const { return m_primaryLabel; }

    // Alternate function (yellow/amber text - bottom line)
    void setAlternateLabel(const QString &label);
    void setAlternateValue(const QString &value);
    QString alternateLabel() const { return m_alternateLabel; }
    QString alternateValue() const { return m_alternateValue; }

    // Context indicator bar color
    void setContext(Context context);
    Context context() const { return m_context; }

    // Show/hide the indicator bar (only one per group should show it)
    void setShowIndicator(bool show);

    // Swap primary and alternate labels/values
    void swapFunctions();

signals:
    void valueScrolled(int delta); // Scroll wheel changed value (only when indicator shown)
    void swapped();                // Primary/alternate were swapped (only when already active)
    void becameActive();           // User clicked to activate this button

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QColor contextColor() const;
    static QFont labelFont(const QWidget *context);
    static QFont valueFont(const QWidget *context);
    static QFont altFont(const QWidget *context);

    QString m_primaryLabel;   // e.g., "WPM", "BW", "M.RF"
    QString m_primaryValue;   // e.g., "15", "0.40", "0"
    QString m_alternateLabel; // e.g., "PTCH", "HI", "M.SQL"
    QString m_alternateValue; // e.g., "600", "2800", "0"
    Context m_context = MainRx;
    bool m_showIndicator = true; // Show the colored bar (active in group)
    bool m_isHovered = false;
    WheelAccumulator m_wheelAccumulator;
};

#endif // DUALCONTROLBUTTON_H
