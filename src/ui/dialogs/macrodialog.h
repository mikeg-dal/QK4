#ifndef MACRODIALOG_H
#define MACRODIALOG_H

#include <QLineEdit>
#include <QList>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>
#include "utils/wheelaccumulator.h"

// Forward declarations
class QLabel;
class QPushButton;

// Single row widget for a macro entry (3 columns: Function | Label | Command)
class MacroItemWidget : public QWidget {
    Q_OBJECT

public:
    explicit MacroItemWidget(const QString &functionId, const QString &displayName, QWidget *parent = nullptr);

    QString functionId() const { return m_functionId; }
    QString label() const;
    QString command() const;

    void setLabel(const QString &label);
    void setCommand(const QString &command);
    void setSelected(bool selected);
    bool isSelected() const { return m_selected; }

    // Enter edit mode for label or command
    void editLabel();
    void editCommand();
    void finishEditing();

signals:
    void clicked();        // Click on function/label columns
    void commandClicked(); // Click on command column
    void labelChanged(const QString &functionId, const QString &label);
    void commandChanged(const QString &functionId, const QString &command);
    void fieldSwitchRequested(); // Tab/Shift+Tab pressed in the label or command editor

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updateDisplay();

    QString m_functionId;
    QString m_label;
    QString m_command;
    bool m_selected = false;

    QLabel *m_functionLabel;
    QLabel *m_labelDisplay;
    QLineEdit *m_labelEdit;
    QLabel *m_commandDisplay;
    QLineEdit *m_commandEdit;
};

// Macro editor overlay; PopupManager sizes it to the spectrum area
class MacroDialog : public QWidget {
    Q_OBJECT

public:
    explicit MacroDialog(QWidget *parent = nullptr);

    void show();
    void hide();
    void loadFromSettings();

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void navigateUp();
    void navigateDown();
    void selectCurrent();
    void closeDialog();
    void onLabelChanged(const QString &functionId, const QString &label);
    void onCommandChanged(const QString &functionId, const QString &command);

private:
    void setupUi();
    void populateItems();
    void updateSelection();
    void ensureSelectedVisible();
    void setEditMode(bool editing, bool editCommand = false);

    QWidget *m_contentWidget;
    QScrollArea *m_scrollArea;
    QWidget *m_listContainer;
    QVBoxLayout *m_listLayout;
    QLabel *m_headerLabel;

    QList<MacroItemWidget *> m_itemWidgets;
    int m_selectedIndex = 0;
    bool m_editMode = false;
    bool m_editingCommand = false; // true = editing command, false = editing label

    // Navigation buttons
    QPushButton *m_upBtn;
    QPushButton *m_downBtn;
    QPushButton *m_editBtn;
    QPushButton *m_backBtn;
    WheelAccumulator m_wheelAccumulator;
};

#endif // MACRODIALOG_H
