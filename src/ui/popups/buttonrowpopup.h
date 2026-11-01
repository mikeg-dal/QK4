#ifndef BUTTONROWPOPUP_H
#define BUTTONROWPOPUP_H

#include "ui/popups/k4popupbase.h"
#include <QList>

/**
 * RxMenuButton - Dual-line button with white primary text and amber secondary text.
 * Similar to FnMenuButton but for the MAIN RX / SUB RX / TX row buttons.
 */
class RxMenuButton : public QWidget {
    Q_OBJECT

public:
    explicit RxMenuButton(const QString &primaryText, const QString &alternateText = QString(),
                          QWidget *parent = nullptr);

    void setPrimaryText(const QString &text);
    void setAlternateText(const QString &text);
    void setHasAlternateFunction(bool has); // If false, alternate text is white (just label), not amber
    bool hasAlternateFunction() const { return m_hasAlternateFunction; }

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
    bool m_hovered = false;
    bool m_hasAlternateFunction = true; // If true, alternate text is amber; if false, white
};

/**
 * ButtonRowPopup - A single-row popup menu with 7 dual-line buttons
 *
 * Based on K4PopupBase for consistent popup behavior.
 * Used for MAIN RX, SUB RX, and TX bottom menu popups.
 * Features:
 * - 7 buttons in a horizontal row with primary/alternate text
 * - Customizable button labels (primary white, alternate amber)
 */
class ButtonRowPopup : public K4PopupBase {
    Q_OBJECT

public:
    explicit ButtonRowPopup(QWidget *parent = nullptr);

    // Configure individual button (primary and alternate text)
    // If hasAlternateFunction is false, alternate text is white (just a label, no right-click function)
    void setButtonLabel(int index, const QString &primary, const QString &alternate = QString(),
                        bool hasAlternateFunction = true);

signals:
    void buttonClicked(int index);      // Emits 0-6 for left-click
    void buttonRightClicked(int index); // Emits 0-6 for right-click

protected:
    QSize contentSize() const override;

private:
    void setupUi();

    QList<RxMenuButton *> m_buttons; // 7 buttons
};

#endif // BUTTONROWPOPUP_H
