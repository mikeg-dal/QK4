// SPDX-License-Identifier: GPL-3.0-or-later
//
// PaddleKeyer against K4-Companion's recorded keying sweeps (Dale Farnsworth, GPL-3.0).
//
// 1. Golden replay. For each of the 11 recorded sweeps (554 runs) the port must send the K4 the
//    same commands, byte for byte, as Dale's keyer did. That gate says the port is his keyer and
//    not an approximation of it.
// 2. Rules beyond the recordings. Squeeze, dah-first and stall verdicts at speeds he ran but did
//    not record (20-50 WPM), and the squeeze rule with Dit Mem at the 33 % QK4 ships with.
//
// Fixture: tests/data/paddlekeyer/ (see its README).

#include "paddlekeyersim.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QtTest>

using namespace PaddleSim;

namespace {

QString dataPath(const QString &file) {
    return QStringLiteral(QK4_PADDLEKEYER_DATA_DIR "/") + file;
}

std::vector<std::string> toStrings(const QJsonArray &a) {
    std::vector<std::string> out;
    for (const auto &v : a)
        out.push_back(v.toString().toStdString());
    return out;
}

QString joined(const std::vector<std::string> &v) {
    QStringList l;
    for (const auto &s : v)
        l << QString::fromStdString(s);
    return l.join(QLatin1Char(' '));
}

Result runOnce(const Config &c, int tapMs) {
    Run run(c, tapMs / 1000.0);
    return run.go();
}

} // namespace

class TestPaddleKeyerSweeps : public QObject {
    Q_OBJECT

private slots:
    void goldenReplay_data() {
        QTest::addColumn<QString>("file");
        QTest::addColumn<int>("scenario");
        QTest::addColumn<int>("wpm");
        const struct {
            const char *file;
            Scenario scenario;
            int wpm;
        } sets[] = {
            {"summary_30wpm_squeeze.json", Scenario::Squeeze, 30},
            {"summary_40wpm_squeeze.json", Scenario::Squeeze, 40},
            {"summary_30wpm_swap.json", Scenario::Swap, 30},
            {"summary_40wpm_swap.json", Scenario::Swap, 40},
            {"summary_30wpm_dit-tap.json", Scenario::DitTap, 30},
            {"summary_40wpm_dit-tap.json", Scenario::DitTap, 40},
            {"summary_40wpm_tap.json", Scenario::Tap, 40},
            {"summary_30wpm_dah-first.json", Scenario::DahFirst, 30},
            {"summary_40wpm_dah-first.json", Scenario::DahFirst, 40},
            {"summary_30wpm_stall.json", Scenario::Stall, 30},
            {"summary_40wpm_stall.json", Scenario::Stall, 40},
        };
        for (const auto &s : sets)
            QTest::newRow(s.file) << QString::fromLatin1(s.file) << static_cast<int>(s.scenario) << s.wpm;
    }

    // Every run of every recorded sweep sends exactly what Dale's keyer sent. A squeeze released on
    // the very instant a tone starts may instead satisfy the Iambic B rule (README: "allowed
    // either answer").
    void goldenReplay() {
        QFETCH(QString, file);
        QFETCH(int, scenario);
        QFETCH(int, wpm);

        QFile f(dataPath(file));
        QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(f.fileName()));
        const QJsonArray runs = QJsonDocument::fromJson(f.readAll()).array();
        QVERIFY(!runs.isEmpty());

        Config c;
        c.scenario = static_cast<Scenario>(scenario);
        c.wpm = wpm;
        const Geometry geo(c);
        QCOMPARE(static_cast<qsizetype>(geo.taps().size()), runs.size());

        int exact = 0;
        for (const auto &v : runs) {
            const QJsonObject run = v.toObject();
            const int tapMs = run.value("tap_ms").toInt();
            const auto want = toStrings(run.value("sent").toArray());
            const Result got = runOnce(c, tapMs);
            if (got.sent == want) {
                ++exact;
                QCOMPARE(static_cast<int>(got.edges.size()), run.value("edges").toInt());
                continue;
            }
            const double releaseAt = tapMs / 1000.0;
            const bool tie = c.scenario == Scenario::Squeeze && squeezeReleaseIsTie(got, releaseAt);
            const bool ruleMet = tie && squeezeVerdict(got, releaseAt, wpm).met;
            QVERIFY2(ruleMet, qPrintable(QStringLiteral("%1 @ %2 ms\n  want: %3\n  got:  %4")
                                             .arg(file)
                                             .arg(tapMs)
                                             .arg(joined(want), joined(got.sent))));
        }
        qInfo("%s: %d/%lld runs byte-identical", qPrintable(file), exact, static_cast<long long>(runs.size()));
    }

    // README: "run besides at 20, 25, 35, 45 and 50 the keyer follows the rule in every run of
    // every one." Not recorded, so checked against the rule.
    void squeezeRuleAtEverySpeed_data() {
        QTest::addColumn<int>("wpm");
        QTest::addColumn<double>("ditMem");
        for (int wpm : {20, 25, 30, 35, 40, 45, 50})
            QTest::addRow("%d wpm, dit mem 0", wpm) << wpm << 0.0;
        // QK4 ships Dit Mem at 33 %, K4-Companion's app default.
        for (int wpm : {20, 30, 40, 50})
            QTest::addRow("%d wpm, dit mem 33", wpm) << wpm << 33.0;
    }

    void squeezeRuleAtEverySpeed() {
        QFETCH(int, wpm);
        QFETCH(double, ditMem);
        Config c;
        c.scenario = Scenario::Squeeze;
        c.wpm = wpm;
        c.ditMemPercent = ditMem;
        for (int tapMs : Geometry(c).taps()) {
            const Result r = runOnce(c, tapMs);
            const auto v = squeezeVerdict(r, tapMs / 1000.0, wpm);
            QVERIFY2(v.met, qPrintable(QStringLiteral("released at %1 ms during the %2: %3 owed, got %4")
                                           .arg(tapMs)
                                           .arg(QString::fromStdString(v.during), QString::fromStdString(v.owed),
                                                v.after.empty() ? QStringLiteral("nothing") : joined(v.after))));
        }
    }

    // With Dit Mem at 33 %, at least one release early in a dah owes and gets nothing. That proves
    // the setting reaches the keyer at all, and isn't silently 0.
    void ditMemoryDelaySuppressesEarlyRelease() {
        Config c;
        c.scenario = Scenario::Squeeze;
        c.wpm = 30;
        c.ditMemPercent = 33.0;
        int nothingOwed = 0;
        for (int tapMs : Geometry(c).taps()) {
            const auto v = squeezeVerdict(runOnce(c, tapMs), tapMs / 1000.0, c.wpm);
            QVERIFY(v.met);
            if (v.owed == "nothing")
                ++nothingOwed;
        }
        QVERIFY(nothingOwed > 0);
    }

    void dahFirstAtEverySpeed_data() {
        QTest::addColumn<int>("wpm");
        for (int wpm : {20, 25, 30, 35, 40, 45, 50})
            QTest::addRow("%d wpm", wpm) << wpm;
    }

    void dahFirstAtEverySpeed() {
        QFETCH(int, wpm);
        Config c;
        c.scenario = Scenario::DahFirst;
        c.wpm = wpm;
        for (int tapMs : Geometry(c).taps())
            QVERIFY2(dahFirstVerdict(runOnce(c, tapMs), wpm),
                     qPrintable(QStringLiteral("dit closed at %1 ms").arg(tapMs)));
    }

    // A stall never adds an element, loses one or runs the S into the T.
    void stallAtEverySpeed_data() {
        QTest::addColumn<int>("wpm");
        for (int wpm : {20, 25, 30, 35, 40, 45, 50})
            QTest::addRow("%d wpm", wpm) << wpm;
    }

    void stallAtEverySpeed() {
        QFETCH(int, wpm);
        Config c;
        c.scenario = Scenario::Stall;
        c.wpm = wpm;
        const Geometry geo(c);
        // Whole-ms dits replay the operator's gap exactly; otherwise each of the S's elements rounds
        // up by under 1 ms and the gap shrinks by that much.
        const long long tolerance = 1200 % wpm == 0 ? 0 : 4;
        for (int tapMs : geo.taps()) {
            std::string gap;
            QVERIFY2(stallVerdict(runOnce(c, tapMs), wpm, geo.stallGapMs, &gap, tolerance),
                     qPrintable(
                         QStringLiteral("stall to %1 ms: T's KZD was %2").arg(tapMs).arg(QString::fromStdString(gap))));
        }
    }

    // Every KZU carries an element's full nominal length, whatever the sidetone did. Sweeping
    // every scenario at 40 WPM: only KZU0030 and KZU0090 ever appear.
    void kzuCarriesNominalLength() {
        for (Scenario s : {Scenario::Squeeze, Scenario::Swap, Scenario::Tap, Scenario::DitTap, Scenario::DahFirst,
                           Scenario::Stall}) {
            Config c;
            c.scenario = s;
            c.wpm = 40;
            for (int tapMs : Geometry(c).taps())
                for (const auto &cmd : runOnce(c, tapMs).sent)
                    if (cmd.rfind("KZU", 0) == 0)
                        QVERIFY2(cmd == "KZU0030;" || cmd == "KZU0090;",
                                 qPrintable(QStringLiteral("%1 @ %2: %3")
                                                .arg(scenarioName(s))
                                                .arg(tapMs)
                                                .arg(QString::fromStdString(cmd))));
        }
    }

    // Monitor mute off (QK4's default until the bench says otherwise): the same keying, and no ML.
    void monitorMuteOffSendsNoMl() {
        Config on;
        on.scenario = Scenario::Squeeze;
        Config off = on;
        off.muteRadioMonitor = false;
        for (int tapMs : Geometry(on).taps()) {
            auto withMute = runOnce(on, tapMs).sent;
            const auto without = runOnce(off, tapMs).sent;
            withMute.erase(std::remove(withMute.begin(), withMute.end(), std::string("ML0000;")), withMute.end());
            QCOMPARE(joined(without), joined(withMute));
        }
    }
};

QTEST_GUILESS_MAIN(TestPaddleKeyerSweeps)
#include "test_paddlekeyersweeps.moc"
