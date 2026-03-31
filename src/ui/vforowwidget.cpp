#include "vforowwidget.h"
#include "k4styles.h"
#include <QHBoxLayout>
#include <QResizeEvent>

// ============== VfoSquareWidget Implementation ==============

VfoSquareWidget::VfoSquareWidget(const QString &text, const QColor &color, QWidget *parent)
    : QWidget(parent), m_text(text), m_color(color) {
    // Size: 30 wide x 44 high (4 top pad + 10 arc space + 30 square)
    setFixedSize(30, 44);
    setCursor(Qt::PointingHandCursor);
}

void VfoSquareWidget::setLocked(bool locked) {
    if (m_locked != locked) {
        m_locked = locked;
        update();
    }
}

void VfoSquareWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int topPad = 4;     // Extra space so arc stroke doesn't clip at y=0
    const int arcHeight = 10; // Space reserved for lock arc at top
    const int squareSize = 30;
    const int borderRadius = 4;

    // Draw the rounded square (offset down by topPad + arcHeight)
    QRectF squareRect(0, topPad + arcHeight, squareSize, squareSize);
    p.setBrush(m_color);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(squareRect, borderRadius, borderRadius);

    // Draw "A" or "B" text
    p.setPen(QColor(K4Styles::Colors::DarkBackground));
    QFont font;
    font.setPixelSize(K4Styles::Dimensions::FontSizeTitle);
    font.setBold(true);
    p.setFont(font);
    p.drawText(squareRect, Qt::AlignCenter, m_text);

    // Draw lock arc if locked (creates padlock shackle effect)
    if (m_locked) {
        QPen arcPen(m_color, 4, Qt::SolidLine, Qt::RoundCap);
        p.setPen(arcPen);
        p.setBrush(Qt::NoBrush);

        // Arc rect: centered horizontally, connects to top of square
        // Arc should look like the top of a padlock
        int arcWidth = 18;
        int arcX = (squareSize - arcWidth) / 2;
        QRectF arcRect(arcX, topPad, arcWidth, arcHeight * 2);
        // Draw top half of ellipse (180 degrees starting from 0)
        p.drawArc(arcRect, 0, 180 * 16);
    }
}

// ============== VfoRowWidget Implementation ==============

VfoRowWidget::VfoRowWidget(QWidget *parent) : QWidget(parent) {
    setupWidgets();
    // Set fixed height to match content (A/B squares + mode labels)
    // Increased from 55 to 78 to accommodate lock arc padding + TEST label
    setFixedHeight(78);
}

void VfoRowWidget::setLockA(bool locked) {
    m_vfoASquare->setLocked(locked);
}

void VfoRowWidget::setLockB(bool locked) {
    m_vfoBSquare->setLocked(locked);
}

void VfoRowWidget::setTestVisible(bool visible) {
    m_testLabel->setVisible(visible);
    positionWidgets();
}

void VfoRowWidget::setupWidgets() {
    // No layout manager - we use absolute positioning
    // All containers are children of this widget

    // === VFO A Container (square + mode label) ===
    m_vfoAContainer = new QWidget(this);
    m_vfoAContainer->setFixedWidth(K4Styles::Dimensions::VfoSquareSize);
    auto *vfoAColumn = new QVBoxLayout(m_vfoAContainer);
    vfoAColumn->setContentsMargins(0, 0, 0, 0);
    vfoAColumn->setSpacing(2);

    m_vfoASquare = new VfoSquareWidget("A", QColor(K4Styles::Colors::VfoACyan), m_vfoAContainer);
    vfoAColumn->addWidget(m_vfoASquare, 0, Qt::AlignHCenter);

    m_modeALabel = new QLabel("USB", m_vfoAContainer);
    m_modeALabel->setFixedWidth(K4Styles::Dimensions::VfoSquareSize);
    m_modeALabel->setAlignment(Qt::AlignCenter);
    m_modeALabel->setCursor(Qt::PointingHandCursor);
    m_modeALabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                    .arg(K4Styles::Colors::TextWhite)
                                    .arg(K4Styles::Dimensions::FontSizeLarge));
    vfoAColumn->addWidget(m_modeALabel, 0, Qt::AlignHCenter);

    // TEST indicator - independent label, positioned above TX container
    m_testLabel = new QLabel("TEST", this);
    m_testLabel->setAlignment(Qt::AlignCenter);
    m_testLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                   .arg(K4Styles::Colors::TxRed)
                                   .arg(K4Styles::Dimensions::FontSizePopup));
    m_testLabel->setVisible(false);

    // === TX Container (triangles + TX) ===
    m_txContainer = new QWidget(this);
    auto *txVLayout = new QVBoxLayout(m_txContainer);
    txVLayout->setContentsMargins(0, 0, 0, 0);
    txVLayout->setSpacing(0);

    // TX row (triangles + TX label)
    auto *txIndicatorRow = new QHBoxLayout();
    txIndicatorRow->setSpacing(0);

    m_txTriangle = new QLabel(QString::fromUtf8("\u25C0"), m_txContainer); // ◀
    m_txTriangle->setFixedSize(K4Styles::Dimensions::ButtonHeightMini, K4Styles::Dimensions::ButtonHeightMini);
    m_txTriangle->setAlignment(Qt::AlignCenter);
    m_txTriangle->setStyleSheet(QString("color: %1; font-size: %2px;")
                                    .arg(K4Styles::Colors::AccentAmber)
                                    .arg(K4Styles::Dimensions::FontSizeIndicator));
    txIndicatorRow->addWidget(m_txTriangle);

    m_txIndicator = new QLabel("TX", m_txContainer);
    m_txIndicator->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                     .arg(K4Styles::Colors::AccentAmber)
                                     .arg(K4Styles::Dimensions::FontSizeIndicator));
    txIndicatorRow->addWidget(m_txIndicator);

    m_txTriangleB = new QLabel("", m_txContainer); // Empty by default
    m_txTriangleB->setFixedSize(K4Styles::Dimensions::ButtonHeightMini, K4Styles::Dimensions::ButtonHeightMini);
    m_txTriangleB->setAlignment(Qt::AlignCenter);
    m_txTriangleB->setStyleSheet(QString("color: %1; font-size: %2px;")
                                     .arg(K4Styles::Colors::AccentAmber)
                                     .arg(K4Styles::Dimensions::FontSizeIndicator));
    txIndicatorRow->addWidget(m_txTriangleB);

    txVLayout->addLayout(txIndicatorRow);

    // Adjust size to fit content
    m_txContainer->adjustSize();

    // === VFO B Container (square + mode label) ===
    m_vfoBContainer = new QWidget(this);
    m_vfoBContainer->setFixedWidth(K4Styles::Dimensions::VfoSquareSize);
    auto *vfoBColumn = new QVBoxLayout(m_vfoBContainer);
    vfoBColumn->setContentsMargins(0, 0, 0, 0);
    vfoBColumn->setSpacing(2);

    m_vfoBSquare = new VfoSquareWidget("B", QColor(K4Styles::Colors::VfoBGreen), m_vfoBContainer);
    vfoBColumn->addWidget(m_vfoBSquare, 0, Qt::AlignHCenter);

    m_modeBLabel = new QLabel("USB", m_vfoBContainer);
    m_modeBLabel->setFixedWidth(K4Styles::Dimensions::VfoSquareSize);
    m_modeBLabel->setAlignment(Qt::AlignCenter);
    m_modeBLabel->setCursor(Qt::PointingHandCursor);
    m_modeBLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                    .arg(K4Styles::Colors::TextWhite)
                                    .arg(K4Styles::Dimensions::FontSizeLarge));
    vfoBColumn->addWidget(m_modeBLabel, 0, Qt::AlignHCenter);

    // === SUB/DIV Container ===
    m_subDivContainer = new QWidget(this);
    auto *subDivStack = new QVBoxLayout(m_subDivContainer);
    subDivStack->setSpacing(4);
    subDivStack->setContentsMargins(0, 0, 0, 0);

    m_subLabel = new QLabel("SUB", m_subDivContainer);
    m_subLabel->setAlignment(Qt::AlignCenter);
    m_subLabel->setFixedSize(36, 14);
    m_subLabel->setStyleSheet(QString("background-color: %1;"
                                      "color: %2;"
                                      "font-size: %3px;"
                                      "font-weight: bold;"
                                      "border-radius: 2px;")
                                  .arg(K4Styles::Colors::DisabledBackground)
                                  .arg(K4Styles::Colors::LightGradientTop)
                                  .arg(K4Styles::Dimensions::FontSizeNormal));
    subDivStack->addWidget(m_subLabel);

    m_divLabel = new QLabel("DIV", m_subDivContainer);
    m_divLabel->setAlignment(Qt::AlignCenter);
    m_divLabel->setFixedSize(36, 14);
    m_divLabel->setStyleSheet(QString("background-color: %1;"
                                      "color: %2;"
                                      "font-size: %3px;"
                                      "font-weight: bold;"
                                      "border-radius: 2px;")
                                  .arg(K4Styles::Colors::DisabledBackground)
                                  .arg(K4Styles::Colors::LightGradientTop)
                                  .arg(K4Styles::Dimensions::FontSizeNormal));
    subDivStack->addWidget(m_divLabel);

    m_subDivContainer->adjustSize();

    // SPLIT indicator — centered under TX, aligned with mode labels
    m_splitLabel = new QLabel("SPLIT OFF", this);
    m_splitLabel->setAlignment(Qt::AlignCenter);
    m_splitLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                    .arg(K4Styles::Colors::AccentAmber)
                                    .arg(K4Styles::Dimensions::FontSizeButton));

    // B SET indicator — replaces SPLIT when active
    m_bSetLabel = new QLabel("B SET", this);
    m_bSetLabel->setAlignment(Qt::AlignCenter);
    m_bSetLabel->setStyleSheet(
        QString("background-color: %1; color: black; font-size: %2px; font-weight: bold; border-radius: 4px; "
                "padding: 2px 8px;")
            .arg(K4Styles::Colors::StatusGreen)
            .arg(K4Styles::Dimensions::FontSizeButton));
    m_bSetLabel->setVisible(false);

    // MSG Bank indicator — below SPLIT/BSET
    m_msgBankLabel = new QLabel("MSG: I", this);
    m_msgBankLabel->setAlignment(Qt::AlignCenter);
    m_msgBankLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                      .arg(K4Styles::Colors::AccentAmber)
                                      .arg(K4Styles::Dimensions::FontSizeButton));
}

void VfoRowWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    positionWidgets();
}

void VfoRowWidget::positionWidgets() {
    int w = width();
    int centerX = w / 2;
    int y = 3; // Offset down to prevent lock arc clipping at top

    // TX container - centered at widget center, aligned with square faces
    m_txContainer->adjustSize();
    int txWidth = m_txContainer->width();
    // Align TX row with the square face top: y + topPad(4) + arcHeight(10) = y + 14
    int txY = y + 14;
    m_txContainer->move(centerX - txWidth / 2, txY);

    // TEST label - centered above the TX container
    if (m_testLabel->isVisible()) {
        m_testLabel->adjustSize();
        int txCenterX = m_txContainer->x() + txWidth / 2;
        int testY = txY - m_testLabel->height() - 1; // 1px gap above TX
        m_testLabel->move(txCenterX - m_testLabel->width() / 2, qMax(0, testY));
    }

    // A container - left of TX with gap
    int gap = 25;
    m_vfoAContainer->move(centerX - txWidth / 2 - gap - m_vfoAContainer->width(), y);

    // B container - right of TX with gap (symmetric with A)
    m_vfoBContainer->move(centerX + txWidth / 2 + gap, y);

    // SUB/DIV - to right of B (doesn't affect centering), offset down to align
    m_subDivContainer->move(m_vfoBContainer->x() + m_vfoBContainer->width() + 10, txY);

    // SPLIT label — centered under TX, same row as mode labels
    m_splitLabel->adjustSize();
    m_splitLabel->move((w - m_splitLabel->width()) / 2, 49);

    // B SET label — same position as SPLIT (replaces it when visible)
    m_bSetLabel->adjustSize();
    m_bSetLabel->move((w - m_bSetLabel->width()) / 2, 46);

    // MSG Bank label — below SPLIT/BSET row
    m_msgBankLabel->adjustSize();
    m_msgBankLabel->move((w - m_msgBankLabel->width()) / 2, 65);
}
