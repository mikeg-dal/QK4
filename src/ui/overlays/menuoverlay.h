#ifndef MENUOVERLAY_H
#define MENUOVERLAY_H

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QList>
#include <QWheelEvent>
#include "models/menumodel.h"
#include "utils/wheelaccumulator.h"

/**
 * @brief Single row inside MenuOverlayWidget — name, current value, optional lock icon. Paints
 *        its own K4-style dual-panel selection highlight (light column + dark value column).
 */
class MenuItemWidget : public QWidget {
    Q_OBJECT

public:
    // model is used to resolve "<n>" placeholders in item->name (XVTR Band rows).
    explicit MenuItemWidget(MenuItem *item, const MenuModel *model, QWidget *parent = nullptr);

    void setSelected(bool selected);
    void setEditMode(bool editing);
    bool isSelected() const { return m_selected; }
    bool isEditing() const { return m_editing; }
    MenuItem *menuItem() const { return m_item; }
    void updateDisplay();

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void updateLabelColors();

    MenuItem *m_item;
    const MenuModel *m_model;
    QLabel *m_nameLabel;
    QLabel *m_valueLabel;
    QLabel *m_lockLabel;
    bool m_selected = false;
    bool m_editing = false;
};

/**
 * @brief Full-screen MENU overlay backing the K4's MN menu system. Driven by MenuModel; emits
 *        `menuValueChangeRequested(menuId, "+"/"-"/"/")` back to MainWindow for CAT dispatch.
 *        Implements scroll, search, and the dual-panel K4 highlight style.
 */
class MenuOverlayWidget : public QWidget {
    Q_OBJECT

public:
    explicit MenuOverlayWidget(MenuModel *model, QWidget *parent = nullptr);

    void show();
    void hide();
    void refresh();

signals:
    void closed();
    void menuValueChangeRequested(int menuId, const QString &action); // "+", "-", "/"

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void navigateUp();
    void navigateDown();
    void selectCurrent();
    void closeOverlay();
    void resetToDefault();
    void onMenuValueChanged(int menuId, int newValue);
    void toggleSearchPopup();
    void onSearchTextChanged(const QString &text);

private:
    void setupUi();
    void createSearchPopup();
    void populateItems();
    // Destroy the row widgets and forget the selection. Called before repopulating, and when the
    // model clears — each row holds a MenuItem * that the clear has just invalidated.
    void clearItemWidgets();
    void updateSelection();
    void ensureSelectedVisible();
    void setEditMode(bool editing);
    void updateButtonLabels();
    void updateNormButton();

    MenuModel *m_model;
    QWidget *m_contentWidget;
    QScrollArea *m_scrollArea;
    QWidget *m_listContainer;
    QVBoxLayout *m_listLayout;
    QLabel *m_categoryLabel;

    QList<MenuItemWidget *> m_itemWidgets;
    int m_selectedIndex = 0;
    bool m_editMode = false;

    // Navigation buttons
    QPushButton *m_upBtn;
    QPushButton *m_downBtn;
    QPushButton *m_searchBtn;
    QPushButton *m_normBtn;
    QPushButton *m_backBtn;

    // Search functionality
    QWidget *m_searchPopup;
    QLineEdit *m_searchInput;
    QString m_currentFilter;
    WheelAccumulator m_wheelAccumulator;
};

#endif // MENUOVERLAY_H
