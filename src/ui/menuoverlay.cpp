#include "menuoverlay.h"
#include "k4styles.h"
#include <QPainter>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScrollBar>

// ============== MenuItemWidget ==============

MenuItemWidget::MenuItemWidget(MenuItem *item, QWidget *parent) : QWidget(parent), m_item(item) {
    setFixedHeight(K4Styles::Dimensions::MenuItemHeight);
    setCursor(Qt::PointingHandCursor);

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(15, 5, 15, 5);
    layout->setSpacing(10);

    // Name label
    m_nameLabel = new QLabel(item->name, this);
    m_nameLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                   .arg(K4Styles::Colors::TextGray)
                                   .arg(K4Styles::Dimensions::FontSizePopup)); // Initial unselected state
    layout->addWidget(m_nameLabel, 1);

    // Lock icon (for read-only items)
    m_lockLabel = new QLabel(this);
    if (item->isReadOnly()) {
        m_lockLabel->setText("\xF0\x9F\x94\x92"); // Lock emoji
    }
    m_lockLabel->setFixedWidth(K4Styles::Dimensions::SmallIconSize);
    layout->addWidget(m_lockLabel);

    // Value label
    m_valueLabel = new QLabel(item->displayValue(), this);
    m_valueLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                    .arg(K4Styles::Colors::TextFaded)
                                    .arg(K4Styles::Dimensions::FontSizePopup)); // Initial unselected state
    m_valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_valueLabel->setMinimumWidth(K4Styles::Dimensions::FormLabelWidth);
    layout->addWidget(m_valueLabel);

    setLayout(layout);
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
                                           .arg(K4Styles::Dimensions::FontSizePopup)); // Light text on grey
            m_valueLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                            .arg(K4Styles::Colors::TextDark)
                                            .arg(K4Styles::Dimensions::FontSizePopup)); // Dark text on off-white
        } else {
            // BROWSE MODE: name on off-white, value on grey
            m_nameLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                           .arg(K4Styles::Colors::TextDark)
                                           .arg(K4Styles::Dimensions::FontSizePopup)); // Dark text on off-white
            m_valueLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                            .arg(K4Styles::Colors::TextWhite)
                                            .arg(K4Styles::Dimensions::FontSizePopup)); // White text on grey
        }
    } else {
        // Unselected: grey text on dark background
        m_nameLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                       .arg(K4Styles::Colors::TextGray)
                                       .arg(K4Styles::Dimensions::FontSizePopup));
        m_valueLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                        .arg(K4Styles::Colors::TextFaded)
                                        .arg(K4Styles::Dimensions::FontSizePopup));
    }
}

void MenuItemWidget::setEditMode(bool editing) {
    m_editing = editing;
    updateLabelColors();
    update();
}

void MenuItemWidget::updateDisplay() {
    m_valueLabel->setText(m_item->displayValue());
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

    connect(m_model, &MenuModel::menuValueChanged, this, &MenuOverlayWidget::onMenuValueChanged);
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
                                       .arg(K4Styles::Colors::InactiveGray)
                                       .arg(K4Styles::Dimensions::FontSizeButton));
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
                              .arg(K4Styles::Colors::TextWhite)
                              .arg(K4Styles::Dimensions::BorderRadius)
                              .arg(K4Styles::Dimensions::FontSizeTitle)
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
                                     "QPushButton:pressed { background-color: %5; }")
                                 .arg(K4Styles::Colors::OverlayNavButton)
                                 .arg(K4Styles::Colors::TextGray)
                                 .arg(K4Styles::Dimensions::BorderRadius)
                                 .arg(K4Styles::Dimensions::FontSizeMedium) // Fits in narrow button
                                 .arg(K4Styles::Colors::OverlayNavButtonPressed));
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
                                 .arg(K4Styles::Colors::TextWhite)        // %5
                                 .arg(K4Styles::Dimensions::BorderWidth)  // %6
                                 .arg(K4Styles::Colors::BorderNormal)     // %7
                                 .arg(K4Styles::Dimensions::BorderRadius) // %8
                                 .arg(K4Styles::Dimensions::FontSizeTitle));
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
                                     .arg(K4Styles::Colors::TextWhite)
                                     .arg(K4Styles::Colors::OverlayNavButton)
                                     .arg(K4Styles::Dimensions::FontSizeMedium)
                                     .arg(K4Styles::Colors::VfoACyan));
    layout->addWidget(m_searchInput);

    m_searchPopup->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::OverlayHeaderBg));

    connect(m_searchInput, &QLineEdit::textChanged, this, &MenuOverlayWidget::onSearchTextChanged);

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

void MenuOverlayWidget::populateItems() {
    // Clear existing
    for (auto *widget : m_itemWidgets) {
        m_listLayout->removeWidget(widget);
        delete widget;
    }
    m_itemWidgets.clear();

    // Add menu items (filtered if search is active)
    QVector<MenuItem *> items =
        m_currentFilter.isEmpty() ? m_model->getAllItems() : m_model->filterByName(m_currentFilter);
    for (MenuItem *item : items) {
        MenuItemWidget *widget = new MenuItemWidget(item, m_listContainer);
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
}

void MenuOverlayWidget::show() {
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
        if (!m_itemWidgets.isEmpty() && m_selectedIndex < m_itemWidgets.size()) {
            MenuItem *item = m_itemWidgets[m_selectedIndex]->menuItem();
            emit menuValueChangeRequested(item->id, "+");
        }
        break;
    case Qt::Key_Minus:
        if (!m_itemWidgets.isEmpty() && m_selectedIndex < m_itemWidgets.size()) {
            MenuItem *item = m_itemWidgets[m_selectedIndex]->menuItem();
            emit menuValueChangeRequested(item->id, "-");
        }
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
    int steps = m_wheelAccumulator.accumulate(event);
    if (steps == 0) {
        event->accept();
        return;
    }

    if (m_editMode) {
        // In edit mode, wheel changes value
        if (!m_itemWidgets.isEmpty() && m_selectedIndex < m_itemWidgets.size()) {
            MenuItem *item = m_itemWidgets[m_selectedIndex]->menuItem();
            for (int i = 0; i < qAbs(steps); ++i)
                emit menuValueChangeRequested(item->id, steps > 0 ? "+" : "-");
        }
    } else {
        // In browse mode, wheel scrolls through items
        for (int i = 0; i < qAbs(steps); ++i) {
            if (steps > 0)
                navigateUp();
            else
                navigateDown();
        }
    }
    event->accept();
}

bool MenuOverlayWidget::eventFilter(QObject *watched, QEvent *event) {
    // Intercept wheel events from scroll area to move selection instead of scrolling
    if (event->type() == QEvent::Wheel) {
        if (watched == m_scrollArea || watched == m_scrollArea->viewport()) {
            QWheelEvent *wheelEvent = static_cast<QWheelEvent *>(event);
            wheelEvent->accept();

            int steps = m_wheelAccumulator.accumulate(wheelEvent);
            if (steps == 0)
                return true;

            if (m_editMode) {
                // In edit mode, wheel changes value
                if (!m_itemWidgets.isEmpty() && m_selectedIndex < m_itemWidgets.size()) {
                    MenuItem *item = m_itemWidgets[m_selectedIndex]->menuItem();
                    for (int i = 0; i < qAbs(steps); ++i)
                        emit menuValueChangeRequested(item->id, steps > 0 ? "+" : "-");
                }
            } else {
                // In browse mode, wheel moves selection (with hard stops)
                for (int i = 0; i < qAbs(steps); ++i) {
                    if (steps > 0)
                        navigateUp();
                    else
                        navigateDown();
                }
            }
            return true; // Event handled, don't let scroll area scroll
        }
    }
    return QWidget::eventFilter(watched, event);
}

void MenuOverlayWidget::navigateUp() {
    if (m_editMode) {
        // In edit mode, up increments value
        if (!m_itemWidgets.isEmpty() && m_selectedIndex < m_itemWidgets.size()) {
            MenuItem *item = m_itemWidgets[m_selectedIndex]->menuItem();
            emit menuValueChangeRequested(item->id, "+");
        }
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
        if (!m_itemWidgets.isEmpty() && m_selectedIndex < m_itemWidgets.size()) {
            MenuItem *item = m_itemWidgets[m_selectedIndex]->menuItem();
            emit menuValueChangeRequested(item->id, "-");
        }
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
    if (m_itemWidgets.isEmpty() || m_selectedIndex >= m_itemWidgets.size()) {
        return;
    }

    MenuItem *item = m_itemWidgets[m_selectedIndex]->menuItem();
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
    if (m_itemWidgets.isEmpty() || m_selectedIndex >= m_itemWidgets.size()) {
        return;
    }

    MenuItem *item = m_itemWidgets[m_selectedIndex]->menuItem();
    if (item->isReadOnly()) {
        return;
    }

    // Set value to default
    int defaultVal = item->defaultValue;
    QString cmd = QString("%1").arg(defaultVal, 4, 10, QChar('0'));
    emit menuValueChangeRequested(item->id, cmd);

    // Also update local model immediately
    m_model->updateValue(item->id, defaultVal);
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
                                  .arg(K4Styles::Colors::TextGray)
                                  .arg(K4Styles::Dimensions::BorderRadius)
                                  .arg(K4Styles::Dimensions::FontSizeMedium);

    QString normBtnDefault = QString("QPushButton { background-color: %1; color: %2; border: none; "
                                     "border-radius: %3px; font-size: %4px; font-weight: bold; }"
                                     "QPushButton:pressed { background-color: %5; }")
                                 .arg(K4Styles::Colors::OverlayNavButton)
                                 .arg(K4Styles::Colors::TextGray)
                                 .arg(K4Styles::Dimensions::BorderRadius)
                                 .arg(K4Styles::Dimensions::FontSizeMedium)
                                 .arg(K4Styles::Colors::OverlayNavButtonPressed);

    QString normBtnActive = QString("QPushButton { background-color: %1; color: %2; border: none; "
                                    "border-radius: %3px; font-size: %4px; font-weight: bold; }"
                                    "QPushButton:pressed { background-color: %5; }")
                                .arg(K4Styles::Colors::SelectionLight)
                                .arg(K4Styles::Colors::TextDark)
                                .arg(K4Styles::Dimensions::BorderRadius)
                                .arg(K4Styles::Dimensions::FontSizeMedium)
                                .arg(K4Styles::Colors::TextWhite);

    if (m_itemWidgets.isEmpty() || m_selectedIndex >= m_itemWidgets.size()) {
        m_normBtn->setStyleSheet(normBtnDisabled);
        return;
    }

    MenuItem *item = m_itemWidgets[m_selectedIndex]->menuItem();
    bool isDefault = (item->currentValue == item->defaultValue);

    if (isDefault) {
        // Grey - value is at default
        m_normBtn->setStyleSheet(normBtnDefault);
    } else {
        // White/bright - value differs from default
        m_normBtn->setStyleSheet(normBtnActive);
    }
}

void MenuOverlayWidget::onMenuValueChanged(int menuId, int newValue) {
    Q_UNUSED(newValue);
    // Find and update the widget for this menu item
    for (auto *widget : m_itemWidgets) {
        if (widget->menuItem()->id == menuId) {
            widget->updateDisplay();
            break;
        }
    }
    updateNormButton();
}
