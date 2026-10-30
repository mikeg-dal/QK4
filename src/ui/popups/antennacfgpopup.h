#ifndef ANTENNACFGPOPUP_H
#define ANTENNACFGPOPUP_H

#include "ui/popups/k4popupbase.h"
#include <QLabel>
#include <QPushButton>
#include <QVector>

/**
 * @brief Variant type for antenna configuration popup.
 *
 * MainRx and SubRx have 7 antennas (ANT1-3, RX1-2, =TX ANT, =OPP TX ANT).
 * Tx has 3 antennas (TX ANT1-3).
 */
enum class AntennaCfgVariant { MainRx, SubRx, Tx };

/**
 * @brief Antenna cycling configuration popup.
 *
 * Allows user to select DISPLAY ALL or USE SUBSET mode,
 * and when USE SUBSET is selected, choose which antennas
 * are included in the cycling rotation.
 *
 * CAT Commands:
 * - ACM (Main RX): ACMzabcdefg where z=mode, a-g=antenna enables
 * - ACS (Sub RX): ACSzabcdefg (same format)
 * - ACT (TX): ACTzabc where z=mode, a-c=antenna enables
 *
 * Usage:
 * @code
 * auto *popup = new AntennaCfgPopupWidget(AntennaCfgVariant::MainRx, this);
 * connect(popup, &AntennaCfgPopupWidget::configChanged, this, [](bool displayAll, QVector<bool> mask) {
 *     // Build and send ACM command
 * });
 * popup->showAboveWidget(openButtonRow);
 * @endcode
 */
class AntennaCfgPopupWidget : public K4PopupBase {
    Q_OBJECT

public:
    explicit AntennaCfgPopupWidget(AntennaCfgVariant variant, QWidget *parent = nullptr);

    /**
     * @brief Set the DISPLAY ALL / USE SUBSET mode.
     * @param displayAll true for DISPLAY ALL, false for USE SUBSET
     */
    void setDisplayAll(bool displayAll);

    /**
     * @brief Set an individual antenna enable state.
     * @param index Antenna index (0-6 for RX, 0-2 for TX)
     * @param enabled Whether antenna is in the cycle
     */
    void setAntennaEnabled(int index, bool enabled);

    /**
     * @brief Set all antenna states at once.
     * @param mask Vector of enable states
     */
    void setAntennaMask(const QVector<bool> &mask);

    /**
     * @brief Set antenna name label (for ANT1-3 only).
     * @param index Antenna index (0-2)
     * @param name Custom name from ACN command
     */
    void setAntennaName(int index, const QString &name);

    /**
     * @brief Get current display all mode.
     */
    bool displayAll() const { return m_displayAll; }

    /**
     * @brief Get current antenna mask.
     */
    QVector<bool> antennaMask() const;

signals:
    /**
     * @brief Emitted when configuration changes.
     * @param displayAll Current mode (true=DISPLAY ALL, false=USE SUBSET)
     * @param mask Current antenna enable mask
     */
    void configChanged(bool displayAll, QVector<bool> mask);

protected:
    QSize contentSize() const override;
    void paintContent(QPainter &painter, const QRect &contentRect) override;

private slots:
    void onDisplayAllClicked();
    void onUseSubsetClicked();
    void onCheckboxToggled(int index);

private:
    void setupUi();
    void updateCheckboxStates();
    void emitConfigChanged();

    AntennaCfgVariant m_variant;
    bool m_displayAll = true;
    int m_antennaCount;

    QPushButton *m_displayAllBtn;
    QPushButton *m_useSubsetBtn;
    QVector<QPushButton *> m_checkboxes;
    QVector<QLabel *> m_labels;
    QLabel *m_noteLabel = nullptr; // RX variants only; drives the bracket-line paint

    // Default antenna labels
    static constexpr const char *RX_LABELS[7] = {"ANT1", "ANT2", "ANT3", "RX1", "RX2", "=TX\nANT", "=OPP\nTX ANT"};
    static constexpr const char *TX_LABELS[3] = {"ANT1", "ANT2", "ANT3"};
};

#endif // ANTENNACFGPOPUP_H
