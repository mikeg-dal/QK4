#include "ui/overlays/memoryoverlay.h"

#include "models/memorymodel.h"
#include "ui/styling/k4styles.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QRegularExpressionValidator>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace {

// The row's three zones. The light selection panel covers the number and label; the frequencies
// sit on the dark panel, as on the radio.
constexpr int RowPaddingH = 15;
constexpr int RowPaddingV = 5;
constexpr int RowSpacing = 10;
// Column rules stop short of the row edges so they read as rules, not as a grid.
constexpr int SeparatorInset = 4;
// The search box hangs this far under its key, in a box with this margin (MENU's numbers).
constexpr int SearchPopupGap = 4;
constexpr int SearchPopupMargin = 6;

QString sideText(const QString &prefix, const MemoryChannel::Side &side) {
    if (!side.isSet())
        return QString();
    return QStringLiteral("%1:  %2  %3").arg(prefix, MemoryRowWidget::formatFrequency(side.frequencyHz()), side.mode);
}

} // namespace

MemoryRowWidget::MemoryRowWidget(int channel, const MemoryModel *model, QWidget *parent)
    : QWidget(parent), m_channel(channel), m_model(model) {
    setFixedHeight(K4Styles::Dimensions::MenuItemHeight);
    setCursor(Qt::PointingHandCursor);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(RowPaddingH, RowPaddingV, RowPaddingH, RowPaddingV);
    layout->setSpacing(RowSpacing);

    m_nameLabel = new QLabel(this);
    m_nameLabel->setStyleSheet(K4Styles::menuItemNameLabel());
    layout->addWidget(m_nameLabel, 1);

    m_vfoALabel = new QLabel(this);
    m_vfoALabel->setStyleSheet(K4Styles::menuItemValueLabel());
    m_vfoALabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(m_vfoALabel, 1);

    m_vfoBLabel = new QLabel(this);
    m_vfoBLabel->setStyleSheet(K4Styles::menuItemValueLabel());
    m_vfoBLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(m_vfoBLabel, 1);

    updateDisplay();
}

QString MemoryRowWidget::formatFrequency(qint64 hz) {
    if (hz <= 0)
        return QString();
    const qint64 mhz = hz / 1000000;
    const qint64 khz = (hz % 1000000) / 1000;
    const qint64 hundreds = (hz % 1000) / 100;
    return QStringLiteral("%1.%2.%3").arg(mhz).arg(khz, 3, 10, QChar('0')).arg(hundreds);
}

void MemoryRowWidget::updateDisplay() {
    const MemoryChannel *channel = m_model ? m_model->channel(m_channel) : nullptr;

    // A channel the radio has not reported, or one it reports as blank, shows its number alone —
    // which is what makes the STO list readable: the free slots are the ones with nothing after
    // the colon.
    const QString number = QStringLiteral("%1:").arg(m_channel, 3, 10, QChar('0'));
    if (!channel || channel->isBlank()) {
        m_nameLabel->setText(number);
        m_vfoALabel->clear();
        m_vfoBLabel->clear();
        return;
    }

    m_nameLabel->setText(channel->label.isEmpty() ? number : QStringLiteral("%1 %2").arg(number, channel->label));
    m_vfoALabel->setText(sideText(QStringLiteral("A"), channel->a));
    m_vfoBLabel->setText(sideText(QStringLiteral("B"), channel->b));
}

void MemoryRowWidget::beginRename() {
    const MemoryChannel *channel = m_model ? m_model->channel(m_channel) : nullptr;
    if (!channel || channel->isBlank())
        return; // nothing stored here to name

    if (!m_nameEdit) {
        m_nameEdit = new QLineEdit(this);
        // The same text-entry recipe the MENU overlay's search box uses, so typing looks the same
        // wherever it happens in an overlay: a bounded cell with a cyan focus edge.
        m_nameEdit->setStyleSheet(QString("QLineEdit { background-color: %1; color: %2; "
                                          "border: %3px solid %4; border-radius: %5px; padding: 2px 4px; "
                                          "font-size: %6px; }"
                                          "QLineEdit:focus { border-color: %7; }")
                                      .arg(QLatin1StringView(K4Styles::Colors::OverlayContentBg),
                                           QLatin1StringView(K4Styles::Colors::TextPrimary))
                                      .arg(K4Styles::Dimensions::SeparatorHeight)
                                      .arg(QLatin1StringView(K4Styles::Colors::OverlayNavButton))
                                      .arg(K4Styles::Dimensions::BorderRadius)
                                      .arg(K4Styles::Dimensions::FontSizeReadout)
                                      .arg(QLatin1StringView(K4Styles::Colors::VfoACyan)));
        // The radio's own field separators: a label carrying either would not survive the wire.
        m_nameEdit->setValidator(new QRegularExpressionValidator(QRegularExpression("[^,;]*"), m_nameEdit));
        auto *rowLayout = qobject_cast<QHBoxLayout *>(layout());
        rowLayout->insertWidget(0, m_nameEdit, 1);
        connect(m_nameEdit, &QLineEdit::editingFinished, this, [this]() {
            if (!m_nameEdit->isVisible())
                return;
            const QString label = m_nameEdit->text().trimmed();
            endRename();
            emit renamed(m_channel, label);
        });
    }
    m_nameEdit->setText(channel->label);
    m_nameLabel->hide();
    m_nameEdit->show();
    m_nameEdit->setFocus(Qt::OtherFocusReason);
    m_nameEdit->selectAll();
}

void MemoryRowWidget::endRename() {
    if (!m_nameEdit)
        return;
    m_nameEdit->hide();
    m_nameLabel->show();
}

bool MemoryRowWidget::isRenaming() const {
    return m_nameEdit && m_nameEdit->isVisible();
}

bool MemoryRowWidget::matches(const QString &needle) const {
    const QString haystack =
        m_nameLabel->text() + QLatin1Char(' ') + m_vfoALabel->text() + QLatin1Char(' ') + m_vfoBLabel->text();
    return haystack.contains(needle, Qt::CaseInsensitive);
}

void MemoryRowWidget::setSelected(bool selected) {
    if (m_selected == selected)
        return;
    m_selected = selected;
    updateLabelColors();
    update();
}

void MemoryRowWidget::updateLabelColors() {
    const QString state = m_selected ? QString::fromLatin1(K4Styles::MenuRowBrowse) : QString();
    for (QLabel *label : {m_nameLabel, m_vfoALabel, m_vfoBLabel})
        K4Styles::setStyleState(label, K4Styles::StateProperty, state);
}

void MemoryRowWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // WHY the labels' own edges and not fractions of the width: a fraction put the boundary
    // through the middle of "A:", cutting the letter in half. The seams belong where the layout
    // actually ends each column - half the gap past it, so no column's text touches one.
    const int nameSeam = nameColumnRight();
    const int vfoSeam = vfoAColumnRight();

    if (m_selected) {
        painter.fillRect(QRect(0, 0, nameSeam, height()), QColor(K4Styles::Colors::SelectionLight));
        painter.fillRect(QRect(nameSeam, 0, width() - nameSeam, height()), QColor(K4Styles::Colors::SelectionDark));

        // Rule the selected row into its three fields - name, VFO A, VFO B - the way the radio
        // does. Only the selected row: ruling all 200 would be a grid, and the radio does not.
        // OverlayDividerLight is the token the MENU overlay already uses for an in-row seam.
        painter.setPen(QPen(QColor(K4Styles::Colors::OverlayDividerLight), K4Styles::Dimensions::SeparatorHeight));
        for (const int x : {nameSeam, vfoSeam})
            painter.drawLine(x, SeparatorInset, x, height() - SeparatorInset);
    }

    // Every row carries the between-rows line the MENU overlay draws, so the two lists read as
    // the same kind of list.
    painter.setPen(QPen(QColor(K4Styles::Colors::OverlayDivider), K4Styles::Dimensions::SeparatorHeight));
    painter.drawLine(0, height() - 1, width(), height() - 1);
}

void MemoryRowWidget::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape && isRenaming()) {
        endRename(); // abandons: editingFinished does not fire for a hidden editor
        return;
    }
    QWidget::keyPressEvent(event);
}

void MemoryRowWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        if (event->position().x() <= nameColumnRight())
            emit nameAreaClicked();
        else
            emit clicked();
    }
    QWidget::mousePressEvent(event);
}

int MemoryRowWidget::nameColumnRight() const {
    return m_nameLabel->geometry().right() + RowSpacing / 2;
}

int MemoryRowWidget::vfoAColumnRight() const {
    return m_vfoALabel->geometry().right() + RowSpacing / 2;
}

// =============================================================================
// MemoryOverlayWidget
// =============================================================================

MemoryOverlayWidget::MemoryOverlayWidget(MemoryModel *model, QWidget *parent) : QWidget(parent), m_model(model) {
    setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(true);
    // WA_StyledBackground: without it a plain QWidget ignores the border in its style sheet, and
    // the list reads as a slab dropped on the spectrum rather than a panel sitting over it.
    // PanelBorder is the frame the panadapter and VFO panels already wear, so the list sits in
    // the same family of surfaces rather than inventing an edge of its own.
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QString("QWidget#memoryOverlay { background-color: %1; border: %2px solid %3; "
                          "border-radius: %4px; }")
                      .arg(K4Styles::Colors::OverlayContentBg)
                      .arg(K4Styles::Dimensions::BorderWidth)
                      .arg(QLatin1StringView(K4Styles::Colors::PanelBorder))
                      .arg(K4Styles::Dimensions::BorderRadiusLarge));
    setObjectName("memoryOverlay");

    auto *mainLayout = new QHBoxLayout(this);
    // Inside the border, and clear of it at the corners.
    const int frame = K4Styles::Dimensions::BorderWidth + 2;
    mainLayout->setContentsMargins(frame, frame, frame, frame);
    mainLayout->setSpacing(0);

    auto *content = new QWidget(this);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    m_titleLabel = new QLabel("RECALL", content);
    m_titleLabel->setStyleSheet(QString("background-color: %1; color: %2; font-size: %3px; "
                                        "font-weight: bold; padding: 8px 15px;")
                                    .arg(K4Styles::Colors::OverlayHeaderBg)
                                    .arg(K4Styles::Colors::TextSecondary)
                                    .arg(K4Styles::Dimensions::FontSizeReadout));
    contentLayout->addWidget(m_titleLabel);

    m_scrollArea = new QScrollArea(content);
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
    m_scrollArea->setWidget(m_listContainer);
    contentLayout->addWidget(m_scrollArea);
    mainLayout->addWidget(content, 1);

    // The key column: the mode key at the top and the back key at the bottom, as the radio draws
    // them. The knob and "A" icons between them say the VFO A dial scrolls the list, which a
    // software client has no use for.
    auto *navPanel = new QWidget(this);
    navPanel->setFixedWidth(K4Styles::Dimensions::NavPanelWidth);
    navPanel->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::OverlayHeaderBg));
    auto *navLayout = new QVBoxLayout(navPanel);
    navLayout->setContentsMargins(8, 12, 8, 12);
    navLayout->setSpacing(8);

    const QString keyStyle = QString("QPushButton { background-color: %1; color: %2; border: none; "
                                     "border-radius: %3px; font-size: %4px; font-weight: bold; }"
                                     "QPushButton:pressed { background-color: %5; }")
                                 .arg(K4Styles::Colors::OverlayNavButton)
                                 .arg(K4Styles::Colors::TextPrimary)
                                 .arg(K4Styles::Dimensions::BorderRadius)
                                 .arg(K4Styles::Dimensions::FontSizeIndicator)
                                 .arg(K4Styles::Colors::OverlayNavButtonPressed);

    m_actionBtn = new QPushButton("RCL", navPanel);
    m_actionBtn->setFixedHeight(K4Styles::Dimensions::PopupButtonHeight);
    m_actionBtn->setStyleSheet(keyStyle);
    connect(m_actionBtn, &QPushButton::clicked, this, &MemoryOverlayWidget::actOnSelection);
    navLayout->addWidget(m_actionBtn);

    // The same navigation keys the MENU overlay carries, in the same order and sizes. No NORM:
    // there is no default to restore a memory to.
    auto *stepRow = new QHBoxLayout();
    stepRow->setSpacing(8);
    m_upBtn = new QPushButton("\xE2\x96\xB2", navPanel);   // ▲
    m_downBtn = new QPushButton("\xE2\x96\xBC", navPanel); // ▼
    for (QPushButton *btn : {m_upBtn, m_downBtn}) {
        btn->setFixedSize(K4Styles::Dimensions::NavButtonWidth, K4Styles::Dimensions::PopupButtonHeight);
        btn->setStyleSheet(keyStyle);
        stepRow->addWidget(btn);
    }
    connect(m_upBtn, &QPushButton::clicked, this, [this]() { step(-1); });
    connect(m_downBtn, &QPushButton::clicked, this, [this]() { step(1); });
    navLayout->addLayout(stepRow);
    navLayout->addStretch();

    auto *bottomRow = new QHBoxLayout();
    bottomRow->setSpacing(8);
    m_searchBtn = new QPushButton("\xF0\x9F\x94\x8D", navPanel); // 🔍
    m_backBtn = new QPushButton("\xE2\x86\xA9", navPanel);       // ↩
    for (QPushButton *btn : {m_searchBtn, m_backBtn}) {
        btn->setFixedSize(K4Styles::Dimensions::NavButtonWidth, K4Styles::Dimensions::PopupButtonHeight);
        btn->setStyleSheet(keyStyle);
        bottomRow->addWidget(btn);
    }
    connect(m_searchBtn, &QPushButton::clicked, this, &MemoryOverlayWidget::toggleSearch);
    connect(m_backBtn, &QPushButton::clicked, this, &QWidget::hide);
    navLayout->addLayout(bottomRow);
    mainLayout->addWidget(navPanel);

    createSearchPopup();

    m_scrollArea->installEventFilter(this);
    m_scrollArea->viewport()->installEventFilter(this);

    buildRows();

    // The model is cleared on every disconnect and refilled by the next MCDF dump; the rows hold
    // channel numbers, so they only need redrawing.
    connect(m_model, &MemoryModel::modelCleared, this, &MemoryOverlayWidget::refreshAllRows);
    connect(m_model, &MemoryModel::channelChanged, this, &MemoryOverlayWidget::refreshRow);
    connect(m_model, &MemoryModel::selectedChannelChanged, this,
            [this](int channel) { selectChannel(channel, /*scrollIntoView=*/isVisible()); });

    hide();
}

void MemoryOverlayWidget::buildRows() {
    for (int channel = MemoryModel::MinChannel; channel <= MemoryModel::MaxChannel; ++channel) {
        auto *row = new MemoryRowWidget(channel, m_model, m_listContainer);
        connect(row, &MemoryRowWidget::clicked, this, [this, channel]() { selectChannel(channel); });
        connect(row, &MemoryRowWidget::nameAreaClicked, this, [this, channel, row]() {
            // The name cell is where a name goes, so clicking it is how one is written. Clicking
            // the rest of the row only selects. A blank channel has nothing to name, and
            // beginRename() refuses it.
            selectChannel(channel);
            row->beginRename();
        });
        connect(row, &MemoryRowWidget::renamed, this, &MemoryOverlayWidget::renameRequested);
        m_listLayout->addWidget(row);
        m_rows.append(row);
    }
    m_listLayout->addStretch();
    if (!m_rows.isEmpty())
        m_rows.first()->setSelected(true);
}

void MemoryOverlayWidget::refreshRow(int channel) {
    if (channel >= MemoryModel::MinChannel && channel <= MemoryModel::MaxChannel)
        m_rows.at(channel - MemoryModel::MinChannel)->updateDisplay();
}

void MemoryOverlayWidget::refreshAllRows() {
    for (MemoryRowWidget *row : m_rows)
        row->updateDisplay();
}

void MemoryOverlayWidget::createSearchPopup() {
    // The MENU overlay's search box, built the same way so the two lists are searched the same
    // way: a Qt::Popup hanging under the search key, as wide as the key column.
    m_searchPopup = new QWidget(this, Qt::Popup);
    m_searchPopup->setFixedWidth(K4Styles::Dimensions::NavPanelWidth);

    auto *layout = new QVBoxLayout(m_searchPopup);
    layout->setContentsMargins(SearchPopupMargin, SearchPopupMargin, SearchPopupMargin, SearchPopupMargin);

    m_searchEdit = new QLineEdit(m_searchPopup);
    m_searchEdit->setPlaceholderText(QStringLiteral("Search..."));
    m_searchEdit->setStyleSheet(QString("QLineEdit { background-color: %1; color: %2; border: %3px solid %4; "
                                        "border-radius: %5px; padding: 6px; font-size: %6px; }"
                                        "QLineEdit:focus { border-color: %7; }")
                                    .arg(QLatin1StringView(K4Styles::Colors::OverlayContentBg),
                                         QLatin1StringView(K4Styles::Colors::TextPrimary))
                                    .arg(K4Styles::Dimensions::SeparatorHeight)
                                    .arg(QLatin1StringView(K4Styles::Colors::OverlayNavButton))
                                    .arg(K4Styles::Dimensions::BorderRadius)
                                    .arg(K4Styles::Dimensions::FontSizeCaption)
                                    .arg(QLatin1StringView(K4Styles::Colors::VfoACyan)));
    layout->addWidget(m_searchEdit);
    m_searchPopup->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::OverlayHeaderBg));

    connect(m_searchEdit, &QLineEdit::textChanged, this, &MemoryOverlayWidget::applyFilter);

    // Sees the press that closes the box (see eventFilter).
    m_searchPopup->installEventFilter(this);
    m_searchPopup->hide();
}

void MemoryOverlayWidget::toggleSearch() {
    if (m_searchPopup->isVisible()) {
        m_searchPopup->hide();
        return;
    }
    m_searchPopup->move(m_searchBtn->mapToGlobal(QPoint(0, m_searchBtn->height() + SearchPopupGap)));
    m_searchPopup->show();
    m_searchEdit->setFocus();
    m_searchEdit->selectAll();
}

void MemoryOverlayWidget::applyFilter(const QString &text) {
    const QString needle = text.trimmed();
    for (MemoryRowWidget *row : m_rows) {
        if (needle.isEmpty()) {
            row->show();
            continue;
        }
        row->setVisible(row->matches(needle));
    }
    // Keep the selection on screen if it survived the filter.
    if (m_rows.at(m_selectedChannel - MemoryModel::MinChannel)->isVisibleTo(this))
        selectChannel(m_selectedChannel);
}

void MemoryOverlayWidget::showForMode(Mode mode) {
    m_mode = mode;
    m_titleLabel->setText(mode == Recall ? QStringLiteral("RECALL") : QStringLiteral("STORE"));
    m_actionBtn->setText(mode == Recall ? QStringLiteral("RCL") : QStringLiteral("STO"));
    // Open on the channel the radio has selected, so the list starts where the radio is.
    if (m_model->selectedChannel() >= MemoryModel::MinChannel)
        selectChannel(m_model->selectedChannel(), /*scrollIntoView=*/false);
    show();
    raise();
    setFocus(Qt::OtherFocusReason);
    selectChannel(m_selectedChannel); // scroll once the geometry is real
}

void MemoryOverlayWidget::selectChannel(int channel, bool scrollIntoView) {
    if (channel < MemoryModel::MinChannel || channel > MemoryModel::MaxChannel)
        return;
    m_selectedChannel = channel;
    for (MemoryRowWidget *row : m_rows)
        row->setSelected(row->channel() == channel);
    if (scrollIntoView)
        m_scrollArea->ensureWidgetVisible(m_rows.at(channel - MemoryModel::MinChannel));
}

void MemoryOverlayWidget::step(int delta) {
    selectChannel(qBound(MemoryModel::MinChannel, m_selectedChannel + delta, MemoryModel::MaxChannel));
}

void MemoryOverlayWidget::actOnSelection() {
    if (m_mode == Recall)
        emit recallRequested(m_selectedChannel);
    else
        emit storeRequested(m_selectedChannel);
    hide();
}

void MemoryOverlayWidget::keyPressEvent(QKeyEvent *event) {
    switch (event->key()) {
    case Qt::Key_Escape:
        hide();
        return;
    case Qt::Key_Up:
        step(-1);
        return;
    case Qt::Key_Down:
        step(1);
        return;
    case Qt::Key_PageUp:
        step(-10);
        return;
    case Qt::Key_PageDown:
        step(10);
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        actOnSelection();
        return;
    default:
        QWidget::keyPressEvent(event);
    }
}

bool MemoryOverlayWidget::eventFilter(QObject *watched, QEvent *event) {
    // WHY: on platforms that replay the press which closes a popup (Windows, X11; not macOS), the
    // press that dismissed the search box was replayed to the key under it - the search key - which
    // opened the box straight back up. Suppressing the replay for that one press is what makes a
    // second press on the key close the box. Qt clears the attribute before each new mouse event,
    // so it only ever covers this press, and a press anywhere else is still replayed.
    if (watched == m_searchPopup && event->type() == QEvent::MouseButtonPress) {
        const QPoint globalPos = static_cast<QMouseEvent *>(event)->globalPosition().toPoint();
        const QRect searchKey(m_searchBtn->mapToGlobal(QPoint(0, 0)), m_searchBtn->size());
        if (searchKey.contains(globalPos))
            m_searchPopup->setAttribute(Qt::WA_NoMouseReplay);
        return false; // the popup still handles the press: it closes itself
    }
    if (event->type() == QEvent::Wheel && (watched == m_scrollArea || watched == m_scrollArea->viewport())) {
        // The wheel scrolls the list rather than moving the selection: the selection is what the
        // action key acts on, and a wheel nudge must not arm a recall of the wrong channel.
        return false;
    }
    return QWidget::eventFilter(watched, event);
}

void MemoryOverlayWidget::hideEvent(QHideEvent *event) {
    // Leave no filter behind for the next open, exactly as the MENU overlay does.
    if (m_searchPopup)
        m_searchPopup->hide();
    if (m_searchEdit)
        m_searchEdit->clear();
    QWidget::hideEvent(event);
    emit closed();
}
