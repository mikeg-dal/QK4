#ifndef RXEQPOPUPWIDGET_H
#define RXEQPOPUPWIDGET_H

#include "ui/popups/k4popupbase.h"
#include "ui/styling/k4constants.h"
#include "utils/wheelaccumulator.h"
#include <QColor>
#include <QPushButton>
#include <QRect>
#include <QString>
#include <QVector>
#include <QWidget>

class EqPresetRowWidget;

/**
 * @brief One band's column, drawn the way the K4 draws it.
 *
 * Top to bottom: the dB value, then a track whose two ends are the + and - keys and whose fill is
 * grey above the grip and the popup's accent below it, then the frequency key.
 *
 * WHY it paints itself instead of holding a QLabel, a QSlider and two QPushButtons: a popup is a
 * translucent window, which Qt never erases between repaints (see K4PopupBase::contentPanel()), so
 * a child that leaves any of its own rect unpainted leaves a seam that builds up. The old column's
 * value QLabel had no background at all and its QSlider sheet left ~10 px unpainted either side of
 * the groove; those were the rectangles and the vertical line seen on the bench. This widget
 * declares Qt::WA_OpaquePaintEvent and fills every pixel it covers.
 *
 * The frequency key lights for the band last touched. The radio lights it to say which band its
 * VFO A dial drives; here it says which band the wheel drives, so the highlight keeps a job on a
 * client with no dial.
 */
class EqBandWidget : public QWidget {
    Q_OBJECT

public:
    EqBandWidget(int bandIndex, const QString &freqLabel, const QString &accentColor, QWidget *parent = nullptr);

    /// The column's fixed size, from the Dimensions::Eq* tokens. Used to size the popup.
    static QSize columnSize();

    int value() const { return m_value; }
    void setValue(int dB);

    bool isSelected() const { return m_selected; }
    void setSelected(bool selected);

signals:
    void valueChanged(int bandIndex, int dB);
    /// This band was touched - the wheel now drives it, and its frequency key lights.
    void selectionRequested(int bandIndex);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    // The column's zones, all derived from the tokens so painting and hit testing cannot disagree.
    QRect trackRect() const;
    QRect plusCapRect() const;
    QRect minusCapRect() const;
    QRect bodyRect() const; // between the two caps: where the grip travels
    QRect handleRect() const;
    QRect keyRect() const;

    /// The value a grip centred on @p y would read, clamped to the range.
    int valueFromY(int y) const;
    /// Apply @p dB, repaint and emit valueChanged() if it moved. Always selects this band.
    void applyValue(int dB);

    int m_bandIndex;
    int m_value = 0; // -16 to +16 dB
    QString m_freqLabel;
    QColor m_accent;
    bool m_selected = false;
    bool m_dragging = false;
    WheelAccumulator m_wheelAccumulator;

    static constexpr int MIN_DB = -16;
    static constexpr int MAX_DB = 16;
};

/**
 * @brief RX / TX graphic equalizer popup: eight bands, our four presets and FLAT.
 *
 * Two instances exist (RX cyan, TX amber). Each band adjusts -16 to +16 dB in 1 dB steps over the
 * K4's own frequencies: 100, 200, 400, 800, 1200, 1600, 2400, 3200 Hz.
 *
 * The title band, the dB / Hz units column and the rule before the preset column are painted by
 * paintContent() on the content panel rather than built as child widgets: they carry no behaviour,
 * and a painted decoration cannot be the kind of transparent child this popup used to leave seams
 * around.
 */
class RxEqPopupWidget : public K4PopupBase {
    Q_OBJECT

public:
    explicit RxEqPopupWidget(const QString &title = "RX GRAPHIC EQUALIZER",
                             const QString &accentColor = K4Styles::Colors::VfoACyan, QWidget *parent = nullptr);

    /// @param values up to 8 dB values, in band order.
    void setAllBands(const QVector<int> &values);

    /// All eight bands to 0 dB. Emits nothing: FLAT's own handler sends the CAT.
    void resetToFlat();

signals:
    void bandValueChanged(int bandIndex, int dB); ///< band 0-7, -16..+16 dB
    void flatRequested();
    void presetLoadRequested(int index);  ///< preset 0-3
    void presetSaveRequested(int index);  ///< preset 0-3
    void presetClearRequested(int index); ///< preset 0-3, from the right-click menu

protected:
    QSize contentSize() const override;
    void paintContent(QPainter &painter, const QRect &panelRect) override;

public slots:
    /// Set a preset key's name; empty shows "---". Call after loading presets from RadioSettings.
    void updatePresetName(int index, const QString &name);

private slots:
    void onFlatClicked();

private:
    void setupUi(const QString &accentColor);
    void selectBand(int bandIndex);

    QString m_title;
    QVector<EqBandWidget *> m_bands;
    QPushButton *m_flatBtn = nullptr;
    QVector<EqPresetRowWidget *> m_presetRows;
    int m_selectedBand = 0;

    static constexpr const char *FREQ_LABELS[8] = {"100", "200", "400", "800", "1200", "1600", "2400", "3200"};
};

/**
 * @brief One preset slot: its name key, which loads it, and a square S key, which saves over it.
 *
 * QK4's own addition to the K4's screen; right-clicking a filled slot offers to clear it.
 */
class EqPresetRowWidget : public QWidget {
    Q_OBJECT

public:
    explicit EqPresetRowWidget(int presetIndex, QWidget *parent = nullptr);

    void setPresetName(const QString &name);
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
