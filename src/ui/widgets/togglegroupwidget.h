#ifndef TOGGLEGROUPWIDGET_H
#define TOGGLEGROUPWIDGET_H

#include <QWidget>

/**
 * @brief Pair-toggle widget (e.g., APF on/off + bandwidth). Renders two mutually-exclusive
 *        buttons with an "&" glyph between them; third signal `bothClicked` fires when the
 *        region between buttons is clicked.
 */
class ToggleGroupWidget : public QWidget {
    Q_OBJECT

public:
    explicit ToggleGroupWidget(const QString &leftLabel, const QString &rightLabel, QWidget *parent = nullptr);

    void setLeftSelected(bool selected);
    void setRightSelected(bool selected);

signals:
    void leftClicked();
    void rightClicked();
    void bothClicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QString m_leftLabel;
    QString m_rightLabel;
    bool m_leftSelected = true;
    bool m_rightSelected = false;

    // Hit test rectangles (calculated in paintEvent)
    QRect m_leftRect;
    QRect m_rightRect;
    QRect m_ampRect;
};

#endif // TOGGLEGROUPWIDGET_H
