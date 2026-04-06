#ifndef FNPOPUPWIDGET_H
#define FNPOPUPWIDGET_H

#include "k4popupbase.h"
#include "../models/macroids.h"
#include <QVector>

/**
 * @brief Dual-action button for Fn popup.
 *
 * Similar to DisplayMenuButton - shows primary text (white) on top
 * and alternate text (amber) on bottom. Left-click triggers primary
 * action, right-click triggers secondary.
 */
class FnMenuButton : public QWidget {
    Q_OBJECT

public:
    explicit FnMenuButton(const QString &primaryText, const QString &alternateText, QWidget *parent = nullptr);

    void setPrimaryText(const QString &text);
    void setAlternateText(const QString &text);
    QString primaryText() const { return m_primaryText; }
    QString alternateText() const { return m_alternateText; }

    void setPrimaryFunctionId(const QString &id) { m_primaryFunctionId = id; }
    void setAlternateFunctionId(const QString &id) { m_alternateFunctionId = id; }
    QString primaryFunctionId() const { return m_primaryFunctionId; }
    QString alternateFunctionId() const { return m_alternateFunctionId; }

signals:
    void clicked();      // Left click -> primary action
    void rightClicked(); // Right click -> secondary action

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QString m_primaryText;
    QString m_alternateText;
    QString m_primaryFunctionId;
    QString m_alternateFunctionId;
    bool m_hovered = false;
};

/**
 * @brief Fn popup widget with 7 dual-action buttons.
 *
 * Layout:
 * Buttons 1-4: Fn.F1/F2, F3/F4, F5/F6, F7/F8 (macro buttons)
 * Button 5: SCRN CAP / MACROS
 * Button 6: SW LIST / UPDATE
 * Button 7: DXLIST
 */
class FnPopupWidget : public K4PopupBase {
    Q_OBJECT

public:
    explicit FnPopupWidget(QWidget *parent = nullptr);

    // Update button labels from macro settings
    void updateButtonLabels();

signals:
    void functionTriggered(const QString &functionId);

protected:
    QSize contentSize() const override;

private:
    void setupButtons();
    void onButtonClicked(int buttonIndex);
    void onButtonRightClicked(int buttonIndex);

    QVector<FnMenuButton *> m_buttons;
};

#endif // FNPOPUPWIDGET_H
