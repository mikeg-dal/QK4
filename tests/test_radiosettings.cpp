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
};

QTEST_MAIN(TestRadioSettings)
#include "test_radiosettings.moc"
