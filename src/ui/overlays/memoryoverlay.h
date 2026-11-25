#ifndef MEMORYOVERLAY_H
#define MEMORYOVERLAY_H

#include <QList>
#include <QWidget>

class MemoryModel;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

// One channel's row: "021: *60m Ch1 | A: 5.330.5 USB | B: 5.332.0 CW", with the K4's two-zone
// selection highlight — light over the number and label, dark over the frequencies. A channel the
// radio holds nothing for shows its number alone, as the radio's own list does.
class MemoryRowWidget : public QWidget {
    Q_OBJECT

public:
    // WHY the channel number and not a MemoryChannel *: the model owns its channels and clears them
    // on every disconnect. A row holding a pointer outlived them and crashed on reconnect (#124).
    MemoryRowWidget(int channel, const MemoryModel *model, QWidget *parent = nullptr);

    int channel() const { return m_channel; }
    // Edit this channel's name in place. Enter commits, Escape abandons. Only a channel the radio
    // holds something for can be named: a name on a blank channel would be stored with nothing.
    void beginRename();
    bool isRenaming() const;
    void setSelected(bool selected);
    bool isSelected() const { return m_selected; }
    void updateDisplay();

    // True when @p needle appears in this row's number, name, frequency or mode — what the
    // search field filters on. Case-insensitive.
    bool matches(const QString &needle) const;

    // "5.330.5" — the K4's grouping, megahertz then kilohertz then the hundreds digit.
    static QString formatFrequency(qint64 hz);

signals:
    void clicked();
    // A press in the name cell — the part of the row a name is written in. That is the gesture
    // that opens the editor; a press anywhere else only selects.
    void nameAreaClicked();
    void renamed(int channel, const QString &label);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void updateLabelColors();
    void endRename();
    // The x each column ends at: the seams the selection and its rules are painted at, and - for
    // the name - the edge the name-cell hit test uses, so the two can never disagree.
    int nameColumnRight() const;
    int vfoAColumnRight() const;

    int m_channel;
    const MemoryModel *m_model;
    QLabel *m_nameLabel;
    QLineEdit *m_nameEdit = nullptr;
    QLabel *m_vfoALabel;
    QLabel *m_vfoBLabel;
    bool m_selected = false;
};

// The K4's RCL and STO list screens: every channel 1-200, the selected one highlighted, over the
// panadapter area with the mode key and a back key down the right side. Emits requests only;
// MemoryController sends the CAT.
//
// The radio draws a knob icon and an "A" beside the list to say the VFO A dial scrolls it. QK4 has
// no dial, so neither is replicated — the wheel, a click and the arrow keys do the same job.
class MemoryOverlayWidget : public QWidget {
    Q_OBJECT

public:
    enum Mode { Recall, Store };

    explicit MemoryOverlayWidget(MemoryModel *model, QWidget *parent = nullptr);

    void showForMode(Mode mode);
    Mode mode() const { return m_mode; }
    int selectedChannel() const { return m_selectedChannel; }

signals:
    void recallRequested(int channel);
    void storeRequested(int channel);
    void renameRequested(int channel, const QString &label);
    void closed();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void buildRows();
    void createSearchPopup();
    // Show or hide the search box. It is a Qt::Popup under the search key, as the MENU overlay's
    // is, so both lists are searched the same way.
    void toggleSearch();
    // Hide every row whose number, name or frequency does not contain @p text.
    void applyFilter(const QString &text);
    void selectChannel(int channel, bool scrollIntoView = true);
    void step(int delta);
    void actOnSelection();
    void refreshRow(int channel);
    void refreshAllRows();

    MemoryModel *m_model; // injected, not owned
    Mode m_mode = Recall;
    int m_selectedChannel = 1;

    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_listContainer = nullptr;
    QVBoxLayout *m_listLayout = nullptr;
    QLabel *m_titleLabel = nullptr;
    QWidget *m_searchPopup = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QPushButton *m_actionBtn = nullptr;
    QPushButton *m_upBtn = nullptr;
    QPushButton *m_downBtn = nullptr;
    QPushButton *m_searchBtn = nullptr;
    QPushButton *m_backBtn = nullptr;
    QList<MemoryRowWidget *> m_rows;
};

#endif // MEMORYOVERLAY_H
