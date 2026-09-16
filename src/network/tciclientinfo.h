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

    // Time and text of the last message RECEIVED from this client.
    //
    // WHY inbound only: what goes OUT is the same broadcast to every client, and at rest it is
    // dominated by the sensor timer - so an "exchanged" column that counted it would show the same
    // sensor command on every row, timestamped now, whether the client was alive or wedged. Inbound
    // is the half that actually distinguishes one client from another.
    QDateTime lastMessageTime;
    QString lastMessage;
};

Q_DECLARE_METATYPE(TciClientInfo)

#endif // NETWORK_TCICLIENTINFO_H
