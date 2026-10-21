#include "audio/sidetonegenerator.h"
#include "audio/sidetonescheduler.h"
#include "controllers/connectioncontroller.h"
#include "controllers/cwcontroller.h"
#include "hardware/halikeydevice.h"
#include "hardware/keyerruntime.h"
#include "hardware/kpodplusdevice.h"
#include "hardware/paddlekeyer.h"
#include "models/radiostate.h"
#include "network/protocol.h"
#include "settings/radiosettings.h"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QMutex>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QWaitCondition>
#include <memory>

namespace {

// A real framed connection on a separate event loop: the GUI can block without also blocking
// the radio's echoes, TcpClient's input, or the monitor restore timer under test.
class FakeRadio {
public:
    FakeRadio() {
        m_context = new QObject;
        m_context->moveToThread(&m_thread);
        QObject::connect(&m_thread, &QThread::finished, m_context, &QObject::deleteLater);
        m_thread.start();
        QMetaObject::invokeMethod(
            m_context,
            [this]() {
                m_server = new QTcpServer(m_context);
                if (!m_server->listen(QHostAddress::LocalHost, 0))
                    return;
                m_port = m_server->serverPort();
                m_parser = new Protocol(m_context);
                QObject::connect(m_parser, &Protocol::catResponseReceived, m_context,
                                 [this](const QString &response) { receiveCommands(response); });
                QObject::connect(m_server, &QTcpServer::newConnection, m_context, [this]() {
                    m_socket = m_server->nextPendingConnection();
                    QObject::connect(m_socket, &QTcpSocket::readyRead, m_context, [this]() {
                        const auto bytes = m_socket->readAll();
                        if (!m_authenticated) {
                            m_authentication += bytes;
                            const auto expected = Protocol::buildAuthData(QStringLiteral("cw-monitor-test"));
                            if (m_authentication.size() < expected.size())
                                return;
                            m_authenticated = m_authentication.left(expected.size()) == expected;
                            if (!m_authenticated)
                                return;
                            const auto remainder = m_authentication.mid(expected.size());
                            m_authentication.clear();
                            send(QStringLiteral("MD3;ML0025;KZL80;"));
                            if (!remainder.isEmpty())
                                m_parser->parse(remainder);
                        } else {
                            m_parser->parse(bytes);
                        }
                    });
                });
            },
            Qt::BlockingQueuedConnection);
    }

    ~FakeRadio() {
        QMetaObject::invokeMethod(
            m_context,
            [this]() {
                if (m_socket)
                    m_socket->abort();
                m_server->close();
            },
            Qt::BlockingQueuedConnection);
        m_thread.quit();
        m_thread.wait();
    }

    quint16 port() const { return m_port; }

    QStringList commands() {
        QMutexLocker lock(&m_mutex);
        return m_commands;
    }

    int count(const QString &command) { return commands().count(command); }

    bool waitForCommand(const QString &command, int count = 1, int timeoutMs = 5000) {
        QElapsedTimer elapsed;
        elapsed.start();
        QMutexLocker lock(&m_mutex);
        while (m_commands.count(command) < count) {
            const auto remaining = timeoutMs - elapsed.elapsed();
            if (remaining <= 0)
                return false;
            m_changed.wait(&m_mutex, static_cast<unsigned long>(remaining));
        }
        return true;
    }

    void changeMonitorLevel(int level) {
        QMetaObject::invokeMethod(
            m_context,
            [this, level]() {
                m_level = level;
                send(levelCommand());
            },
            Qt::QueuedConnection);
    }

private:
    QString levelCommand() const { return QStringLiteral("ML0%1;").arg(m_level, 3, 10, QLatin1Char('0')); }

    void send(const QString &response) {
        m_socket->write(Protocol::buildCATPacket(response));
        m_socket->flush();
    }

    void receiveCommands(const QString &response) {
        for (const auto &part : response.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
            const auto command = part + QLatin1Char(';');
            {
                QMutexLocker lock(&m_mutex);
                m_commands.append(command);
                m_changed.wakeAll();
            }
            if (command == QLatin1String("ML0;")) {
                send(levelCommand());
            } else if (command == QLatin1String("KZL;")) {
                send(QStringLiteral("KZL80;"));
            } else if (part.size() == 6 && part.startsWith(QLatin1String("ML0"))) {
                m_level = part.mid(3).toInt();
                send(command);
            } else if (command == QLatin1String("RDY;")) {
                send(QStringLiteral("MD3;ML0025;KZL80;"));
            } else if (command == QLatin1String("FA;")) {
                send(QStringLiteral("FA00014025000;"));
            }
        }
    }

    QThread m_thread;
    QObject *m_context = nullptr;
    QTcpServer *m_server = nullptr;
    QTcpSocket *m_socket = nullptr;
    Protocol *m_parser = nullptr;
    quint16 m_port = 0;
    QByteArray m_authentication;
    bool m_authenticated = false;
    int m_level = 25;
    QMutex m_mutex;
    QWaitCondition m_changed;
    QStringList m_commands;
};

struct Rig {
    RadioState radioState;
    ConnectionController connection{&radioState};
    SteadyKeyerClock clock;
    ForwardingLink link;
    QtOneShotTimer unmuteTimer;
    SidetoneScheduler scheduler{clock, []() -> std::optional<double> { return std::nullopt; }};
    SidetoneGenerator sidetone{scheduler, clock};
    PaddleKeyer keyer{clock, link, scheduler, unmuteTimer};
    HalikeyDevice device{RadioSettings::instance()->halikeyDeviceType()};
    KpodPlusDevice kpod;
    std::unique_ptr<CwController> controller;
    int guiResponses = 0;
    bool wrongGuiThread = false;

    Rig() {
        radioState.parseCATCommand(QStringLiteral("MD3;"));
        QObject::connect(RadioSettings::instance(), &RadioSettings::halikeyDeviceTypeChanged, &device,
                         &HalikeyDevice::setDeviceType);
        QObject::connect(&connection, &ConnectionController::catResponseReceived, &radioState,
                         [this](const QString &response) {
                             wrongGuiThread |= QThread::currentThread() != radioState.thread();
                             ++guiResponses;
                             for (const auto &command : response.split(QLatin1Char(';'), Qt::SkipEmptyParts))
                                 radioState.parseCATCommand(command + QLatin1Char(';'));
                         });
        controller = std::make_unique<CwController>(&radioState, &connection, &keyer, &link, &sidetone, &device, &kpod);
    }

    ~Rig() {
        QMetaObject::invokeMethod(connection.tcpClient(), &TcpClient::disconnectFromHost, Qt::BlockingQueuedConnection);
    }

    void connect(FakeRadio &radio) {
        RadioEntry entry;
        entry.name = QStringLiteral("Local monitor test");
        entry.host = QStringLiteral("127.0.0.1");
        entry.port = radio.port();
        entry.password = QStringLiteral("cw-monitor-test");
        entry.useTls = false;
        connection.connectToRadio(entry);
    }

    quint64 armVail() {
        RadioSettings::instance()->setHalikeyDeviceType(HalikeyDevice::Vail);
        const auto control = device.vailRoutingControl();
        const auto epoch = control->invalidate();
        control->enabled.store(true, std::memory_order_release);
        return epoch;
    }

    void vail(const QString &command, quint64 epoch) { emit device.routedVailKeyingCommand(command, epoch); }
};

} // namespace

class TestCwMonitorRouting : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_settingsDirectory;

private slots:
    void initTestCase() {
        QVERIFY(m_settingsDirectory.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDirectory.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, m_settingsDirectory.path() + "/system");
        const QSettings probe(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("QK4"),
                              QStringLiteral("QK4"));
        QVERIFY(probe.fileName().startsWith(m_settingsDirectory.path() + '/'));
    }

    void init() {
        RadioSettings::instance()->setHalikeyDeviceType(HalikeyDevice::Vail);
        RadioSettings::instance()->setVailComputerSidetone(false);
        RadioSettings::instance()->setHalikeyMuteRadioMonitor(true);
        RadioSettings::instance()->setHalikeyStraightKey(false);
    }

    void vailMutesBeforeDownAndRestoresOnce() {
        FakeRadio radio;
        QVERIFY(radio.port() != 0);
        Rig rig;
        rig.connect(radio);
        QTRY_VERIFY_WITH_TIMEOUT(rig.connection.isConnected(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(rig.radioState.monitorLevelCW(), 25, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(rig.radioState.keyDownInitialDelayMs(), 80, 5000);
        rig.connection.sendCAT(QStringLiteral("FA;"));
        QTRY_COMPARE_WITH_TIMEOUT(rig.radioState.vfoA(), quint64(14025000), 5000);
        const auto epoch = rig.armVail();
        rig.vail(QStringLiteral("KZD0000;"), epoch);
        QVERIFY(radio.waitForCommand(QStringLiteral("KZD0000;")));
        const auto started = radio.commands();
        QVERIFY(started.indexOf(QStringLiteral("ML0000;")) >= 0);
        QVERIFY(started.indexOf(QStringLiteral("ML0000;")) < started.indexOf(QStringLiteral("KZD0000;")));
        rig.vail(QStringLiteral("KZU0060;"), epoch);
        QVERIFY(radio.waitForCommand(QStringLiteral("ML0025;")));
        const auto completed = radio.commands();
        QCOMPARE(completed.count(QStringLiteral("ML0000;")), 1);
        QCOMPARE(completed.count(QStringLiteral("ML0025;")), 1);
        QVERIFY(completed.indexOf(QStringLiteral("KZU0060;")) < completed.indexOf(QStringLiteral("ML0025;")));
        QTRY_COMPARE_WITH_TIMEOUT(rig.radioState.monitorLevelCW(), 25, 5000);
        QVERIFY(!rig.wrongGuiThread);
    }

    void softwareToVailHandoffUsesIoEchoesWhileGuiIsBlocked() {
        RadioSettings::instance()->setHalikeyDeviceType(HalikeyDevice::Midi);
        FakeRadio radio;
        QVERIFY(radio.port() != 0);
        Rig rig;
        rig.connect(radio);
        QTRY_VERIFY_WITH_TIMEOUT(rig.connection.isConnected(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(rig.radioState.monitorLevelCW(), 25, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(rig.radioState.keyDownInitialDelayMs(), 80, 5000);
        rig.connection.sendCAT(QStringLiteral("FA;"));
        QTRY_COMPARE_WITH_TIMEOUT(rig.radioState.vfoA(), quint64(14025000), 5000);
        rig.link.send("KZD0000;");
        QVERIFY(radio.waitForCommand(QStringLiteral("KZD0000;")));
        rig.link.send("KZU0060;");
        QVERIFY(radio.waitForCommand(QStringLiteral("KZU0060;")));
        const auto epoch = rig.armVail();
        rig.vail(QStringLiteral("KZD0060;"), epoch);
        QVERIFY(radio.waitForCommand(QStringLiteral("KZD0060;")));
        const int guiResponses = rig.guiResponses;
        const int mutedBeforeChange = radio.count(QStringLiteral("ML0000;"));
        radio.changeMonitorLevel(37);
        // WHY while held: a delayed fake-radio thread must still apply the operator change before
        // the restore deadline exists. Condition-variable waits leave the GUI queue untouched.
        QVERIFY(radio.waitForCommand(QStringLiteral("ML0000;"), mutedBeforeChange + 1));
        rig.vail(QStringLiteral("KZU0180;"), epoch);
        QVERIFY(radio.waitForCommand(QStringLiteral("ML0037;"), 1, 5000));
        QCOMPARE(rig.guiResponses, guiResponses);
        const auto completed = radio.commands();
        const auto vailDown = completed.indexOf(QStringLiteral("KZD0060;"));
        // A pause during device selection may legitimately finish the earlier software burst.
        for (qsizetype index = 0; index < completed.size(); ++index) {
            if (completed.at(index) == QLatin1String("ML0025;"))
                QVERIFY(index < vailDown);
        }
        QCOMPARE(completed.count(QStringLiteral("ML0037;")), 1);
        QVERIFY(completed.indexOf(QStringLiteral("KZU0060;")) < completed.indexOf(QStringLiteral("KZD0060;")));
        QVERIFY(completed.indexOf(QStringLiteral("KZU0180;")) < completed.indexOf(QStringLiteral("ML0037;")));
        QTRY_COMPARE_WITH_TIMEOUT(rig.radioState.monitorLevelCW(), 37, 5000);
        QVERIFY(!rig.wrongGuiThread);
    }

    void gracefulDisconnectRestoresBeforeRrn() {
        FakeRadio radio;
        QVERIFY(radio.port() != 0);
        Rig rig;
        rig.connect(radio);
        QTRY_VERIFY_WITH_TIMEOUT(rig.connection.isConnected(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(rig.radioState.monitorLevelCW(), 25, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(rig.radioState.keyDownInitialDelayMs(), 80, 5000);
        rig.connection.sendCAT(QStringLiteral("FA;"));
        QTRY_COMPARE_WITH_TIMEOUT(rig.radioState.vfoA(), quint64(14025000), 5000);
        const auto epoch = rig.armVail();
        rig.vail(QStringLiteral("KZD0000;"), epoch);
        QVERIFY(radio.waitForCommand(QStringLiteral("KZD0000;")));
        rig.connection.disconnectFromRadio();
        QVERIFY(radio.waitForCommand(QStringLiteral("RRN;")));
        const auto completed = radio.commands();
        QCOMPARE(completed.count(QStringLiteral("ML0025;")), 1);
        QVERIFY(completed.indexOf(QStringLiteral("ML0025;")) < completed.indexOf(QStringLiteral("RRN;")));
        QTRY_VERIFY_WITH_TIMEOUT(!rig.connection.isConnected(), 5000);
        QVERIFY(!rig.wrongGuiThread);
    }

    void taggedSoftwareRouteDropsQueuedOldSessionWithoutMutingIt() {
        RadioSettings::instance()->setHalikeyDeviceType(HalikeyDevice::Midi);
        FakeRadio radio;
        QVERIFY(radio.port() != 0);
        Rig rig;
        rig.connect(radio);
        QTRY_VERIFY_WITH_TIMEOUT(rig.connection.isConnected(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(rig.radioState.monitorLevelCW(), 25, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(rig.radioState.keyDownInitialDelayMs(), 80, 5000);
        rig.connection.sendCAT(QStringLiteral("FA;"));
        QTRY_COMPARE_WITH_TIMEOUT(rig.radioState.vfoA(), quint64(14025000), 5000);
        auto *tc = rig.connection.tcpClient();
        QMetaObject::invokeMethod(
            tc,
            [&rig, tc]() {
                rig.link.send("KZD0099;");
                tc->kzEgress()->setSoftwareKeyingAllowed(false);
                rig.link.send("KZU0099;");
                tc->kzEgress()->setSoftwareKeyingAllowed(true);
                rig.link.send("KZD0000;");
                rig.link.send("KZU0060;");
            },
            Qt::BlockingQueuedConnection);
        QVERIFY(radio.waitForCommand(QStringLiteral("KZU0060;")));
        QVERIFY(radio.waitForCommand(QStringLiteral("ML0025;")));
        const auto completed = radio.commands();
        QVERIFY(!completed.contains(QStringLiteral("KZD0099;")));
        QVERIFY(!completed.contains(QStringLiteral("KZU0099;")));
        QCOMPARE(completed.count(QStringLiteral("ML0000;")), 1);
        QCOMPARE(completed.count(QStringLiteral("ML0025;")), 1);
        QVERIFY(completed.indexOf(QStringLiteral("ML0000;")) < completed.indexOf(QStringLiteral("KZD0000;")));
    }

    void taggedSoftwareReleaseSurvivesItsGateClosing() {
        RadioSettings::instance()->setHalikeyDeviceType(HalikeyDevice::Midi);
        FakeRadio radio;
        QVERIFY(radio.port() != 0);
        Rig rig;
        rig.connect(radio);
        QTRY_VERIFY_WITH_TIMEOUT(rig.connection.isConnected(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(rig.radioState.monitorLevelCW(), 25, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(rig.radioState.keyDownInitialDelayMs(), 80, 5000);
        rig.connection.sendCAT(QStringLiteral("FA;"));
        QTRY_COMPARE_WITH_TIMEOUT(rig.radioState.vfoA(), quint64(14025000), 5000);
        rig.link.send("KZD0000;");
        QVERIFY(radio.waitForCommand(QStringLiteral("KZD0000;")));
        rig.connection.tcpClient()->kzEgress()->setSoftwareKeyingAllowed(false);
        rig.link.send("KZU0060;");
        QVERIFY(radio.waitForCommand(QStringLiteral("KZU0060;")));
        QVERIFY(radio.waitForCommand(QStringLiteral("ML0025;")));
        const auto completed = radio.commands();
        QCOMPARE(completed.count(QStringLiteral("KZU0060;")), 1);
        QCOMPARE(completed.count(QStringLiteral("ML0025;")), 1);
        QVERIFY(completed.indexOf(QStringLiteral("KZU0060;")) < completed.indexOf(QStringLiteral("ML0025;")));
    }

    void gracefulRestoreSurvivesControllerBeingDestroyedFirst() {
        FakeRadio radio;
        QVERIFY(radio.port() != 0);
        Rig rig;
        rig.connect(radio);
        QTRY_VERIFY_WITH_TIMEOUT(rig.connection.isConnected(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(rig.radioState.monitorLevelCW(), 25, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(rig.radioState.keyDownInitialDelayMs(), 80, 5000);
        rig.connection.sendCAT(QStringLiteral("FA;"));
        QTRY_COMPARE_WITH_TIMEOUT(rig.radioState.vfoA(), quint64(14025000), 5000);
        const auto epoch = rig.armVail();
        rig.vail(QStringLiteral("KZD0000;"), epoch);
        QVERIFY(radio.waitForCommand(QStringLiteral("KZD0000;")));
        rig.controller.reset();
        rig.connection.disconnectFromRadio();
        QVERIFY(radio.waitForCommand(QStringLiteral("RRN;")));
        const auto completed = radio.commands();
        QCOMPARE(completed.count(QStringLiteral("ML0025;")), 1);
        QVERIFY(completed.indexOf(QStringLiteral("ML0025;")) < completed.indexOf(QStringLiteral("RRN;")));
    }

    void failedDeviceCleanupDisconnectsAndRestoresOwnedMonitor() {
        FakeRadio radio;
        QVERIFY(radio.port() != 0);
        Rig rig;
        rig.connect(radio);
        QTRY_VERIFY_WITH_TIMEOUT(rig.connection.isConnected(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(rig.radioState.monitorLevelCW(), 25, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(rig.radioState.keyDownInitialDelayMs(), 80, 5000);
        rig.connection.sendCAT(QStringLiteral("FA;"));
        QTRY_COMPARE_WITH_TIMEOUT(rig.radioState.vfoA(), quint64(14025000), 5000);
        const auto epoch = rig.armVail();
        rig.vail(QStringLiteral("KZD0000;"), epoch);
        QVERIFY(radio.waitForCommand(QStringLiteral("KZD0000;")));
        emit rig.device.vailCleanupFailed();
        QVERIFY(radio.waitForCommand(QStringLiteral("RRN;")));
        const auto completed = radio.commands();
        QCOMPARE(completed.count(QStringLiteral("ML0025;")), 1);
        QVERIFY(completed.indexOf(QStringLiteral("ML0025;")) < completed.indexOf(QStringLiteral("RRN;")));
        QTRY_VERIFY_WITH_TIMEOUT(!rig.connection.isConnected(), 5000);
    }
};

QTEST_GUILESS_MAIN(TestCwMonitorRouting)
#include "test_cwmonitorrouting.moc"
