#include "memorybuttonscontroller.h"

#include "connectioncontroller.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPushButton>

MemoryButtonsController::MemoryButtonsController(ConnectionController *connection, QPushButton *m1, QPushButton *m2,
                                                 QPushButton *m3, QPushButton *m4, QPushButton *rec, QPushButton *store,
                                                 QPushButton *rcl, QObject *parent)
    : QObject(parent), m_connection(connection), m_recBtn(rec), m_storeBtn(store), m_rclBtn(rcl) {

    // Primary (left-click) actions — map each button to its K4 SW command.
    connect(m1, &QPushButton::clicked, this, [this]() { m_connection->sendCAT("SW17;"); });
    connect(m2, &QPushButton::clicked, this, [this]() { m_connection->sendCAT("SW51;"); });
    connect(m3, &QPushButton::clicked, this, [this]() { m_connection->sendCAT("SW18;"); });
    connect(m4, &QPushButton::clicked, this, [this]() { m_connection->sendCAT("SW52;"); });
    connect(rec, &QPushButton::clicked, this, [this]() { m_connection->sendCAT("SW19;"); });
    connect(store, &QPushButton::clicked, this, [this]() { m_connection->sendCAT("SW20;"); });
    connect(rcl, &QPushButton::clicked, this, [this]() { m_connection->sendCAT("SW34;"); });

    // Right-click alt actions handled by event filter — REC=BANK,
    // STORE=AF REC, RCL=AF PLAY.
    rec->installEventFilter(this);
    store->installEventFilter(this);
    rcl->installEventFilter(this);
}

bool MemoryButtonsController::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::MouseButtonPress) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::RightButton) {
            if (watched == m_recBtn) {
                m_connection->sendCAT("SW137;"); // BANK
                return true;
            } else if (watched == m_storeBtn) {
                m_connection->sendCAT("SW138;"); // AF REC
                return true;
            } else if (watched == m_rclBtn) {
                m_connection->sendCAT("SW139;"); // AF PLAY
                return true;
            }
        }
    }
    return QObject::eventFilter(watched, event);
}
