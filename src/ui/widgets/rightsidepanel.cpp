#include "ui/widgets/rightsidepanel.h"
#include "ui/styling/k4styles.h"
#include "ui/widgets/kpa1500minipanel.h"
#include "ui/widgets/sidepanelbutton.h"
#include <QEvent>
#include <QGridLayout>
#include <QMouseEvent>

RightSidePanel::RightSidePanel(QWidget *parent)
    : QWidget(parent), m_preBtn(nullptr), m_nbBtn(nullptr), m_nrBtn(nullptr), m_ntchBtn(nullptr), m_filBtn(nullptr),
      m_abBtn(nullptr), m_revBtn(nullptr), m_atobBtn(nullptr), m_spotBtn(nullptr), m_modeBtn(nullptr),
      m_bsetBtn(nullptr), m_clrBtn(nullptr), m_ritBtn(nullptr), m_xitBtn(nullptr), m_freqEntBtn(nullptr),
      m_rateBtn(nullptr), m_lockABtn(nullptr), m_subBtn(nullptr) {
    setupUi();
}

void RightSidePanel::setupUi() {
    setFixedWidth(K4Styles::Dimensions::RightSidePanelWidth);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(K4Styles::Dimensions::SidePanelMarginH, K4Styles::Dimensions::SidePanelMarginV,
                                 K4Styles::Dimensions::SidePanelMarginH, K4Styles::Dimensions::SidePanelMarginV);
    m_layout->setSpacing(K4Styles::Dimensions::SidePanelItemSpacing);

    // Each grid keeps its own width and sits centred, so its columns line up with the left panel's.
    auto addGrid = [this](QGridLayout *grid) {
        m_layout->addLayout(grid);
        m_layout->setAlignment(grid, Qt::AlignHCenter);
    };

    // Create 5×2 button grid
    auto *buttonGrid = SidePanelButton::createGrid();

    // Row 0: PRE/ATTN, NB/LEVEL
    buttonGrid->addWidget(createFunctionButton("PRE", "ATTN", m_preBtn), 0, 0);
    buttonGrid->addWidget(createFunctionButton("NB", "LEVEL", m_nbBtn), 0, 1);

    // Row 1: NR/ADJ, NTCH/MANUAL
    buttonGrid->addWidget(createFunctionButton("NR", "ADJ", m_nrBtn), 1, 0);
    buttonGrid->addWidget(createFunctionButton("NTCH", "MANUAL", m_ntchBtn), 1, 1);

    // Row 2: FIL/APF, A/B/SPLIT
    buttonGrid->addWidget(createFunctionButton("FIL", "APF", m_filBtn), 2, 0);
    buttonGrid->addWidget(createFunctionButton("A/B", "SPLIT", m_abBtn), 2, 1);

    // Row 3: REV, A->B/B->A
    buttonGrid->addWidget(createFunctionButton("REV", "", m_revBtn), 3, 0);
    buttonGrid->addWidget(createFunctionButton("A->B", "B->A", m_atobBtn), 3, 1);

    // Row 4: SPOT/AUTO, MODE/ALT
    buttonGrid->addWidget(createFunctionButton("SPOT", "AUTO", m_spotBtn), 4, 0);
    buttonGrid->addWidget(createFunctionButton("MODE", "ALT", m_modeBtn), 4, 1);

    addGrid(buttonGrid);

    // Connect 5×2 grid signals
    connect(m_preBtn, &QPushButton::clicked, this, &RightSidePanel::preClicked);
    connect(m_nbBtn, &QPushButton::clicked, this, &RightSidePanel::nbClicked);
    connect(m_nrBtn, &QPushButton::clicked, this, &RightSidePanel::nrClicked);
    connect(m_ntchBtn, &QPushButton::clicked, this, &RightSidePanel::ntchClicked);
    connect(m_filBtn, &QPushButton::clicked, this, &RightSidePanel::filClicked);
    connect(m_abBtn, &QPushButton::clicked, this, &RightSidePanel::abClicked);
    connect(m_revBtn, &QPushButton::pressed, this, &RightSidePanel::revPressed);
    connect(m_revBtn, &QPushButton::released, this, &RightSidePanel::revReleased);
    connect(m_atobBtn, &QPushButton::clicked, this, &RightSidePanel::atobClicked);
    connect(m_spotBtn, &QPushButton::clicked, this, &RightSidePanel::spotClicked);
    connect(m_modeBtn, &QPushButton::clicked, this, &RightSidePanel::modeClicked);

    // Install event filters for right-click handling on main 5x2 grid
    m_preBtn->installEventFilter(this);
    m_nbBtn->installEventFilter(this);
    m_nrBtn->installEventFilter(this);
    m_ntchBtn->installEventFilter(this);
    m_filBtn->installEventFilter(this);
    m_abBtn->installEventFilter(this);
    // m_revBtn - uses press/release (not right-click)
    m_atobBtn->installEventFilter(this);
    m_spotBtn->installEventFilter(this);
    m_modeBtn->installEventFilter(this);

    // Reserve a layout slot for the KPA1500 mini panel — the actual
    // widget is constructed + owned by KPA1500UiController and dropped
    // here via embedKpa1500Panel().
    SidePanelButton::addGap(m_layout, K4Styles::Dimensions::SidePanelGroupGap);
    m_kpa1500SlotIdx = m_layout->count();
    // WHY a fixed gap as well as the stretch: the stretch is zero at the window's minimum height, which
    // left the PF keys touching the card.
    SidePanelButton::addGap(m_layout, K4Styles::Dimensions::SidePanelGroupGap);

    // Add stretch to push remaining buttons to bottom (above PTT)
    m_layout->addStretch();

    // Create 2×2 PF button grid (B SET, CLR, RIT, XIT)
    auto *pfGrid = SidePanelButton::createGrid();

    // Row 0: B SET/PF 1, CLR/PF 2 (lighter grey)
    pfGrid->addWidget(createFunctionButton("B SET", "PF 1", m_bsetBtn, true), 0, 0);
    pfGrid->addWidget(createFunctionButton("CLR", "PF 2", m_clrBtn, true), 0, 1);

    // Row 1: RIT/PF 3, XIT/PF 4 (lighter grey)
    pfGrid->addWidget(createFunctionButton("RIT", "PF 3", m_ritBtn, true), 1, 0);
    pfGrid->addWidget(createFunctionButton("XIT", "PF 4", m_xitBtn, true), 1, 1);

    addGrid(pfGrid);

    // Connect PF button signals
    connect(m_bsetBtn, &QPushButton::clicked, this, &RightSidePanel::bsetClicked);
    connect(m_clrBtn, &QPushButton::clicked, this, &RightSidePanel::clrClicked);
    connect(m_ritBtn, &QPushButton::clicked, this, &RightSidePanel::ritClicked);
    connect(m_xitBtn, &QPushButton::clicked, this, &RightSidePanel::xitClicked);

    // Install event filters for right-click handling on PF row
    m_bsetBtn->installEventFilter(this);
    m_clrBtn->installEventFilter(this);
    m_ritBtn->installEventFilter(this);
    m_xitBtn->installEventFilter(this);

    SidePanelButton::addGap(m_layout, K4Styles::Dimensions::SidePanelSectionGap);

    // Create 2×2 bottom button grid (FREQ ENT, RATE, LOCK A, SUB)
    auto *bottomGrid = SidePanelButton::createGrid();

    // Row 0: FREQ ENT/SCAN (stacked text), RATE/KHZ
    bottomGrid->addWidget(createFunctionButton("FREQ\nENT", "SCAN", m_freqEntBtn), 0, 0);
    bottomGrid->addWidget(createFunctionButton("RATE", "KHZ", m_rateBtn), 0, 1);

    // Row 1: LOCK A/LOCK B, SUB/DIVERSITY
    bottomGrid->addWidget(createFunctionButton("LOCK A", "LOCK B", m_lockABtn), 1, 0);
    bottomGrid->addWidget(createFunctionButton("SUB", "DIVERSITY", m_subBtn), 1, 1);

    addGrid(bottomGrid);

    // Connect bottom button signals
    connect(m_freqEntBtn, &QPushButton::clicked, this, &RightSidePanel::freqEntClicked);
    connect(m_rateBtn, &QPushButton::clicked, this, &RightSidePanel::rateClicked);
    connect(m_lockABtn, &QPushButton::clicked, this, &RightSidePanel::lockAClicked);
    connect(m_subBtn, &QPushButton::clicked, this, &RightSidePanel::subClicked);

    // Install event filter for right-click on RATE, LOCK A, and SUB buttons
    m_rateBtn->installEventFilter(this);
    m_lockABtn->installEventFilter(this);
    m_subBtn->installEventFilter(this);
}

QWidget *RightSidePanel::createFunctionButton(const QString &mainText, const QString &subText, QPushButton *&btnOut,
                                              bool isLighter) {
    const auto shade = isLighter ? SidePanelButton::Shade::Light : SidePanelButton::Shade::Dark;
    return SidePanelButton::createWithSubLabel(mainText, subText, shade, btnOut, this);
}

bool RightSidePanel::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::MouseButtonPress) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::RightButton) {
            // Main 5x2 grid right-click signals
            if (watched == m_preBtn) {
                emit attnClicked();
                return true;
            } else if (watched == m_nbBtn) {
                emit levelClicked();
                return true;
            } else if (watched == m_nrBtn) {
                emit adjClicked();
                return true;
            } else if (watched == m_ntchBtn) {
                emit manualClicked();
                return true;
            } else if (watched == m_filBtn) {
                emit apfClicked();
                return true;
            } else if (watched == m_abBtn) {
                emit splitClicked();
                return true;
            } else if (watched == m_atobBtn) {
                emit btoaClicked();
                return true;
            } else if (watched == m_spotBtn) {
                emit autoClicked();
                return true;
            } else if (watched == m_modeBtn) {
                emit altClicked();
                return true;
            }
            // PF row right-click signals
            else if (watched == m_bsetBtn) {
                emit pf1Clicked();
                return true;
            } else if (watched == m_clrBtn) {
                emit pf2Clicked();
                return true;
            } else if (watched == m_ritBtn) {
                emit pf3Clicked();
                return true;
            } else if (watched == m_xitBtn) {
                emit pf4Clicked();
                return true;
            }
            // Bottom row right-click signals
            else if (watched == m_rateBtn) {
                emit khzClicked();
                return true;
            } else if (watched == m_lockABtn) {
                emit lockBClicked();
                return true;
            } else if (watched == m_subBtn) {
                emit diversityClicked();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void RightSidePanel::embedKpa1500Panel(Kpa1500MiniPanel *panel) {
    Q_ASSERT(m_kpa1500SlotIdx >= 0 && !m_kpa1500Panel);
    m_kpa1500Panel = panel;
    panel->setParent(this);
    m_layout->insertWidget(m_kpa1500SlotIdx, panel);
}

void RightSidePanel::setKpa1500SlotReserved(bool reserved) {
    Q_ASSERT(m_kpa1500Panel);
    // WHY: the card keeps its space while hidden ("the layout will not be changed by hiding the
    // widget" - QSizePolicy::setRetainSizeWhenHidden), but only while the amplifier is enabled.
    QSizePolicy policy = m_kpa1500Panel->sizePolicy();
    policy.setRetainSizeWhenHidden(reserved);
    m_kpa1500Panel->setSizePolicy(policy);
    setFixedWidth(reserved ? K4Styles::Dimensions::RightSidePanelKpaWidth : K4Styles::Dimensions::RightSidePanelWidth);
    // WHY announce it here: the card is explicitly hidden until the amplifier connects, and "for
    // explicitly hidden widgets, updateGeometry() is a no-op" (QWidget docs), so switching the KPA1500
    // on or off in Options would otherwise leave the window's minimum where it was.
    m_layout->invalidate();
    updateGeometry();
}
