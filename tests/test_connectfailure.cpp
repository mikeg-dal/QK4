#include <QtTest/QtTest>
#include "network/connect_failure.h"

using namespace ConnectFailure;

namespace {
constexpr const char *kHost = "192.168.1.50";
constexpr quint16 kTlsPort = 9204;
constexpr quint16 kPlainPort = 9205;
} // namespace

class TestConnectFailure : public QObject {
    Q_OBJECT

private slots:
    // --- The bug in issue #20: a radio that was never there must not be blamed on the password ---

    void poweredOffRadioDoesNotBlameThePassword() {
        // macOS lies twice here: connected() fires for a host that is not there, so the phase is
        // Authenticating, and the drop arrives as an ordinary socket close.
        const Result r = classify(Event::SocketClosed, Phase::Authenticating, false, kHost, kTlsPort);
        QCOMPARE(r.kind, Kind::ClosedWithoutResponding);
        QVERIFY(!r.message.contains("Authentication", Qt::CaseInsensitive));
        QVERIFY(!r.message.contains("Auth Failed", Qt::CaseInsensitive));
    }

    void wrongPasswordIsClassifiedIdenticallyToAPoweredOffRadio() {
        // Not a shortcoming to fix later: the K4 sends no TLS alert and no error code for a refused
        // PSK, so these two cases carry identical evidence. If a future change makes them diverge,
        // that divergence is a claim QK4 cannot support and this test should fail.
        const Result poweredOff = classify(Event::SocketClosed, Phase::Authenticating, false, kHost, kTlsPort);
        const Result badPassword = classify(Event::SocketError, Phase::Authenticating, false, kHost, kTlsPort,
                                            "The remote host closed the connection");
        QCOMPARE(poweredOff.kind, badPassword.kind);
        QCOMPARE(poweredOff.message, badPassword.message);
    }

    void everyFailureMessageNamesHostAndPort() {
        const Result cases[] = {
            classify(Event::SocketClosed, Phase::Authenticating, false, kHost, kTlsPort),
            classify(Event::SocketError, Phase::Authenticating, false, kHost, kTlsPort, "closed"),
            classify(Event::SocketError, Phase::Connecting, false, kHost, kPlainPort, "Connection refused"),
            classify(Event::ConnectTimeout, Phase::Connecting, false, kHost, kPlainPort),
            classify(Event::AuthTimeout, Phase::Authenticating, false, kHost, kTlsPort),
        };
        for (const Result &r : cases) {
            QVERIFY(r.kind != Kind::None);
            QVERIFY2(r.message.contains(QLatin1String(kHost)), qPrintable(r.message));
            QVERIFY2(r.message.contains(QLatin1String(":92")), qPrintable(r.message));
        }
    }

    // --- One case per failure mode ---

    void nothingAtTheAddressIsTheConnectTimeout() {
        // Verified against the radio: with nothing at the address the socket reports neither
        // connected() nor an error, so only this timer ever speaks.
        const Result r = classify(Event::ConnectTimeout, Phase::Connecting, false, kHost, kPlainPort);
        QCOMPARE(r.kind, Kind::NoResponse);
        QVERIFY(r.message.contains("no response"));
        QVERIFY(r.message.contains("powered on"));
    }

    void heldOpenButSilentIsTheAuthTimeout() {
        const Result r = classify(Event::AuthTimeout, Phase::Authenticating, false, kHost, kTlsPort);
        QCOMPARE(r.kind, Kind::AcceptedButSilent);
        QVERIFY(r.message.contains("accepted the connection"));
        // The one case where the port pairing is worth naming: something answered on the wrong port.
        QVERIFY(r.message.contains("9204"));
        QVERIFY(r.message.contains("9205"));
    }

    void refusedConnectionQuotesTheSocketReason() {
        const Result r =
            classify(Event::SocketError, Phase::Connecting, false, kHost, kPlainPort, "Connection refused");
        QCOMPARE(r.kind, Kind::ConnectFailed);
        QVERIFY(r.message.startsWith("Unable to connect"));
        QVERIFY(r.message.contains("Connection refused"));
    }

    void establishedSessionDropIsALostConnection() {
        const Result r = classify(Event::SocketError, Phase::Connected, true, kHost, kTlsPort, "Network unreachable");
        QCOMPARE(r.kind, Kind::SessionLost);
        QVERIFY(r.message.contains("lost"));
        // Nothing about reaching the radio or about credentials - it was already working.
        QVERIFY(!r.message.startsWith("Unable to connect"));
        QVERIFY(!r.message.contains("password", Qt::CaseInsensitive));
    }

    void authenticatedThenDroppedWhileStillInAuthIsALostConnection() {
        // The radio answered at least once, so the silence explanations no longer apply even though
        // the phase has not advanced yet.
        const Result r =
            classify(Event::SocketError, Phase::Authenticating, true, kHost, kTlsPort, "The remote host closed");
        QCOMPARE(r.kind, Kind::ConnectFailed);
        QVERIFY(!r.message.contains("password", Qt::CaseInsensitive));
    }

    // --- Silence where silence is correct ---

    void cleanDisconnectSaysNothing() {
        QCOMPARE(classify(Event::SocketClosed, Phase::Connected, true, kHost, kTlsPort).kind, Kind::None);
        QCOMPARE(classify(Event::SocketClosed, Phase::Disconnected, false, kHost, kTlsPort).kind, Kind::None);
    }

    void aSocketCloseAfterTheRadioAnsweredSaysNothing() {
        // The user pulled the plug on a working session; onSocketDisconnected is not where that is
        // reported. Emitting here as well is how a status bar ends up erasing its own error.
        QCOMPARE(classify(Event::SocketClosed, Phase::Authenticating, true, kHost, kTlsPort).kind, Kind::None);
    }

    void timersThatLostTheirRaceSayNothing() {
        // Both timers can fire just after the state moved on. Neither may speak then.
        QCOMPARE(classify(Event::ConnectTimeout, Phase::Authenticating, false, kHost, kTlsPort).kind, Kind::None);
        QCOMPARE(classify(Event::ConnectTimeout, Phase::Connected, true, kHost, kTlsPort).kind, Kind::None);
        QCOMPARE(classify(Event::AuthTimeout, Phase::Connected, true, kHost, kTlsPort).kind, Kind::None);
        QCOMPARE(classify(Event::AuthTimeout, Phase::Authenticating, true, kHost, kTlsPort).kind, Kind::None);
    }

    void everyResultCarriesAMessageIffItIsAFailure() {
        const Event events[] = {Event::SocketClosed, Event::SocketError, Event::ConnectTimeout, Event::AuthTimeout};
        const Phase phases[] = {Phase::Disconnected, Phase::Connecting, Phase::Authenticating, Phase::Connected};
        for (Event e : events) {
            for (Phase p : phases) {
                for (bool answered : {false, true}) {
                    const Result r = classify(e, p, answered, kHost, kTlsPort, "some socket reason");
                    QCOMPARE(r.message.isEmpty(), r.kind == Kind::None);
                }
            }
        }
    }
};

QTEST_MAIN(TestConnectFailure)
#include "test_connectfailure.moc"
