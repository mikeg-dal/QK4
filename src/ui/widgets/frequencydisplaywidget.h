#ifndef FREQUENCYDISPLAYWIDGET_H
#define FREQUENCYDISPLAYWIDGET_H

#include <QWidget>
#include <QColor>
#include <QFont>
#include "utils/wheelaccumulator.h"

/**
 * FrequencyDisplayWidget - Inline frequency display with type-in entry.
 *
 * Features:
 * - Custom-painted frequency with dot separators (7.024.980 … 10.368.000.000)
 * - Up to kDigits (11) digits, the K4's own limit (FA/FB: 1 to 11 digits)
 * - When a frequency is wider than the widget, the characters are set closer together - the font
 *   size never changes
 * - Click a digit from 1 Hz to 10 kHz to make it the tuning rate (tuningDigitClicked)
 * - beginEntry() (the FREQ ENT button) opens an empty entry in the edit color (VFO theme: cyan for
 *   A, green for B). Digits fill in from the right as typed, grouped with dots; Backspace removes
 *   the last; Enter sends; Escape or a click outside cancels.
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
    // 11 digits: DC to 99.999.999.999 Hz, the most the K4's FA/FB take (Programmer's Reference D12,
    // FA). Covers HF through the microwave transverter bands (10.368.000.000 for 10 GHz).
    static constexpr int kDigits = 11;
    static constexpr int kMaxDigitIndex = kDigits - 1;

    explicit FrequencyDisplayWidget(QWidget *parent = nullptr);

    // Set the displayed frequency (with or without dots)
    // Examples: "7.024.980", "7024980", "14.024.980"
    // While editing, the value is held and shown only if the entry is cancelled.
    void setFrequency(const QString &frequency);

    // The frequency as kDigits plain digits, left-padded with zeros. While editing, what has been
    // typed so far (no padding; empty before the first digit).
    QString frequency() const;

    // The text shown, with dots (e.g., "7.024.980"); while editing, the typed digits grouped the same way
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

    // Open an empty frequency entry, or back out of it without sending. Driven by the FREQ ENT button.
    void beginEntry();
    void cancelEntry();

    // A locked VFO (LK / LK$) takes no typed frequency: while locked, beginEntry() does nothing, and
    // locking during an entry cancels it, showing the radio's frequency again.
    void setEntryLocked(bool locked);

signals:
    // Emitted when the operator presses Enter on a non-empty entry. digits is the frequency in Hz as
    // exactly kDigits digits (e.g., "00014074000"), so the K4 never reads it as a short MHz/kHz form.
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
    bool m_entryLocked = false;

    // Exit edit mode, optionally sending the typed frequency
    void exitEditMode(bool send);

    // Tuning-rate digit under x, counted from the right, or -1 when x is not on a digit that can be a
    // tuning rate (a dot, past the end, or while editing).
    int tuningDigitAtX(int x) const;

    // Index of the first m_digits position shown: leading zeros beyond the 8-digit window are
    // hidden, plus one more below 10 MHz.
    int displayStartIndex() const;

    // Digits grouped in threes from the right with dots ("14074000" -> "14.074.000")
    static QString groupWithDots(const QString &digits);

    // Strip non-digits and normalize to kDigits digits
    static QString normalizeDigits(const QString &freq);

    // Horizontal factor applied to every character's natural advance so `text` (plus the entry
    // caret while editing) fits the widget: 1 when it fits, less when it would not.
    qreal pitchFor(const QString &text) const;

    // Member variables
    QString m_digits;      // kDigits digits, left-padded with zeros
    QString m_radioDigits; // Latest radio value while editing; restored on cancel
    QString m_typed;       // Digits typed so far while editing
    bool m_editing = false;

    QColor m_normalColor; // White - normal display color
    QColor m_editColor;   // Cyan/Green - edit mode color
    QFont m_font;         // Inter with tabular figures, 32px bold

    // Tuning rate indicator: digits from this position to 0 show in gray
    int m_tuningRateDigit = -1; // -1 = no indicator, 0-4 = position from right

    WheelAccumulator m_wheelAccumulator;

    // Natural character advances, from the font
    int m_charWidth = 0;
    int m_dotWidth = 0;
};

#endif // FREQUENCYDISPLAYWIDGET_H
