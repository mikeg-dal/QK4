#include "ui/overlays/memoryoverlay.h"

#include "models/memorymodel.h"
#include "ui/styling/k4styles.h"

#include <QHBoxLayout>
#include <QKeyEvent>
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
constexpr double NameZoneFraction = 0.35;
constexpr int RowPaddingH = 15;
constexpr int RowPaddingV = 5;
constexpr int RowSpacing = 10;

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
    if (!m_selected)
        return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const int demarcX = static_cast<int>(width() * NameZoneFraction);
    painter.fillRect(QRect(0, 0, demarcX, height()), QColor(K4Styles::Colors::SelectionLight));
    painter.fillRect(QRect(demarcX, 0, width() - demarcX, height()), QColor(K4Styles::Colors::SelectionDark));
}

void MemoryRowWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton)
        emit clicked();
    QWidget::mousePressEvent(event);
}

// =============================================================================
// MemoryOverlayWidget
// =============================================================================

MemoryOverlayWidget::MemoryOverlayWidget(MemoryModel *model, QWidget *parent) : QWidget(parent), m_model(model) {
    setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(true);
    setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::OverlayContentBg));

    auto *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
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
    navLayout->addStretch();

    m_backBtn = new QPushButton("\xE2\x86\xA9", navPanel); // ↩
    m_backBtn->setFixedHeight(K4Styles::Dimensions::PopupButtonHeight);
    m_backBtn->setStyleSheet(keyStyle);
    connect(m_backBtn, &QPushButton::clicked, this, &QWidget::hide);
    navLayout->addWidget(m_backBtn);
    mainLayout->addWidget(navPanel);

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
    if (event->type() == QEvent::Wheel && (watched == m_scrollArea || watched == m_scrollArea->viewport())) {
        // The wheel scrolls the list rather than moving the selection: the selection is what the
        // action key acts on, and a wheel nudge must not arm a recall of the wrong channel.
        return false;
    }
    return QWidget::eventFilter(watched, event);
}

void MemoryOverlayWidget::hideEvent(QHideEvent *event) {
    QWidget::hideEvent(event);
    emit closed();
}
