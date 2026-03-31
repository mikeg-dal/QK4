#ifndef RIGCONTROLPAGE_H
#define RIGCONTROLPAGE_H

#include <QWidget>
#include <QCheckBox>
#include <QLineEdit>
#include <QLabel>

class CatServer;

class RigControlPage : public QWidget {
    Q_OBJECT

public:
    explicit RigControlPage(CatServer *catServer, QWidget *parent = nullptr);

    void refresh();

private:
    void updateCatServerStatus();

    CatServer *m_catServer;
    QCheckBox *m_catServerEnableCheckbox = nullptr;
    QLineEdit *m_catServerPortEdit = nullptr;
    QLabel *m_catServerStatusLabel = nullptr;
    QLabel *m_catServerClientsLabel = nullptr;
};

#endif // RIGCONTROLPAGE_H
