#ifndef DISPLAYMENUBUTTON_H
#define DISPLAYMENUBUTTON_H

#include <QWidget>

class DisplayMenuButton : public QWidget {
    Q_OBJECT

public:
    explicit DisplayMenuButton(const QString &primaryText, const QString &alternateText, QWidget *parent = nullptr);

    void setSelected(bool selected);
    bool isSelected() const { return m_selected; }

    QString primaryText() const { return m_primaryText; }
    QString alternateText() const { return m_alternateText; }

    void setPrimaryText(const QString &text);
    void setAlternateText(const QString &text);

signals:
    void clicked();
    void rightClicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QString m_primaryText;
    QString m_alternateText;
    bool m_selected = false;
    bool m_hovered = false;
};

#endif // DISPLAYMENUBUTTON_H
