#ifndef CTR2PAGE_H
#define CTR2PAGE_H

#include <QMap>
#include <QWidget>
class HardwareController;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

class Ctr2Page final : public QWidget {
    Q_OBJECT
public:
    explicit Ctr2Page(HardwareController *controller, QWidget *parent = nullptr);
    void refresh();

private:
    void apply();
    HardwareController *m_controller;
    QComboBox *m_device = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_connect = nullptr;
    QCheckBox *m_extended = nullptr;
    QMap<int, QComboBox *> m_knobs;
    QMap<int, QComboBox *> m_buttons;
};
#endif
