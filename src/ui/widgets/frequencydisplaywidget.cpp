#include "ui/widgets/frequencydisplaywidget.h"
#include "ui/styling/k4styles.h"
#include "utils/radioutils.h"
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QFontMetrics>

FrequencyDisplayWidget::FrequencyDisplayWidget(QWidget *parent)
    : QWidget(parent), m_digits(QString(kDigits, '0')), m_normalColor(K4Styles::Colors::TextWhite),
      m_editColor(K4Styles::Colors::VfoACyan) {

    // Set up font with tabular figures for consistent digit widths
    m_font = K4Styles::Fonts::dataFont(K4Styles::Dimensions::FontSizeFrequency);

    // Calculate character widths for click detection
    QFontMetrics fm(m_font);
    m_charWidth = fm.horizontalAdvance('0');
    m_dotWidth = fm.horizontalAdvance('.');

    // Widget configuration
    setFocusPolicy(Qt::ClickFocus);
    setMouseTracking(true); // hand cursor only over digits that can be a tuning rate

    // Size hint based on max display width (X.XXX.XXX.XXX = 10 digits + 3 dots)
    int width = m_charWidth * kDigits + m_dotWidth * 3 + 4; // +4 for padding
    setMinimumWidth(width);
    setFixedHeight(K4Styles::Dimensions::MenuItemHeight);
}

void FrequencyDisplayWidget::setFrequency(const QString &frequency) {
    parseFrequency(frequency);
    update();
}

QString FrequencyDisplayWidget::frequency() const {
    return m_digits;
}

QString FrequencyDisplayWidget::displayText() const {
    return formatWithDots();
}

void FrequencyDisplayWidget::setEditModeColor(const QColor &color) {
    m_editColor = color;
    if (m_cursorPosition >= 0) {
        update();
    }
}

void FrequencyDisplayWidget::setNormalColor(const QColor &color) {
    m_normalColor = color;
    if (m_cursorPosition < 0) {
        update();
    }
}

void FrequencyDisplayWidget::setTuningRateDigit(int digitFromRight) {
    if (m_tuningRateDigit != digitFromRight) {
        m_tuningRateDigit = digitFromRight;
        update();
    }
}

bool FrequencyDisplayWidget::isEditing() const {
    return m_cursorPosition >= 0;
}

void FrequencyDisplayWidget::beginEntry() {
    if (m_cursorPosition < 0)
        enterEditMode(0);
}

void FrequencyDisplayWidget::cancelEntry() {
    exitEditMode(false);
}

void FrequencyDisplayWidget::parseFrequency(const QString &freq) {
    // Remove dots and any non-digit characters
    QString digits;
    for (const QChar &c : freq) {
        if (c.isDigit()) {
            digits.append(c);
        }
    }

    // Pad left with zeros to kDigits, or truncate (keeping rightmost) if too long
    while (digits.length() < kDigits) {
        digits.prepend('0');
    }
    if (digits.length() > kDigits) {
        digits = digits.right(kDigits);
    }

    m_digits = digits;
}

int FrequencyDisplayWidget::displayStartIndex() const {
    // Default to showing the rightmost 8 digits (matches pre-VHF behavior:
    // HF defaults to "0.000.000" with one suppressed leading zero, 14 MHz reads
    // "14.074.000", 7 MHz reads "7.024.980"). For higher frequencies, additional
    // leading digits are revealed automatically: 144 MHz → "144.100.000",
    // 1.296 GHz → "1.296.000.000". In edit mode show all digits so any position
    // is reachable for typing higher-MHz frequencies.
    if (m_cursorPosition >= 0) {
        return 0;
    }
    constexpr int kBaseVisible = 8;
    constexpr int kBaseStart = kDigits - kBaseVisible; // index of the 8-digit-wide window
    int start = 0;
    while (start < kBaseStart && m_digits[start] == '0') {
        ++start;
    }
    // Mimic old <10 MHz behavior: strip the one remaining leading zero so 7 MHz
    // renders as "7.024.980" not "07.024.980".
    if (start == kBaseStart && m_digits[start] == '0') {
        ++start;
    }
    return start;
}

QString FrequencyDisplayWidget::formatWithDots() const {
    // Format groups three digits from the right separated by dots:
    //   "0000007024980" → "7.024.980"
    //   "0000014074000" → "14.074.000"
    //   "0000144100000" → "144.100.000"
    //   "0001296000000" → "1.296.000.000"

    QString result;
    int startIdx = displayStartIndex();

    for (int i = startIdx; i < kDigits; ++i) {
        result.append(m_digits[i]);

        // Insert a dot when the next digit starts a new 3-digit group from the right.
        // posFromRight of the dot's gap is (kDigits - i - 1); if that's >0 and a
        // multiple of 3, the dot belongs between m_digits[i] and m_digits[i+1].
        int posFromRight = kMaxDigitIndex - i;
        if (posFromRight > 0 && posFromRight % 3 == 0) {
            result.append('.');
        }
    }

    return result;
}

int FrequencyDisplayWidget::digitIndexFromCharIndex(int charIndex) const {
    // Map character index in display string to digit index (0-7)
    QString display = formatWithDots();
    if (charIndex < 0 || charIndex >= display.length()) {
        return -1;
    }

    // Count digits before this position
    int digitCount = 0;
    for (int i = 0; i < charIndex; ++i) {
        if (display[i] != '.') {
            digitCount++;
        }
    }

    // If this character is a dot, return -1
    if (display[charIndex] == '.') {
        return -1;
    }

    // Adjust for leading zero skip
    int offset = displayStartIndex();
    return offset + digitCount;
}

QRect FrequencyDisplayWidget::charRectAt(int charIndex) const {
    QString display = formatWithDots();
    if (charIndex < 0 || charIndex >= display.length()) {
        return QRect();
    }

    // Calculate X position by summing widths of preceding characters
    int x = 0;
    for (int i = 0; i < charIndex; ++i) {
        x += (display[i] == '.') ? m_dotWidth : m_charWidth;
    }

    int charW = (display[charIndex] == '.') ? m_dotWidth : m_charWidth;
    return QRect(x, 0, charW, height());
}

int FrequencyDisplayWidget::digitPositionFromX(int x) const {
    QString display = formatWithDots();

    int currentX = 0;
    for (int i = 0; i < display.length(); ++i) {
        int charW = (display[i] == '.') ? m_dotWidth : m_charWidth;

        if (x >= currentX && x < currentX + charW) {
            int digitIdx = digitIndexFromCharIndex(i);
            if (digitIdx >= 0) {
                return digitIdx;
            }
            // Clicked on a dot - find nearest digit
            // Check left neighbor first
            if (i > 0) {
                int leftDigit = digitIndexFromCharIndex(i - 1);
                if (leftDigit >= 0) {
                    return leftDigit;
                }
            }
            // Check right neighbor
            if (i + 1 < display.length()) {
                int rightDigit = digitIndexFromCharIndex(i + 1);
                if (rightDigit >= 0) {
                    return rightDigit;
                }
            }
        }
        currentX += charW;
    }

    // Click beyond display - select last digit
    return kMaxDigitIndex;
}

int FrequencyDisplayWidget::tuningDigitAtX(int x) const {
    const QString display = formatWithDots();
    int currentX = 0;
    for (int i = 0; i < display.length(); ++i) {
        const int charW = (display[i] == '.') ? m_dotWidth : m_charWidth;
        if (x >= currentX && x < currentX + charW) {
            const int digitIdx = digitIndexFromCharIndex(i);
            if (digitIdx < 0)
                return -1;
            const int digitFromRight = kMaxDigitIndex - digitIdx;
            return RadioUtils::tuningStepForDigit(digitFromRight) >= 0 ? digitFromRight : -1;
        }
        currentX += charW;
    }
    return -1;
}

void FrequencyDisplayWidget::enterEditMode(int digitPosition) {
    if (digitPosition < 0 || digitPosition > kMaxDigitIndex) {
        return;
    }

    m_originalDigits = m_digits;
    m_cursorPosition = digitPosition;
    setFocus();
    grabMouse(); // Capture all mouse events to detect clicks outside
    update();
}

void FrequencyDisplayWidget::exitEditMode(bool send) {
    if (m_cursorPosition < 0) {
        return; // Not in edit mode
    }

    releaseMouse(); // Release mouse grab

    if (send) {
        // Remove leading zeros for the signal (but keep at least one digit)
        QString digits = m_digits;
        while (digits.length() > 1 && digits[0] == '0') {
            digits.remove(0, 1);
        }
        emit frequencyEntered(digits);
    } else {
        // Restore original frequency
        m_digits = m_originalDigits;
    }

    m_cursorPosition = -1;
    clearFocus();
    update();
}

void FrequencyDisplayWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setFont(m_font);

    QString display = formatWithDots();

    // Draw each character
    int x = 0;
    int digitIdx = displayStartIndex(); // Start digit index

    for (int i = 0; i < display.length(); ++i) {
        QChar c = display[i];
        int charW = (c == '.') ? m_dotWidth : m_charWidth;

        // Determine color for this character
        QColor charColor;
        if (m_cursorPosition >= 0) {
            // Edit mode: all characters in edit color
            charColor = m_editColor;
        } else if (c == '.') {
            // Dots always in normal color
            charColor = m_normalColor;
        } else {
            // Normal mode: digits strictly BELOW the tuning rate position render in gray
            // ("inactive trailing digits"). The digit AT the tuning rate position stays in
            // m_normalColor and is marked by the underline below — that's the active
            // rate indicator the user reads to know the current tuning step.
            int posFromRight = kMaxDigitIndex - digitIdx;
            if (m_tuningRateDigit >= 0 && posFromRight < m_tuningRateDigit) {
                charColor = QColor(K4Styles::Colors::TextGray);
            } else {
                charColor = m_normalColor;
            }
        }
        p.setPen(charColor);

        // Draw the character
        QRect charRect(x, 0, charW, height());
        p.drawText(charRect, Qt::AlignCenter, c);

        // Draw cursor underline if this is the selected digit (edit mode)
        if (c != '.' && digitIdx == m_cursorPosition) {
            int underlineY = height() - 4;
            p.fillRect(x + 2, underlineY, charW - 4, 2, m_editColor);
        }

        // Tuning-rate indicator underline. Guarded by m_cursorPosition < 0 so the
        // edit-mode cursor's underline takes precedence and we don't double-draw.
        if (m_cursorPosition < 0 && c != '.' && m_tuningRateDigit >= 0 &&
            (kMaxDigitIndex - digitIdx) == m_tuningRateDigit) {
            int underlineY = height() - 4;
            p.fillRect(x + 2, underlineY, charW - 4, 2, m_normalColor);
        }

        // Advance digit index (only for non-dot characters)
        if (c != '.') {
            digitIdx++;
        }

        x += charW;
    }
}

void FrequencyDisplayWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        // Check if click is inside widget bounds
        QRect widgetRect(0, 0, width(), height());
        bool insideWidget = widgetRect.contains(event->pos());

        if (m_cursorPosition >= 0 && !insideWidget) {
            // In edit mode but clicked outside - cancel
            exitEditMode(false);
            return;
        }

        if (insideWidget) {
            if (m_cursorPosition < 0) {
                // Not editing: a click picks the tuning rate. Entry is opened by FREQ ENT instead.
                const int digitFromRight = tuningDigitAtX(event->pos().x());
                if (digitFromRight >= 0)
                    emit tuningDigitClicked(digitFromRight);
            } else {
                // Already in edit mode - move cursor
                int digitPos = digitPositionFromX(event->pos().x());
                if (digitPos >= 0) {
                    m_cursorPosition = digitPos;
                    update();
                }
            }
        }
    }
    QWidget::mousePressEvent(event);
}

void FrequencyDisplayWidget::mouseMoveEvent(QMouseEvent *event) {
    const bool clickable = m_cursorPosition < 0 && tuningDigitAtX(event->pos().x()) >= 0;
    setCursor(clickable ? Qt::PointingHandCursor : Qt::ArrowCursor);
    QWidget::mouseMoveEvent(event);
}

void FrequencyDisplayWidget::keyPressEvent(QKeyEvent *event) {
    if (m_cursorPosition < 0) {
        QWidget::keyPressEvent(event);
        return;
    }

    int key = event->key();

    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        // Replace digit at cursor position
        m_digits[m_cursorPosition] = QChar('0' + (key - Qt::Key_0));

        // Advance cursor (stop at end)
        if (m_cursorPosition < kMaxDigitIndex) {
            m_cursorPosition++;
        }
        update();
    } else if (key == Qt::Key_Left) {
        if (m_cursorPosition > 0) {
            m_cursorPosition--;
            update();
        }
    } else if (key == Qt::Key_Right) {
        if (m_cursorPosition < kMaxDigitIndex) {
            m_cursorPosition++;
            update();
        }
    } else if (key == Qt::Key_Home) {
        m_cursorPosition = 0;
        update();
    } else if (key == Qt::Key_End) {
        m_cursorPosition = kMaxDigitIndex;
        update();
    } else if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        exitEditMode(true); // Send frequency
    } else if (key == Qt::Key_Escape) {
        exitEditMode(false); // Cancel
    } else {
        QWidget::keyPressEvent(event);
    }
}

void FrequencyDisplayWidget::wheelEvent(QWheelEvent *event) {
    if (m_cursorPosition >= 0) {
        event->ignore(); // In edit mode — don't tune
        return;
    }
    int steps = m_wheelAccumulator.accumulate(event);
    if (steps != 0)
        emit frequencyScrolled(steps);
    event->accept();
}

void FrequencyDisplayWidget::focusOutEvent(QFocusEvent *event) {
    // Cancel edit mode when focus is lost
    if (m_cursorPosition >= 0) {
        exitEditMode(false);
    }
    QWidget::focusOutEvent(event);
}
