#include <QtTest>

#include "network/catframes.h"
#include "network/cwmacro.h"

// CW by CAT: the TCI macro grammar, and the K4 commands it turns into.
//
// Two subjects, one file, because they are two halves of one journey and the interesting cases
// live at the seam - the characters TCI and the K4 both use, for different things.
class TestCwMacro : public QObject {
    Q_OBJECT

private:
    static QString joined(const QList<QByteArray> &frames) {
        QStringList out;
        for (const QByteArray &f : frames) {
            out << QString::fromLatin1(f);
        }
        return out.join(QString());
    }

private slots:
    // ------------------------------------------------------------------ TCI grammar

    void theReservedCharactersComeBack() {
        // TCI cannot carry : , or ; in a macro - they frame the protocol - so it substitutes.
        QCOMPARE(CwMacro::unescape(QStringLiteral("A^B~C*D")), QStringLiteral("A:B,C;D"));
    }

    void plainTextIsOneSegmentAtTheCurrentSpeed() {
        // What QLog actually sends, captured on the bench: no markers, no escapes.
        const auto segments = CwMacro::parse(QStringLiteral("CQ CQ CQ DE NY4I NY4I NY4I K"), 25);
        QCOMPARE(segments.size(), 1);
        QCOMPARE(segments[0].wpm, 25);
        QCOMPARE(segments[0].text, QStringLiteral("CQ CQ CQ DE NY4I NY4I NY4I K"));
    }

    void speedMarkersSplitTheTextAndAreConsumed() {
        // The spec's own shape: > raises by 5, cumulatively.
        const auto segments = CwMacro::parse(QStringLiteral(">TU >599 004"), 25);
        QCOMPARE(segments.size(), 2);
        QCOMPARE(segments[0].wpm, 30);
        QCOMPARE(segments[0].text, QStringLiteral("TU "));
        QCOMPARE(segments[1].wpm, 35);
        QCOMPARE(segments[1].text, QStringLiteral("599 004"));

        // CONSUMED, not forwarded. This is the one that matters: a '>' reaching a KY command does
        // not change speed, it takes the radio out of TX TEST mode - and a '<' puts it in.
        for (const CwMacroSegment &s : segments) {
            QVERIFY2(!s.text.contains(QLatin1Char('>')), qPrintable(s.text));
            QVERIFY2(!s.text.contains(QLatin1Char('<')), qPrintable(s.text));
        }
    }

    void theSpeedWalksBothWays() {
        const auto segments = CwMacro::parse(QStringLiteral(">UP<<DOWN"), 25);
        QCOMPARE(segments.size(), 2);
        QCOMPARE(segments[0].wpm, 30); // +5
        QCOMPARE(segments[1].wpm, 20); // -5 -5
    }

    void anAbsurdMacroCannotAskForAnUnkeyableSpeed() {
        // Ten markers is 50 WPM of change from a base of 25. The K4's KS range is 8..100, and a
        // segment claiming 75 when the radio will do 100 - or 0 when it will do 8 - would be a lie
        // about what is going to happen.
        const auto fast = CwMacro::parse(QStringLiteral(">>>>>>>>>>>>>>>>X"), 25);
        QCOMPARE(fast.size(), 1);
        QCOMPARE(fast[0].wpm, 100);

        const auto slow = CwMacro::parse(QStringLiteral("<<<<<<<<<<X"), 25);
        QCOMPARE(slow.size(), 1);
        QCOMPARE(slow[0].wpm, 8);
    }

    void aMacroWithNothingToKeyProducesNothing() {
        QVERIFY(CwMacro::parse(QString(), 25).isEmpty());
        QVERIFY(CwMacro::parse(QStringLiteral(">><<"), 25).isEmpty());
    }

    // ------------------------------------------------------------------ the send plan

    void aPlainMacroSetsNoSpeedAndWaitsForNothing() {
        const auto steps = CwMacro::plan(CwMacro::parse(QStringLiteral("CQ DE NY4I K"), 20), 20);
        QCOMPARE(steps.size(), 1);
        QCOMPARE(steps[0].setWpm, -1);
        QVERIFY2(!steps[0].wait, "nothing follows, so nothing to hold the radio off for");
    }

    void aRisingMacroEndsAwayFromBaseAndIsPutBack() {
        // ">TU >599" from 21: 26, then 31, then back to 21.
        const auto steps = CwMacro::plan(CwMacro::parse(QStringLiteral(">TU >599"), 21), 21);
        QCOMPARE(steps.size(), 3);
        QCOMPARE(steps[0].setWpm, 26);
        QVERIFY(steps[0].wait); // a KS follows
        QCOMPARE(steps[1].setWpm, 31);
        QVERIFY(steps[1].wait); // the restore follows
        QCOMPARE(steps[2].setWpm, 21);
        QVERIFY2(steps[2].text.isEmpty(), "the restore carries no text");
    }

    void aMacroThatComesBackToBaseNeitherRestoresNorStalls() {
        // REGRESSION. "TEST >FAST <AGAIN" ends at the speed it started at. The first version asked
        // "has the speed moved at all?" rather than "is a speed command coming next?", so it set
        // the wait flag on the last text AND emitted a restore to the speed already in force -
        // stalling every later command for the length of the message to protect a no-op. A 68
        // character message was measured at 35.9 seconds.
        const auto steps = CwMacro::plan(CwMacro::parse(QStringLiteral("TEST >FAST <AGAIN"), 20), 20);
        QCOMPARE(steps.size(), 3);

        QCOMPARE(steps[0].setWpm, -1); // already at 20
        QVERIFY(steps[0].wait);        // KS025 follows
        QCOMPARE(steps[1].setWpm, 25);
        QVERIFY(steps[1].wait); // KS020 follows
        QCOMPARE(steps[2].setWpm, 20);

        QVERIFY2(!steps[2].wait, "back at base: nothing follows, so no stall");
        QCOMPARE(steps[2].text, QStringLiteral("AGAIN"));
    }

    void theSpeedGoesDownAndBackUpToo() {
        const auto steps = CwMacro::plan(CwMacro::parse(QStringLiteral("A <SLOW >B"), 25), 25);
        QCOMPARE(steps.size(), 3);
        QCOMPARE(steps[0].setWpm, -1);
        QCOMPARE(steps[1].setWpm, 20); // down 5
        QCOMPARE(steps[2].setWpm, 25); // back up, which is base, so no restore step after it
        QVERIFY(!steps[2].wait);
    }

    void withNoKnownBaseSpeedNothingIsRestored() {
        // RadioState holds -1 until the radio reports. Restoring to a speed nobody knows would be
        // inventing one.
        const auto steps = CwMacro::plan(CwMacro::parse(QStringLiteral(">FAST"), -1), -1);
        QCOMPARE(steps.size(), 1);
        QVERIFY(!steps[0].wait);
    }

    // ------------------------------------------------------------------ K4 commands

    void shortTextIsOneKyCommandWithNoPadding() {
        // NOT padded. The padding other Elecraft drivers apply guards a short KY following a keyer
        // abort - a flow QK4 does not have - and bench runs at 60 and 68 characters were unpadded
        // and keyed correctly.
        const QList<QByteArray> frames = CatFrames::cwText(QStringLiteral("QRZ?"), false);
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames[0], QByteArray("KY QRZ?;"));
    }

    void aSixtyCharacterMessageIsOneCommand() {
        // Exactly the documented maximum, and exactly the text the bench keyed in a single KY.
        // Reading TR4W's minimum-length 22 as a MAXIMUM used to split this into three.
        const QString sixty = QStringLiteral("CQ TEST NY4I NY4I CQ TEST NY4I NY4I CQ TEST NY4I NY4I CQ TES");
        QCOMPARE(sixty.size(), 60);
        const QList<QByteArray> frames = CatFrames::cwText(sixty, false);
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames[0], QByteArray("KY ") + sixty.toUtf8() + ";");
    }

    void theWaitFlagIsOnlyUsedWhenAsked() {
        QVERIFY(CatFrames::cwText(QStringLiteral("TU"), false)[0].startsWith("KY "));
        // KYW holds the radio off following commands until the text has been sent - which is what
        // the manual prescribes when a KS follows, and a needless stall otherwise.
        QVERIFY(CatFrames::cwText(QStringLiteral("TU"), true)[0].startsWith("KYW"));
    }

    void longTextIsSplitOnWordBoundaries() {
        const QString message =
            QStringLiteral("DE NY4I GE OM TNX FER CALL UR RST 599 599 NAME TOM TOM QTH CLEARWATER CLEARWATER");
        const QList<QByteArray> frames = CatFrames::cwText(message, false);
        QVERIFY(frames.size() > 1);

        // Nothing is lost or invented: strip the framing, drop the pad, and the text is back.
        QString rebuilt;
        for (const QByteArray &f : frames) {
            rebuilt += QString::fromLatin1(f.mid(3, f.size() - 4));
        }
        while (rebuilt.endsWith(QLatin1Char(' '))) {
            rebuilt.chop(1);
        }
        QCOMPARE(rebuilt, message);

        // No chunk ENDS on a space. The radio trims trailing spaces, so a chunk that ended on a
        // real word gap would lose it and run two words together.
        for (int i = 0; i + 1 < frames.size(); ++i) {
            const QString text = QString::fromLatin1(frames[i].mid(3, frames[i].size() - 4));
            QVERIFY2(!text.endsWith(QLatin1Char(' ')), qPrintable(text));
        }
    }

    void aSingleUnbrokenRunIsSplitAnyway() {
        // No space to break on, so the hard split has to happen or the command is over-length.
        const QList<QByteArray> frames = CatFrames::cwText(QString(140, QLatin1Char('X')), false);
        QVERIFY(frames.size() >= 3);
        for (const QByteArray &f : frames) {
            QVERIFY2(f.size() <= 2 + 1 + 60 + 1, f.constData());
        }
    }

    void prosignsBecomeTheirK4Spelling() {
        // From the K4 manual's KY entry. TCI writes a prosign as |XX|.
        QVERIFY(joined(CatFrames::cwText(QStringLiteral("TU |SK|"), false)).contains(QLatin1String("TU *")));
        QVERIFY(joined(CatFrames::cwText(QStringLiteral("|AR|"), false)).contains(QLatin1Char('+')));
        QVERIFY(joined(CatFrames::cwText(QStringLiteral("|BT|"), false)).contains(QLatin1Char('=')));
        QVERIFY(joined(CatFrames::cwText(QStringLiteral("|KN|"), false)).contains(QLatin1Char('(')));
        QVERIFY(joined(CatFrames::cwText(QStringLiteral("|AS|"), false)).contains(QLatin1Char('%')));
        QVERIFY(joined(CatFrames::cwText(QStringLiteral("|VE|"), false)).contains(QLatin1Char('!')));
    }

    void anUnspellableProsignKeysItsLettersRatherThanVanishing() {
        // The K4 has no character for it. Keying K and N separately is wrong, but audible; deleting
        // part of the operator's message silently is worse.
        const QString out = joined(CatFrames::cwText(QStringLiteral("|XY| TEST"), false));
        QVERIFY2(out.contains(QLatin1String("XY TEST")), qPrintable(out));
        QVERIFY(!out.contains(QLatin1Char('|')));
    }

    void charactersTheRadioWouldACTonAreRemoved() {
        // THE DANGEROUS CASE. Each of these does something inside a KY command, and none of it is
        // keying: ';' ends the CAT command, '@' truncates the message, '<' takes the radio into TX
        // TEST mode until '>' returns it, '|' terminates TX in FSK/PSK. CwMacro::parse eats the
        // speed markers first, but this builder is public and must stand on its own.
        const QString out = joined(CatFrames::cwText(QStringLiteral("A;B@C<D>E|F"), false));
        QVERIFY2(out.contains(QLatin1String("ABCDEF")), qPrintable(out));
        for (const QChar &bad : {QLatin1Char('@'), QLatin1Char('<'), QLatin1Char('>'), QLatin1Char('|')}) {
            QVERIFY2(!out.mid(3).contains(bad), qPrintable(out));
        }
        // Exactly one ';' - the command's own terminator, not one from the text.
        QCOMPARE(out.count(QLatin1Char(';')), 1);
    }

    void nothingKeyableProducesNoCommandAtAll() {
        // Better than KY with an empty payload, which would key the pad.
        QVERIFY(CatFrames::cwText(QString(), false).isEmpty());
        QVERIFY(CatFrames::cwText(QStringLiteral("@@@"), false).isEmpty());
    }

    void onlyCwAndDataModesKeyKyText() {
        // Confirmed on a K4: in LSB a correctly formed KY produced NOTHING - no keying, no error,
        // no response. QK4 sends it anyway (the mode is the operator's to manage, and in the DATA
        // modes the radio sends the text as data) but warns, because the silence is otherwise
        // unexplainable from anywhere the operator can see.
        QVERIFY(CatFrames::modeKeysCwText(RadioState::CW));
        QVERIFY(CatFrames::modeKeysCwText(RadioState::CW_R));
        QVERIFY(CatFrames::modeKeysCwText(RadioState::DATA));
        QVERIFY(CatFrames::modeKeysCwText(RadioState::DATA_R));

        QVERIFY(!CatFrames::modeKeysCwText(RadioState::LSB));
        QVERIFY(!CatFrames::modeKeysCwText(RadioState::USB));
        QVERIFY(!CatFrames::modeKeysCwText(RadioState::AM));
        QVERIFY(!CatFrames::modeKeysCwText(RadioState::FM));

        // Not yet reported by the radio. Warning about a mode nobody knows would be noise.
        QVERIFY(!CatFrames::modeKeysCwText(RadioState::Unknown));
    }

    void theAbortIsTheBytesTheRadioExpects() {
        // KY<0x04>;RX; - bench-confirmed on Elecraft hardware.
        QByteArray expected = "KY ";
        expected.append(char(0x04));
        expected.append(";RX;");
        QCOMPARE(CatFrames::cwAbort(), expected);
    }

    void theKeyerSpeedIsClampedToWhatTheRadioAccepts() {
        // The macro grammar reaches this with arithmetic, so the bound cannot live at the call site.
        QCOMPARE(CatFrames::keyerSpeed(25), QByteArray("KS025;"));
        QCOMPARE(CatFrames::keyerSpeed(200), QByteArray("KS100;"));
        QCOMPARE(CatFrames::keyerSpeed(0), QByteArray("KS008;"));
        QCOMPARE(CatFrames::keyerSpeed(-30), QByteArray("KS008;"));
    }
};

QTEST_MAIN(TestCwMacro)
#include "test_cwmacro.moc"
