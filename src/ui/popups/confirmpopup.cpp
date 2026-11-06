#include "ui/popups/confirmpopup.h"

#include "ui/styling/k4styles.h"

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
constexpr int kBodyMaxWidth = 360;
constexpr int kVerticalSpacing = 10;
constexpr int kButtonRowSpacing = 10;
constexpr int kButtonMinWidth = 100;
} // namespace

ConfirmPopup::ConfirmPopup(const QString &title, const QString &body, const QString &confirmLabel,
                           const QString &cancelLabel, QWidget *parent)
    : K4PopupBase(parent), m_titleLabel(new QLabel(title, this)), m_bodyLabel(new QLabel(body, this)),
      m_confirmButton(new QPushButton(confirmLabel, this)), m_cancelButton(new QPushButton(cancelLabel, this)) {

    m_titleLabel->setStyleSheet(K4Styles::Dialog::titleLabel());

    m_bodyLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    m_bodyLabel->setWordWrap(true);
    m_bodyLabel->setMaximumWidth(kBodyMaxWidth);

    m_confirmButton->setStyleSheet(K4Styles::menuBarButtonActive());
    m_confirmButton->setMinimumWidth(kButtonMinWidth);
    m_confirmButton->setMinimumHeight(K4Styles::Dimensions::ButtonHeightMedium);
    m_confirmButton->setFocusPolicy(Qt::NoFocus);

    m_cancelButton->setStyleSheet(K4Styles::menuBarButton());
    m_cancelButton->setMinimumWidth(kButtonMinWidth);
    m_cancelButton->setMinimumHeight(K4Styles::Dimensions::ButtonHeightMedium);
    m_cancelButton->setFocusPolicy(Qt::NoFocus);

    connect(m_confirmButton, &QPushButton::clicked, this, &ConfirmPopup::onConfirmClicked);
    connect(m_cancelButton, &QPushButton::clicked, this, &ConfirmPopup::onCancelClicked);

    // Click-away / Escape (handled by K4PopupBase) reach us via closed().
    // Emit cancelled() unless the user explicitly confirmed. The latch belongs to one show, so it is
    // consumed here; a reused popup must report a later cancel.
    connect(this, &K4PopupBase::closed, this, [this]() {
        if (!m_confirmedLatch)
            emit cancelled();
        m_confirmedLatch = false;
    });

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(contentMargins());
    layout->setSpacing(kVerticalSpacing);
    layout->addWidget(m_titleLabel);
    layout->addWidget(m_bodyLabel);
    layout->addSpacing(kVerticalSpacing);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(kButtonRowSpacing);
    buttonRow->addStretch();
    buttonRow->addWidget(m_cancelButton);
    buttonRow->addWidget(m_confirmButton);
    layout->addLayout(buttonRow);

    // Compute and cache content size for K4PopupBase::initPopup().
    const int cm = K4Styles::Dimensions::PopupContentMargin;
    const QFontMetrics bodyMetrics(m_bodyLabel->font());
    const int bodyTextWidth = std::min(bodyMetrics.horizontalAdvance(body), kBodyMaxWidth);
    const int bodyHeight = bodyMetrics.boundingRect(0, 0, kBodyMaxWidth, 0, Qt::TextWordWrap, body).height();
    const QFontMetrics titleMetrics(m_titleLabel->font());
    const int titleWidth = titleMetrics.horizontalAdvance(title);
    const int titleHeight = titleMetrics.height();
    const int buttonsWidth = 2 * kButtonMinWidth + kButtonRowSpacing;
    const int width = std::max({titleWidth, bodyTextWidth, buttonsWidth}) + 2 * cm;
    const int height = titleHeight + kVerticalSpacing + bodyHeight + 2 * kVerticalSpacing +
                       K4Styles::Dimensions::ButtonHeightMedium + 2 * cm;
    m_contentSize = QSize(width, height);

    initPopup();
}

QSize ConfirmPopup::contentSize() const {
    return m_contentSize;
}

void ConfirmPopup::onConfirmClicked() {
    m_confirmedLatch = true;
    emit confirmed();
    hidePopup();
}

void ConfirmPopup::onCancelClicked() {
    hidePopup(); // closed() handler emits cancelled().
}
