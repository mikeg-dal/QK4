#ifndef CONFIRMPOPUP_H
#define CONFIRMPOPUP_H

#include "ui/popups/k4popupbase.h"

class QLabel;
class QPushButton;

// Reusable yes/no confirmation popup styled in the K4 popup language.
//
// Used by the K4 remote power-off confirm. Emits exactly one of
// confirmed() / cancelled() per show: Escape, click-away, and the cancel
// button all map to cancelled(). The confirm button maps to confirmed().
class ConfirmPopup : public K4PopupBase {
    Q_OBJECT

public:
    explicit ConfirmPopup(const QString &title, const QString &body, const QString &confirmLabel,
                          const QString &cancelLabel, QWidget *parent = nullptr);

signals:
    void confirmed();
    void cancelled();

protected:
    QSize contentSize() const override;

private:
    void onConfirmClicked();
    void onCancelClicked();

    QLabel *m_titleLabel;
    QLabel *m_bodyLabel;
    QPushButton *m_confirmButton;
    QPushButton *m_cancelButton;
    QSize m_contentSize;
    bool m_confirmedLatch = false;
};

#endif // CONFIRMPOPUP_H
