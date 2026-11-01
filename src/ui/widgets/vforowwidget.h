#ifndef VFOROWWIDGET_H
#define VFOROWWIDGET_H

#include <QLabel>
#include <QWidget>

/**
 * VfoSquareWidget - Custom painted VFO A/B indicator with lock arc
 *
 * Draws a rounded square with "A" or "B" text, and optionally a
 * semi-circular arc on top to indicate lock state (padlock effect).
 */
class VfoSquareWidget : public QWidget {
    Q_OBJECT

public:
    explicit VfoSquareWidget(const QString &text, const QColor &color, QWidget *parent = nullptr);

    void setLocked(bool locked);
    bool isLocked() const { return m_locked; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_text;
    QColor m_color;
    bool m_locked = false;
};

/**
 * VfoRowWidget - First row of center section with absolute positioning
 *
 * Contains: A/B squares with mode labels, TEST, TX indicator, SUB/DIV, SPLIT / B SET, MSG bank
 * Uses absolute positioning to perfectly center TX regardless of
 * asymmetric SUB/DIV on B side.
 */
class VfoRowWidget : public QWidget {
    Q_OBJECT

public:
    explicit VfoRowWidget(QWidget *parent = nullptr);

    // Lock state setters
    void setLockA(bool locked);
    void setLockB(bool locked);
    void setTestVisible(bool visible);

    // Accessors for MainWindow to connect signals and install event filters
    VfoSquareWidget *vfoASquare() const { return m_vfoASquare; }
    VfoSquareWidget *vfoBSquare() const { return m_vfoBSquare; }
    QLabel *modeALabel() const { return m_modeALabel; }
    QLabel *modeBLabel() const { return m_modeBLabel; }
    QLabel *txIndicator() const { return m_txIndicator; }
    QLabel *txTriangle() const { return m_txTriangle; }
    QLabel *txTriangleB() const { return m_txTriangleB; }
    QLabel *subLabel() const { return m_subLabel; }
    QLabel *divLabel() const { return m_divLabel; }
    QLabel *splitLabel() const { return m_splitLabel; }
    QLabel *bSetLabel() const { return m_bSetLabel; }
    QLabel *msgBankLabel() const { return m_msgBankLabel; }

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void setupWidgets();
    void positionWidgets();

    // Containers (absolute positioned within this widget)
    QWidget *m_vfoAContainer;
    QWidget *m_txContainer;
    QWidget *m_vfoBContainer;
    QWidget *m_subDivContainer;

    // VFO squares (custom painted for lock arc)
    VfoSquareWidget *m_vfoASquare;
    VfoSquareWidget *m_vfoBSquare;
    // Labels within containers
    QLabel *m_modeALabel;
    QLabel *m_modeBLabel;
    QLabel *m_txIndicator;
    QLabel *m_txTriangle;
    QLabel *m_txTriangleB;
    QLabel *m_testLabel;
    QLabel *m_subLabel;
    QLabel *m_divLabel;
    QLabel *m_splitLabel;
    QLabel *m_bSetLabel;
    QLabel *m_msgBankLabel;
};

#endif // VFOROWWIDGET_H
