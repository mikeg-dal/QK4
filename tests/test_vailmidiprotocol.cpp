#include "hardware/vail_midi_protocol.h"

#include <QtTest>
#include <limits>

using namespace VailMidiProtocol;

namespace {
constexpr std::int64_t ms(int value) {
    return std::int64_t(value) * 1'000'000;
}
} // namespace

class TestVailMidiProtocol : public QObject {
    Q_OBJECT

private slots:
    void configuresMidiAndAdapterKeyer();
    void configuresSafeStraightProgram();
    void quantizesSpeedAndPitch();
    void doesNotRepeatEepromWrites();
    void rejectsInvalidConfiguration();
    void pairsOnlyUniqueExactSelectedPorts();
    void pairsWinmmPortsWithIndependentIndices();
    void survivesWinmmIndexRenumbering();
    void rejectsAmbiguousWinmmDeviceNames();
    void preservesDeviceNumbersAndOtherBackendNames();
    void acceptsAllFirmwareKeyedNotes_data();
    void acceptsAllFirmwareKeyedNotes();
    void ignoresMalformedAndUnrelatedMidi_data();
    void ignoresMalformedAndUnrelatedMidi();
    void overlappingNotesDoNotUnkey();
    void deduplicatesRepeatedEdges();
    void forwardsElapsedBeforeEachEdge();
    void usesCaptureTimestampsForQueuedBursts();
    void longIdleStartsNewTimingSession();
    void disabledInputDoesNotKey();
    void repeatedEnableDoesNotInterruptHeldKey();
    void enableWhileHeldRequiresRelease();
    void disableAbortsThenRequiresRelease();
    void holdTimeoutAbortsThenRequiresRelease();
    void lateReleaseAbortsWithBoundedDuration();
    void nonMonotonicReleaseAborts();
    void timestampOverflowCannotEscapeDurationLimit();
};

void TestVailMidiProtocol::configuresMidiAndAdapterKeyer() {
    const auto messages = configurationMessages({20, 600, 0});
    QVERIFY(messages == std::vector<Message>({{0xB0, 0, 0}, {0xC0, 7}, {0xB0, 1, 30}, {0xB0, 2, 74}}));
    QVERIFY(configurationMessages({20, 600, 1})[1] == Message({0xC0, 8}));
}

void TestVailMidiProtocol::configuresSafeStraightProgram() {
    // PC0 dereferences a null keyer in released adapter firmware; PC1 is straight/cootie.
    QVERIFY(configurationMessages({20, 600, 2})[1] == Message({0xC0, 1}));
}

void TestVailMidiProtocol::quantizesSpeedAndPitch() {
    QCOMPARE((Configuration{23, 440, 0}).ditController(), 26);
    QCOMPARE((Configuration{5, 440, 0}).ditController(), 120);
    QCOMPARE((Configuration{100, 440, 0}).ditController(), 6);
    QCOMPARE((Configuration{20, 440, 0}).pitchController(), 69);
    QCOMPARE((Configuration{20, 600, 0}).pitchController(), 74);
    QCOMPARE((Configuration{20, 700, 0}).pitchController(), 77);
}

void TestVailMidiProtocol::doesNotRepeatEepromWrites() {
    const Configuration before{20, 600, 0};
    QVERIFY(configurationMessages(before, &before).empty());
    // Nearby frequencies can select the same MIDI note and do not need an EEPROM write.
    QVERIFY(configurationMessages({20, 601, 0}, &before).empty());
    QVERIFY(configurationMessages({20, 600, 1}, &before) == std::vector<Message>({{0xC0, 8}}));
    QVERIFY(configurationMessages({30, 600, 0}, &before) == std::vector<Message>({{0xB0, 1, 20}}));
}

void TestVailMidiProtocol::rejectsInvalidConfiguration() {
    for (const Configuration configuration : {Configuration{0, 600, 0},
                                              {4, 600, 0},
                                              {101, 600, 0},
                                              {20, 0, 0},
                                              {20, 2001, 0},
                                              {20, 600, -1},
                                              {20, 600, 3}}) {
        QVERIFY(!configuration.valid());
        QVERIFY(configurationMessages(configuration).empty());
    }
}

void TestVailMidiProtocol::pairsOnlyUniqueExactSelectedPorts() {
    const QStringList names{"Vail", "Vail 2", "Other"};
    QCOMPARE(uniquePort(names, "Vail"), 0);
    QCOMPARE(uniquePort(names, "Vail 2"), 1);
    QCOMPARE(uniquePort(names, "vail"), -1);
    QCOMPARE(uniquePort(names, "Vai"), -1);
    QCOMPARE(uniquePort(names, QString()), -1);
    QCOMPARE(uniquePort({}, "Vail"), -1);
    QCOMPARE(uniquePort({"Vail", "Vail"}, "Vail"), -1);
}

void TestVailMidiProtocol::pairsWinmmPortsWithIndependentIndices() {
    constexpr auto naming = PortNaming::WinmmIndexed;
    QCOMPARE(uniquePort({"Vail Adapter 0"}, "Vail Adapter 0", naming), 0);
    QCOMPARE(uniquePort({"Microsoft GS Wavetable Synth 0", "Vail Adapter 1"}, "Vail Adapter 0", naming), 1);
    QCOMPARE(uniquePort({"Other 0"}, "Vail Adapter 0", naming), -1);
    QCOMPARE(uniquePort({}, "Vail Adapter 0", naming), -1);
    QCOMPARE(uniquePort({"Vail Adapter 0"}, QString(), naming), -1);
}

void TestVailMidiProtocol::survivesWinmmIndexRenumbering() {
    constexpr auto naming = PortNaming::WinmmIndexed;
    QCOMPARE(uniquePort({"Other 0", "Vail Adapter 1"}, "Vail Adapter 1", naming), 1);
    QCOMPARE(uniquePort({"Vail Adapter 0"}, "Vail Adapter 1", naming), 0);
    QCOMPARE(uniquePort({"Vail Adapter 0", "Other 1"}, "Vail Adapter 1", naming), 0);
}

void TestVailMidiProtocol::rejectsAmbiguousWinmmDeviceNames() {
    constexpr auto naming = PortNaming::WinmmIndexed;
    QCOMPARE(uniquePort({"Vail Adapter 0", "Vail Adapter 1"}, "Vail Adapter 0", naming), -1);
    QCOMPARE(uniquePort({"Vail Adapter 0", "Vail Adapter 1"}, "Vail Adapter 1", naming), -1);
    QCOMPARE(uniquePort({"Other 0", "Vail Adapter 1", "Vail Adapter 2"}, "Vail Adapter 0", naming), -1);
}

void TestVailMidiProtocol::preservesDeviceNumbersAndOtherBackendNames() {
    constexpr auto naming = PortNaming::WinmmIndexed;
    QCOMPARE(uniquePort({"Vail 2 0", "Vail 3 1"}, "Vail 2 4", naming), 0);
    QCOMPARE(uniquePort({"Vail 2 0", "Vail 3 1"}, "Vail 3 4", naming), 1);
    QCOMPARE(uniquePort({"Vail Adapter 2nd 0"}, "Vail Adapter 0", naming), -1);
    QCOMPARE(uniquePort({"Vail Adapter 0"}, "vail adapter 0", naming), -1);
    QCOMPARE(uniquePort({"Vail 2"}, "Vail 0"), -1);
    QCOMPARE(uniquePort({"Vail 2"}, "Vail 2"), 0);
    QCOMPARE(uniquePort({"Vail 20:0"}, "Vail 21:0"), -1);
}

void TestVailMidiProtocol::acceptsAllFirmwareKeyedNotes_data() {
    QTest::addColumn<int>("note");
    QTest::newRow("straight-or-documented-output") << 0;
    QTest::newRow("timed-dit-output") << 1;
    QTest::newRow("timed-dah-output") << 2;
}

void TestVailMidiProtocol::acceptsAllFirmwareKeyedNotes() {
    QFETCH(int, note);
    Notes notes;
    const auto number = static_cast<unsigned char>(note);
    QCOMPARE(notes.accept({0x90, number, 1}), Edge::Down);
    QVERIFY(notes.down());
    QCOMPARE(notes.accept({0x90, number, 0}), Edge::Up);
    QVERIFY(!notes.down());
    QCOMPARE(notes.accept({0x90, number, 127}), Edge::Down);
    QCOMPARE(notes.accept({0x80, number, 127}), Edge::Up);
}

void TestVailMidiProtocol::ignoresMalformedAndUnrelatedMidi_data() {
    QTest::addColumn<QByteArray>("bytes");
    for (const auto &row : std::vector<std::pair<const char *, Message>>{{"empty", {}},
                                                                         {"short", {0x90, 0}},
                                                                         {"long", {0x90, 0, 127, 0}},
                                                                         {"other-channel", {0x91, 0, 127}},
                                                                         {"control-change", {0xB0, 0, 127}},
                                                                         {"program-change", {0xC0, 7}},
                                                                         {"halikey-note", {0x90, 20, 127}},
                                                                         {"bad-note", {0x90, 128, 127}},
                                                                         {"bad-velocity", {0x90, 0, 128}},
                                                                         {"realtime", {0xF8}}}) {
        QTest::newRow(row.first) << QByteArray(reinterpret_cast<const char *>(row.second.data()), row.second.size());
    }
}

void TestVailMidiProtocol::ignoresMalformedAndUnrelatedMidi() {
    QFETCH(QByteArray, bytes);
    const Message message(bytes.begin(), bytes.end());
    Notes notes;
    QCOMPARE(notes.accept(message), Edge::Ignored);
    QVERIFY(!notes.down());
}

void TestVailMidiProtocol::overlappingNotesDoNotUnkey() {
    Notes notes;
    QCOMPARE(notes.accept({0x90, 0, 127}), Edge::Down);
    QCOMPARE(notes.accept({0x90, 1, 127}), Edge::Unchanged);
    QCOMPARE(notes.accept({0x90, 2, 127}), Edge::Unchanged);
    QCOMPARE(notes.accept({0x80, 0, 0}), Edge::Unchanged);
    QCOMPARE(notes.accept({0x80, 1, 0}), Edge::Unchanged);
    QCOMPARE(notes.accept({0x80, 2, 0}), Edge::Up);
}

void TestVailMidiProtocol::deduplicatesRepeatedEdges() {
    Stream stream;
    stream.setEnabled(true, 0);
    QCOMPARE(stream.accept({0x90, 1, 127}, ms(100)).command, QString("KZD0000;"));
    QVERIFY(stream.accept({0x90, 1, 127}, ms(130)).command.isEmpty());
    QCOMPARE(stream.accept({0x80, 1, 0}, ms(160)).command, QString("KZU0060;"));
    QVERIFY(stream.accept({0x80, 1, 0}, ms(180)).command.isEmpty());
}

void TestVailMidiProtocol::forwardsElapsedBeforeEachEdge() {
    Stream stream;
    stream.setEnabled(true, 0);
    QCOMPARE(stream.accept({0x90, 1, 127}, ms(100)).command, QString("KZD0000;"));
    QCOMPARE(stream.accept({0x80, 1, 0}, ms(160)).command, QString("KZU0060;"));
    QCOMPARE(stream.accept({0x90, 2, 127}, ms(220)).command, QString("KZD0060;"));
    QCOMPARE(stream.accept({0x80, 2, 0}, ms(400)).command, QString("KZU0180;"));
}

void TestVailMidiProtocol::usesCaptureTimestampsForQueuedBursts() {
    Stream stream;
    stream.setEnabled(true, 0);
    // Processing these together (after a busy event loop) still uses captured 60 ms timing.
    const std::array<std::pair<Message, std::int64_t>, 4> queued{
        {{{0x90, 1, 127}, ms(100)}, {{0x80, 1, 0}, ms(160)}, {{0x90, 1, 127}, ms(220)}, {{0x80, 1, 0}, ms(280)}}};
    QStringList commands;
    for (const auto &event : queued) {
        commands.append(stream.accept(event.first, event.second).command);
    }
    QCOMPARE(commands, QStringList({"KZD0000;", "KZU0060;", "KZD0060;", "KZU0060;"}));
}

void TestVailMidiProtocol::longIdleStartsNewTimingSession() {
    Stream stream;
    stream.setEnabled(true, 0);
    stream.accept({0x90, 1, 127}, ms(100));
    stream.accept({0x80, 1, 0}, ms(160));
    QCOMPARE(stream.accept({0x90, 1, 127}, ms(4000)).command, QString("KZD0000;"));
}

void TestVailMidiProtocol::disabledInputDoesNotKey() {
    Stream stream;
    QVERIFY(stream.accept({0x90, 1, 127}, ms(100)).command.isEmpty());
    QVERIFY(stream.accept({0x80, 1, 0}, ms(160)).command.isEmpty());
    QVERIFY(!stream.active());
}

void TestVailMidiProtocol::repeatedEnableDoesNotInterruptHeldKey() {
    Stream stream;
    stream.setEnabled(true, 0);
    stream.accept({0x90, 1, 127}, ms(100));
    QVERIFY(stream.setEnabled(true, ms(120)).command.isEmpty());
    QCOMPARE(stream.accept({0x80, 1, 0}, ms(160)).command, QString("KZU0060;"));
}

void TestVailMidiProtocol::enableWhileHeldRequiresRelease() {
    Stream stream;
    stream.accept({0x90, 1, 127}, ms(100));
    stream.setEnabled(true, ms(120));
    QVERIFY(stream.accept({0x90, 2, 127}, ms(130)).command.isEmpty());
    QVERIFY(stream.accept({0x80, 1, 0}, ms(140)).command.isEmpty());
    QVERIFY(stream.accept({0x80, 2, 0}, ms(150)).command.isEmpty());
    QCOMPARE(stream.accept({0x90, 1, 127}, ms(200)).command, QString("KZD0000;"));
}

void TestVailMidiProtocol::disableAbortsThenRequiresRelease() {
    Stream stream;
    stream.setEnabled(true, 0);
    stream.accept({0x90, 1, 127}, ms(100));
    const auto abort = stream.setEnabled(false, ms(130));
    QCOMPARE(abort.command, QString("KZU0030;"));
    QVERIFY(abort.aborted);
    QVERIFY(abort.stateChanged);
    QVERIFY(!abort.down);
    stream.setEnabled(true, ms(140));
    QVERIFY(stream.accept({0x90, 1, 127}, ms(150)).command.isEmpty());
    QVERIFY(stream.accept({0x80, 1, 0}, ms(160)).command.isEmpty());
    QCOMPARE(stream.accept({0x90, 1, 127}, ms(220)).command, QString("KZD0000;"));
}

void TestVailMidiProtocol::holdTimeoutAbortsThenRequiresRelease() {
    Stream stream;
    stream.setEnabled(true, 0);
    stream.accept({0x90, 1, 127}, ms(100));
    QVERIFY(stream.checkTimeout(ms(2499)).command.isEmpty());
    const auto abort = stream.checkTimeout(ms(2500));
    QCOMPARE(abort.command, QString("KZU2400;"));
    QVERIFY(abort.aborted);
    QVERIFY(!stream.active());
    QVERIFY(stream.checkTimeout(ms(3000)).command.isEmpty());
    QVERIFY(stream.accept({0x90, 1, 127}, ms(3100)).command.isEmpty());
    QVERIFY(stream.accept({0x80, 1, 0}, ms(3200)).command.isEmpty());
    QCOMPARE(stream.accept({0x90, 1, 127}, ms(3300)).command, QString("KZD0000;"));
}

void TestVailMidiProtocol::lateReleaseAbortsWithBoundedDuration() {
    Stream stream;
    stream.setEnabled(true, 0);
    stream.accept({0x90, 1, 127}, ms(100));
    const auto release = stream.accept({0x80, 1, 0}, ms(4000));
    QCOMPARE(release.command, QString("KZU2500;"));
    QVERIFY(release.aborted);
}

void TestVailMidiProtocol::nonMonotonicReleaseAborts() {
    Stream stream;
    stream.setEnabled(true, 0);
    stream.accept({0x90, 1, 127}, ms(100));
    const auto release = stream.accept({0x80, 1, 0}, ms(90));
    QCOMPARE(release.command, QString("KZU0000;"));
    QVERIFY(release.aborted);
}

void TestVailMidiProtocol::timestampOverflowCannotEscapeDurationLimit() {
    Stream stream;
    stream.setEnabled(true, 0);
    stream.accept({0x90, 1, 127}, std::numeric_limits<std::int64_t>::min());
    const auto release = stream.accept({0x80, 1, 0}, std::numeric_limits<std::int64_t>::max());
    QCOMPARE(release.command, QString("KZU2500;"));
    QVERIFY(release.aborted);
}

QTEST_APPLESS_MAIN(TestVailMidiProtocol)
#include "test_vailmidiprotocol.moc"
