#include "ft8/ft8logbook.h"
#include "ft8/ft8session.h"
#include "ft8/ft8transmitter.h"
#include "sstv/sstvencoder.h"
#include "sstv/sstvmoderegistry.h"
#include "sstv/sstvstorage.h"
#include "audio/digitaltxguard.h"
#include "ft8/wsjtbroadcaster.h"

#include <QSet>
#include <QTemporaryDir>
#include <QtTest>
#include <QDataStream>

class TestDigitalModes final : public QObject {
    Q_OBJECT

private slots:
    void ftxWaveformsAreStandardAndTimed();
    void ftxSessionBuildsCqExchange();
    void logbookPersistsAndRoundTripsAdif();
    void sstvRegistryAndEncoderAreUsable();
    void sstvReceivedImagesPersist();
    void digitalTxGuardTargetsSafeAlc();
    void wsjtPacketsUseCompatibleHeader();
};

void TestDigitalModes::ftxWaveformsAreStandardAndTimed() {
    QString error;
    const auto ft8 = ft8TransmitWaveform(QStringLiteral("CQ W1AW FN31"), Ft8::Mode::FT8, 1500, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(ft8.size(), 151680); // 12.64 seconds at 12 kHz.

    const auto ft4 = ft8TransmitWaveform(QStringLiteral("CQ W1AW FN31"), Ft8::Mode::FT4, 1200, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(ft4.size(), 60480); // 5.04 seconds at 12 kHz.

    QVERIFY(ft8TransmitWaveform(QStringLiteral("unsupported free text here"), Ft8::Mode::FT8, 1500, &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

void TestDigitalModes::ftxSessionBuildsCqExchange() {
    Ft8Session session;
    session.myCall = QStringLiteral("W1AW");
    session.myGrid = QStringLiteral("FN31");
    QVERIFY(session.callCq());
    QCOMPARE(session.nextMessage, QStringLiteral("CQ W1AW FN31"));
    QVERIFY(session.arm());
    QVERIFY(session.sent(QDateTime::currentDateTimeUtc()));
    QVERIFY(session.armed);
}

void TestDigitalModes::logbookPersistsAndRoundTripsAdif() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("contacts.json"));
    Ft8Logbook log(path);
    QVERIFY(log.load());

    const AdifRecord contact{{QStringLiteral("CALL"), QStringLiteral("K1ABC")},
                             {QStringLiteral("STATION_CALLSIGN"), QStringLiteral("W1AW")},
                             {QStringLiteral("QSO_DATE"), QStringLiteral("20260914")},
                             {QStringLiteral("TIME_ON"), QStringLiteral("123456")},
                             {QStringLiteral("MODE"), QStringLiteral("FT4")},
                             {QStringLiteral("FREQ"), QStringLiteral("14.080000")}};
    QString error;
    QVERIFY2(log.append(contact, &error), qPrintable(error));
    QCOMPARE(log.records().size(), 1);
    QCOMPARE(Ft8Logbook::canonicalMode(log.records().first()), QStringLiteral("FT4"));

    const QString adif = log.exportAdif(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const auto parsed = Ft8Logbook::parse(adif);
    QCOMPARE(parsed.errors.size(), 0);
    QCOMPARE(parsed.records.size(), 1);

    Ft8Logbook reloaded(path);
    QVERIFY2(reloaded.load(&error), qPrintable(error));
    QCOMPARE(reloaded.records().size(), 1);
    QVERIFY(reloaded.worked(QStringLiteral("K1ABC"), QStringLiteral("20m"), QStringLiteral("FT4")));
}

void TestDigitalModes::sstvRegistryAndEncoderAreUsable() {
    QCOMPARE(SstvModeRegistry::all().size(), 22);
    QSet<int> visCodes;
    for (const auto &mode : SstvModeRegistry::all()) {
        QVERIFY(mode.encoderImplemented);
        QVERIFY(mode.decoderImplemented);
        QVERIFY(mode.width > 0);
        QVERIFY(mode.height > 0);
        QVERIFY(!visCodes.contains(mode.visCode));
        visCodes.insert(mode.visCode);
        QCOMPARE(SstvModeRegistry::findByVis(mode.visCode)->id, mode.id);
    }

    const auto &mode = SstvModeRegistry::defaultTransmitMode();
    QImage frame(mode.width, mode.height, QImage::Format_RGB32);
    frame.fill(qRgb(35, 120, 210));
    SstvEncoder encoder;
    QString error;
    QVERIFY2(encoder.begin(frame, mode.id, &error, QStringLiteral("W1AW"), 20, QStringLiteral("W1AW"), 100, 100),
             qPrintable(error));
    QVERIFY(encoder.totalSamples() > SstvEncoder::SampleRate * 10);
    QCOMPARE(encoder.nextSamples(2048).size(), 2048);
    QCOMPARE(encoder.emittedSamples(), 2048);
}

void TestDigitalModes::sstvReceivedImagesPersist() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SstvStorage storage(directory.path());
    QImage image(320, 256, QImage::Format_RGB32);
    image.fill(qRgb(220, 90, 40));
    SstvRxRecord saved;
    QString error;
    QVERIFY2(storage.saveReceived(image, int(SstvModeId::ScottieS1), QStringLiteral("Scottie S1"),
                                  QStringLiteral("locked"), 14230000, &saved, &error),
             qPrintable(error));
    QVERIFY(!saved.id.isEmpty());
    const auto received = storage.received(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(received.size(), 1);
    QCOMPARE(received.first().frequencyHz, qint64(14230000));
    QVERIFY(QFile::exists(received.first().imagePath));
}

void TestDigitalModes::digitalTxGuardTargetsSafeAlc() {
    auto control = std::make_shared<DigitalTxControl>();
    DigitalTxGuard guard(control);
    QVERIFY(guard.begin(DigitalTxGuard::Mode::Ft8, 7, 0, true));
    guard.audioAccepted(1);
    QCOMPARE(guard.meter(QStringLiteral("TM000000000000"), 700), DigitalTxGuard::Action::None);
    QVERIFY(guard.gain() > DigitalTxGuard::CalibrationStartGain);
    guard.audioAccepted(1000);
    QCOMPARE(guard.meter(QStringLiteral("TM004000000000"), 1400), DigitalTxGuard::Action::None);
    guard.audioAccepted(2400);
    QCOMPARE(guard.meter(QStringLiteral("TM004000000000"), 2700), DigitalTxGuard::Action::Calibrated);
    guard.stop();
    guard.acknowledge();
    QVERIFY(guard.begin(DigitalTxGuard::Mode::Ft8, 8, 3000));
    QCOMPARE(guard.meter(QStringLiteral("TM010000000000"), 3100), DigitalTxGuard::Action::Tripped);
}

void TestDigitalModes::wsjtPacketsUseCompatibleHeader() {
    const QByteArray packet = WsjtBroadcaster::header(2);
    QDataStream in(packet);
    in.setByteOrder(QDataStream::BigEndian);
    in.setVersion(QDataStream::Qt_5_4);
    quint32 magic = 0, schema = 0, type = 0;
    QByteArray id;
    in >> magic >> schema >> type >> id;
    QCOMPARE(magic, quint32(0xadbccbda));
    QCOMPARE(schema, quint32(3));
    QCOMPARE(type, quint32(2));
    QCOMPARE(id, QByteArray("QK4"));
}

QTEST_MAIN(TestDigitalModes)
#include "test_digitalmodes.moc"
