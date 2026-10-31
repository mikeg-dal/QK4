#include <QtTest>

#include <QDateTime>
#include <QFileInfo>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "settings/radiosettings.h"

// RadioSettings: the "connect at startup" rule.
//
// The rule is that AT MOST ONE radio may be flagged. It is enforced in the model rather than only
// in the dialog, because a settings file can also be hand-edited or restored from a backup, and a
// second flagged radio would make startup silently depend on list order.
//
// ============================================================================================
// HOW THIS SUITE IS KEPT AWAY FROM REAL DATA - read before changing initTestCase.
//
// RadioSettings is a singleton holding `QSettings m_settings{"QK4", "QK4"}` - the store is named in
// its constructor, so a test CANNOT redirect it by asking RadioSettings for anything. An earlier
// version of this file tried to redirect by calling QCoreApplication::setOrganizationName, which
// does nothing at all against a QSettings constructed with an explicit organisation. The suite
// therefore ran against the developer's own QK4 preferences, and init() below - which empties the
// list before each test - DELETED EVERY SAVED RADIO on the machine that ran it.
//
// The redirect that does work is process-wide and must happen BEFORE the singleton is first
// touched: set the default QSettings format to Ini and point Ini/UserScope at a temporary
// directory. That reaches RadioSettings only because its constructor names the format explicitly -
// Qt's two-argument QSettings(org, app) ignores setDefaultFormat() entirely and always opens the
// native store, which is why the original attempt failed silently.
//
// Because a silent failure of that redirect is destructive rather than merely wrong, it is checked
// twice: a probe proves the redirect took effect before anything writes, and cleanupTestCase
// asserts the real preferences file is byte-for-byte as it was when the suite started.
// ============================================================================================
class TestRadioSettings : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_tempDir;
    QString m_realPath;
    qint64 m_realSize = -1;
    QDateTime m_realModified;
    // What the singleton loaded from the hand-edited file written in initTestCase - captured there,
    // because the singleton loads once and init() clears the radio list before every test.
    QVariantMap m_loaded;

    RadioEntry makeRadio(const QString &name, const QString &host) const {
        RadioEntry e;
        e.name = name;
        e.host = host;
        e.port = 9205;
        return e;
    }

private slots:
    void initTestCase() {
        // Where the REAL preferences live, recorded before anything is redirected so the end of the
        // suite can prove it was never written to.
        {
            const QSettings real(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("QK4"),
                                 QStringLiteral("QK4"));
            m_realPath = real.fileName();
        }
        const QFileInfo before(m_realPath);
        m_realSize = before.exists() ? before.size() : -1;
        m_realModified = before.exists() ? before.lastModified() : QDateTime();

        QVERIFY2(m_tempDir.isValid(), "no temporary directory; refusing to run against real settings");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_tempDir.path());

        // Prove the redirect took, BEFORE a single write. If this ever fails the suite stops here
        // rather than quietly deleting somebody's radios.
        // Spelled the same way RadioSettings spells it. QSettings("QK4", "QK4") would NOT do - the
        // two-argument constructor ignores defaultFormat() - and using it here would make this
        // probe pass while the code under test still wrote to the real preferences.
        const QSettings probe(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("QK4"),
                              QStringLiteral("QK4"));
        QVERIFY2(probe.fileName().startsWith(m_tempDir.path()),
                 qPrintable(QStringLiteral("settings not redirected; would have written to ") + probe.fileName()));

        // A file nobody validated: out-of-range, zero and unparseable values, written BEFORE the
        // singleton exists so its load() is what reads them.
        {
            QSettings raw(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("QK4"),
                          QStringLiteral("QK4"));
            raw.setValue(QStringLiteral("catServer/port"), 70000); // would wrap to 4464 as quint16
            raw.setValue(QStringLiteral("tciServer/port"), 0);
            raw.setValue(QStringLiteral("kpa1500/port"), 70000);
            raw.setValue(QStringLiteral("kpa1500/pollInterval"), 0); // would busy-poll
            raw.setValue(QStringLiteral("dxCluster/spotAge"), 5);
            raw.setValue(QStringLiteral("halikey/sidetoneVolume"), 500);
            raw.setValue(QStringLiteral("halikey/deviceType"), 7);
            raw.setValue(QStringLiteral("kpodPlus/encodeMode"), 9);
            raw.beginWriteArray(QStringLiteral("radios"));
            raw.setArrayIndex(0);
            raw.setValue(QStringLiteral("name"), QStringLiteral("Wrapped"));
            raw.setValue(QStringLiteral("port"), 70000);
            raw.setArrayIndex(1);
            raw.setValue(QStringLiteral("name"), QStringLiteral("Zero"));
            raw.setValue(QStringLiteral("port"), 0);
            raw.setValue(QStringLiteral("useTls"), true);
            raw.endArray();
            raw.beginWriteArray(QStringLiteral("dxClusters"));
            raw.setArrayIndex(0);
            raw.setValue(QStringLiteral("host"), QStringLiteral("dx.example"));
            raw.setValue(QStringLiteral("port"), QStringLiteral("abc"));
            raw.endArray();
            raw.sync();
        }
        RadioSettings *s = RadioSettings::instance();
        m_loaded[QStringLiteral("catPort")] = s->catServerPort();
        m_loaded[QStringLiteral("tciPort")] = s->tciServerPort();
        m_loaded[QStringLiteral("kpaPort")] = s->kpa1500Port();
        m_loaded[QStringLiteral("kpaPoll")] = s->kpa1500PollInterval();
        m_loaded[QStringLiteral("spotAge")] = s->dxClusterSpotAge();
        m_loaded[QStringLiteral("sidetone")] = s->sidetoneVolume();
        m_loaded[QStringLiteral("deviceType")] = s->halikeyDeviceType();
        m_loaded[QStringLiteral("encodeMode")] = s->kpodPlusEncodeMode();
        for (const RadioEntry &r : s->radios()) {
            m_loaded[QStringLiteral("radio:") + r.name] = r.port;
        }
        m_loaded[QStringLiteral("dxPort")] = s->dxClusters().isEmpty() ? 0 : s->dxClusters().first().port;
    }

    void loadHoldsAHandEditedFileToTheSetterRules() {
        QCOMPARE(m_loaded.value(QStringLiteral("catPort")).toInt(), 65535);
        QCOMPARE(m_loaded.value(QStringLiteral("tciPort")).toInt(), 1024);
        QCOMPARE(m_loaded.value(QStringLiteral("kpaPort")).toInt(), 1500);
        QCOMPARE(m_loaded.value(QStringLiteral("kpaPoll")).toInt(), 100);
        QCOMPARE(m_loaded.value(QStringLiteral("spotAge")).toInt(), 300);
        QCOMPARE(m_loaded.value(QStringLiteral("sidetone")).toInt(), 100);
        QCOMPARE(m_loaded.value(QStringLiteral("deviceType")).toInt(), 2);
        QCOMPARE(m_loaded.value(QStringLiteral("encodeMode")).toInt(), 1);
        // A K4 port that cannot be valid falls back to the default for its TLS mode, never 0.
        QCOMPARE(m_loaded.value(QStringLiteral("radio:Wrapped")).toInt(), int(K4Protocol::DEFAULT_PORT));
        QCOMPARE(m_loaded.value(QStringLiteral("radio:Zero")).toInt(), int(K4Protocol::TLS_PORT));
        QCOMPARE(m_loaded.value(QStringLiteral("dxPort")).toInt(), int(kDefaultDxClusterPort));
    }

    void settersApplyTheSameRules() {
        RadioSettings *s = RadioSettings::instance();
        const quint16 kpaPort = s->kpa1500Port();
        s->setKpa1500Port(0);
        QCOMPARE(s->kpa1500Port(), kpaPort); // 0 is "any port"; refused
        s->setHalikeyDeviceType(5);
        QCOMPARE(s->halikeyDeviceType(), 2);
        s->setHalikeyDeviceType(0);
        RadioEntry r = makeRadio(QStringLiteral("NoPort"), QStringLiteral("10.0.0.9"));
        r.port = 0;
        s->addRadio(r);
        QCOMPARE(s->radios().first().port, K4Protocol::DEFAULT_PORT);
    }

    void aRadioEntryHasAPortByDefault() {
        const RadioEntry r;
        QCOMPARE(r.port, K4Protocol::DEFAULT_PORT);
    }

    void cleanupTestCase() {
        // The guarantee this suite owes the machine it runs on.
        const QFileInfo after(m_realPath);
        if (m_realSize < 0) {
            QVERIFY2(!after.exists(), qPrintable(QStringLiteral("suite created real settings at ") + m_realPath));
            return;
        }
        QVERIFY2(after.exists(), qPrintable(QStringLiteral("suite deleted real settings at ") + m_realPath));
        QCOMPARE(after.size(), m_realSize);
        QCOMPARE(after.lastModified(), m_realModified);
    }

    void init() {
        // Each test starts from an empty list. This is the line that did the damage when the
        // redirect above was not working; it is safe only because initTestCase proves it.
        RadioSettings *s = RadioSettings::instance();
        while (!s->radios().isEmpty()) {
            s->removeRadio(0);
        }
    }

    // The Radio Manager reselects lastSelectedIndex after every save, and the list re-sorts on save.
    // The index must stay on the radio just saved, or the form refills from a different entry: an
    // SL3 save looked as if it bounced back to SL0 (2026-10-29, two entries for one K4).
    void savingSelectsTheSavedRadioAcrossTheSort() {
        RadioSettings *s = RadioSettings::instance();
        s->addRadio(makeRadio(QStringLiteral("B"), QStringLiteral("10.0.0.2")));
        s->addRadio(makeRadio(QStringLiteral("C"), QStringLiteral("10.0.0.3")));
        s->addRadio(makeRadio(QStringLiteral("A"), QStringLiteral("10.0.0.1"))); // sorts to the front
        QCOMPARE(s->radios().at(s->lastSelectedIndex()).name, QStringLiteral("A"));

        // Rename C to "0" so the update moves it to the front; the selection goes with it.
        RadioEntry c = s->radios().at(2);
        c.name = QStringLiteral("0");
        c.streamingLatency = 3;
        s->updateRadio(2, c);
        QCOMPARE(s->radios().at(s->lastSelectedIndex()).name, QStringLiteral("0"));
        QCOMPARE(s->radios().at(s->lastSelectedIndex()).streamingLatency, 3);
    }

    void sameRadioSavedAgainIsUpdatedNotDuplicated() {
        RadioSettings *s = RadioSettings::instance();
        RadioEntry k4 = makeRadio(QStringLiteral("K4"), QStringLiteral("10.0.0.1"));
        k4.streamingLatency = 0;
        s->addRadio(k4);
        k4.streamingLatency = 3;
        s->addRadio(k4);
        QCOMPARE(s->radios().size(), 1);
        QCOMPARE(s->radios().first().streamingLatency, 3);
    }

    void removingARadioKeepsTheSelectionOnItsRadio() {
        RadioSettings *s = RadioSettings::instance();
        s->addRadio(makeRadio(QStringLiteral("A"), QStringLiteral("10.0.0.1")));
        s->addRadio(makeRadio(QStringLiteral("B"), QStringLiteral("10.0.0.2")));
        s->addRadio(makeRadio(QStringLiteral("C"), QStringLiteral("10.0.0.3")));
        s->setLastSelectedIndex(2); // C
        s->removeRadio(0);
        QCOMPARE(s->radios().at(s->lastSelectedIndex()).name, QStringLiteral("C"));
        s->removeRadio(s->lastSelectedIndex()); // removing the selected one selects its neighbour
        QCOMPARE(s->radios().at(s->lastSelectedIndex()).name, QStringLiteral("B"));
        s->removeRadio(0);
        QCOMPARE(s->lastSelectedIndex(), -1);
    }

    void noRadioIsFlaggedByDefault() {
        RadioSettings *s = RadioSettings::instance();
        s->addRadio(makeRadio(QStringLiteral("A"), QStringLiteral("10.0.0.1")));
        QCOMPARE(s->connectAtStartupIndex(), -1);
        QCOMPARE(s->radios().first().connectAtStartup, false);
    }

    void flaggingOneRadioClearsEveryOther() {
        // The point of the rule. Without it, startup would connect to whichever flagged radio
        // happened to sort first.
        RadioSettings *s = RadioSettings::instance();
        s->addRadio(makeRadio(QStringLiteral("A"), QStringLiteral("10.0.0.1")));
        s->addRadio(makeRadio(QStringLiteral("B"), QStringLiteral("10.0.0.2")));
        s->addRadio(makeRadio(QStringLiteral("C"), QStringLiteral("10.0.0.3")));

        s->setConnectAtStartupRadio(1);
        QCOMPARE(s->connectAtStartupIndex(), 1);

        s->setConnectAtStartupRadio(2);
        QCOMPARE(s->connectAtStartupIndex(), 2);

        // Exactly one, not merely "the last one set".
        int flagged = 0;
        for (const RadioEntry &e : s->radios()) {
            if (e.connectAtStartup) {
                ++flagged;
            }
        }
        QCOMPARE(flagged, 1);
    }

    void minusOneDisablesAutoConnectEntirely() {
        RadioSettings *s = RadioSettings::instance();
        s->addRadio(makeRadio(QStringLiteral("A"), QStringLiteral("10.0.0.1")));
        s->setConnectAtStartupRadio(0);
        QCOMPARE(s->connectAtStartupIndex(), 0);

        s->setConnectAtStartupRadio(-1);
        QCOMPARE(s->connectAtStartupIndex(), -1);
        QCOMPARE(s->radios().first().connectAtStartup, false);
    }

    void aChangeIsAnnounced() {
        // The dialog and anything else showing the list need to redraw.
        RadioSettings *s = RadioSettings::instance();
        s->addRadio(makeRadio(QStringLiteral("A"), QStringLiteral("10.0.0.1")));
        QSignalSpy spy(s, &RadioSettings::radiosChanged);

        s->setConnectAtStartupRadio(0);
        QCOMPARE(spy.count(), 1);

        // Setting the same value again changes nothing and must not emit.
        s->setConnectAtStartupRadio(0);
        QCOMPARE(spy.count(), 1);
    }

    void aRadioIsFoundByNameWhateverTheCase() {
        // The name comes off a command line or out of a desktop shortcut's properties, where
        // matching the stored capitalisation exactly is a needless way to fail.
        RadioSettings *s = RadioSettings::instance();
        s->addRadio(makeRadio(QStringLiteral("Shack K4"), QStringLiteral("10.0.0.1")));
        s->addRadio(makeRadio(QStringLiteral("Remote K4"), QStringLiteral("10.0.0.2")));

        const int shack = s->indexOfRadioNamed(QStringLiteral("Shack K4"));
        QVERIFY(shack >= 0);
        QCOMPARE(s->radios()[shack].name, QStringLiteral("Shack K4"));
        QCOMPARE(s->indexOfRadioNamed(QStringLiteral("shack k4")), shack);
        QCOMPARE(s->indexOfRadioNamed(QStringLiteral("SHACK K4")), shack);
    }

    void anUnknownNameFindsNothingRatherThanSomething() {
        // The caller opens NO radio on -1. A near miss returning the wrong index would have a
        // shortcut silently key up a different K4 than the one it names - the failure this lookup
        // exists to prevent.
        RadioSettings *s = RadioSettings::instance();
        s->addRadio(makeRadio(QStringLiteral("Shack K4"), QStringLiteral("10.0.0.1")));

        QCOMPARE(s->indexOfRadioNamed(QStringLiteral("Shack")), -1);     // prefix is not a match
        QCOMPARE(s->indexOfRadioNamed(QStringLiteral("Shack K4 ")), -1); // nor is trailing space
        QCOMPARE(s->indexOfRadioNamed(QStringLiteral("nope")), -1);
        QCOMPARE(s->indexOfRadioNamed(QString()), -1);
    }

    void anOutOfRangeIndexClearsRatherThanCrashes() {
        RadioSettings *s = RadioSettings::instance();
        s->addRadio(makeRadio(QStringLiteral("A"), QStringLiteral("10.0.0.1")));
        s->setConnectAtStartupRadio(0);

        // No entry matches, so every flag is cleared - auto-connect off, not a stale flag left
        // pointing at a radio the caller did not mean.
        s->setConnectAtStartupRadio(99);
        QCOMPARE(s->connectAtStartupIndex(), -1);
    }

    void loggingIsOffAndNormalByDefault() {
        QSettings raw(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("QK4"), QStringLiteral("QK4"));
        raw.remove(QStringLiteral("Logging"));
        raw.sync();
        RadioSettings *s = RadioSettings::instance();
        QVERIFY(!s->logFileEnabled());
        QCOMPARE(s->logLevel(), QStringLiteral("normal"));
        QVERIFY(s->logGroups().isEmpty());
        QVERIFY(s->logBench().isEmpty());
    }

    void loggingSettingsAreSanitisedAndSignalOnlyOnChange() {
        RadioSettings *s = RadioSettings::instance();
        QSignalSpy spy(s, &RadioSettings::loggingChanged);
        s->setLogLevel(QStringLiteral("loud"));
        QCOMPARE(spy.count(), 0); // unknown is Normal, which it already was
        s->setLogLevel(QStringLiteral("everything"));
        s->setLogGroups({QStringLiteral("tci"), QStringLiteral("bogus"), QStringLiteral("app")});
        s->setLogGroups({QStringLiteral("app"), QStringLiteral("tci")});
        s->setLogFileEnabled(true);
        s->setLogBench({QStringLiteral("cat"), QStringLiteral("app")});
        QCOMPARE(spy.count(), 4);
        QCOMPARE(s->logLevel(), QStringLiteral("everything"));
        QCOMPARE(s->logGroups(), (QStringList{QStringLiteral("app"), QStringLiteral("tci")}));
        QCOMPARE(s->logBench(), QStringList{QStringLiteral("cat")});
        QVERIFY(s->logFileEnabled());

        // Bench items are never written to the store, so the next launch starts without them.
        QSettings raw(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("QK4"), QStringLiteral("QK4"));
        QVERIFY(!raw.contains(QStringLiteral("Logging/bench")));
        QCOMPARE(raw.value(QStringLiteral("Logging/level")).toString(), QStringLiteral("everything"));

        s->setLogBench({});
        s->setLogFileEnabled(false);
        s->setLogLevel(QStringLiteral("normal"));
        s->setLogGroups({});
    }

    void interfaceScaleDefaultsToAuto() {
        QSettings raw(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("QK4"), QStringLiteral("QK4"));
        raw.remove(QStringLiteral("ui"));
        raw.sync();
        QCOMPARE(RadioSettings::instance()->interfaceScale(), RadioSettings::kInterfaceScaleAuto);
        bool isAuto = false;
        QCOMPARE(RadioSettings::startupInterfaceScale(&isAuto), 1.0);
        QVERIFY(isAuto);
    }

    void interfaceScaleRoundTripsAndSignalsOnlyOnChange() {
        RadioSettings *s = RadioSettings::instance();
        s->setInterfaceScale(RadioSettings::kInterfaceScaleAuto);
        QSignalSpy spy(s, &RadioSettings::interfaceScaleChanged);
        s->setInterfaceScale(80);
        s->setInterfaceScale(80);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().toInt(), 80);
        QCOMPARE(s->interfaceScale(), 80);
        bool isAuto = true;
        QCOMPARE(RadioSettings::startupInterfaceScale(&isAuto), 0.8);
        QVERIFY(!isAuto);
        s->setInterfaceScale(RadioSettings::kInterfaceScaleAuto);
    }

    void interfaceScaleAcceptsTheLargeSteps() {
        RadioSettings *s = RadioSettings::instance();
        for (int percent : {125, 150}) {
            s->setInterfaceScale(percent);
            QCOMPARE(s->interfaceScale(), percent);
            bool isAuto = true;
            QCOMPARE(RadioSettings::startupInterfaceScale(&isAuto), percent / 100.0);
            QVERIFY(!isAuto);
        }
        s->setInterfaceScale(RadioSettings::kInterfaceScaleAuto);
    }

    void unknownInterfaceScaleFallsBackToAuto() {
        // Hand-edited or from a step since removed. Auto is the one value that cannot leave the
        // window larger than the screen.
        RadioSettings *s = RadioSettings::instance();
        s->setInterfaceScale(83);
        QCOMPARE(s->interfaceScale(), RadioSettings::kInterfaceScaleAuto);
        s->setInterfaceScale(175);
        QCOMPARE(s->interfaceScale(), RadioSettings::kInterfaceScaleAuto);
        QSettings raw(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("QK4"), QStringLiteral("QK4"));
        raw.setValue(QStringLiteral("ui/interfaceScale"), 300);
        raw.sync();
        QCOMPARE(s->interfaceScale(), RadioSettings::kInterfaceScaleAuto);
        s->setInterfaceScale(RadioSettings::kInterfaceScaleAuto);
    }

    void autoUsesTheFittedFactorWithinBounds() {
        RadioSettings *s = RadioSettings::instance();
        s->setInterfaceScale(RadioSettings::kInterfaceScaleAuto);
        s->setAutoScaleFactor(0.75);
        QCOMPARE(RadioSettings::startupInterfaceScale(), 0.75);
        s->setAutoScaleFactor(0.1);
        QCOMPARE(s->autoScaleFactor(), 0.5);
        s->setAutoScaleFactor(1.25);
        QCOMPARE(RadioSettings::startupInterfaceScale(), 1.25);
        s->setAutoScaleFactor(3.0);
        QCOMPARE(s->autoScaleFactor(), 1.25);
        QSettings raw(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("QK4"), QStringLiteral("QK4"));
        raw.setValue(QStringLiteral("ui/autoScaleFactor"), QStringLiteral("nonsense"));
        raw.sync();
        // An unparseable value reads as 0, which clamps to the floor rather than to full size -
        // the safe direction for a value that exists only because a screen was too small.
        QCOMPARE(RadioSettings::startupInterfaceScale(), 0.5);
        s->setAutoScaleFactor(1.0);
    }
};

QTEST_MAIN(TestRadioSettings)
#include "test_radiosettings.moc"
