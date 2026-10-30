#ifndef RXEQPOPUPWIDGET_H
#define RXEQPOPUPWIDGET_H

#include "ui/popups/k4popupbase.h"
#include "ui/styling/k4constants.h"
#include "utils/wheelaccumulator.h"
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSlider>
#include <QWidget>

class EqPresetRowWidget;

/**
 * @brief Internal widget representing a single EQ band with slider and +/- buttons.
 *
 * Layout (vertical):
 *   [dB value label]
 *   [+ button]
 *   [vertical slider]
 *   [- button]
 *   [frequency label]
 */
class EqBandWidget : public QWidget {
    Q_OBJECT

public:
    explicit EqBandWidget(int bandIndex, const QString &freqLabel, const QString &accentColor,
                          QWidget *parent = nullptr);

    int value() const;
    void setValue(int dB);

signals:
    void valueChanged(int bandIndex, int dB);

protected:
    void wheelEvent(QWheelEvent *event) override;

private slots:
    void onPlusClicked();
    void onMinusClicked();
    void onSliderChanged(int value);

private:
    void updateValueLabel();

    int m_bandIndex;
    int m_value = 0; // -16 to +16 dB

    QLabel *m_valueLabel;
    QPushButton *m_plusBtn;
    QSlider *m_slider;
    QPushButton *m_minusBtn;
    QLabel *m_freqLabel;

    static constexpr int MIN_DB = -16;
    static constexpr int MAX_DB = 16;
    WheelAccumulator m_wheelAccumulator;
};

/**
 * @brief RX Graphic Equalizer popup with 8 frequency bands.
 *
 * Used for Main RX EQ, Sub RX EQ, and TX EQ (with different titles/colors).
 * Each band adjusts -16 to +16 dB in 1 dB steps.
 *
 * Frequency bands: 100, 200, 400, 800, 1200, 1600, 2400, 3200 Hz
 *
 * Usage:
 * @code
 * auto *popup = new RxEqPopupWidget(this);
 * connect(popup, &RxEqPopupWidget::bandValueChanged, this, [](int band, int dB) {
 *     // Send CAT command for band (0-7) with value dB (-16 to +16)
 * });
 * connect(popup, &RxEqPopupWidget::flatRequested, this, []() {
 *     // Reset all bands to 0
 * });
 * popup->showAboveWidget(openButtonRow);
 * @endcode
 */
class RxEqPopupWidget : public K4PopupBase {
    Q_OBJECT

public:
    /**
     * @brief Construct an RX EQ popup.
     *
     * @param title Popup title (e.g., "RX GRAPHIC EQUALIZER", "TX GRAPHIC EQUALIZER")
     * @param accentColor Slider accent color (e.g., K4Styles::Colors::VfoACyan)
     * @param parent Parent widget
     */
    explicit RxEqPopupWidget(const QString &title = "RX GRAPHIC EQUALIZER",
                             const QString &accentColor = K4Styles::Colors::VfoACyan, QWidget *parent = nullptr);

    /**
     * @brief Set a band's dB value.
     * @param bandIndex Band 0-7 (100Hz to 3200Hz)
     * @param dB Value -16 to +16
     */
    void setBandValue(int bandIndex, int dB);

    /**
     * @brief Get a band's current dB value.
     * @param bandIndex Band 0-7
     * @return dB value -16 to +16
     */
    int bandValue(int bandIndex) const;

    /**
     * @brief Set all bands at once.
     * @param values Array of 8 dB values
     */
    void setAllBands(const QVector<int> &values);

    /**
     * @brief Reset all bands to 0 dB (flat response).
     */
    void resetToFlat();

signals:
    /**
     * @brief Emitted when a band value changes.
     * @param bandIndex Band 0-7
     * @param dB New value -16 to +16
     */
    void bandValueChanged(int bandIndex, int dB);

    /**
     * @brief Emitted when FLAT button is clicked.
     */
    void flatRequested();

    /**
     * @brief Emitted when a preset is clicked for loading.
     * @param index Preset index 0-3
     */
    void presetLoadRequested(int index);

    /**
     * @brief Emitted when save button is clicked for a preset slot.
     * @param index Preset index 0-3
     */
    void presetSaveRequested(int index);

    /**
     * @brief Emitted when clear is requested via right-click menu.
     * @param index Preset index 0-3
     */
    void presetClearRequested(int index);

protected:
    QSize contentSize() const override;

public slots:
    /**
     * @brief Update a preset button's display name.
     * Call this after loading presets from RadioSettings.
     * @param index Preset index 0-3
     * @param name Display name (empty for "---")
     */
    void updatePresetName(int index, const QString &name);

private slots:
    void onBandValueChanged(int bandIndex, int dB);
    void onFlatClicked();
    void onPresetLoadClicked(int index);
    void onPresetSaveClicked(int index);
    void onPresetClearClicked(int index);

private:
    void setupUi(const QString &title, const QString &accentColor);

    QVector<EqBandWidget *> m_bands;
    QPushButton *m_flatBtn;
    QVector<EqPresetRowWidget *> m_presetRows;

    // Frequency labels for the 8 bands
    static constexpr const char *FREQ_LABELS[8] = {"100", "200", "400", "800", "1200", "1600", "2400", "3200"};
};

/**
 * @brief Internal widget representing a single preset row with load and save buttons.
 *
 * Layout (horizontal):
 *   [Preset Name Button] [S]
 */
class EqPresetRowWidget : public QWidget {
    Q_OBJECT

public:
    explicit EqPresetRowWidget(int presetIndex, QWidget *parent = nullptr);

    void setPresetName(const QString &name);
    QString presetName() const { return m_name; }
    bool hasPreset() const { return !m_name.isEmpty(); }

signals:
    void loadClicked(int index);
    void saveClicked(int index);
    void clearRequested(int index);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;

private slots:
    void onLoadClicked();
    void onSaveClicked();

private:
    int m_presetIndex;
    QString m_name;
    QPushButton *m_loadBtn;
    QPushButton *m_saveBtn;
};

#endif // RXEQPOPUPWIDGET_H
