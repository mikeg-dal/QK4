#ifndef ABOUTPAGE_H
#define ABOUTPAGE_H

#include <QWidget>
#include <QLabel>

class RadioState;

class AboutPage : public QWidget {
    Q_OBJECT

public:
    explicit AboutPage(RadioState *radioState, QWidget *parent = nullptr);

    void refresh();

private:
    RadioState *m_radioState;
    QLabel *m_radioIdLabel = nullptr;
    QLabel *m_radioModelLabel = nullptr;
    QWidget *m_optionsWidget = nullptr;
    QWidget *m_versionsWidget = nullptr;
};

#endif // ABOUTPAGE_H
