#ifndef FREQUENCYDISPLAYWIDGET_H
#define FREQUENCYDISPLAYWIDGET_H

#include <QWidget>
#include <QColor>
#include <QFont>
#include "utils/wheelaccumulator.h"

/**
 * FrequencyDisplayWidget - Inline frequency display with segment-based editing.
 *
 * Features:
 * - Custom-painted frequency with dot separators (XX.XXX.XXX)
 * - Click a digit from 1 Hz to 10 kHz to make it the tuning rate (tuningDigitClicked)
 * - beginEntry() (the FREQ ENT button) enters edit mode; cursor starts at the leftmost digit
 * - In edit mode all 8 digits are shown (incl. a leading zero) so any
 *   position is reachable — required to go from <10 MHz to >=10 MHz
 * - All digits change to edit color (VFO theme: cyan for A, green for B)
 * - Type digits to replace at cursor position (auto-advances)
 * - Arrow keys to navigate between digits
 * - Enter to send frequency, Escape/click-outside to cancel
 *
 * Usage:
 *   auto *freq = new FrequencyDisplayWidget(parent);
 *   freq->setEditModeColor(QColor(K4Styles::Colors::VfoACyan));
 *   freq->setFrequency("7.024.980");
 *   connect(freq, &FrequencyDisplayWidget::frequencyEntered, ...);
 *   connect(freq, &FrequencyDisplayWidget::tuningDigitClicked, ...);
 */
class FrequencyDisplayWidget : public QWidget {
    Q_OBJECT

public:
    // 10 digits supports DC to 9.999.999.999 Hz (= ~10 GHz) — covers HF, VHF, UHF,
    // and L-band XVTRs (e.g., 1.296.000.000 for 1296 MHz).
    static constexpr int kDigits = 10;
    static constexpr int kMaxDigitIndex = kDigits - 1;

    explicit FrequencyDisplayWidget(QWidget *parent = nullptr);

    // Set the displayed frequency (with or without dots)
    // Examples: "7.024.980", "7024980", "14.024.980"
    void setFrequency(const QString &frequency);

    // Get the current frequency as plain digits (no dots)
    QString frequency() const;

    // Get the displayed text with dots (e.g., "7.024.980")
    QString displayText() const;

    // Set the color used when in edit mode (typically VFO theme color)
    void setEditModeColor(const QColor &color);

    // Set the color used when NOT in edit mode (normally white, gray when dimmed)
    void setNormalColor(const QColor &color);

    // Set tuning rate indicator - digits at this position and below show in gray
    // digitFromRight: 0=1Hz, 1=10Hz, 2=100Hz, 3=1kHz, 4=10kHz, -1=none
    // e.g., rate 2 (100Hz) grays out digits 0,1,2 (1s, 10s, 100s places)
    void setTuningRateDigit(int digitFromRight);

    // Check if currently in edit mode
    bool isEditing() const;

    // Open direct frequency entry with the cursor on the leftmost digit, or back out of it without
    // sending. Driven by the FREQ ENT button.
    void beginEntry();
    void cancelEntry();

signals:
    // Emitted when user presses Enter to confirm frequency entry
    // digits is the frequency as plain digits (e.g., "7024980")
    void frequencyEntered(const QString &digits);

    // Emitted when user scrolls mouse wheel over frequency (not in edit mode)
    void frequencyScrolled(int steps);

    // Emitted when user clicks a digit that can be a tuning rate (not in edit mode).
    // digitFromRight: 0=1Hz ... 4=10kHz. Higher digits emit nothing.
    void tuningDigitClicked(int digitFromRight);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    // Enter edit mode with cursor at the specified digit position (0-7)
    void enterEditMode(int digitPosition);

    // Exit edit mode, optionally sending the frequency
    void exitEditMode(bool send);

    // Convert mouse X coordinate to digit position (0-7), or -1 if not on a digit
    int digitPositionFromX(int x) const;

    // Tuning-rate digit under x, counted from the right, or -1 when x is not on a digit that can be a
    // tuning rate. Strict: unlike digitPositionFromX, a click on a dot or past the end is no digit.
    int tuningDigitAtX(int x) const;

    // Get the bounding rectangle for a character at the given index
    QRect charRectAt(int charIndex) const;

    // Convert character index in display string to digit index (0-7)
    // Returns -1 for dot characters
    int digitIndexFromCharIndex(int charIndex) const;

    // Index of the first m_digits position shown in the display: 1 when the
    // leading zero is suppressed (<10 MHz and not editing), else 0. In edit
    // mode all 8 digits are shown so digit 0 stays reachable.
    int displayStartIndex() const;

    // Format m_digits as display string with dots (e.g., "7.024.980")
    QString formatWithDots() const;

    // Parse frequency string and normalize to 8 digits
    void parseFrequency(const QString &freq);

    // Member variables
    QString m_digits;          // 8-digit string, left-padded with zeros
    QString m_originalDigits;  // Backup for cancel operation
    int m_cursorPosition = -1; // -1 = not editing, 0-7 = digit position

    QColor m_normalColor; // White - normal display color
    QColor m_editColor;   // Cyan/Green - edit mode color
    QFont m_font;         // Inter with tabular figures, 32px bold

    // Tuning rate indicator: digits from this position to 0 show in gray
    int m_tuningRateDigit = -1; // -1 = no indicator, 0-4 = position from right

    WheelAccumulator m_wheelAccumulator;

    // Cached character metrics for click detection
    int m_charWidth = 0;
    int m_dotWidth = 0;
};

#endif // FREQUENCYDISPLAYWIDGET_H
