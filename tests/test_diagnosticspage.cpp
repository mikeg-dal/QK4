// Options -> Diagnostics, offscreen against a sandboxed settings file: the switches reach
// RadioSettings, and through Logging::apply() (wired exactly as main.cpp wires it) change what the
// real categories log, live. Opening the folder is not exercised: it would open a window.

#include "logging/logcategories.h"
#include "logging/logsession.h"
#include "logging/logsink.h"
#include "settings/radiosettings.h"
#include "ui/pages/diagnosticspage.h"
#include "ui/pages/optionspagelayout.h"
#include "ui/styling/k4styles.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

namespace D = K4Styles::Dimensions;

namespace {

QCheckBox *checkFor(DiagnosticsPage &page, const QString &id) {
    for (QCheckBox *check : page.findChildren<QCheckBox *>()) {
        if (check->property("logId").toString() == id)
            return check;
    }
    return nullptr;
}

} // namespace

class DiagnosticsPageTests : public QObject {
    Q_OBJECT

    QTemporaryDir m_settingsDir;
    QTemporaryDir m_logHome;
    QByteArray m_savedRules;
    bool m_rulesWereSet = false;

private slots:
    void initTestCase() {
        QVERIFY2(m_settingsDir.isValid(), "no temporary directory; refusing to run against real settings");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
        const QSettings probe(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("QK4"),
                              QStringLiteral("QK4"));
        QVERIFY2(probe.fileName().startsWith(m_settingsDir.path()),
                 qPrintable(QStringLiteral("settings not redirected; would have written to ") + probe.fileName()));

        // The log folder follows the environment; point it into a temporary directory.
        qputenv("XDG_STATE_HOME", m_logHome.path().toUtf8());
        qputenv("LOCALAPPDATA", m_logHome.path().toUtf8());
        m_rulesWereSet = qEnvironmentVariableIsSet("QT_LOGGING_RULES");
        m_savedRules = qgetenv("QT_LOGGING_RULES");
        qunsetenv("QT_LOGGING_RULES");

        Logging::install();
        QObject::connect(RadioSettings::instance(), &RadioSettings::loggingChanged, RadioSettings::instance(), [] {
            const RadioSettings *s = RadioSettings::instance();
            Logging::apply(s->logFileEnabled(), s->logConfig());
        });
        Logging::sink()->start();
    }

    void cleanupTestCase() {
        RadioSettings::instance()->setLogFileEnabled(false);
        Logging::shutdown();
        QLoggingCategory::setFilterRules(QString());
        if (m_rulesWereSet)
            qputenv("QT_LOGGING_RULES", m_savedRules);
    }

    void fitsTheNarrowestDialog() {
        auto *page = new DiagnosticsPage();
        std::unique_ptr<QScrollArea> scroll(OptionsPageLayout::wrapInScrollArea(page, nullptr));
        const int width = D::OptionsDialogMinWidth - D::TabListWidth - 8;
        scroll->resize(width, D::OptionsDialogMinHeight);
        scroll->show();
        QVERIFY(QTest::qWaitForWindowExposed(scroll.get()));
        QVERIFY2(
            page->minimumSizeHint().width() <= width,
            qPrintable(
                QString("page needs %1 px, the dialog gives it %2").arg(page->minimumSizeHint().width()).arg(width)));
    }

    void everySwitchIsOnThePage() {
        DiagnosticsPage page;
        for (const auto &group : Logging::groups())
            QVERIFY2(checkFor(page, QString::fromLatin1(group.id)), group.id);
        for (const auto &item : Logging::benchItems())
            QVERIFY2(checkFor(page, QString::fromLatin1(item.id)), item.id);
        QStringList buttons;
        for (const QPushButton *button : page.findChildren<QPushButton *>())
            buttons << button->text();
        QVERIFY(buttons.contains("Open Log Folder"));
        QVERIFY(buttons.contains("Copy Path"));
    }

    void aGroupSwitchChangesDebugLive() {
        DiagnosticsPage page;
        QVERIFY(!lcCwKeyer().isDebugEnabled());
        checkFor(page, "cw")->setChecked(true);
        QCOMPARE(RadioSettings::instance()->logGroups(), QStringList{"cw"});
        QVERIFY(lcCwKeyer().isDebugEnabled());
        QVERIFY(lcKpodPlus().isDebugEnabled());
        QVERIFY(!lcAudio().isDebugEnabled());
        checkFor(page, "cw")->setChecked(false);
        QVERIFY(!lcCwKeyer().isDebugEnabled());
    }

    void benchItemsAreLiveButNotSaved() {
        DiagnosticsPage page;
        checkFor(page, "cat")->setChecked(true);
        QVERIFY(lcCatTx().isDebugEnabled());
        QVERIFY(lcCatRx().isDebugEnabled());
        const QSettings raw(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("QK4"),
                            QStringLiteral("QK4"));
        QVERIFY(!raw.contains(QStringLiteral("Logging/bench")));
        checkFor(page, "cat")->setChecked(false);
        QVERIFY(!lcCatTx().isDebugEnabled());
    }

    void levelComboAppliesTheLevel() {
        DiagnosticsPage page;
        auto *combo = page.findChild<QComboBox *>();
        QVERIFY(combo);
        combo->setCurrentIndex(combo->findData("warnings"));
        QVERIFY(!lcLink().isInfoEnabled());
        combo->setCurrentIndex(combo->findData("everything"));
        QVERIFY(lcLink().isDebugEnabled());
        QVERIFY(!lcAudioTx().isDebugEnabled());
        combo->setCurrentIndex(combo->findData("normal"));
        QVERIFY(lcLink().isInfoEnabled());
        QVERIFY(!lcLink().isDebugEnabled());
    }

    void theFileSwitchOpensAndClosesTheLog() {
        DiagnosticsPage page;
        QCheckBox *fileCheck = nullptr;
        for (QCheckBox *check : page.findChildren<QCheckBox *>()) {
            if (check->text() == "Write a log file")
                fileCheck = check;
        }
        QVERIFY(fileCheck);
        QVERIFY(Logging::currentLogFile().isEmpty());
        fileCheck->setChecked(true);
        QVERIFY(!Logging::currentLogFile().isEmpty());
        QVERIFY(Logging::currentLogFile().startsWith(m_logHome.path()) ||
                Logging::currentLogFile().contains("Library/Logs/QK4"));
        fileCheck->setChecked(false);
        QVERIFY(Logging::currentLogFile().isEmpty());
    }

    void overrideNoteFollowsTheEnvironment() {
        {
            DiagnosticsPage page;
            page.show();
            bool shown = false;
            for (const QLabel *label : page.findChildren<QLabel *>())
                shown |= label->text().startsWith("Logging rules from") && label->isVisible();
            QVERIFY(!shown);
        }
        qputenv("QT_LOGGING_RULES", "qk4.app.debug=true");
        DiagnosticsPage page;
        page.show();
        bool shown = false;
        for (const QLabel *label : page.findChildren<QLabel *>())
            shown |= label->text().startsWith("Logging rules from") && label->isVisible();
        qunsetenv("QT_LOGGING_RULES");
        QVERIFY(shown);
    }
};

QTEST_MAIN(DiagnosticsPageTests)
#include "test_diagnosticspage.moc"
