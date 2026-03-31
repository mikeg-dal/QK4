#ifndef TOGGLEGROUPWIDGET_H
#define TOGGLEGROUPWIDGET_H

#include <QWidget>

class ToggleGroupWidget : public QWidget {
    Q_OBJECT

public:
    explicit ToggleGroupWidget(const QString &leftLabel, const QString &rightLabel, QWidget *parent = nullptr);

    void setLeftSelected(bool selected);
    void setRightSelected(bool selected);
    void setRightEnabled(bool enabled);

    bool isLeftSelected() const { return m_leftSelected; }
    bool isRightSelected() const { return m_rightSelected; }
    bool isRightEnabled() const { return m_rightEnabled; }

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
    bool m_rightEnabled = true;

    // Hit test rectangles (calculated in paintEvent)
    QRect m_leftRect;
    QRect m_rightRect;
    QRect m_ampRect;
};

#endif // TOGGLEGROUPWIDGET_H
