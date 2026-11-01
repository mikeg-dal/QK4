#ifndef CONTROLGROUPWIDGET_H
#define CONTROLGROUPWIDGET_H

#include "utils/wheelaccumulator.h"
#include <QWidget>

/**
 * @brief Labelled +/- control with an optional AUTO toggle, used by DisplayPopupWidget (SPAN, REF,
 *        SCALE, AVERAGE, NB, WTRFALL, WTR CLRS). Emits incrementClicked / decrementClicked /
 *        autoClicked; wheel steps are accumulated via WheelAccumulator.
 */
class ControlGroupWidget : public QWidget {
    Q_OBJECT

public:
    explicit ControlGroupWidget(const QString &label, QWidget *parent = nullptr);

    void setValue(const QString &value);
    QString value() const { return m_value; }

    void setShowAutoButton(bool show);
    void setAutoEnabled(bool enabled);
    void setValueFaded(bool faded);

    QString label() const { return m_label; }

signals:
    void incrementClicked();
    void decrementClicked();
    void autoClicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    QString m_label;
    QString m_value;
    bool m_showAutoButton = false;
    bool m_autoEnabled = false;
    bool m_valueFaded = false;
    QRect m_autoRect;
    QRect m_minusRect;
    QRect m_plusRect;
    WheelAccumulator m_wheelAccumulator;
};

#endif // CONTROLGROUPWIDGET_H
