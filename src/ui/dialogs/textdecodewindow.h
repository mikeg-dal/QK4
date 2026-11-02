#ifndef TEXTDECODEWINDOW_H
#define TEXTDECODEWINDOW_H

#include <QWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include "utils/wheelaccumulator.h"

// Standalone floating window for the K4's on-radio decoder output (CW /
// AFSK / FSK / PSK / DATA / SSB). One instance per receiver (Main/Sub).
// Owns its title-bar controls (on/off, WPM range, threshold, data rate)
// and emits state-change signals for TextDecodeController to forward as CAT.
// Resizable + draggable.
//
// Intentionally NOT a K4PopupBase subclass. K4PopupBase is for +/-
// button-row popups that auto-position above a trigger and dismiss via
// ESC or close button. TextDecodeWindow is a free-floating tool window
// with custom title-bar chrome, drag/resize handles, and per-receiver
// colored borders (cyan for Main RX, green for Sub RX) — none of which
// fit the K4PopupBase model.
class TextDecodeWindow : public QWidget {
    Q_OBJECT

public:
    enum Receiver { MainRx, SubRx };
    enum OperatingMode { ModeCW, ModeAFSK, ModeFSK, ModePSK, ModeData, ModeSSB, ModeOther };

    explicit TextDecodeWindow(Receiver rx, QWidget *parent = nullptr);

    void appendText(const QString &text);
    void clearText();
    void setMaxLines(int lines);
    int maxLines() const { return m_maxLines; }
    Receiver receiver() const { return m_receiver; }

    // Decode state getters
    bool isDecodeEnabled() const { return m_decodeEnabled; }
    int wpmRange() const { return m_wpmRange; }
    bool autoThreshold() const { return m_autoThreshold; }
    int threshold() const { return m_threshold; }
    int dataRate() const { return m_dataRate; }
    OperatingMode operatingMode() const { return m_operatingMode; }

    // Decode state setters. None of them emit: the signals report operator actions only, so applying
    // the radio's own report can never be sent back to it.
    void setDecodeEnabled(bool enabled);
    void setWpmRange(int range);
    void setAutoThreshold(bool isAuto);
    void setThreshold(int value);
    void setDataRate(int rate);
    void setOperatingMode(OperatingMode mode);

signals:
    void closeRequested();
    void enabledChanged(bool on);
    void wpmRangeChanged(int range);
    void thresholdModeChanged(bool isAuto);
    void thresholdChanged(int value);
    void dataRateChanged(int rate);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void setupUi();
    void trimToMaxLines();
    QRect titleBarRect() const;
    QRect resizeGripRect() const;
    void updateButtonStates();
    void updateSpeedButton();
    void updateThresholdControls();
    void updateModeVisibility();
    void applyTextFontSize();

    Receiver m_receiver;
    OperatingMode m_operatingMode = ModeCW;
    int m_maxLines = 1;

    // Decode state
    bool m_decodeEnabled = false;
    int m_wpmRange = 0; // 0=8-45, 1=8-60, 2=8-90
    bool m_autoThreshold = true;
    int m_threshold = 5; // 1-9
    int m_dataRate = 0;  // 0=slower (RTTY45/PSK31), 1=faster (RTTY75/PSK63)

    // Title bar controls
    QLabel *m_titleLabel;
    QPushButton *m_onOffBtn;
    QPushButton *m_wpmBtn;
    QPushButton *m_autoManualBtn;
    QPushButton *m_thresholdMinusBtn;
    QLabel *m_thresholdValueLabel;
    QPushButton *m_thresholdPlusBtn;
    QPushButton *m_clearBtn;
    QPushButton *m_fontMinusBtn;
    QPushButton *m_fontPlusBtn;
    QPushButton *m_closeBtn;
    QPlainTextEdit *m_textDisplay;
    int m_textFontSize = 9;

    // Drag/resize state
    QPoint m_dragPosition;
    bool m_dragging = false;
    bool m_resizing = false;
    QPoint m_resizeStartPos;
    QSize m_resizeStartSize;
    WheelAccumulator m_wheelAccumulator;
};

#endif // TEXTDECODEWINDOW_H
