#include "hardware/halikeydevice.h"
#include "settings/radiosettings.h"
#include "ui/pages/cwkeyerpage.h"

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QFileInfo>
#include <QLabel>
#include <QPixmap>
#include <QSettings>
#include <QSlider>
#include <QTemporaryDir>
#include <QTest>

class TestVailKeyerPage : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_tempDir;
    QString m_realPath;
    qint64 m_realSize = -1;
    QDateTime m_realModified;

    static QComboBox *deviceTypeCombo(CwKeyerPage &page) {
        for (auto *combo : page.findChildren<QComboBox *>()) {
            if (combo->findText(QStringLiteral("Vail Adapter (local keyer)")) >= 0)
                return combo;
        }
        return nullptr;
    }

private slots:
    void initTestCase() {
        // Redirect before the singleton is touched. Changing the application's name does not
        // redirect RadioSettings: its QSettings constructor names the QK4 store explicitly.
        const QSettings real(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("QK4"),
                             QStringLiteral("QK4"));
        m_realPath = real.fileName();
        const QFileInfo before(m_realPath);
        m_realSize = before.exists() ? before.size() : -1;
        m_realModified = before.exists() ? before.lastModified() : QDateTime();

        QVERIFY2(m_tempDir.isValid(), "no temporary directory; refusing to use real settings");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_tempDir.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, m_tempDir.path() + "/system");
        const QSettings probe(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("QK4"),
                              QStringLiteral("QK4"));
        QVERIFY2(probe.fileName().startsWith(m_tempDir.path() + '/'),
                 qPrintable(QStringLiteral("settings not redirected: ") + probe.fileName()));
    }

    void cleanupTestCase() {
        const QFileInfo after(m_realPath);
        if (m_realSize < 0) {
            QVERIFY2(!after.exists(), qPrintable(QStringLiteral("test created real settings: ") + m_realPath));
        } else {
            QVERIFY2(after.exists(), qPrintable(QStringLiteral("test deleted real settings: ") + m_realPath));
            QCOMPARE(after.size(), m_realSize);
            QCOMPARE(after.lastModified(), m_realModified);
        }
    }

    void init() {
        RadioSettings::instance()->setHalikeyDeviceType(HalikeyDevice::Serial);
        RadioSettings::instance()->setHalikeyPortName(QString());
        RadioSettings::instance()->setSidetoneVolume(37);
        RadioSettings::instance()->setVailComputerSidetone(false);
    }

    void selectionPersistsAndRestores() {
        {
            CwKeyerPage page(nullptr);
            auto *combo = deviceTypeCombo(page);
            QVERIFY(combo);
            const int vailIndex = combo->findData(HalikeyDevice::Vail);
            QVERIFY(vailIndex >= 0);
            combo->setCurrentIndex(vailIndex);
            QCOMPARE(combo->currentData().toInt(), 2);
            QCOMPARE(RadioSettings::instance()->halikeyDeviceType(), 2);
        }

        QSettings persisted(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("QK4"),
                            QStringLiteral("QK4"));
        QCOMPARE(persisted.value(QStringLiteral("halikey/deviceType")).toInt(), 2);

        CwKeyerPage reopened(nullptr);
        auto *combo = deviceTypeCombo(reopened);
        QVERIFY(combo);
        QCOMPARE(combo->currentData().toInt(), 2);
        auto *slider = reopened.findChild<QSlider *>();
        QVERIFY(slider);
        QVERIFY(slider->isHidden() || slider->parentWidget()->isHidden());
        QCOMPARE(slider->value(), 37);
    }

    void sidetoneSourceControlsVisibilityAndPersists() {
        RadioSettings::instance()->setHalikeyDeviceType(HalikeyDevice::Vail);
        {
            CwKeyerPage page(nullptr);
            page.resize(620, 650);
            page.show();
            auto *combo = deviceTypeCombo(page);
            auto *source = page.findChild<QComboBox *>(QStringLiteral("vailSidetoneSource"));
            auto *help = page.findChild<QLabel *>(QStringLiteral("sidetoneHelp"));
            auto *slider = page.findChild<QSlider *>();
            QVERIFY(combo);
            QVERIFY(source);
            QVERIFY(help);
            QVERIFY(slider);
            QVERIFY(source->isVisible());
            QVERIFY(!slider->isVisible());
            QVERIFY(help->text().contains(QStringLiteral("Computer sidetone is off")));
            QVERIFY(help->text().contains(QStringLiteral("fixed level")));
            QVERIFY(help->text().contains(QStringLiteral("cannot switch")));

            source->setCurrentIndex(source->findData(true));
            QVERIFY(RadioSettings::instance()->vailComputerSidetone());
            QVERIFY(slider->isVisible());
            QVERIFY(slider->isEnabled());
            QCOMPARE(slider->value(), 37);
            QVERIFY(help->text().contains(QStringLiteral("Disconnect above")));
            QVERIFY(help->text().contains(QStringLiteral("over 5 seconds")));
            slider->setValue(0);
            QCOMPARE(RadioSettings::instance()->sidetoneVolume(), 0);

            combo->setCurrentIndex(combo->findData(HalikeyDevice::Midi));
            QVERIFY(!source->isVisible());
            QVERIFY(slider->isVisible());
            QVERIFY(help->text().contains(QStringLiteral("0% mutes")));
            combo->setCurrentIndex(combo->findData(HalikeyDevice::Serial));
            QVERIFY(slider->isVisible());
            combo->setCurrentIndex(combo->findData(HalikeyDevice::Vail));
            QCOMPARE(source->currentData().toBool(), true);
            QVERIFY(slider->isVisible());
        }
        QSettings persisted(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("QK4"),
                            QStringLiteral("QK4"));
        QCOMPARE(persisted.value(QStringLiteral("vail/computerSidetone")).toBool(), true);
        CwKeyerPage reopened(nullptr);
        auto *source = reopened.findChild<QComboBox *>(QStringLiteral("vailSidetoneSource"));
        QVERIFY(source);
        QCOMPARE(source->currentData().toBool(), true);
        source->setCurrentIndex(source->findData(false));
        QVERIFY(!RadioSettings::instance()->vailComputerSidetone());
        QCOMPARE(RadioSettings::instance()->sidetoneVolume(), 0);
    }

    void localKeyerInstructionsAreVisible() {
        RadioSettings::instance()->setHalikeyDeviceType(HalikeyDevice::Vail);
        CwKeyerPage page(nullptr);
        page.resize(620, 650);
        page.show();
        QCoreApplication::processEvents();

        QLabel *description = nullptr;
        for (auto *label : page.findChildren<QLabel *>()) {
            if (label->text().contains(QStringLiteral("Reverse paddles physically"))) {
                description = label;
                break;
            }
        }
        QVERIFY2(description, "Vail instructions must explain physical paddle reversal");
        QVERIFY(description->text().contains(QStringLiteral("generates CW locally")));
        QVERIFY(description->text().contains(QStringLiteral("radio mode off")));
        QVERIFY(description->text().contains(QStringLiteral("2.4 seconds")));
        QVERIFY(description->isVisible());
        QVERIFY(description->wordWrap());
        QVERIFY2(description->height() >= description->heightForWidth(description->width()),
                 "Vail instructions are clipped at the options-page width");
        QVERIFY(page.rect().contains(description->geometry()));
        auto *combo = deviceTypeCombo(page);
        QVERIFY(combo);
        QVERIFY(description->geometry().bottom() < combo->geometry().top());
        auto *help = page.findChild<QLabel *>(QStringLiteral("sidetoneHelp"));
        auto *source = page.findChild<QComboBox *>(QStringLiteral("vailSidetoneSource"));
        QVERIFY(help);
        QVERIFY(source);
        const QString screenshot = qEnvironmentVariable("QK4_TEST_SCREENSHOT");
        for (int mode = 0; mode < source->count(); ++mode) {
            source->setCurrentIndex(mode);
            QCoreApplication::processEvents();
            QVERIFY(help->isVisible());
            QVERIFY2(help->height() >= help->heightForWidth(help->width()), "Sidetone instructions are clipped");
            QVERIFY2(page.rect().contains(help->geometry()), "Sidetone instructions extend outside the page");
            if (!screenshot.isEmpty() && mode == 1)
                QVERIFY(page.grab().save(screenshot + QStringLiteral(".computer.png")));
        }
        source->setCurrentIndex(0);
        QCoreApplication::processEvents();

        // Opt-in visual QA without opening MainWindow or connecting to a radio/device.
        if (!screenshot.isEmpty())
            QVERIFY2(page.grab().save(screenshot), qPrintable(QStringLiteral("could not save ") + screenshot));
    }
};

QTEST_MAIN(TestVailKeyerPage)
#include "test_vailkeyerpage.moc"
