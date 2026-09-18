#include <QtTest>

#include "network/tciprotocol.h"

using namespace TciProtocol;

// TCI grammar. Pure text in, pure text out.
//
// Most of these pin a specific documented failure from a real client rather than an abstract rule;
// the comments say which. See docs/tci-server-design.md for the wire evidence.
class TestTciProtocol : public QObject {
    Q_OBJECT

private slots:
    // ---- parsing -----------------------------------------------------------------------------

    void parsesABareCommand() {
        const Command c = parseOne(QStringLiteral("ready"));
        QVERIFY(c.valid);
        QCOMPARE(c.name, QStringLiteral("ready"));
        QCOMPARE(c.argCount(), 0);
    }

    void parsesNameAndArguments() {
        const Command c = parseOne(QStringLiteral("vfo:0,1,14074000"));
        QVERIFY(c.valid);
        QCOMPARE(c.name, QStringLiteral("vfo"));
        QCOMPARE(c.argCount(), 3);
        QCOMPARE(c.arg(0), QStringLiteral("0"));
        QCOMPARE(c.arg(2), QStringLiteral("14074000"));
    }

    void lowercasesTheNameButNotTheArguments() {
        // ExpertSDR's documentation writes commands uppercase; the wire is case-insensitive.
        // Arguments can be mode strings that a caller may want to compare case-insensitively
        // itself, so they are left as sent.
        const Command c = parseOne(QStringLiteral("MODULATION:0,DIGU"));
        QCOMPARE(c.name, QStringLiteral("modulation"));
        QCOMPARE(c.arg(1), QStringLiteral("DIGU"));
    }

    void trimsWhitespaceAroundArguments() {
        const Command c = parseOne(QStringLiteral("vfo: 0 , 1 , 14074000 "));
        QCOMPARE(c.arg(0), QStringLiteral("0"));
        QCOMPARE(c.arg(1), QStringLiteral("1"));
        QCOMPARE(c.arg(2), QStringLiteral("14074000"));
    }

    void keepsEmptyArgumentPositions() {
        // Dropping an empty field would shift every later argument left and turn a malformed
        // command into a plausible-looking valid one.
        const Command c = parseOne(QStringLiteral("vfo:0,,14074000"));
        QCOMPARE(c.argCount(), 3);
        QVERIFY(c.arg(1).isEmpty());
    }

    void rejectsEmptyAndNamelessInput() {
        QVERIFY(!parseOne(QStringLiteral("")).valid);
        QVERIFY(!parseOne(QStringLiteral("   ")).valid);
        QVERIFY(!parseOne(QStringLiteral(":1,2")).valid);
    }

    // ---- argument conversion -------------------------------------------------------------------

    void convertsNumericArguments() {
        const Command c = parseOne(QStringLiteral("vfo:0,0,14074000"));
        int trx = -1;
        qint64 hz = 0;
        QVERIFY(c.argAsInt(0, &trx));
        QCOMPARE(trx, 0);
        QVERIFY(c.argAsLongLong(2, &hz));
        QCOMPARE(hz, 14074000LL);
    }

    void refusesANonBooleanRatherThanCoercingIt() {
        // The reference server reads anything that is not "true" as false, so "trx:0,yes"
        // silently unkeys and "split_enable:0,yes" silently tears down a split.
        bool value = true;
        QVERIFY(!parseOne(QStringLiteral("trx:0,yes")).argAsBool(1, &value));
        QVERIFY(!parseOne(QStringLiteral("trx:0,1")).argAsBool(1, &value));
        QVERIFY(!parseOne(QStringLiteral("trx:0,")).argAsBool(1, &value));

        QVERIFY(parseOne(QStringLiteral("trx:0,TRUE")).argAsBool(1, &value));
        QCOMPARE(value, true);
        QVERIFY(parseOne(QStringLiteral("trx:0,false")).argAsBool(1, &value));
        QCOMPARE(value, false);
    }

    void refusesOutOfRangeArgumentIndexes() {
        const Command c = parseOne(QStringLiteral("ready"));
        int v = 0;
        QVERIFY(!c.argAsInt(0, &v));
        QVERIFY(c.arg(5).isEmpty());
    }

    // ---- streaming ------------------------------------------------------------------------------

    void splitsSeveralCommandsInOneFrame() {
        Parser p;
        const auto cmds = p.feed(QStringLiteral("trx_count:1;channels_count:2;ready;"));
        QCOMPARE(cmds.size(), 3);
        QCOMPARE(cmds.at(0).name, QStringLiteral("trx_count"));
        QCOMPARE(cmds.at(2).name, QStringLiteral("ready"));
    }

    void buffersACommandSplitAcrossFrames() {
        // A command can straddle a WebSocket frame boundary; half of one must not be parsed.
        Parser p;
        QCOMPARE(p.feed(QStringLiteral("vfo:0,0,140")).size(), 0);
        const auto cmds = p.feed(QStringLiteral("74000;"));
        QCOMPARE(cmds.size(), 1);
        QCOMPARE(cmds.at(0).arg(2), QStringLiteral("14074000"));
    }

    void keepsATrailingPartialForTheNextFeed() {
        Parser p;
        auto cmds = p.feed(QStringLiteral("ready;start"));
        QCOMPARE(cmds.size(), 1);
        QCOMPARE(cmds.at(0).name, QStringLiteral("ready"));

        cmds = p.feed(QStringLiteral(";"));
        QCOMPARE(cmds.size(), 1);
        QCOMPARE(cmds.at(0).name, QStringLiteral("start"));
    }

    void discardsAnUnterminatedFloodRatherThanGrowing() {
        // CONVENTIONS.md rule 5: an externally-fed buffer needs a ceiling.
        Parser p;
        const QString flood(Parser::MAX_BUFFERED_CHARS + 1, QLatin1Char('x'));
        QCOMPARE(p.feed(flood).size(), 0);
        // The buffer was dropped, so a well-formed command afterwards still parses.
        const auto cmds = p.feed(QStringLiteral("ready;"));
        QCOMPARE(cmds.size(), 1);
    }

    void handlesTheFortyTwoCommandInitBurstAsOneFrame() {
        // The design-phase probe sent the whole burst in a single frame and WSJT-X accepted it,
        // so the receive path has to cope with the same shape.
        QString burst;
        for (int i = 0; i < 42; ++i) {
            burst += QStringLiteral("modulation:0,digu;");
        }
        Parser p;
        QCOMPARE(p.feed(burst).size(), 42);
    }

    // ---- the global one-argument form ------------------------------------------------------------

    void expandsTheGlobalSplitEnableForm() {
        // WSJT-X really sends this. Unexpanded it reads as a GET for receiver -1 and is answered
        // with silence, which is the failure mode TR4W documented.
        const Command c = expandGlobalForm(parseOne(QStringLiteral("split_enable:false")));
        QCOMPARE(c.argCount(), 2);
        QCOMPARE(c.arg(0), QStringLiteral("0"));
        QCOMPARE(c.arg(1), QStringLiteral("false"));
    }

    void leavesAnIndexedFormAlone() {
        const Command c = expandGlobalForm(parseOne(QStringLiteral("split_enable:0,true")));
        QCOMPARE(c.argCount(), 2);
        QCOMPARE(c.arg(0), QStringLiteral("0"));
        QCOMPARE(c.arg(1), QStringLiteral("true"));
    }

    void doesNotRewriteAGetIntoASet() {
        // "split_enable:0" is a GET for receiver 0. Treating a lone numeric argument as the global
        // form would turn every GET into a SET that tears down split.
        const Command c = expandGlobalForm(parseOne(QStringLiteral("split_enable:0")));
        QCOMPARE(c.argCount(), 1);
        QCOMPARE(c.arg(0), QStringLiteral("0"));
    }

    void doesNotExpandUnrelatedCommands() {
        const Command c = expandGlobalForm(parseOne(QStringLiteral("mute:true")));
        QCOMPARE(c.argCount(), 1);
    }

    // ---- formatting -------------------------------------------------------------------------------

    void formatsNameOnlyAndWithArguments() {
        QCOMPARE(message(QStringLiteral("ready")), QStringLiteral("ready;"));
        QCOMPARE(message(QStringLiteral("trx_count"), QStringLiteral("1")), QStringLiteral("trx_count:1;"));
        QCOMPARE(message(QStringLiteral("vfo"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("14074000")),
                 QStringLiteral("vfo:0,0,14074000;"));
    }

    void lowercasesTheOutputName() {
        QCOMPARE(message(QStringLiteral("TRX_Count"), QStringLiteral("1")), QStringLiteral("trx_count:1;"));
    }

    void stripsDelimitersFromArguments() {
        // The grammar has no escape, so a stray ';' or ',' in a value corrupts framing for every
        // client on the socket - not just the command carrying it.
        QCOMPARE(message(QStringLiteral("device"), QStringLiteral("QK4;evil,radio")),
                 QStringLiteral("device:QK4evilradio;"));
    }

    void leavesCommaBearingIdentityValuesIntact() {
        // The bug TR4W's own tests caught: scrubbing produced "protocol:expertsdr3_1.5;", which is
        // exactly the string WSJT-X fails to match before it halves transmit amplitude.
        QCOMPARE(messageFreeText(QStringLiteral("protocol"), QStringLiteral("ExpertSDR3,1.5")),
                 QStringLiteral("protocol:ExpertSDR3,1.5;"));
        QCOMPARE(messageFreeText(QStringLiteral("modulations_list"), QStringLiteral("usb,lsb,cw,digu")),
                 QStringLiteral("modulations_list:usb,lsb,cw,digu;"));
    }

    void formatsBooleansAsTheLiteralWords() {
        QCOMPARE(boolText(true), QStringLiteral("true"));
        QCOMPARE(boolText(false), QStringLiteral("false"));
    }

    // ---- round trip ---------------------------------------------------------------------------------

    void everythingThisServerEmitsParsesBack() {
        // A formatter that emits something its own parser rejects is a latent interop bug.
        const QStringList emitted{
            message(QStringLiteral("vfo"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("14074000")),
            message(QStringLiteral("split_enable"), QStringLiteral("0"), boolText(false)),
            message(QStringLiteral("trx"), QStringLiteral("0"), boolText(true)),
            message(QStringLiteral("drive"), QStringLiteral("0"), QStringLiteral("100")),
            message(QStringLiteral("ready")),
            messageFreeText(QStringLiteral("protocol"), QStringLiteral("ExpertSDR3,1.5")),
        };

        Parser p;
        const auto cmds = p.feed(emitted.join(QString()));
        QCOMPARE(cmds.size(), emitted.size());
        QCOMPARE(cmds.at(0).name, QStringLiteral("vfo"));
        QCOMPARE(cmds.at(0).arg(2), QStringLiteral("14074000"));
        QCOMPARE(cmds.at(4).name, QStringLiteral("ready"));
        QCOMPARE(cmds.at(5).argCount(), 2); // protocol's comma is a real field separator
    }

    void parsesEveryCommandWsjtxActuallySends() {
        // The complete client vocabulary observed in the captured session, including the ',tci'
        // audio-source tag that appears on the unkey as well as the key.
        Parser p;
        const auto cmds = p.feed(QStringLiteral("split_enable:false;"
                                                "audio_start:0;"
                                                "rx_sensors_enable:false,500;"
                                                "tx_sensors_enable:false,500;"
                                                "modulation:0,digu;"
                                                "vfo:0,0,14074000;"
                                                "trx:0,true,tci;"
                                                "trx:0,false,tci;"));
        QCOMPARE(cmds.size(), 8);

        QCOMPARE(expandGlobalForm(cmds.at(0)).argCount(), 2);

        bool keyed = false;
        QVERIFY(cmds.at(6).argAsBool(1, &keyed));
        QCOMPARE(keyed, true);
        QCOMPARE(cmds.at(6).arg(2), QStringLiteral("tci"));

        QVERIFY(cmds.at(7).argAsBool(1, &keyed));
        QCOMPARE(keyed, false);
        QCOMPARE(cmds.at(7).arg(2), QStringLiteral("tci"));
    }
};

QTEST_MAIN(TestTciProtocol)
#include "test_tciprotocol.moc"
