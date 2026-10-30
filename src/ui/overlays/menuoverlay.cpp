#include "ui/overlays/menuoverlay.h"
#include "ui/styling/k4constants.h"
#include <QPainter>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScrollBar>

// ============== MenuItemWidget ==============

MenuItemWidget::MenuItemWidget(int menuId, const MenuModel *model, QWidget *parent)
    : QWidget(parent), m_menuId(menuId), m_model(model) {
    setFixedHeight(K4Styles::Dimensions::MenuItemHeight);
    setCursor(Qt::PointingHandCursor);

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(15, 5, 15, 5);
    layout->setSpacing(10);

    m_nameLabel = new QLabel(this);
    m_nameLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                   .arg(K4Styles::Colors::TextSecondary)
                                   .arg(K4Styles::Dimensions::FontSizeHeading)); // Initial unselected state
    layout->addWidget(m_nameLabel, 1);

    m_lockLabel = new QLabel(this);
    m_lockLabel->setFixedWidth(K4Styles::Dimensions::SmallIconSize);
    layout->addWidget(m_lockLabel);

    // Value label
    m_valueLabel = new QLabel(this);
    m_valueLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                    .arg(K4Styles::Colors::TextSecondary)
                                    .arg(K4Styles::Dimensions::FontSizeHeading)); // Initial unselected state
    m_valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_valueLabel->setMinimumWidth(K4Styles::Dimensions::FormLabelWidth);
    layout->addWidget(m_valueLabel);

    setLayout(layout);
    updateDisplay();
}

void MenuItemWidget::setSelected(bool selected) {
    m_selected = selected;
    updateLabelColors();
    update();
}

void MenuItemWidget::updateLabelColors() {
    // Update label colors based on selection and edit state
    // Two-zone highlighting: left zone (name) and right zone (value) have different colors
    if (m_selected) {
        if (m_editing) {
            // EDITING MODE: name on grey, value on off-white
            m_nameLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                           .arg(K4Styles::Colors::SelectionLight)
                                           .arg(K4Styles::Dimensions::FontSizeHeading)); // Light text on grey
            m_valueLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                            .arg(K4Styles::Colors::TextOnLight)
                                            .arg(K4Styles::Dimensions::FontSizeHeading)); // Dark text on off-white
        } else {
            // BROWSE MODE: name on off-white, value on grey
            m_nameLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                           .arg(K4Styles::Colors::TextOnLight)
                                           .arg(K4Styles::Dimensions::FontSizeHeading)); // Dark text on off-white
            m_valueLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                            .arg(K4Styles::Colors::TextPrimary)
                                            .arg(K4Styles::Dimensions::FontSizeHeading)); // White text on grey
        }
    } else {
        // Unselected: grey text on dark background
        m_nameLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                       .arg(K4Styles::Colors::TextSecondary)
                                       .arg(K4Styles::Dimensions::FontSizeHeading));
        m_valueLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                        .arg(K4Styles::Colors::TextSecondary)
                                        .arg(K4Styles::Dimensions::FontSizeHeading));
    }
}

void MenuItemWidget::setEditMode(bool editing) {
    m_editing = editing;
    updateLabelColors();
    update();
}

void MenuItemWidget::updateDisplay() {
    const MenuItem *item = m_model->getMenuItem(m_menuId);
    if (!item) {
        return;
    }
    // Resolved so "<n>" in the XVTR Band rows shows the current XVTR Band # Select value.
    m_nameLabel->setText(m_model->resolvedName(*item));
    m_lockLabel->setText(item->isReadOnly() ? QString::fromUtf8("\xF0\x9F\x94\x92") : QString()); // Lock emoji
    m_valueLabel->setText(item->displayValue());
}

void MenuItemWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Demarcation at 70% width (separates menu item name from value)
    int demarcX = static_cast<int>(width() * 0.7);

    if (m_selected) {
        if (m_editing) {
            // EDITING MODE: Colors swapped to indicate editing value
            // Left zone (item name): Grey
            painter.fillRect(QRect(0, 0, demarcX, height()), QColor(K4Styles::Colors::SelectionDark));
            // Right zone (value): Off-white to show it's being edited
            painter.fillRect(QRect(demarcX, 0, width() - demarcX, height()), QColor(K4Styles::Colors::SelectionLight));
        } else {
            // BROWSE MODE: Normal highlighting
            // Left zone (item name): Off-white to show selection
            painter.fillRect(QRect(0, 0, demarcX, height()), QColor(K4Styles::Colors::SelectionLight));
            // Right zone (value): Grey
            painter.fillRect(QRect(demarcX, 0, width() - demarcX, height()), QColor(K4Styles::Colors::SelectionDark));
        }
        // Draw vertical demarcation line
        painter.setPen(QColor(K4Styles::Colors::OverlayDividerLight));
        painter.drawLine(demarcX, 0, demarcX, height());
    } else {
        // Unselected: dark background
        painter.fillRect(rect(), QColor(K4Styles::Colors::OverlayItemBg));
    }

    // Bottom border
    painter.setPen(QColor(K4Styles::Colors::OverlayDivider));
    painter.drawLine(0, height() - 1, width(), height() - 1);
}

void MenuItemWidget::mousePressEvent(QMouseEvent *event) {
    Q_UNUSED(event);
    emit clicked();
}

// ============== MenuOverlayWidget ==============

MenuOverlayWidget::MenuOverlayWidget(MenuModel *model, QWidget *parent) : QWidget(parent), m_model(model) {
    setWindowFlags(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setFocusPolicy(Qt::StrongFocus);

    setupUi();

    // WHY: the radio sends its menu as one MEDF line per item, so an open overlay rebuilds once
    // per burst rather than once per item. A zero interval runs after the pending events.
    m_repopulateTimer = new QTimer(this);
    m_repopulateTimer->setSingleShot(true);
    m_repopulateTimer->setInterval(0);
    connect(m_repopulateTimer, &QTimer::timeout, this, &MenuOverlayWidget::repopulateKeepingSelection);

    connect(m_model, &MenuModel::menuValueChanged, this, &MenuOverlayWidget::onMenuValueChanged);
    connect(m_model, &MenuModel::menuItemAdded, this, &MenuOverlayWidget::onMenuItemAdded);
    connect(m_model, &MenuModel::modelCleared, this, &MenuOverlayWidget::onModelCleared);
}

void MenuOverlayWidget::setupUi() {
    // Main layout
    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Content area (left side - menu list)
    m_contentWidget = new QWidget(this);
    m_contentWidget->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::OverlayContentBg));

    QVBoxLayout *contentLayout = new QVBoxLayout(m_contentWidget);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    // Category header
    m_categoryLabel = new QLabel("MENU", m_contentWidget);
    m_categoryLabel->setStyleSheet(QString("background-color: %1; color: %2; font-size: %3px; "
                                           "font-weight: bold; padding: 8px 15px;")
                                       .arg(K4Styles::Colors::OverlayHeaderBg)
                                       .arg(K4Styles::Colors::TextSecondary)
                                       .arg(K4Styles::Dimensions::FontSizeReadout));
    contentLayout->addWidget(m_categoryLabel);

    // Scroll area for menu items
    m_scrollArea = new QScrollArea(m_contentWidget);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setStyleSheet(QString("QScrollArea { border: none; background: transparent; }"
                                        "QScrollBar:vertical { background: %1; width: 8px; }"
                                        "QScrollBar::handle:vertical { background: %2; border-radius: 4px; }"
                                        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }")
                                    .arg(K4Styles::Colors::OverlayContentBg)
                                    .arg(K4Styles::Colors::OverlayNavButton));

    m_listContainer = new QWidget();
    m_listContainer->setStyleSheet("background: transparent;");
    m_listLayout = new QVBoxLayout(m_listContainer);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(0);
    m_listLayout->addStretch();

    m_scrollArea->setWidget(m_listContainer);
    contentLayout->addWidget(m_scrollArea);

    // Install event filter to capture wheel events from scroll area
    m_scrollArea->installEventFilter(this);
    m_scrollArea->viewport()->installEventFilter(this);

    mainLayout->addWidget(m_contentWidget, 1);

    // Navigation panel (right side) - using grid layout for 2-column arrangement
    QWidget *navPanel = new QWidget(this);
    navPanel->setFixedWidth(K4Styles::Dimensions::NavPanelWidth);
    navPanel->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::OverlayHeaderBg));

    QVBoxLayout *navOuterLayout = new QVBoxLayout(navPanel);
    navOuterLayout->setContentsMargins(8, 12, 8, 12);
    navOuterLayout->setSpacing(8);

    QString buttonStyle = QString("QPushButton { background-color: %1; color: %2; border: none; "
                                  "border-radius: %3px; font-size: %4px; font-weight: bold; }"
                                  "QPushButton:pressed { background-color: %5; }")
                              .arg(K4Styles::Colors::OverlayNavButton)
                              .arg(K4Styles::Colors::TextPrimary)
                              .arg(K4Styles::Dimensions::BorderRadius)
                              .arg(K4Styles::Dimensions::FontSizeIndicator)
                              .arg(K4Styles::Colors::OverlayNavButtonPressed);

    // Row 1: Up and Down buttons side by side
    QHBoxLayout *row1 = new QHBoxLayout();
    row1->setSpacing(8);

    m_upBtn = new QPushButton("\xE2\x96\xB2", navPanel); // ▲
    m_upBtn->setFixedSize(K4Styles::Dimensions::NavButtonWidth, K4Styles::Dimensions::PopupButtonHeight);
    m_upBtn->setStyleSheet(buttonStyle);
    connect(m_upBtn, &QPushButton::clicked, this, &MenuOverlayWidget::navigateUp);
    row1->addWidget(m_upBtn);

    m_downBtn = new QPushButton("\xE2\x96\xBC", navPanel); // ▼
    m_downBtn->setFixedSize(K4Styles::Dimensions::NavButtonWidth, K4Styles::Dimensions::PopupButtonHeight);
    m_downBtn->setStyleSheet(buttonStyle);
    connect(m_downBtn, &QPushButton::clicked, this, &MenuOverlayWidget::navigateDown);
    row1->addWidget(m_downBtn);

    navOuterLayout->addLayout(row1);
    navOuterLayout->addStretch();

    // Row 2: Search, NORM, and Back buttons (3 buttons, smaller width)
    QHBoxLayout *row3 = new QHBoxLayout();
    row3->setSpacing(6); // Tighter spacing for 3 buttons

    constexpr int smallNavBtnWidth = 34; // Smaller buttons for 3-button row

    m_searchBtn = new QPushButton("\xF0\x9F\x94\x8D", navPanel); // 🔍
    m_searchBtn->setFixedSize(smallNavBtnWidth, K4Styles::Dimensions::PopupButtonHeight);
    m_searchBtn->setStyleSheet(buttonStyle);
    connect(m_searchBtn, &QPushButton::clicked, this, &MenuOverlayWidget::toggleSearchPopup);
    row3->addWidget(m_searchBtn);

    m_normBtn = new QPushButton("NORM", navPanel);
    m_normBtn->setFixedSize(smallNavBtnWidth, K4Styles::Dimensions::PopupButtonHeight);
    m_normBtn->setStyleSheet(QString("QPushButton { background-color: %1; color: %2; border: none; "
                                     "border-radius: %3px; font-size: %4px; font-weight: bold; }"
                                     "QPushButton:pressed { background-color: %5; color: %6; }")
                                 .arg(K4Styles::Colors::OverlayNavButton)
                                 .arg(K4Styles::Colors::TextSecondaryOnRaised)
                                 .arg(K4Styles::Dimensions::BorderRadius)
                                 .arg(K4Styles::Dimensions::FontSizeCaption) // Fits in narrow button
                                 .arg(K4Styles::Colors::OverlayNavButtonPressed)
                                 .arg(K4Styles::Colors::TextPrimary));
    connect(m_normBtn, &QPushButton::clicked, this, &MenuOverlayWidget::resetToDefault);
    row3->addWidget(m_normBtn);

    m_backBtn = new QPushButton("\xE2\x86\xA9", navPanel); // ↩
    m_backBtn->setFixedSize(smallNavBtnWidth, K4Styles::Dimensions::PopupButtonHeight);
    m_backBtn->setStyleSheet(QString("QPushButton { background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
                                     "stop:0 %1, stop:0.4 %2, stop:0.6 %3, stop:1 %4);"
                                     "color: %5; border: %6px solid %7; border-radius: %8px; "
                                     "font-size: %9px; font-weight: bold; }"
                                     "QPushButton:pressed { background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
                                     "stop:0 %4, stop:0.4 %3, stop:0.6 %2, stop:1 %1); }")
                                 .arg(K4Styles::Colors::GradientTop)      // %1
                                 .arg(K4Styles::Colors::GradientMid1)     // %2
                                 .arg(K4Styles::Colors::GradientMid2)     // %3
                                 .arg(K4Styles::Colors::GradientBottom)   // %4
                                 .arg(K4Styles::Colors::TextPrimary)      // %5
                                 .arg(K4Styles::Dimensions::BorderWidth)  // %6
                                 .arg(K4Styles::Colors::BorderNormal)     // %7
                                 .arg(K4Styles::Dimensions::BorderRadius) // %8
                                 .arg(K4Styles::Dimensions::FontSizeIndicator));
    connect(m_backBtn, &QPushButton::clicked, this, &MenuOverlayWidget::closeOverlay);
    row3->addWidget(m_backBtn);

    navOuterLayout->addLayout(row3);

    mainLayout->addWidget(navPanel);

    setLayout(mainLayout);

    // Create search popup (initially hidden)
    createSearchPopup();
}

void MenuOverlayWidget::createSearchPopup() {
    m_searchPopup = new QWidget(this, Qt::Popup);
    m_searchPopup->setFixedWidth(K4Styles::Dimensions::NavPanelWidth);

    QVBoxLayout *layout = new QVBoxLayout(m_searchPopup);
    layout->setContentsMargins(6, 6, 6, 6);

    m_searchInput = new QLineEdit(m_searchPopup);
    m_searchInput->setPlaceholderText("Search...");
    m_searchInput->setStyleSheet(QString("QLineEdit { background-color: %1; color: %2; border: 1px solid %3; "
                                         "border-radius: 4px; padding: 6px; font-size: %4px; }"
                                         "QLineEdit:focus { border-color: %5; }")
                                     .arg(K4Styles::Colors::OverlayContentBg)
                                     .arg(K4Styles::Colors::TextPrimary)
                                     .arg(K4Styles::Colors::OverlayNavButton)
                                     .arg(K4Styles::Dimensions::FontSizeCaption)
                                     .arg(K4Styles::Colors::VfoACyan));
    layout->addWidget(m_searchInput);

    m_searchPopup->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::OverlayHeaderBg));

    connect(m_searchInput, &QLineEdit::textChanged, this, &MenuOverlayWidget::onSearchTextChanged);

    // Sees the press that closes the box (see eventFilter).
    m_searchPopup->installEventFilter(this);
    m_searchPopup->hide();
}

void MenuOverlayWidget::toggleSearchPopup() {
    if (m_searchPopup->isVisible()) {
        m_searchPopup->hide();
    } else {
        // Position below search button
        QPoint pos = m_searchBtn->mapToGlobal(QPoint(0, m_searchBtn->height() + 4));
        m_searchPopup->move(pos);
        m_searchPopup->show();
        m_searchInput->setFocus();
        m_searchInput->selectAll();
    }
}

void MenuOverlayWidget::onSearchTextChanged(const QString &text) {
    m_currentFilter = text;
    populateItems();
}

void MenuOverlayWidget::clearItemWidgets() {
    for (auto *widget : m_itemWidgets) {
        m_listLayout->removeWidget(widget);
        delete widget;
    }
    m_itemWidgets.clear();
    m_selectedIndex = 0;
    m_editMode = false;
}

void MenuOverlayWidget::populateItems() {
    clearItemWidgets();

    const QList<int> ids = m_model->itemIds(m_currentFilter);
    for (const int id : ids) {
        MenuItemWidget *widget = new MenuItemWidget(id, m_model, m_listContainer);
        connect(widget, &MenuItemWidget::clicked, this, [this, widget]() {
            int index = m_itemWidgets.indexOf(widget);
            if (index >= 0) {
                if (m_selectedIndex == index && !m_editMode) {
                    // Click on already-selected item enters edit mode
                    setEditMode(true);
                } else {
                    m_selectedIndex = index;
                    setEditMode(false);
                    updateSelection();
                }
            }
        });
        m_listLayout->insertWidget(m_listLayout->count() - 1, widget); // Before stretch
        m_itemWidgets.append(widget);
    }

    m_selectedIndex = 0;
    m_editMode = false;
    updateSelection();
    updateButtonLabels();
    updateNormButton();

    // WHY: QScrollArea's scrollbar value is owned by the scroll area, not by the
    // child widgets we just destroyed + recreated — so without this explicit reset
    // the scrollbar keeps its previous numeric value, leaving the highlight (item 0)
    // off-screen above the visible area on reopen or on search-filter clear.
    m_scrollArea->verticalScrollBar()->setValue(0);
}

void MenuOverlayWidget::repopulateKeepingSelection() {
    const MenuItem *selected = selectedMenuItem();
    const int selectedId = selected ? selected->id : 0;
    const bool hadSelection = selected != nullptr;
    populateItems();
    if (!hadSelection) {
        return;
    }
    for (int i = 0; i < m_itemWidgets.size(); ++i) {
        if (m_itemWidgets[i]->menuId() == selectedId) {
            m_selectedIndex = i;
            updateSelection();
            updateNormButton();
            ensureSelectedVisible();
            return;
        }
    }
}

void MenuOverlayWidget::onMenuItemAdded() {
    if (isVisible()) {
        m_repopulateTimer->start();
    }
}

void MenuOverlayWidget::onModelCleared() {
    m_repopulateTimer->stop();
    clearItemWidgets();
    updateButtonLabels();
    updateNormButton();
}

void MenuOverlayWidget::show() {
    m_repopulateTimer->stop();
    populateItems();
    QWidget::show();
    setFocus();
}

void MenuOverlayWidget::hide() {
    // Clear search filter when closing
    m_currentFilter.clear();
    if (m_searchInput) {
        m_searchInput->clear();
    }
    if (m_searchPopup && m_searchPopup->isVisible()) {
        m_searchPopup->hide();
    }
    QWidget::hide();
    emit closed();
}

void MenuOverlayWidget::refresh() {
    for (auto *widget : m_itemWidgets) {
        widget->updateDisplay();
    }
    updateNormButton();
}

void MenuOverlayWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), QColor(K4Styles::Colors::OverlayContentBg));
}

void MenuOverlayWidget::keyPressEvent(QKeyEvent *event) {
    switch (event->key()) {
    case Qt::Key_Up:
        navigateUp();
        break;
    case Qt::Key_Down:
        navigateDown();
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        selectCurrent();
        break;
    case Qt::Key_Escape:
        // Close search popup first if open
        if (m_searchPopup && m_searchPopup->isVisible()) {
            m_searchPopup->hide();
        } else if (m_editMode) {
            setEditMode(false);
        } else {
            closeOverlay();
        }
        break;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        if (const MenuItem *item = selectedMenuItem())
            emit menuValueChangeRequested(item->id, "+");
        break;
    case Qt::Key_Minus:
        if (const MenuItem *item = selectedMenuItem())
            emit menuValueChangeRequested(item->id, "-");
        break;
    default:
        QWidget::keyPressEvent(event);
    }
}

void MenuOverlayWidget::mousePressEvent(QMouseEvent *event) {
    // Close if clicking outside content area (but not on nav panel)
    QRect contentRect = m_contentWidget->geometry();
    if (!contentRect.contains(event->pos()) && event->pos().x() < contentRect.x()) {
        closeOverlay();
    }
}

void MenuOverlayWidget::wheelEvent(QWheelEvent *event) {
    applyWheelSteps(m_wheelAccumulator.accumulate(event));
    event->accept();
}

bool MenuOverlayWidget::eventFilter(QObject *watched, QEvent *event) {
    // WHY: the search box is a Qt::Popup. A press outside it closes it, and where the platform's
    // ReplayMousePressOutsidePopup hint is set (Windows, X11; not macOS) Qt then replays the press
    // to the widget under it. On the search key that replay is a second click, which opened the box
    // it had just closed. Qt::WA_NoMouseReplay, set on the popup while it handles that press, keeps
    // the press from being replayed; a press anywhere else is still replayed, so it works where it
    // lands. Qt clears the attribute before each new mouse event, so it only ever covers this press.
    if (watched == m_searchPopup && event->type() == QEvent::MouseButtonPress) {
        const QPoint globalPos = static_cast<QMouseEvent *>(event)->globalPosition().toPoint();
        const QRect searchKey(m_searchBtn->mapToGlobal(QPoint(0, 0)), m_searchBtn->size());
        if (searchKey.contains(globalPos))
            m_searchPopup->setAttribute(Qt::WA_NoMouseReplay);
        return false; // the popup still handles the press: it closes itself
    }
    // Intercept wheel events from scroll area to move selection instead of scrolling
    if (event->type() == QEvent::Wheel && (watched == m_scrollArea || watched == m_scrollArea->viewport())) {
        auto *wheelEvent = static_cast<QWheelEvent *>(event);
        wheelEvent->accept();
        applyWheelSteps(m_wheelAccumulator.accumulate(wheelEvent));
        return true; // Event handled, don't let scroll area scroll
    }
    return QWidget::eventFilter(watched, event);
}

void MenuOverlayWidget::applyWheelSteps(int steps) {
    // Browse mode moves the selection (with hard stops); edit mode steps the value - navigateUp/Down do both.
    for (int i = 0; i < qAbs(steps); ++i) {
        if (steps > 0)
            navigateUp();
        else
            navigateDown();
    }
}

const MenuItem *MenuOverlayWidget::selectedMenuItem() const {
    if (m_selectedIndex < 0 || m_selectedIndex >= m_itemWidgets.size())
        return nullptr;
    return m_model->getMenuItem(m_itemWidgets[m_selectedIndex]->menuId());
}

void MenuOverlayWidget::navigateUp() {
    if (m_editMode) {
        // In edit mode, up increments value
        if (const MenuItem *item = selectedMenuItem())
            emit menuValueChangeRequested(item->id, "+");
    } else {
        // In browse mode, up moves selection
        if (m_selectedIndex > 0) {
            m_selectedIndex--;
            updateSelection();
            ensureSelectedVisible();
            updateNormButton();
        }
    }
}

void MenuOverlayWidget::navigateDown() {
    if (m_editMode) {
        // In edit mode, down decrements value
        if (const MenuItem *item = selectedMenuItem())
            emit menuValueChangeRequested(item->id, "-");
    } else {
        // In browse mode, down moves selection
        if (m_selectedIndex < m_itemWidgets.size() - 1) {
            m_selectedIndex++;
            updateSelection();
            ensureSelectedVisible();
            updateNormButton();
        }
    }
}

void MenuOverlayWidget::selectCurrent() {
    const MenuItem *item = selectedMenuItem();
    if (!item)
        return;
    if (item->isReadOnly()) {
        return;
    }

    if (m_editMode) {
        // Exit edit mode (confirm)
        setEditMode(false);
    } else {
        // Enter edit mode
        setEditMode(true);
    }
}

void MenuOverlayWidget::closeOverlay() {
    if (m_editMode) {
        setEditMode(false);
    } else {
        hide();
    }
}

void MenuOverlayWidget::resetToDefault() {
    const MenuItem *item = selectedMenuItem();
    if (!item)
        return;
    if (item->isReadOnly()) {
        return;
    }

    const int id = item->id;
    const int defaultVal = item->defaultValue;
    emit menuValueChangeRequested(id, QString("%1").arg(defaultVal, 4, 10, QChar('0')));

    // Also update local model immediately
    m_model->updateValue(id, defaultVal);
}

void MenuOverlayWidget::updateSelection() {
    for (int i = 0; i < m_itemWidgets.size(); ++i) {
        m_itemWidgets[i]->setSelected(i == m_selectedIndex);
        m_itemWidgets[i]->setEditMode(i == m_selectedIndex && m_editMode);
    }
}

void MenuOverlayWidget::ensureSelectedVisible() {
    if (m_selectedIndex >= 0 && m_selectedIndex < m_itemWidgets.size()) {
        m_scrollArea->ensureWidgetVisible(m_itemWidgets[m_selectedIndex]);
    }
}

void MenuOverlayWidget::setEditMode(bool editing) {
    m_editMode = editing;
    updateSelection();
    updateButtonLabels();
    updateNormButton();
}

void MenuOverlayWidget::updateButtonLabels() {
    if (m_editMode) {
        m_upBtn->setText("+");
        m_downBtn->setText("-");
    } else {
        m_upBtn->setText("\xE2\x96\xB2");   // ▲
        m_downBtn->setText("\xE2\x96\xBC"); // ▼
    }
}

void MenuOverlayWidget::updateNormButton() {
    QString normBtnDisabled = QString("QPushButton { background-color: %1; color: %2; border: none; "
                                      "border-radius: %3px; font-size: %4px; font-weight: bold; }")
                                  .arg(K4Styles::Colors::OverlayNavButton)
                                  .arg(K4Styles::Colors::TextDisabled)
                                  .arg(K4Styles::Dimensions::BorderRadius)
                                  .arg(K4Styles::Dimensions::FontSizeCaption);

    QString normBtnDefault = QString("QPushButton { background-color: %1; color: %2; border: none; "
                                     "border-radius: %3px; font-size: %4px; font-weight: bold; }"
                                     "QPushButton:pressed { background-color: %5; color: %6; }")
                                 .arg(K4Styles::Colors::OverlayNavButton)
                                 .arg(K4Styles::Colors::TextSecondaryOnRaised)
                                 .arg(K4Styles::Dimensions::BorderRadius)
                                 .arg(K4Styles::Dimensions::FontSizeCaption)
                                 .arg(K4Styles::Colors::OverlayNavButtonPressed)
                                 .arg(K4Styles::Colors::TextPrimary);

    QString normBtnActive = QString("QPushButton { background-color: %1; color: %2; border: none; "
                                    "border-radius: %3px; font-size: %4px; font-weight: bold; }"
                                    "QPushButton:pressed { background-color: %5; }")
                                .arg(K4Styles::Colors::SelectionLight)
                                .arg(K4Styles::Colors::TextOnLight)
                                .arg(K4Styles::Dimensions::BorderRadius)
                                .arg(K4Styles::Dimensions::FontSizeCaption)
                                .arg(K4Styles::Colors::White);

    const MenuItem *item = selectedMenuItem();
    if (!item) {
        m_normBtn->setStyleSheet(normBtnDisabled);
        return;
    }

    if (item->isReadOnly()) {
        // NORM cannot change a read-only item, so it must not light up for one either
        m_normBtn->setStyleSheet(normBtnDisabled);
        return;
    }

    // Lit when the item IS at its default - NORM reads "this is the normal setting". Pressing it on
    // an item that is not sets the default, and the value change re-runs this and lights it.
    const bool isDefault = (item->currentValue == item->defaultValue);
    m_normBtn->setStyleSheet(isDefault ? normBtnActive : normBtnDefault);
}

void MenuOverlayWidget::onMenuValueChanged(int menuId, int newValue) {
    Q_UNUSED(newValue);
    // XVTR Band # Select renames every "<n>" row, so it refreshes them all.
    const bool everyRow = menuId == MenuModel::XVTR_BAND_SELECT_ID;
    for (auto *widget : m_itemWidgets) {
        if (everyRow || widget->menuId() == menuId) {
            widget->updateDisplay();
        }
    }
    updateNormButton();
}
