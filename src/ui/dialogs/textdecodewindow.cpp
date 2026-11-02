#include "ui/dialogs/textdecodewindow.h"
#include "models/radiostate/textdecodestate.h"
#include "settings/radiosettings.h"
#include "ui/styling/k4styles.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QWheelEvent>

#include <algorithm>
#include <initializer_list>

namespace {
const int TitleBarHeight = K4Styles::Dimensions::ButtonHeightMedium;
const int BorderWidth = 4;
const int MinWidth = 350;
const int MinHeight = 150;
const int DefaultWidth = 400;
const int DefaultHeight = 300;
const int CloseButtonSize = K4Styles::Dimensions::ButtonHeightMini;
const int ControlButtonHeight = K4Styles::Dimensions::ButtonHeightMini;
const int ResizeGripSize = 16;
const int SpeedButtonMinWidth = 40;
const int AutoManualButtonMinWidth = 48;

// Fixes a key's width to the widest text it can show, never below `floorWidth`, so a text change
// never clips. WHY: an explicit minimum width overrides the button's own minimum size hint (Qt docs,
// QWidget::minimumSize), so a floor narrower than the text squeezed "MANUAL" into a 48 px key.
void fitWidthToTexts(QPushButton *button, std::initializer_list<const char *> texts, int floorWidth) {
    button->ensurePolished(); // the style sheet's font decides the width
    const QString shown = button->text();
    int widest = floorWidth;
    for (const char *text : texts) {
        button->setText(QString::fromUtf8(text));
        widest = (std::max)(widest, button->sizeHint().width());
    }
    button->setText(shown);
    button->setMinimumWidth(widest);
}
} // namespace

TextDecodeWindow::TextDecodeWindow(Receiver rx, QWidget *parent) : QWidget(parent), m_receiver(rx) {
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    resize(DefaultWidth, DefaultHeight);
    setupUi();
}

void TextDecodeWindow::setupUi() {
    // Main layout
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(BorderWidth, BorderWidth, BorderWidth, BorderWidth);
    mainLayout->setSpacing(0);
    // WHY: the window's minimum follows its layout, so it can never be narrower than the title row
    // the current mode shows. A fixed minimum would override the layout's (Qt docs,
    // QLayout::SizeConstraint), which is how the keys came to be drawn over each other.
    mainLayout->setSizeConstraint(QLayout::SetMinimumSize);

    // Title bar container
    auto *titleBar = new QWidget(this);
    titleBar->setFixedHeight(TitleBarHeight);
    auto *titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(K4Styles::Dimensions::PaddingSmall, K4Styles::Dimensions::PaddingSmall,
                                    K4Styles::Dimensions::PaddingSmall, K4Styles::Dimensions::PaddingSmall);
    titleLayout->setSpacing(2);

    // ON/OFF button
    m_onOffBtn = new QPushButton("OFF", titleBar);
    m_onOffBtn->setFixedHeight(ControlButtonHeight);
    m_onOffBtn->setMinimumWidth(36);
    m_onOffBtn->setCursor(Qt::PointingHandCursor);
    m_onOffBtn->setStyleSheet(K4Styles::controlButtonSelectable());

    // Speed button: WPM range in CW, data rate in AFSK/FSK/PSK
    m_wpmBtn = new QPushButton("8-45", titleBar);
    m_wpmBtn->setFixedHeight(ControlButtonHeight);
    m_wpmBtn->setCursor(Qt::PointingHandCursor);
    m_wpmBtn->setStyleSheet(controlButtonStyle(false));

    // AUTO/MANUAL button
    m_autoManualBtn = new QPushButton("AUTO", titleBar);
    m_autoManualBtn->setFixedHeight(ControlButtonHeight);
    m_autoManualBtn->setCursor(Qt::PointingHandCursor);
    m_autoManualBtn->setStyleSheet(K4Styles::controlButtonSelectable());
    K4Styles::setSelected(m_autoManualBtn, true); // AUTO is default (highlighted)

    // Threshold controls: [-] [5] [+]
    m_thresholdMinusBtn = new QPushButton("-", titleBar);
    m_thresholdMinusBtn->setFixedSize(ControlButtonHeight, ControlButtonHeight);
    m_thresholdMinusBtn->setCursor(Qt::PointingHandCursor);
    m_thresholdMinusBtn->setStyleSheet(controlButtonStyle(false));

    m_thresholdValueLabel = new QLabel("5", titleBar);
    m_thresholdValueLabel->setFixedSize(20, ControlButtonHeight);
    m_thresholdValueLabel->setAlignment(Qt::AlignCenter);
    m_thresholdValueLabel->setStyleSheet(QString("QLabel { color: %1; font-size: %2px; font-weight: bold; }")
                                             .arg(K4Styles::Colors::DarkBackground)
                                             .arg(K4Styles::Dimensions::FontSizeCaption));

    m_thresholdPlusBtn = new QPushButton("+", titleBar);
    m_thresholdPlusBtn->setFixedSize(ControlButtonHeight, ControlButtonHeight);
    m_thresholdPlusBtn->setCursor(Qt::PointingHandCursor);
    m_thresholdPlusBtn->setStyleSheet(controlButtonStyle(false));

    // CLR button - clear text buffer (local only)
    m_clearBtn = new QPushButton("CLR", titleBar);
    m_clearBtn->setFixedHeight(ControlButtonHeight);
    m_clearBtn->setMinimumWidth(36);
    m_clearBtn->setCursor(Qt::PointingHandCursor);
    m_clearBtn->setStyleSheet(controlButtonStyle(false));

    // A- / A+ buttons - shrink/grow decoded-text font size (per receiver, persisted)
    m_fontMinusBtn = new QPushButton(QString::fromUtf8("A−"), titleBar); // A−
    m_fontMinusBtn->setFixedHeight(ControlButtonHeight);
    m_fontMinusBtn->setMinimumWidth(36);
    m_fontMinusBtn->setCursor(Qt::PointingHandCursor);
    m_fontMinusBtn->setStyleSheet(controlButtonStyle(false));
    m_fontMinusBtn->setToolTip("Decrease decoded text size");

    m_fontPlusBtn = new QPushButton("A+", titleBar);
    m_fontPlusBtn->setFixedHeight(ControlButtonHeight);
    m_fontPlusBtn->setMinimumWidth(36);
    m_fontPlusBtn->setCursor(Qt::PointingHandCursor);
    m_fontPlusBtn->setStyleSheet(controlButtonStyle(false));
    m_fontPlusBtn->setToolTip("Increase decoded text size");

    // Title label - smaller, right-aligned
    QString titleText = (m_receiver == MainRx) ? "MAIN RX" : "SUB RX";
    m_titleLabel = new QLabel(titleText, titleBar);
    m_titleLabel->setStyleSheet(QString("QLabel { color: %1; font-size: %2px; font-weight: bold; }")
                                    .arg(K4Styles::Colors::DarkBackground)
                                    .arg(K4Styles::Dimensions::FontSizeCaption));

    // Close button - dark text to match
    m_closeBtn = new QPushButton("\u2715", titleBar); // ✕
    m_closeBtn->setFixedSize(CloseButtonSize, CloseButtonSize);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setStyleSheet(
        QString("QPushButton {"
                "  background: transparent;"
                "  color: %1;"
                "  border: none;"
                "  font-size: %2px;"
                "  font-weight: bold;"
                "}"
                "QPushButton:hover {"
                "  background: %3;"
                "  border-radius: 4px;"
                "}")
            .arg(K4Styles::Colors::DarkBackground)
            .arg(K4Styles::Dimensions::FontSizeHeading)
            .arg(K4Styles::cssRgba(K4Styles::Colors::Black, K4Styles::Colors::Alpha::CloseButtonHover)));

    // Layout: [ON][WPM][AUTO][-][5][+][CLR][A-][A+] <stretch> TITLE [X]
    titleLayout->addWidget(m_onOffBtn);
    titleLayout->addWidget(m_wpmBtn);
    titleLayout->addWidget(m_autoManualBtn);
    titleLayout->addWidget(m_thresholdMinusBtn);
    titleLayout->addWidget(m_thresholdValueLabel);
    titleLayout->addWidget(m_thresholdPlusBtn);
    titleLayout->addWidget(m_clearBtn);
    titleLayout->addWidget(m_fontMinusBtn);
    titleLayout->addWidget(m_fontPlusBtn);
    titleLayout->addStretch();
    titleLayout->addWidget(m_titleLabel);
    titleLayout->addWidget(m_closeBtn);

    // Text display area
    m_textDisplay = new QPlainTextEdit(this);
    m_textDisplay->setReadOnly(true);
    m_textDisplay->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    // The window's floor size, carried by the text area so the layout includes it.
    m_textDisplay->setMinimumSize(MinWidth - 2 * BorderWidth, MinHeight - TitleBarHeight - 2 * BorderWidth);

    // Initial font size from persisted settings (per receiver), then apply stylesheet + line spacing.
    m_textFontSize = RadioSettings::instance()->textDecodeFontSize(m_receiver == SubRx);
    applyTextFontSize();

    mainLayout->addWidget(titleBar);
    mainLayout->addWidget(m_textDisplay, 1);

    // Connect close button
    connect(m_closeBtn, &QPushButton::clicked, this, [this]() { emit closeRequested(); });

    // CLR button - clear local text buffer
    connect(m_clearBtn, &QPushButton::clicked, this, [this]() { clearText(); });

    // A- / A+ buttons - adjust decoded-text font size, persist per receiver
    connect(m_fontMinusBtn, &QPushButton::clicked, this, [this]() {
        if (m_textFontSize <= K4Styles::Dimensions::FontSizeTextDecodeMin)
            return;
        m_textFontSize -= 1;
        applyTextFontSize();
        RadioSettings::instance()->setTextDecodeFontSize(m_receiver == SubRx, m_textFontSize);
    });
    connect(m_fontPlusBtn, &QPushButton::clicked, this, [this]() {
        if (m_textFontSize >= K4Styles::Dimensions::FontSizeTextDecodeMax)
            return;
        m_textFontSize += 1;
        applyTextFontSize();
        RadioSettings::instance()->setTextDecodeFontSize(m_receiver == SubRx, m_textFontSize);
    });

    // ON/OFF toggle
    connect(m_onOffBtn, &QPushButton::clicked, this, [this]() {
        m_decodeEnabled = !m_decodeEnabled;
        updateButtonStates();
        emit enabledChanged(m_decodeEnabled);
    });

    // Speed button click: CW cycles WPM range, DATA modes toggle data rate
    connect(m_wpmBtn, &QPushButton::clicked, this, [this]() {
        if (m_operatingMode == ModeCW) {
            m_wpmRange = (m_wpmRange + 1) % 3;
            updateSpeedButton();
            emit wpmRangeChanged(m_wpmRange);
        } else if (m_operatingMode == ModeAFSK || m_operatingMode == ModeFSK || m_operatingMode == ModePSK) {
            m_dataRate = (m_dataRate == 0) ? 1 : 0;
            updateSpeedButton();
            emit dataRateChanged(m_dataRate);
        }
    });

    // AUTO/MANUAL toggle
    connect(m_autoManualBtn, &QPushButton::clicked, this, [this]() {
        m_autoThreshold = !m_autoThreshold;
        updateThresholdControls();
        emit thresholdModeChanged(m_autoThreshold);
    });

    // Threshold -
    connect(m_thresholdMinusBtn, &QPushButton::clicked, this, [this]() {
        if (!m_autoThreshold && m_threshold > 1) {
            m_threshold--;
            m_thresholdValueLabel->setText(QString::number(m_threshold));
            emit thresholdChanged(m_threshold);
        }
    });

    // Threshold +
    connect(m_thresholdPlusBtn, &QPushButton::clicked, this, [this]() {
        if (!m_autoThreshold && m_threshold < 9) {
            m_threshold++;
            m_thresholdValueLabel->setText(QString::number(m_threshold));
            emit thresholdChanged(m_threshold);
        }
    });

    fitWidthToTexts(m_autoManualBtn, {"AUTO", "MANUAL"}, AutoManualButtonMinWidth);

    // Initial state
    updateModeVisibility();
    updateThresholdControls();
}

void TextDecodeWindow::appendText(const QString &text) {
    m_textDisplay->moveCursor(QTextCursor::End);
    m_textDisplay->insertPlainText(text);
    m_textDisplay->moveCursor(QTextCursor::End);
    trimToMaxLines();
}

void TextDecodeWindow::clearText() {
    m_textDisplay->clear();
    // Reapply line spacing after clear resets formatting
    QTextCursor cursor = m_textDisplay->textCursor();
    QTextBlockFormat blockFormat;
    blockFormat.setLineHeight(150, QTextBlockFormat::ProportionalHeight);
    cursor.mergeBlockFormat(blockFormat);
    m_textDisplay->setTextCursor(cursor);
}

void TextDecodeWindow::setMaxLines(int lines) {
    m_maxLines = qBound(TextDecodeState::LinesMin, lines, TextDecodeState::LinesMax);
    trimToMaxLines();
}

void TextDecodeWindow::trimToMaxLines() {
    QTextDocument *doc = m_textDisplay->document();
    int blockCount = doc->blockCount();
    if (blockCount > m_maxLines) {
        QTextCursor cursor(doc);
        cursor.movePosition(QTextCursor::Start);
        for (int i = 0; i < blockCount - m_maxLines; ++i) {
            cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
        }
        cursor.removeSelectedText();
    }
}

QRect TextDecodeWindow::titleBarRect() const {
    return QRect(BorderWidth, BorderWidth, width() - 2 * BorderWidth, TitleBarHeight);
}

QRect TextDecodeWindow::resizeGripRect() const {
    return QRect(width() - ResizeGripSize, height() - ResizeGripSize, ResizeGripSize, ResizeGripSize);
}

void TextDecodeWindow::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Get border color based on receiver
    QColor borderColor =
        (m_receiver == MainRx) ? QColor(K4Styles::Colors::VfoACyan) : QColor(K4Styles::Colors::VfoBGreen);

    // Draw drop shadow
    QRect contentRect = rect().adjusted(0, 0, 0, 0);
    K4Styles::drawDropShadow(painter, contentRect.adjusted(4, 4, -4, -4), 8);

    // Draw main background
    painter.setBrush(QColor(K4Styles::Colors::Background));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(rect().adjusted(2, 2, -2, -2), 8, 8);

    // Draw colored border at top (title bar area)
    painter.setBrush(borderColor);
    painter.setPen(Qt::NoPen);

    // Top border with rounded corners
    QPainterPath topPath;
    topPath.addRoundedRect(QRectF(2, 2, width() - 4, TitleBarHeight + BorderWidth), 8, 8);
    QPainterPath bottomClip;
    bottomClip.addRect(QRectF(0, TitleBarHeight, width(), BorderWidth + 10));
    topPath = topPath.subtracted(bottomClip);
    painter.fillPath(topPath, borderColor);

    // Draw left border stripe
    painter.fillRect(QRect(2, TitleBarHeight, BorderWidth, height() - TitleBarHeight - 8), borderColor);

    // Draw right border stripe
    painter.fillRect(QRect(width() - 2 - BorderWidth, TitleBarHeight, BorderWidth, height() - TitleBarHeight - 8),
                     borderColor);

    // Draw bottom border stripe
    QPainterPath bottomPath;
    bottomPath.addRoundedRect(QRectF(2, height() - BorderWidth - 6, width() - 4, BorderWidth + 4), 8, 8);
    QPainterPath topClip;
    topClip.addRect(QRectF(0, 0, width(), height() - BorderWidth - 2));
    bottomPath = bottomPath.subtracted(topClip);
    painter.fillPath(bottomPath, borderColor);

    // Draw resize grip indicator (three diagonal lines)
    painter.setPen(QPen(borderColor.lighter(150), 1));
    int gx = width() - 14;
    int gy = height() - 14;
    for (int i = 0; i < 3; ++i) {
        painter.drawLine(gx + i * 4, gy + 10, gx + 10, gy + i * 4);
    }
}

void TextDecodeWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        if (resizeGripRect().contains(event->pos())) {
            m_resizing = true;
            m_resizeStartPos = event->globalPosition().toPoint();
            m_resizeStartSize = size();
        } else if (titleBarRect().contains(event->pos())) {
            m_dragging = true;
            m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
        }
    }
    QWidget::mousePressEvent(event);
}

void TextDecodeWindow::mouseMoveEvent(QMouseEvent *event) {
    if (m_dragging) {
        move(event->globalPosition().toPoint() - m_dragPosition);
    } else if (m_resizing) {
        QPoint delta = event->globalPosition().toPoint() - m_resizeStartPos;
        int newWidth = (std::max)(minimumWidth(), m_resizeStartSize.width() + delta.x());
        int newHeight = (std::max)(minimumHeight(), m_resizeStartSize.height() + delta.y());
        resize(newWidth, newHeight);
    } else {
        // Update cursor for resize grip
        if (resizeGripRect().contains(event->pos())) {
            setCursor(Qt::SizeFDiagCursor);
        } else if (titleBarRect().contains(event->pos())) {
            setCursor(Qt::SizeAllCursor);
        } else {
            setCursor(Qt::ArrowCursor);
        }
    }
    QWidget::mouseMoveEvent(event);
}

void TextDecodeWindow::mouseReleaseEvent(QMouseEvent *event) {
    m_dragging = false;
    m_resizing = false;
    QWidget::mouseReleaseEvent(event);
}

void TextDecodeWindow::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    update(); // Repaint border
}

void TextDecodeWindow::wheelEvent(QWheelEvent *event) {
    // Scroll wheel adjusts threshold when in manual mode
    if (!m_autoThreshold && m_operatingMode == ModeCW) {
        int steps = m_wheelAccumulator.accumulate(event);
        if (steps != 0) {
            int newVal = qBound(1, m_threshold + steps, 9);
            if (newVal != m_threshold) {
                m_threshold = newVal;
                m_thresholdValueLabel->setText(QString::number(m_threshold));
                emit thresholdChanged(m_threshold);
            }
        }
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

QString TextDecodeWindow::controlButtonStyle(bool selected) const {
    return K4Styles::controlButton(selected);
}

void TextDecodeWindow::applyTextFontSize() {
    // Only the font size varies. Re-applying mergeBlockFormat after the size change re-anchors the
    // 1.5x line spacing on every existing block so scrollback re-flows cleanly.
    m_textDisplay->setStyleSheet(QString("QPlainTextEdit {"
                                         "  background: %1;"
                                         "  color: %2;"
                                         "  border: none;"
                                         "  font-family: '%6';"
                                         "  font-size: %3px;"
                                         "  padding: 8px;"
                                         "}"
                                         "QScrollBar:vertical {"
                                         "  background: %4;"
                                         "  width: 10px;"
                                         "  border-radius: 5px;"
                                         "}"
                                         "QScrollBar::handle:vertical {"
                                         "  background: %5;"
                                         "  border-radius: 5px;"
                                         "  min-height: 20px;"
                                         "}"
                                         "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
                                         "  height: 0px;"
                                         "}")
                                     .arg(K4Styles::Colors::DarkBackground)
                                     .arg(K4Styles::Colors::TextPrimary)
                                     .arg(m_textFontSize)
                                     .arg(K4Styles::Colors::Background)
                                     .arg(K4Styles::Colors::BorderNormal)
                                     .arg(K4Styles::Fonts::Data));

    QTextCursor cursor(m_textDisplay->document());
    QTextBlockFormat blockFormat;
    blockFormat.setLineHeight(150, QTextBlockFormat::ProportionalHeight);
    cursor.select(QTextCursor::Document);
    cursor.mergeBlockFormat(blockFormat);
}

void TextDecodeWindow::updateButtonStates() {
    m_onOffBtn->setText(m_decodeEnabled ? "ON" : "OFF");
    K4Styles::setSelected(m_onOffBtn, m_decodeEnabled);
}

void TextDecodeWindow::updateSpeedButton() {
    // Sized for the widest label of the current mode, so cycling the range never changes the key's width.
    switch (m_operatingMode) {
    case ModeCW:
        fitWidthToTexts(m_wpmBtn, {"8-45", "8-60", "8-90"}, SpeedButtonMinWidth);
        break;
    case ModeAFSK:
    case ModeFSK:
        fitWidthToTexts(m_wpmBtn, {"RTTY45", "RTTY75"}, SpeedButtonMinWidth);
        break;
    case ModePSK:
        fitWidthToTexts(m_wpmBtn, {"PSK31", "PSK63"}, SpeedButtonMinWidth);
        break;
    default:
        break;
    }

    switch (m_operatingMode) {
    case ModeCW: {
        static const char *wpmLabels[] = {"8-45", "8-60", "8-90"};
        m_wpmBtn->setText(wpmLabels[m_wpmRange]);
        break;
    }
    case ModeAFSK:
    case ModeFSK:
        m_wpmBtn->setText(m_dataRate == 0 ? "RTTY45" : "RTTY75");
        break;
    case ModePSK:
        m_wpmBtn->setText(m_dataRate == 0 ? "PSK31" : "PSK63");
        break;
    default:
        break;
    }
}

void TextDecodeWindow::updateThresholdControls() {
    m_autoManualBtn->setText(m_autoThreshold ? "AUTO" : "MANUAL");
    K4Styles::setSelected(m_autoManualBtn, m_autoThreshold);

    // Enable/disable threshold +/- buttons based on mode
    bool enableThreshold = !m_autoThreshold && m_operatingMode == ModeCW;
    m_thresholdMinusBtn->setEnabled(enableThreshold);
    m_thresholdPlusBtn->setEnabled(enableThreshold);
    m_thresholdValueLabel->setEnabled(enableThreshold);
}

void TextDecodeWindow::updateModeVisibility() {
    bool isCW = (m_operatingMode == ModeCW);
    bool hasSpeed = isCW || m_operatingMode == ModeAFSK || m_operatingMode == ModeFSK || m_operatingMode == ModePSK;

    m_wpmBtn->setVisible(hasSpeed);
    m_autoManualBtn->setVisible(isCW);
    m_thresholdMinusBtn->setVisible(isCW);
    m_thresholdValueLabel->setVisible(isCW);
    m_thresholdPlusBtn->setVisible(isCW);

    if (hasSpeed)
        updateSpeedButton();
}

void TextDecodeWindow::setDecodeEnabled(bool enabled) {
    if (m_decodeEnabled != enabled) {
        m_decodeEnabled = enabled;
        updateButtonStates();
    }
}

void TextDecodeWindow::setWpmRange(int range) {
    if (m_wpmRange != range && range >= 0 && range <= 2) {
        m_wpmRange = range;
        updateSpeedButton();
    }
}

void TextDecodeWindow::setAutoThreshold(bool isAuto) {
    if (m_autoThreshold != isAuto) {
        m_autoThreshold = isAuto;
        updateThresholdControls();
    }
}

void TextDecodeWindow::setThreshold(int value) {
    if (m_threshold != value && value >= 1 && value <= 9) {
        m_threshold = value;
        m_thresholdValueLabel->setText(QString::number(m_threshold));
    }
}

void TextDecodeWindow::setDataRate(int rate) {
    if (m_dataRate != rate && rate >= 0 && rate <= 1) {
        m_dataRate = rate;
        updateSpeedButton();
    }
}

void TextDecodeWindow::setOperatingMode(OperatingMode mode) {
    if (m_operatingMode != mode) {
        m_operatingMode = mode;
        updateModeVisibility();
        updateThresholdControls();
    }
}
