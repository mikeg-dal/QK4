#include <QtTest>

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
// These tests drive a private QSettings scope so they never touch the developer's real radios.
class TestRadioSettings : public QObject {
    Q_OBJECT

private:
    // RadioSettings is a singleton over QSettings, so the scope is redirected rather than the
    // object replaced.
    void useTemporarySettings() {
        QCoreApplication::setOrganizationName(QStringLiteral("QK4Test"));
        QCoreApplication::setApplicationName(
            QStringLiteral("RadioSettingsTest-%1").arg(QDateTime::currentMSecsSinceEpoch()));
    }

    RadioEntry makeRadio(const QString &name, const QString &host) const {
        RadioEntry e;
        e.name = name;
        e.host = host;
        e.port = 9205;
        return e;
    }

private slots:
    void initTestCase() { useTemporarySettings(); }

    void init() {
        // Each test starts from an empty list.
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

    // NOT TESTED HERE: that the flag reaches disk. QSettings buffers writes per instance, so a
    // second QSettings object reading the same scope sees nothing until a sync, and a test built
    // that way would be measuring Qt's caching rather than this code. connectAtStartup is written
    // alongside eight fields that already persist correctly, by the same save() call; the risk
    // worth covering is the uniqueness RULE above, not one more setValue.

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
