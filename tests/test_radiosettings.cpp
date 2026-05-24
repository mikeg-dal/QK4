#include "settings/radiosettings.h"

#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest/QtTest>

class TestRadioSettings : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(m_tempDir.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_tempDir.path());
    }

    void testMonoMixDefaultsOff() {
        QVERIFY(!RadioSettings::instance()->monoMixEnabled());
    }

    void testMonoMixPersistsAndEmitsOnChange() {
        auto *settings = RadioSettings::instance();
        QSignalSpy spy(settings, &RadioSettings::monoMixEnabledChanged);

        settings->setMonoMixEnabled(true);

        QVERIFY(settings->monoMixEnabled());
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toBool(), true);

        settings->setMonoMixEnabled(true);
        QCOMPARE(spy.count(), 0);

        QSettings raw("QK4", "QK4");
        QCOMPARE(raw.value("audio/monoMixEnabled", false).toBool(), true);
    }

private:
    QTemporaryDir m_tempDir;
};

QTEST_MAIN(TestRadioSettings)
#include "test_radiosettings.moc"
