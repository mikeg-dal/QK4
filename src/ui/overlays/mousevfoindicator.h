#ifndef MOUSEVFOINDICATOR_H
#define MOUSEVFOINDICATOR_H

#include <QWidget>

/**
 * @brief Small panadapter badge showing which VFO the scroll wheel tunes: A (cyan) or B (green),
 *        set by the last click-tune. Purely visual — no CAT, no RadioState sync.
 */
class MouseVfoIndicator : public QWidget {
    Q_OBJECT

public:
    explicit MouseVfoIndicator(QWidget *parent = nullptr);

    void setActiveVfo(bool isB);
    bool isVfoB() const { return m_isB; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    bool m_isB = false;
};

#endif // MOUSEVFOINDICATOR_H
