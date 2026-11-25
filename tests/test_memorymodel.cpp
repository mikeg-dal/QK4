// MemoryModel: the K4's 200 frequency memories as MCDF reports them (R40).
//
// Every MCDF and MC line here is verbatim from a K4D at R41.FT1 on 2026-11-25
// (bench-logs/probe-memories-20261125-133345.log). Rev D12 lists MC as "[Pending] / TBD", so the
// capture is the specification and these cases are what pins it.

#include "models/memorymodel.h"

#include <QSignalSpy>
#include <QTest>

class MemoryModelTests : public QObject {
    Q_OBJECT

private slots:
    // --- The field layout ---------------------------------------------------------------------

    void channelWithOnlyVfoAParsesEveryField() {
        MemoryModel model;
        QVERIFY(model.parseMCDF("MCDF001,,2.8,1.5,14.227,USB,14.221,50,100,,,,,,,;"));

        const MemoryChannel *ch = model.channel(1);
        QVERIFY(ch);
        QCOMPARE(ch->number, 1);
        QVERIFY(ch->label.isEmpty());
        QCOMPARE(ch->a.bandwidthKhz, QString("2.8"));
        QCOMPARE(ch->a.shiftKhz, QString("1.5"));
        QCOMPARE(ch->a.frequencyMhz, QString("14.227"));
        QCOMPARE(ch->a.mode, QString("USB"));
        QCOMPARE(ch->a.panCentreMhz, QString("14.221"));
        QCOMPARE(ch->a.spanKhz, QString("50"));
        QCOMPARE(ch->a.unidentified, QString("100"));
        QCOMPARE(ch->a.frequencyHz(), 14227000LL);

        // Channel 1 holds nothing for VFO B — the STO screen shows only its A row.
        QVERIFY(!ch->b.isSet());
        QVERIFY(ch->b.frequencyMhz.isEmpty());
        QCOMPARE(ch->b.frequencyHz(), 0LL);
        QVERIFY(!ch->isBlank());
    }

    void channelWithBothVfosSplitsTheTwoGroups() {
        MemoryModel model;
        QVERIFY(model.parseMCDF("MCDF021,*60m Ch1,,0,5.3305,USB,,,,,0,5.332,CW,,,;"));

        const MemoryChannel *ch = model.channel(21);
        QVERIFY(ch);
        QCOMPARE(ch->label, QString("*60m Ch1"));
        QCOMPARE(ch->a.frequencyMhz, QString("5.3305"));
        QCOMPARE(ch->a.mode, QString("USB"));
        QCOMPARE(ch->a.shiftKhz, QString("0"));
        // The B group starts at field 9: this is the one that catches an off-by-one.
        QCOMPARE(ch->b.frequencyMhz, QString("5.332"));
        QCOMPARE(ch->b.mode, QString("CW"));
        QCOMPARE(ch->b.shiftKhz, QString("0"));
        QVERIFY(ch->b.isSet());
        QCOMPARE(ch->a.frequencyHz(), 5330500LL);
        QCOMPARE(ch->b.frequencyHz(), 5332000LL);
    }

    void aBlankChannelIsBlankButReported() {
        // Qt drops a trailing empty field unless asked for every part, and a blank channel's last
        // field is empty: without KeepEmptyParts this line parses one field short and is refused.
        MemoryModel model;
        QVERIFY(model.parseMCDF("MCDF002,,,,,,,,,,,,,,,;"));

        const MemoryChannel *ch = model.channel(2);
        QVERIFY(ch);
        QVERIFY(ch->isBlank());
        QVERIFY(!ch->a.isSet());
        QVERIFY(model.channelsInUse().isEmpty());
    }

    void aLabelWithNoFrequencyIsNotBlank() {
        // "A label with no frequency stays stored" (R40 notes), so it must not read as blank.
        MemoryModel model;
        QVERIFY(model.parseMCDF("MCDF007,SKED,,,,,,,,,,,,,,;"));
        const MemoryChannel *ch = model.channel(7);
        QVERIFY(ch);
        QVERIFY(!ch->isBlank());
        QVERIFY(!ch->a.isSet());
        QCOMPARE(model.channelsInUse(), QList<int>({7}));
    }

    void fieldsRoundTripWhatTheRadioSent() {
        // The eighth field of each group is unidentified. A write has to send back exactly what
        // was read rather than a value we invented.
        MemoryModel model;
        QVERIFY(model.parseMCDF("MCDF001,,2.8,1.5,14.227,USB,14.221,50,100,,,,,,,;"));
        QCOMPARE(model.channel(1)->fields().join(','), QString(",2.8,1.5,14.227,USB,14.221,50,100,,,,,,,"));
    }

    // --- Refusals and malformed input ---------------------------------------------------------

    void refusalAndMalformedLinesAreRejected() {
        MemoryModel model;
        QSignalSpy spy(&model, &MemoryModel::channelChanged);
        // The radio's refusal form, measured: the command echoed with a question mark, not an ER.
        QVERIFY(!model.parseMCDF("MCDF?;"));
        QVERIFY(!model.parseMCDF("MCDF;"));
        QVERIFY(!model.parseMCDF("MCDF001;"));                                       // no fields at all
        QVERIFY(!model.parseMCDF("MCDF001,,2.8,1.5;"));                              // too few fields
        QVERIFY(!model.parseMCDF("MCDF000,,,,,,,,,,,,,,,;"));                        // channel 0 is out of range
        QVERIFY(!model.parseMCDF("MCDF201,,,,,,,,,,,,,,,;"));                        // past the 200th
        QVERIFY(!model.parseMCDF("MEDF0001,Speaker,AUDIO,BIN,0,0,1,1,1,1,OFF,ON;")); // a menu line
        QCOMPARE(spy.count(), 0);
    }

    // --- Selection (MC / MCNF / MCNU) ---------------------------------------------------------

    void selectionFollowsMcAndItsNavigationForms() {
        MemoryModel model;
        QSignalSpy spy(&model, &MemoryModel::selectedChannelChanged);
        QCOMPARE(model.selectedChannel(), -1); // unknown until the radio says

        QVERIFY(model.parseSelection("MC002;"));
        QCOMPARE(model.selectedChannel(), 2);
        QCOMPARE(spy.count(), 1);

        // MCNF and MCNU answer with the channel they landed on, so they move the selection too.
        QVERIFY(model.parseSelection("MCNF003;"));
        QCOMPARE(model.selectedChannel(), 3);
        QVERIFY(model.parseSelection("MCNU021;"));
        QCOMPARE(model.selectedChannel(), 21);
        QCOMPARE(spy.count(), 3);

        // The same channel again is not a change.
        QVERIFY(model.parseSelection("MCNU021;"));
        QCOMPARE(spy.count(), 3);

        QVERIFY(!model.parseSelection("MC;"));
        QVERIFY(!model.parseSelection("MC999;"));
        QVERIFY(!model.parseSelection("MD2;"));
        QCOMPARE(model.selectedChannel(), 21);
    }

    // --- The MEM and GEN band pages -----------------------------------------------------------

    void memAndGenPagesSplitOnTheHamBands() {
        // The split matches the radio's own pages: on the bench the MEM page showed #001 (20 m)
        // alongside the five legacy 60 m channels and the GEN page was empty, so 60 m counts as a
        // ham band here exactly as BandPlan has it (5.25-5.45 MHz). Only the 9.58 MHz broadcast
        // entry below belongs to GEN.
        MemoryModel model;
        model.parseMCDF("MCDF001,,2.8,1.5,14.227,USB,14.221,50,100,,,,,,,;");
        model.parseMCDF("MCDF021,*60m Ch1,,0,5.3305,USB,,,,,0,5.332,CW,,,;");
        model.parseMCDF("MCDF022,*60m Ch2,,0,5.3465,USB,,,,,0,5.348,CW,,,;");
        model.parseMCDF("MCDF030,,2.8,1.5,9.58,AM,9.58,50,100,,,,,,,;"); // shortwave broadcast
        model.parseMCDF("MCDF031,,2.8,1.5,7.2,LSB,7.2,50,100,,,,,,,;");  // 40 m
        model.parseMCDF("MCDF040,,,,,,,,,,,,,,,;");                      // blank: in neither page

        QCOMPARE(model.firstInHamBands(12), QList<int>({1, 21, 22, 31}));
        QCOMPARE(model.firstOutsideHamBands(12), QList<int>({30}));

        // The pages hold twelve cells, and the search starts at #001 and stops once they are full.
        QCOMPARE(model.firstInHamBands(2), QList<int>({1, 21}));
        QVERIFY(model.firstInHamBands(0).isEmpty());
    }

    void unnamedChannelsShowTheirNumber() {
        MemoryModel model;
        model.parseMCDF("MCDF001,,2.8,1.5,14.227,USB,14.221,50,100,,,,,,,;");
        model.parseMCDF("MCDF021,*60m Ch1,,0,5.3305,USB,,,,,0,5.332,CW,,,;");
        QCOMPARE(model.channel(1)->displayLabel(), QString("#001"));
        QCOMPARE(model.channel(21)->displayLabel(), QString("*60m Ch1"));
    }

    // --- Lifetime -----------------------------------------------------------------------------

    void clearEmptiesTheModelOnce() {
        // The radio is the only source: a channel list left over from the last connection would
        // describe a different radio. Same rule as MenuModel, and the same crash if it is skipped.
        MemoryModel model;
        model.parseMCDF("MCDF001,,2.8,1.5,14.227,USB,14.221,50,100,,,,,,,;");
        model.parseSelection("MC002;");

        QSignalSpy cleared(&model, &MemoryModel::modelCleared);
        model.clear();
        QCOMPARE(cleared.count(), 1);
        QVERIFY(!model.channel(1));
        QVERIFY(!model.hasChannel(1));
        QVERIFY(model.channelsInUse().isEmpty());
        QCOMPARE(model.selectedChannel(), -1);

        model.clear(); // already empty: nothing to announce
        QCOMPARE(cleared.count(), 1);
    }
};

QTEST_MAIN(MemoryModelTests)
#include "test_memorymodel.moc"
