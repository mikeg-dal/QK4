#include "ui/widgets/frequencydisplaywidget.h"
#include "ui/styling/k4styles.h"
#include "utils/radioutils.h"
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QFontMetrics>

#include <algorithm>

namespace {
// WHY 10 and not kDigits: the widget keeps the width it always had (X.XXX.XXX.XXX), so the VFO layout
// does not move. An 11-digit frequency (10 GHz and up) fits by setting its characters closer.
constexpr int kMinimumWidthDigits = 10;
constexpr int kMinimumWidthDots = 3;
constexpr int kPadding = 4;
constexpr int kUnderlineInset = 2;
constexpr int kUnderlineHeight = 2;
constexpr int kUnderlineFromBottom = 4;
} // namespace

FrequencyDisplayWidget::FrequencyDisplayWidget(QWidget *parent)
    : QWidget(parent), m_digits(QString(kDigits, '0')), m_normalColor(K4Styles::Colors::TextPrimary),
      m_editColor(K4Styles::Colors::VfoACyan) {

    // Set up font with tabular figures for consistent digit widths
    m_font = K4Styles::Fonts::dataFont(K4Styles::Dimensions::FontSizeDisplay);

    QFontMetrics fm(m_font);
    m_charWidth = fm.horizontalAdvance('0');
    m_dotWidth = fm.horizontalAdvance('.');

    // Widget configuration
    setFocusPolicy(Qt::ClickFocus);
    setMouseTracking(true); // hand cursor only over digits that can be a tuning rate

    setMinimumWidth(m_charWidth * kMinimumWidthDigits + m_dotWidth * kMinimumWidthDots + kPadding);
    setFixedHeight(K4Styles::Dimensions::MenuItemHeight);
}

void FrequencyDisplayWidget::setFrequency(const QString &frequency) {
    // WHY: while the operator is typing, the radio keeps reporting its frequency. Hold the latest report
    // instead of overwriting the entry; Escape/click-outside then shows the radio's current value.
    if (m_editing) {
        m_radioDigits = normalizeDigits(frequency);
        return;
    }
    m_digits = normalizeDigits(frequency);
    update();
}

QString FrequencyDisplayWidget::frequency() const {
    return m_editing ? m_typed : m_digits;
}

QString FrequencyDisplayWidget::displayText() const {
    if (m_editing)
        return groupWithDots(m_typed);
    return groupWithDots(m_digits.mid(displayStartIndex()));
}

void FrequencyDisplayWidget::setEditModeColor(const QColor &color) {
    m_editColor = color;
    if (m_editing)
        update();
}

void FrequencyDisplayWidget::setNormalColor(const QColor &color) {
    m_normalColor = color;
    if (!m_editing)
        update();
}

void FrequencyDisplayWidget::setTuningRateDigit(int digitFromRight) {
    if (m_tuningRateDigit != digitFromRight) {
        m_tuningRateDigit = digitFromRight;
        update();
    }
}

bool FrequencyDisplayWidget::isEditing() const {
    return m_editing;
}

void FrequencyDisplayWidget::beginEntry() {
    if (m_editing || m_entryLocked)
        return;
    m_radioDigits = m_digits;
    m_typed.clear();
    m_editing = true;
    setFocus();
    grabMouse(); // Capture all mouse events to detect clicks outside
    update();
}

void FrequencyDisplayWidget::setEntryLocked(bool locked) {
    m_entryLocked = locked;
    if (locked)
        exitEditMode(false);
}

void FrequencyDisplayWidget::cancelEntry() {
    exitEditMode(false);
}

QString FrequencyDisplayWidget::normalizeDigits(const QString &freq) {
    // Remove dots and any non-digit characters
    QString digits;
    for (const QChar &c : freq) {
        if (c.isDigit())
            digits.append(c);
    }
    // Pad left with zeros to kDigits. More than kDigits is not a K4 frequency; keep the rightmost.
    digits = digits.rightJustified(kDigits, '0');
    if (digits.size() > kDigits)
        digits = digits.right(kDigits);
    return digits;
}

int FrequencyDisplayWidget::displayStartIndex() const {
    // Default to showing the rightmost 8 digits (HF defaults to "0.000.000" with one
    // suppressed leading zero, 14 MHz reads "14.074.000", 7 MHz reads "7.024.980").
    // For higher frequencies, additional leading digits are revealed automatically:
    // 144 MHz → "144.100.000", 1.296 GHz → "1.296.000.000", 10.368 GHz → "10.368.000.000".
    constexpr int kBaseVisible = 8;
    constexpr int kBaseStart = kDigits - kBaseVisible; // index of the 8-digit-wide window
    int start = 0;
    while (start < kBaseStart && m_digits[start] == '0')
        ++start;
    // Below 10 MHz strip the one remaining leading zero so 7 MHz
    // renders as "7.024.980" not "07.024.980".
    if (start == kBaseStart && m_digits[start] == '0')
        ++start;
    return start;
}

QString FrequencyDisplayWidget::groupWithDots(const QString &digits) {
    QString result;
    const int n = static_cast<int>(digits.size());
    for (int i = 0; i < n; ++i) {
        result.append(digits[i]);
        const int fromRight = n - i - 1;
        if (fromRight > 0 && fromRight % 3 == 0)
            result.append('.');
    }
    return result;
}

qreal FrequencyDisplayWidget::pitchFor(const QString &text) const {
    int natural = kPadding;
    for (qsizetype i = 0; i < text.size(); ++i)
        natural += text.at(i).unicode() == u'.' ? m_dotWidth : m_charWidth;
    if (m_editing)
        natural += m_charWidth; // the caret after the last digit
    if (natural <= width() || natural <= 0)
        return 1.0;
    // WHY closer, not smaller: the frequency keeps its type size, which the operator reads at a glance
    // and which the other VFO shows; only the gaps between characters give way.
    return qreal(width()) / natural;
}

int FrequencyDisplayWidget::tuningDigitAtX(int x) const {
    if (m_editing)
        return -1;
    const QString display = displayText();
    const qreal pitch = pitchFor(display);
    qreal currentX = 0;
    int digitIdx = displayStartIndex();
    for (qsizetype i = 0; i < display.size(); ++i) {
        const char16_t c = display.at(i).unicode();
        const qreal charW = (c == u'.' ? m_dotWidth : m_charWidth) * pitch;
        if (x >= currentX && x < currentX + charW) {
            if (c == u'.')
                return -1;
            const int digitFromRight = kMaxDigitIndex - digitIdx;
            return RadioUtils::tuningStepForDigit(digitFromRight) >= 0 ? digitFromRight : -1;
        }
        if (c != u'.')
            ++digitIdx;
        currentX += charW;
    }
    return -1;
}

void FrequencyDisplayWidget::exitEditMode(bool send) {
    if (!m_editing)
        return;

    releaseMouse();
    m_editing = false;
    if (send && !m_typed.isEmpty()) {
        m_digits = normalizeDigits(m_typed); // shown until the radio reports the new frequency
        emit frequencyEntered(m_digits);
    } else {
        m_digits = m_radioDigits;
    }
    m_typed.clear();
    clearFocus();
    update();
}

void FrequencyDisplayWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setFont(m_font);

    const QString display = displayText();
    const qreal pitch = pitchFor(display);
    const int underlineY = height() - kUnderlineFromBottom;

    qreal x = 0;
    int digitIdx = displayStartIndex();
    for (qsizetype i = 0; i < display.size(); ++i) {
        const char16_t c = display.at(i).unicode();
        const qreal charW = (c == u'.' ? m_dotWidth : m_charWidth) * pitch;

        QColor charColor;
        if (m_editing) {
            charColor = m_editColor;
        } else if (c == u'.') {
            charColor = m_normalColor; // dots always in normal color
        } else {
            // Digits strictly BELOW the tuning rate position render in gray ("inactive trailing
            // digits"). The digit AT the rate stays in m_normalColor and carries the underline.
            const int posFromRight = kMaxDigitIndex - digitIdx;
            charColor = (m_tuningRateDigit >= 0 && posFromRight < m_tuningRateDigit)
                            ? QColor(K4Styles::Colors::TextSecondary)
                            : m_normalColor;
        }
        p.setPen(charColor);
        // The glyph keeps its own width, centred on its (possibly narrower) cell.
        p.drawText(QRectF(x, 0, charW, height()), Qt::AlignCenter, QString(QChar(c)));

        if (!m_editing && c != u'.' && m_tuningRateDigit >= 0 && (kMaxDigitIndex - digitIdx) == m_tuningRateDigit)
            p.fillRect(QRectF(x + kUnderlineInset, underlineY, charW - 2 * kUnderlineInset, kUnderlineHeight),
                       m_normalColor);

        if (c != u'.')
            ++digitIdx;
        x += charW;
    }

    // Entry caret: where the next digit will go.
    if (m_editing && m_typed.size() < kDigits) {
        const qreal caretW = m_charWidth * pitch;
        p.fillRect(QRectF(x + kUnderlineInset, underlineY, caretW - 2 * kUnderlineInset, kUnderlineHeight),
                   m_editColor);
    }
}

void FrequencyDisplayWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        const bool insideWidget = rect().contains(event->pos());
        if (m_editing && !insideWidget) {
            exitEditMode(false); // clicked outside while typing: cancel
            return;
        }
        if (!m_editing && insideWidget) {
            // A click picks the tuning rate. Entry is opened by FREQ ENT instead.
            const int digitFromRight = tuningDigitAtX(event->pos().x());
            if (digitFromRight >= 0)
                emit tuningDigitClicked(digitFromRight);
        }
    }
    QWidget::mousePressEvent(event);
}

void FrequencyDisplayWidget::mouseMoveEvent(QMouseEvent *event) {
    const bool clickable = !m_editing && tuningDigitAtX(event->pos().x()) >= 0;
    setCursor(clickable ? Qt::PointingHandCursor : Qt::ArrowCursor);
    QWidget::mouseMoveEvent(event);
}

void FrequencyDisplayWidget::keyPressEvent(QKeyEvent *event) {
    if (!m_editing) {
        QWidget::keyPressEvent(event);
        return;
    }

    const int key = event->key();
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        // A leading zero changes nothing, and the entry holds no more than the K4 takes.
        if ((key != Qt::Key_0 || !m_typed.isEmpty()) && m_typed.size() < kDigits) {
            m_typed.append(QChar('0' + (key - Qt::Key_0)));
            update();
        }
    } else if (key == Qt::Key_Backspace) {
        if (!m_typed.isEmpty()) {
            m_typed.chop(1);
            update();
        }
    } else if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        exitEditMode(true); // empty: nothing to send, closes like Escape
    } else if (key == Qt::Key_Escape) {
        exitEditMode(false);
    } else {
        QWidget::keyPressEvent(event);
    }
}

void FrequencyDisplayWidget::wheelEvent(QWheelEvent *event) {
    if (m_editing) {
        event->ignore(); // In edit mode — don't tune
        return;
    }
    const int steps = m_wheelAccumulator.accumulate(event);
    if (steps != 0)
        emit frequencyScrolled(steps);
    event->accept();
}

void FrequencyDisplayWidget::focusOutEvent(QFocusEvent *event) {
    // Cancel edit mode when focus is lost
    if (m_editing)
        exitEditMode(false);
    QWidget::focusOutEvent(event);
}
