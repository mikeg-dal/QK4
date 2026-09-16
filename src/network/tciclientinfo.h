#ifndef NETWORK_TCICLIENTINFO_H
#define NETWORK_TCICLIENTINFO_H

#include <QDateTime>
#include <QMetaType>
#include <QString>

// One connected TCI client, as the options page lists it.
//
// WHY a header of its own: the options page needs this and nothing else from the server. Putting it
// in tciserver.h would make a settings widget include the server, its parser and its socket
// transport in order to render three columns.
struct TciClientInfo {
    int id = 0;
    QString address;

    // Time and text of the last message exchanged with this client, in EITHER direction, and which
    // way it went. The page renders the direction as an arrow.
    //
    // Text messages only, and not the periodic sensor stream. Both are continuous output QK4
    // started rather than an exchange, and both would pin the cell forever: RX audio leaves at ~47
    // frames a second, sensors at five, against commands that arrive now and then. The subscribe
    // that turns sensors on is recorded, once; the readings that follow are not.
    // Inbound TX audio IS counted, as a single "(tx audio)" - a WSJT-X client sends no text at all
    // for the length of a 15-second transmission, and reporting it silent at the one moment it is
    // busiest is the failure this column exists to avoid.
    QDateTime lastMessageTime;
    QString lastMessage;
    bool lastMessageOutbound = false;
};

Q_DECLARE_METATYPE(TciClientInfo)

#endif // NETWORK_TCICLIENTINFO_H
