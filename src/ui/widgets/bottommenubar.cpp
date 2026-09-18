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
    // Left margin matches side panel width to align buttons with waterfall above
    layout->setContentsMargins(K4Styles::Dimensions::LeftSidePanelWidth, 6, 10, 6);
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
    btn->setStyleSheet(K4Styles::menuBarButton());
    return btn;
}

void BottomMenuBar::setMenuActive(bool active) {
    if (active) {
        m_menuBtn->setStyleSheet(K4Styles::menuBarButtonActive());
    } else {
        m_menuBtn->setStyleSheet(K4Styles::menuBarButton());
    }
}

void BottomMenuBar::setDisplayActive(bool active) {
    if (active) {
        m_displayBtn->setStyleSheet(K4Styles::menuBarButtonActive());
    } else {
        m_displayBtn->setStyleSheet(K4Styles::menuBarButton());
    }
}

void BottomMenuBar::setBandActive(bool active) {
    if (active) {
        m_bandBtn->setStyleSheet(K4Styles::menuBarButtonActive());
    } else {
        m_bandBtn->setStyleSheet(K4Styles::menuBarButton());
    }
}

void BottomMenuBar::setFnActive(bool active) {
    if (active) {
        m_fnBtn->setStyleSheet(K4Styles::menuBarButtonActive());
    } else {
        m_fnBtn->setStyleSheet(K4Styles::menuBarButton());
    }
}

void BottomMenuBar::setMainRxActive(bool active) {
    if (active) {
        m_mainRxBtn->setStyleSheet(K4Styles::menuBarButtonActive());
    } else {
        m_mainRxBtn->setStyleSheet(K4Styles::menuBarButton());
    }
}

void BottomMenuBar::setSubRxActive(bool active) {
    if (active) {
        m_subRxBtn->setStyleSheet(K4Styles::menuBarButtonActive());
    } else {
        m_subRxBtn->setStyleSheet(K4Styles::menuBarButton());
    }
}

void BottomMenuBar::setTxActive(bool active) {
    if (active) {
        m_txBtn->setStyleSheet(K4Styles::menuBarButtonActive());
    } else {
        m_txBtn->setStyleSheet(K4Styles::menuBarButton());
    }
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
                // REQUEST ONLY. This used to set m_pttLocked and repaint the button itself, which
                // made the widget lie in both directions when something else held the
                // transmitter: right-clicking during an XMIT transmission latched and lit a button
                // whose engage() was refused, and clicking again greyed it out while XMIT was
                // still keyed and still streaming. Observed on the bench 2026-09-18.
                //
                // The colour now comes from transmittingChanged and the latch from
                // setPttLatched(), both of which follow what actually happened.
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
