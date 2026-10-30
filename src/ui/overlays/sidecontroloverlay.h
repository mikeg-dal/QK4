#ifndef SIDECONTROLOVERLAY_H
#define SIDECONTROLOVERLAY_H

#include <QWidget>
#include "utils/wheelaccumulator.h"

/**
 * @brief Base class for overlay panels in SideControlPanel.
 *
 * These overlays appear over existing DualControlButton groups when
 * activated (e.g., MON overlay over WPM/PWR, BAL overlay over RF/SQL).
 *
 * Features:
 * - Colored indicator bar (orange for Global, cyan for MainRx, green for SubRx)
 * - Dark background with rounded corners
 * - Mouse wheel handling for value adjustment
 * - Closed by pressing its key again, the other overlay's key, or the disconnect reset
 */
class SideControlOverlay : public QWidget {
    Q_OBJECT

public:
    enum Context { Global, MainRx, SubRx };

    explicit SideControlOverlay(Context ctx, QWidget *parent = nullptr);
    virtual ~SideControlOverlay() = default;

    /**
     * @brief Show the overlay positioned over the given widgets.
     * @param topWidget The top button/widget to cover
     * @param bottomWidget The bottom button/widget to cover
     */
    void showOverGroup(QWidget *topWidget, QWidget *bottomWidget);

    /**
     * @brief Get the context (color theme) of this overlay.
     */
    Context context() const { return m_context; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

    /**
     * @brief Get the color for the indicator bar based on context.
     */
    QColor indicatorColor() const;

    /**
     * @brief Width of the indicator bar on the left side.
     */
    static constexpr int IndicatorBarWidth = 5;

    /**
     * @brief Corner radius for the overlay background.
     */
    static constexpr int CornerRadius = 6;

    WheelAccumulator m_wheelAccumulator;

private:
    Context m_context;
};

#endif // SIDECONTROLOVERLAY_H
