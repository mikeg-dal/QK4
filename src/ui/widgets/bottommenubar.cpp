#include "ui/widgets/bottommenubar.h"
#include "ui/styling/k4styles.h"
#include <QHBoxLayout>
#include <QMouseEvent>

BottomMenuBar::BottomMenuBar(QWidget *parent) : QWidget(parent) {
    setupUi();
}

void BottomMenuBar::setupUi() {
    setFixedHeight(K4Styles::Dimensions::MenuBarHeight);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, K4Styles::Dimensions::MenuBarMarginV, 10, K4Styles::Dimensions::MenuBarMarginV);
    layout->setSpacing(10); // Equal spacing between all buttons

    // Add stretch before buttons to center them
    layout->addStretch();

    // ===== Menu Buttons =====
    m_menuBtn = createMenuButton("MENU");
    m_fnBtn = createMenuButton("Fn");
    m_displayBtn = createMenuButton("DISPLAY");
    m_bandBtn = createMenuButton("BAND");
    m_mainRxBtn = createMenuButton("MAIN RX");
    m_subRxBtn = createMenuButton("SUB RX");
    m_txBtn = createMenuButton("TX");

    layout->addWidget(m_menuBtn);
    layout->addWidget(m_fnBtn);
    layout->addWidget(m_displayBtn);
    layout->addWidget(m_bandBtn);
    layout->addWidget(m_mainRxBtn);
    layout->addWidget(m_subRxBtn);
    layout->addWidget(m_txBtn);

    // Add stretch after buttons to center them
    layout->addStretch();

    // PTT button at far right (separated from main buttons)
    m_pttBtn = createMenuButton("PTT");
    // PTT swaps to its own pressed look in setPttActive(), so it keeps the plain sheet it returns to.
    m_pttBtn->setStyleSheet(K4Styles::menuBarButton());
    layout->addWidget(m_pttBtn);

    // ===== Connect Signals =====
    connect(m_menuBtn, &QPushButton::clicked, this, &BottomMenuBar::menuClicked);
    connect(m_fnBtn, &QPushButton::clicked, this, &BottomMenuBar::fnClicked);
    connect(m_displayBtn, &QPushButton::clicked, this, &BottomMenuBar::displayClicked);
    connect(m_bandBtn, &QPushButton::clicked, this, &BottomMenuBar::bandClicked);
    connect(m_mainRxBtn, &QPushButton::clicked, this, &BottomMenuBar::mainRxClicked);
    connect(m_subRxBtn, &QPushButton::clicked, this, &BottomMenuBar::subRxClicked);
    connect(m_txBtn, &QPushButton::clicked, this, &BottomMenuBar::txClicked);

    // PTT uses press/release for momentary activation
    connect(m_pttBtn, &QPushButton::pressed, this, &BottomMenuBar::pttPressed);
    connect(m_pttBtn, &QPushButton::released, this, &BottomMenuBar::pttReleased);

    // Right-click toggle (latch) mode for PTT with 180-second safety timeout
    m_pttLockTimer = new QTimer(this);
    m_pttLockTimer->setSingleShot(true);
    m_pttLockTimer->setInterval(180000);
    connect(m_pttLockTimer, &QTimer::timeout, this, [this]() {
        if (m_pttLocked) {
            m_pttLocked = false;
            emit pttLatchRequested(false);
        }
    });
    m_pttBtn->installEventFilter(this);
}

QPushButton *BottomMenuBar::createMenuButton(const QString &text) {
    auto *btn = new QPushButton(text, this);
    btn->setFixedSize(K4Styles::Dimensions::MenuBarButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(K4Styles::menuBarButtonSelectable());
    return btn;
}

void BottomMenuBar::setMenuActive(bool active) {
    K4Styles::setSelected(m_menuBtn, active);
}

void BottomMenuBar::setDisplayActive(bool active) {
    K4Styles::setSelected(m_displayBtn, active);
}

void BottomMenuBar::setBandActive(bool active) {
    K4Styles::setSelected(m_bandBtn, active);
}

void BottomMenuBar::setFnActive(bool active) {
    K4Styles::setSelected(m_fnBtn, active);
}

void BottomMenuBar::setMainRxActive(bool active) {
    K4Styles::setSelected(m_mainRxBtn, active);
}

void BottomMenuBar::setSubRxActive(bool active) {
    K4Styles::setSelected(m_subRxBtn, active);
}

void BottomMenuBar::setTxActive(bool active) {
    K4Styles::setSelected(m_txBtn, active);
}

void BottomMenuBar::setPttActive(bool active) {
    if (active) {
        m_pttBtn->setStyleSheet(K4Styles::menuBarButtonPttPressed());
    } else {
        // Transmission ended for whatever reason - our release, someone else's, the radio's, a
        // disconnect. A latch cannot outlive it.
        m_pttLocked = false;
        m_pttLockTimer->stop();
        m_pttBtn->setStyleSheet(K4Styles::menuBarButton());
    }
}

void BottomMenuBar::setPttLatched(bool latched) {
    m_pttLocked = latched;
    if (latched)
        m_pttLockTimer->start();
    else
        m_pttLockTimer->stop();
}

bool BottomMenuBar::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_pttBtn) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::RightButton) {
                // WHY request only: another owner may hold the transmitter and engage() can be
                // refused. The colour comes from setPttActive() (transmittingChanged) and the latch
                // from setPttLatched().
                emit pttLatchRequested(!m_pttLocked);
                return true;
            }
            // Left-click while locked: give up the lock.
            if (me->button() == Qt::LeftButton && m_pttLocked) {
                emit pttLatchRequested(false);
                return true;
            }
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton && m_pttLocked)
                return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}
