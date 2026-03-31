#ifndef KPA1500PAGE_H
#define KPA1500PAGE_H

#include <QWidget>
#include <QCheckBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>

class KPA1500Client;

class Kpa1500Page : public QWidget {
    Q_OBJECT

public:
    explicit Kpa1500Page(KPA1500Client *kpa1500Client, QWidget *parent = nullptr);

    void refresh();

private:
    void updateKpa1500Status();

    KPA1500Client *m_kpa1500Client;
    QCheckBox *m_kpa1500EnableCheckbox = nullptr;
    QLineEdit *m_kpa1500HostEdit = nullptr;
    QLineEdit *m_kpa1500PortEdit = nullptr;
    QLabel *m_kpa1500StatusLabel = nullptr;
    QPushButton *m_kpa1500ConnectBtn = nullptr;
    QLabel *m_kpa1500BandLabel = nullptr;
    QLabel *m_kpa1500FirmwareLabel = nullptr;
    QLabel *m_kpa1500SerialLabel = nullptr;
    QLabel *m_kpa1500PollLabel = nullptr;
};

#endif // KPA1500PAGE_H
