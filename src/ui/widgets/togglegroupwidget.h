#ifndef TOGGLEGROUPWIDGET_H
#define TOGGLEGROUPWIDGET_H

#include <QWidget>

/**
 * @brief Two-way selector with a third "&" key between the sides (DISPLAY popup's LCD / EXT and
 *        A / B). Emits leftClicked, rightClicked, or bothClicked for "&".
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
