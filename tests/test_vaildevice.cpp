#include <QtTest>
#include "hardware/halikeydevice.h"
#include "hardware/vailmidiworker.h"
#include <RtMidi.h>
#include <QMutex>
#include <memory>

class TestVailDevice : public QObject {
    Q_OBJECT

private slots:
    void stopBeforeStartDoesNotOpenAnAdapter() {
        VailMidiWorker worker(QStringLiteral("must not enumerate or open"));
        QSignalSpy opened(&worker, &HaliKeyWorkerBase::portOpened);
        QSignalSpy errors(&worker, &HaliKeyWorkerBase::errorOccurred);
        worker.stop();
        worker.start();
        QCOMPARE(opened.size(), 0);
        QCOMPARE(errors.size(), 0);
    }
    void virtualAdapterLifecycle();
};

void TestVailDevice::virtualAdapterLifecycle() {
#ifndef Q_OS_MACOS
    QSKIP("Virtual duplex MIDI fixture uses CoreMIDI; protocol tests run on every platform");
#else
    const auto name = QStringLiteral("QK4 Vail Test %1").arg(QCoreApplication::applicationPid()).toStdString();
    QMutex configurationMutex;
    std::vector<std::vector<unsigned char>> configuration;
    struct Capture {
        QMutex *mutex;
        std::vector<std::vector<unsigned char>> *messages;
    } capture{&configurationMutex, &configuration};
    std::unique_ptr<RtMidiIn> adapterInput;
    std::unique_ptr<RtMidiOut> adapterOutput;
    try {
        adapterInput = std::make_unique<RtMidiIn>();
        adapterOutput = std::make_unique<RtMidiOut>();
        adapterInput->setCallback(
            [](double, std::vector<unsigned char> *message, void *context) {
                auto *capture = static_cast<Capture *>(context);
                QMutexLocker lock(capture->mutex);
                capture->messages->push_back(*message);
            },
            &capture);
        adapterInput->openVirtualPort(name);
        adapterOutput->openVirtualPort(name);
    } catch (const RtMidiError &error) {
        QFAIL(error.getMessage().c_str());
    }

    HalikeyDevice device(HalikeyDevice::Vail);
    std::atomic<int> softwareMidiEvents{0};
    std::atomic<int> lastSoftwareStatus{0};
    device.setMidiSink([&](int status, int, int, double) {
        lastSoftwareStatus.store(status);
        softwareMidiEvents.fetch_add(1);
    });
    QStringList commands;
    connect(
        &device, &HalikeyDevice::timedKeyingCommand, this,
        [&commands](const QString &command) { commands.append(command); }, Qt::QueuedConnection);
    QSignalSpy errors(&device, &HalikeyDevice::connectionError);
    QSignalSpy paddles(&device, &HalikeyDevice::paddleChanged);
    QSignalSpy keying(&device, &HalikeyDevice::routedVailKeyingStateChanged);
    device.configureVailKeyer(20, 600, 1);
    device.setVailKeyingEnabled(true);
    QVERIFY(device.openPort(QString::fromStdString(name)));
    QTRY_VERIFY_WITH_TIMEOUT(device.isConnected() || !errors.isEmpty(), 5000);
    QVERIFY2(device.isConnected(),
             errors.isEmpty() ? "No connected signal" : qPrintable(errors.first().first().toString()));
    const auto configurationSize = [&]() {
        QMutexLocker lock(&configurationMutex);
        return configuration.size();
    };
    QTRY_COMPARE(configurationSize(), std::size_t(4));
    {
        QMutexLocker lock(&configurationMutex);
        QVERIFY(configuration[0] == std::vector<unsigned char>({0xB0, 0, 0}));
        QVERIFY(configuration[1] == std::vector<unsigned char>({0xC0, 8}));
        QVERIFY(configuration[2] == std::vector<unsigned char>({0xB0, 1, 30}));
        QVERIFY(configuration[3] == std::vector<unsigned char>({0xB0, 2, 74}));
    }
    const auto send = [&](std::initializer_list<unsigned char> bytes) {
        const std::vector<unsigned char> message(bytes);
        adapterOutput->sendMessage(&message);
    };
    send({0x90, 1, 127});
    // Hold the main receiver queue across both edges. Forwarded times must
    // describe MIDI ingress, not collapse to their near-simultaneous delivery.
    QTest::qSleep(60);
    send({0x80, 1, 0});
    QTest::qSleep(80);
    QTRY_COMPARE(commands.size(), 2);
    QCOMPARE(commands.first(), QStringLiteral("KZD0000;"));
    QTRY_COMPARE(keying.size(), 2);
    QCOMPARE(keying[0][0].toBool(), true);
    const quint64 initialEpoch = keying[0][1].toULongLong();
    device.setVailKeyingEnabled(true);
    QVERIFY(commands.last().startsWith(QStringLiteral("KZU")));
    QVERIFY(commands.last().mid(3, 4).toInt() >= 40);
    QVERIFY(commands.last().mid(3, 4).toInt() < 1000);
    QCOMPARE(paddles.size(), 0);
    QCOMPARE(softwareMidiEvents.load(), 0);
    QTRY_COMPARE(keying.size(), 2);
    QCOMPARE(keying[1][0].toBool(), false);
    QCOMPARE(keying[1][1].toULongLong(), initialEpoch);
    const qint64 downNs = keying[0][2].toLongLong();
    const qint64 upNs = keying[1][2].toLongLong();
    QVERIFY(upNs > downNs);
    const qint64 heldMs = (upNs - downNs + 500000) / 1000000;
    QVERIFY(heldMs >= 40);
    QCOMPARE(heldMs, static_cast<qint64>(commands[1].mid(3, 4).toInt()));

    device.configureVailKeyer(20, 600, 1);
    QTest::qWait(80);
    QCOMPARE(configurationSize(), std::size_t(4));

    send({0x90, 2, 127});
    QTRY_COMPARE(commands.size(), 3);
    device.setVailKeyingEnabled(false);
    QTRY_COMPARE(commands.size(), 5);
    QVERIFY(commands[3].startsWith(QStringLiteral("KZU")));
    QCOMPARE(commands[4], QStringLiteral("RX;"));
    QTRY_COMPARE(keying.size(), 4);
    QCOMPARE(keying[2][0].toBool(), true);
    QCOMPARE(keying[3][0].toBool(), false);
    QCOMPARE(keying[3][1].toULongLong(), keying[2][1].toULongLong());
    device.setVailKeyingEnabled(true);
    send({0x90, 2, 127});
    QTest::qWait(80);
    QCOMPARE(commands.size(), 5);
    QCOMPARE(keying.size(), 4);
    send({0x80, 2, 0});
    QTest::qWait(50);
    send({0x90, 0, 127});
    QTRY_COMPARE(commands.size(), 6);
    QCOMPARE(commands.last(), QStringLiteral("KZD0000;"));
    device.closePort();
    QTRY_COMPARE(commands.size(), 8);
    QVERIFY(commands[6].startsWith(QStringLiteral("KZU")));
    QCOMPARE(commands[7], QStringLiteral("RX;"));
    QTRY_COMPARE(keying.size(), 6);
    QCOMPARE(keying[4][0].toBool(), true);
    QCOMPARE(keying[5][0].toBool(), false);
    QCOMPARE(keying[5][1].toULongLong(), keying[4][1].toULongLong());
    QVERIFY(!device.isConnected());
    QCOMPARE(errors.size(), 0);

    // A queued failure from a replaced open attempt must not close the new connection.
    QVERIFY(device.openPort(QStringLiteral("Missing Vail device")));
    QVERIFY(device.openPort(QString::fromStdString(name)));
    QTRY_VERIFY_WITH_TIMEOUT(device.isConnected() || !errors.isEmpty(), 5000);
    QVERIFY(device.isConnected());
    QCOMPARE(errors.size(), 0);
    commands.clear();
    keying.clear();
    send({0x90, 1, 127});
    QTRY_COMPARE(commands.size(), 1);
    QTRY_COMPARE(keying.size(), 1);
    // Changing device type uses the same matched release path as an explicit close.
    device.setDeviceType(HalikeyDevice::Midi);
    QTRY_COMPARE(commands.size(), 3);
    QTRY_COMPARE(keying.size(), 2);
    QCOMPARE(keying[0][0].toBool(), true);
    QCOMPARE(keying[1][0].toBool(), false);
    QCOMPARE(keying[1][1].toULongLong(), keying[0][1].toULongLong());
    QVERIFY(!device.isConnected());
    QVERIFY(device.openPort(QString::fromStdString(name)));
    QTRY_VERIFY_WITH_TIMEOUT(device.isConnected() || !errors.isEmpty(), 5000);
    QVERIFY(device.isConnected());
    send({0x91, 20, 127});
    QTRY_COMPARE(softwareMidiEvents.load(), 1);
    QCOMPARE(lastSoftwareStatus.load(), 0x90);
    send({0x91, 20, 0});
    QTRY_COMPARE(softwareMidiEvents.load(), 2);
    QCOMPARE(lastSoftwareStatus.load(), 0x80);
    device.setDeviceType(HalikeyDevice::Vail);
    QVERIFY(device.openPort(QString::fromStdString(name)));
    QTRY_VERIFY_WITH_TIMEOUT(device.isConnected() || !errors.isEmpty(), 5000);
    QVERIFY(device.isConnected());
    commands.clear();
    keying.clear();
    send({0x90, 1, 127});
    QTRY_COMPARE(commands.size(), 1);
    QTRY_COMPARE(keying.size(), 1);
    QCOMPARE(softwareMidiEvents.load(), 2);
    adapterOutput.reset();
    QTRY_VERIFY_WITH_TIMEOUT(!device.isConnected(), 4000);
    QTRY_COMPARE(commands.size(), 3);
    QVERIFY(commands[1].startsWith(QStringLiteral("KZU")));
    QCOMPARE(commands[2], QStringLiteral("RX;"));
    QTRY_COMPARE(keying.size(), 2);
    QCOMPARE(keying[0][0].toBool(), true);
    QCOMPARE(keying[1][0].toBool(), false);
    QCOMPARE(keying[1][1].toULongLong(), keying[0][1].toULongLong());
    QCOMPARE(errors.size(), 1);
    adapterInput->cancelCallback();
#endif
}

QTEST_GUILESS_MAIN(TestVailDevice)
#include "test_vaildevice.moc"
