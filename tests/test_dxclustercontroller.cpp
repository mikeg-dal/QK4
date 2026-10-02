#include <QtTest>

#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>

#include <limits>

#include "controllers/dxclustercontroller.h"

// A stand-in cluster node: prompts for a login on every connection, which is what moves
// DxClusterClient to Connected, and keeps the accepted sockets so a test can talk to one of them.
class FakeClusterNode : public QObject {
    Q_OBJECT

public:
    FakeClusterNode() {
        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                socket->setParent(this);
                socket->write("login: ");
                m_sockets.append(socket);
            }
        });
        QVERIFY(m_server.listen(QHostAddress::LocalHost));
    }

    quint16 port() const { return m_server.serverPort(); }
    QList<QTcpSocket *> sockets() const { return m_sockets; }

private:
    QTcpServer m_server;
    QList<QTcpSocket *> m_sockets;
};

class TestDxClusterController : public QObject {
    Q_OBJECT

private slots:
    // Removing a saved cluster shifts every later entry up a row in the settings list. The live
    // connections have to move with them, or the page shows a connected cluster as disconnected
    // and its disconnect and console actions reach a different connection.
    void removingAClusterKeepsLaterConnectionsOnTheirRows() {
        FakeClusterNode nodeA;
        FakeClusterNode nodeB;
        DxClusterController controller;

        controller.connectCluster(0, "127.0.0.1", nodeA.port(), "N0CALL");
        controller.connectCluster(1, "127.0.0.1", nodeB.port(), "N0CALL");
        QTRY_COMPARE(controller.clusterState(0), DxClusterClient::Connected);
        QTRY_COMPARE(controller.clusterState(1), DxClusterClient::Connected);

        // The operator disconnects A and removes it from the list; B becomes row 0.
        controller.disconnectCluster(0);
        QTRY_COMPARE(controller.clusterState(0), DxClusterClient::Disconnected);
        controller.removeCluster(0);

        QCOMPARE(controller.clusterState(0), DxClusterClient::Connected);
        QCOMPARE(controller.clusterState(1), DxClusterClient::Disconnected);

        // Traffic from B is reported against its new row...
        QSignalSpy lines(&controller, &DxClusterController::clusterLineReceived);
        QTRY_COMPARE(nodeB.sockets().size(), 1);
        nodeB.sockets().first()->write("hello from B\r\n");
        QTRY_COMPARE(lines.count(), 1);
        QCOMPARE(lines.at(0).at(0).toInt(), 0);
        QCOMPARE(controller.consoleBuffer(0).last(), QStringLiteral("hello from B"));

        // ...and a console command for row 0 reaches B.
        QByteArray received;
        connect(nodeB.sockets().first(), &QTcpSocket::readyRead, this,
                [&]() { received += nodeB.sockets().first()->readAll(); });
        controller.sendCommand(0, "SH/DX");
        QTRY_VERIFY(received.contains("SH/DX"));

        controller.disconnectAll();
    }

    // Removing a cluster that is still connected closes its connection rather than leaving it
    // running with no list entry to reach it.
    void removingAConnectedClusterClosesIt() {
        FakeClusterNode node;
        DxClusterController controller;

        controller.connectCluster(0, "127.0.0.1", node.port(), "N0CALL");
        QTRY_COMPARE(controller.clusterState(0), DxClusterClient::Connected);
        QTRY_COMPARE(node.sockets().size(), 1);
        QTcpSocket *peer = node.sockets().first();

        controller.removeCluster(0);

        QCOMPARE(controller.clusterState(0), DxClusterClient::Disconnected);
        QTRY_COMPARE(peer->state(), QAbstractSocket::UnconnectedState);
    }

    // One cluster going away must not take the other's spots with it (#167). Covers both ways a
    // cluster goes: the operator disconnecting it, and the node dropping the connection.
    void disconnectingOneClusterKeepsTheOthersSpots() {
        FakeClusterNode nodeA;
        FakeClusterNode nodeB;
        DxClusterController controller;
        connectAndSpot(controller, nodeA, nodeB);

        controller.disconnectCluster(1);
        QCOMPARE(spottedCalls(controller), QStringList{QStringLiteral("K0RX")});

        // B again, this time dropped by the node rather than by the operator.
        controller.connectCluster(1, "127.0.0.1", nodeB.port(), "N0CALL");
        QTRY_COMPARE(nodeB.sockets().size(), 2);
        QTRY_COMPARE(controller.clusterState(1), DxClusterClient::Connected);
        nodeB.sockets().last()->write(kSpotB);
        QTRY_COMPARE(spottedCalls(controller).size(), 2);
        nodeB.sockets().last()->disconnectFromHost();
        QTRY_COMPARE(controller.clusterState(1), DxClusterClient::Disconnected);
        QCOMPARE(spottedCalls(controller), QStringList{QStringLiteral("K0RX")});

        controller.disconnectAll();
    }

    // Removing a cluster drops its spots, and the spots of every later cluster move to its new row
    // with it - so a later disconnect of that row still finds them.
    void removingAClusterKeepsAndRenumbersTheOthersSpots() {
        FakeClusterNode nodeA;
        FakeClusterNode nodeB;
        DxClusterController controller;
        connectAndSpot(controller, nodeA, nodeB);

        controller.removeCluster(0);
        QCOMPARE(spottedCalls(controller), QStringList{QStringLiteral("JA1ABC")});

        controller.disconnectCluster(0); // B, now on row 0
        QVERIFY(spottedCalls(controller).isEmpty());

        controller.disconnectAll();
    }

private:
    static constexpr const char *kSpotA =
        "DX de K3GMQ-#:  14031.00  K0RX           CW    18 dB  24 WPM  CQ      1902Z\r\n";
    static constexpr const char *kSpotB =
        "DX de W3LPL:    14074.00  JA1ABC         FT8   -12 dB                  1903Z\r\n";

    // A on row 0 spots K0RX, B on row 1 spots JA1ABC.
    static void connectAndSpot(DxClusterController &controller, FakeClusterNode &nodeA, FakeClusterNode &nodeB) {
        controller.connectCluster(0, "127.0.0.1", nodeA.port(), "N0CALL");
        controller.connectCluster(1, "127.0.0.1", nodeB.port(), "N0CALL");
        QTRY_COMPARE(controller.clusterState(0), DxClusterClient::Connected);
        QTRY_COMPARE(controller.clusterState(1), DxClusterClient::Connected);
        QTRY_COMPARE(nodeA.sockets().size(), 1);
        QTRY_COMPARE(nodeB.sockets().size(), 1);
        nodeA.sockets().first()->write(kSpotA);
        nodeB.sockets().first()->write(kSpotB);
        QTRY_COMPARE(spottedCalls(controller).size(), 2);
    }

    static QStringList spottedCalls(const DxClusterController &controller) {
        QStringList calls;
        for (const DxSpot &spot : controller.spotsForFrequencyRange(0, std::numeric_limits<qint64>::max()))
            calls << spot.spottedCall;
        return calls;
    }
};

QTEST_GUILESS_MAIN(TestDxClusterController)
#include "test_dxclustercontroller.moc"
