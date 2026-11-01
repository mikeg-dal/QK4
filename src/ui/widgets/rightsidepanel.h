#ifndef RIGHTSIDEPANEL_H
#define RIGHTSIDEPANEL_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

class Kpa1500MiniPanel;

/**
 * RightSidePanel - Right-side vertical panel for QK4
 *
 * Contains button grids:
 *
 * 5×2 grid (main functions):
 * - Row 0: PRE/ATTN, NB/LEVEL
 * - Row 1: NR/ADJ, NTCH/MANUAL
 * - Row 2: FIL/APF, A/B/SPLIT
 * - Row 3: REV, A->B/B->A
 * - Row 4: SPOT/AUTO, MODE/ALT
 *
 * 2×2 grid (PF buttons):
 * - Row 0: B SET/PF 1, CLR/PF 2
 * - Row 1: RIT/PF 3, XIT/PF 4
 *
 * 2×2 grid (bottom functions):
 * - Row 0: FREQ ENT/SCAN, RATE/KHZ
 * - Row 1: LOCK A/LOCK B, SUB/DIVERSITY
 *
 * Dimensions:
 * - Fixed width: 105px (matches left panel)
 * - Margins: 6, 8, 6, 8
 * - Spacing: 4
 */
class RightSidePanel : public QWidget {
    Q_OBJECT

public:
    explicit RightSidePanel(QWidget *parent = nullptr);
    ~RightSidePanel() = default;

    // Drop a KPA1500 mini panel into the reserved slot of this panel's
    // vertical layout. RightSidePanel takes Qt parent ownership but
    // KPA1500UiController constructs the widget and drives its state —
    // this method exists so the panel layout's exact slot ordering stays
    // here, not in the controller. Single-shot — calling twice is a
    // programming error.
    void embedKpa1500Panel(Kpa1500MiniPanel *panel);

    // With the KPA1500 enabled the panel is wide enough for its meters and keeps the card's height
    // while the card is hidden, so the window is sized for it from launch; disabled, the panel is as
    // narrow as the left one and spends no height on a card that never appears.
    void setKpa1500SlotReserved(bool reserved);

signals:
    // Button click signals (main function - left click)
    void preClicked();
    void nbClicked();
    void nrClicked();
    void ntchClicked();
    void filClicked();
    void abClicked();
    void revPressed();
    void revReleased();
    void atobClicked();
    void spotClicked();
    void modeClicked();

    // Secondary signals (right-click on main 5x2 grid)
    void attnClicked();   // PRE right-click
    void levelClicked();  // NB right-click
    void adjClicked();    // NR right-click
    void manualClicked(); // NTCH right-click
    void apfClicked();    // FIL right-click
    void splitClicked();  // A/B right-click
    // REV right-click - TBD
    void btoaClicked(); // A→B right-click
    void autoClicked(); // SPOT right-click
    void altClicked();  // MODE right-click

    // PF row signals (left click)
    void bsetClicked();
    void clrClicked();
    void ritClicked();
    void xitClicked();

    // PF row secondary signals (right-click)
    void pf1Clicked(); // B SET right-click
    void pf2Clicked(); // CLR right-click
    void pf3Clicked(); // RIT right-click
    void pf4Clicked(); // XIT right-click

    // Bottom row signals (left click)
    void freqEntClicked();
    void rateClicked();
    void lockAClicked();
    void subClicked();

    // Bottom row secondary signals (right-click)
    void khzClicked();       // RATE right-click
    void lockBClicked();     // LOCK A right-click (LOCK B)
    void diversityClicked(); // SUB right-click

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setupUi();
    QWidget *createFunctionButton(const QString &mainText, const QString &subText, QPushButton *&btnOut,
                                  bool isLighter = false);

    QVBoxLayout *m_layout;
    int m_kpa1500SlotIdx = -1; // layout index reserved for KPA1500 mini panel
    Kpa1500MiniPanel *m_kpa1500Panel = nullptr;

    // Button pointers (existing 5x2 grid)
    QPushButton *m_preBtn;
    QPushButton *m_nbBtn;
    QPushButton *m_nrBtn;
    QPushButton *m_ntchBtn;
    QPushButton *m_filBtn;
    QPushButton *m_abBtn;
    QPushButton *m_revBtn;
    QPushButton *m_atobBtn;
    QPushButton *m_spotBtn;
    QPushButton *m_modeBtn;

    // New button pointers (PF row)
    QPushButton *m_bsetBtn;
    QPushButton *m_clrBtn;
    QPushButton *m_ritBtn;
    QPushButton *m_xitBtn;

    // New button pointers (bottom row)
    QPushButton *m_freqEntBtn;
    QPushButton *m_rateBtn;
    QPushButton *m_lockABtn;
    QPushButton *m_subBtn;
};

#endif // RIGHTSIDEPANEL_H
