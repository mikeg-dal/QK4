#ifndef CONNECT_FAILURE_H
#define CONNECT_FAILURE_H

#include <QString>

// Decides what a failed connection attempt is called, and what the operator is told about it.
// Pure logic with no socket or Qt::Network dependency so it can be unit-tested without standing up
// a TcpClient, mirroring hardware/halikey_edge.h.
//
// WHY this is a separate decision rather than four inline branches in TcpClient: the K4 sends no
// error codes. Nothing means "wrong password" and nothing means "accepted" - a good password
// produces ordinary traffic and a bad one produces silence or a closed socket. Which sentence the
// operator gets is therefore a judgement about what is *certain*, not a readout of a socket error,
// and getting it wrong sends them to re-type a password that was never the problem (issue #20).
namespace ConnectFailure {

// What woke the failure path. Each carries different evidence: a socket that closed is not the same
// as one still held open in silence, and a timer firing is not the same as an error arriving.
enum class Event { SocketClosed, SocketError, ConnectTimeout, AuthTimeout };

// Mirrors TcpClient::ConnectionState. Duplicated rather than included so this header - and the test
// that links it alone - stays clear of QSslSocket and Protocol. tcpclient.cpp static_asserts that
// the two enumerations agree, so drift is a build error, not a silent misclassification.
enum class Phase { Disconnected = 0, Connecting = 1, Authenticating = 2, Connected = 3 };

enum class Kind {
    None,                    // Nothing to report - an ordinary close, or a timer that lost its race
    ClosedWithoutResponding, // Reached the radio's port, got dropped before any data
    NoResponse,              // Nothing at the address at all; only the connect timer ever spoke
    AcceptedButSilent,       // Connection held open, no data - wrong port pairing or refused PSK
    ConnectFailed,           // Never established, with a socket reason worth quoting verbatim
    SessionLost              // An established session dropped; not about reaching the radio
};

struct Result {
    Kind kind = Kind::None;
    QString message; // Empty iff kind == Kind::None
};

// `authResponseReceived` is the only evidence the radio was ever really there: the first packet
// back. `socketErrorText` is QAbstractSocket::errorString(), used only by Kind::ConnectFailed and
// Kind::SessionLost - the other cases deliberately do not quote it, because in those it is
// indistinguishable between the causes and reads as a diagnosis QK4 cannot make.
inline Result classify(Event event, Phase phase, bool authResponseReceived, const QString &host, quint16 port,
                       const QString &socketErrorText = QString()) {
    // Entered before anything has been heard back from the radio, so it covers a refused password,
    // a blocked port and a host that was never a K4 alike. Say what is certain, list the causes.
    const bool silentInAuth = (phase == Phase::Authenticating && !authResponseReceived);

    switch (event) {
    case Event::ConnectTimeout:
        if (phase != Phase::Connecting)
            return {};
        return {Kind::NoResponse, QString("Unable to connect - no response from %1:%2. Check the radio is "
                                          "powered on and on the network.")
                                      .arg(host)
                                      .arg(port)};

    case Event::AuthTimeout:
        if (!silentInAuth)
            return {};
        return {Kind::AcceptedButSilent, QString("Unable to connect - %1:%2 accepted the connection but sent no "
                                                 "data. Check the password, and that the port matches the mode "
                                                 "(9204 encrypted, 9205 unencrypted).")
                                             .arg(host)
                                             .arg(port)};

    case Event::SocketClosed:
        if (!silentInAuth)
            return {};
        return {Kind::ClosedWithoutResponding, QString("Unable to connect to %1:%2 - it closed the connection without "
                                                       "responding. Check the radio is on, the port is right, and the "
                                                       "password matches.")
                                                   .arg(host)
                                                   .arg(port)};

    case Event::SocketError:
        if (silentInAuth)
            return {Kind::ClosedWithoutResponding,
                    QString("Unable to connect to %1:%2 - it closed the connection without "
                            "responding. Check the radio is on, the port is right, and the "
                            "password matches.")
                        .arg(host)
                        .arg(port)};
        if (phase == Phase::Connecting || phase == Phase::Authenticating)
            return {Kind::ConnectFailed,
                    QString("Unable to connect to %1:%2 - %3").arg(host).arg(port).arg(socketErrorText)};
        return {Kind::SessionLost, QString("Connection to %1 lost - %2").arg(host, socketErrorText)};
    }

    return {};
}

} // namespace ConnectFailure

#endif // CONNECT_FAILURE_H
