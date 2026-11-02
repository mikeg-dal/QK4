#include "settings/radiosettings.h"
#include "ui/dialogs/radiomanagerdialog.h"

#include <QApplication>
#include <QMessageBox>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <functional>

namespace {
QLineEdit *field(RadioManagerDialog &dialog, const QString &placeholder) {
    for (auto *edit : dialog.findChildren<QLineEdit *>()) {
        if (edit->placeholderText() == placeholder)
            return edit;
    }
    return nullptr;
}

void click(RadioManagerDialog &dialog, const QString &text) {
    for (auto *button : dialog.findChildren<QPushButton *>()) {
        if (button->text() == text) {
            button->click();
            return;
        }
    }
    QFAIL("button not found");
}

bool respond(RadioManagerDialog &dialog, QMessageBox::StandardButton choice, const std::function<void()> &action,
             bool *saveEnabled = nullptr) {
    bool prompted = false;
    QTimer::singleShot(0, &dialog, [&] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!box)
            return;
        prompted = true;
        if (saveEnabled)
            *saveEnabled = box->button(QMessageBox::Save)->isEnabled();
        box->button(choice)->click();
    });
    action();
    QCoreApplication::processEvents();
    return prompted;
}

void enterHost(RadioManagerDialog &dialog, const QString &name = "Test") {
    field(dialog, "Server Name")->setText(name);
    field(dialog, "192.168.1.100")->setText("test.local");
}
} // namespace

class RadioManagerDialogTests : public QObject {
    Q_OBJECT
    QTemporaryDir m_settingsDir;

private slots:
    void initTestCase() {
        QVERIFY(m_settingsDir.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
        const QSettings probe(QSettings::defaultFormat(), QSettings::UserScope, "QK4", "QK4");
        QVERIFY2(probe.fileName().startsWith(m_settingsDir.path()), "refusing to write real settings");
    }

    void init() {
        auto *settings = RadioSettings::instance();
        while (!settings->radios().isEmpty())
            settings->removeRadio(0);
    }

    void addCancelKeepsDraft() {
        RadioManagerDialog dialog;
        enterHost(dialog);
        QVERIFY(respond(dialog, QMessageBox::Cancel, [&] { click(dialog, "Add"); }));
        QCOMPARE(field(dialog, "Server Name")->text(), "Test");
        QVERIFY(RadioSettings::instance()->radios().isEmpty());
    }

    void addSavePersistsThenStartsBlank() {
        RadioManagerDialog dialog;
        enterHost(dialog);
        field(dialog, "Password")->setText("secret");
        QVERIFY(respond(dialog, QMessageBox::Save, [&] { click(dialog, "Add"); }));
        QCOMPARE(RadioSettings::instance()->radios().size(), 1);
        QCOMPARE(RadioSettings::instance()->radios().first().password, "secret");
        QVERIFY(field(dialog, "Server Name")->text().isEmpty());
        QVERIFY(!respond(dialog, QMessageBox::Cancel, [&] { click(dialog, "Add"); }));
    }

    void addDiscardStartsBlank() {
        RadioManagerDialog dialog;
        enterHost(dialog);
        QVERIFY(respond(dialog, QMessageBox::Discard, [&] { click(dialog, "Add"); }));
        QVERIFY(field(dialog, "192.168.1.100")->text().isEmpty());
        QVERIFY(RadioSettings::instance()->radios().isEmpty());
    }

    void invalidDraftCannotSave() {
        RadioManagerDialog dialog;
        field(dialog, "Server Name")->setText("Incomplete");
        bool saveEnabled = true;
        QVERIFY(respond(dialog, QMessageBox::Cancel, [&] { click(dialog, "Add"); }, &saveEnabled));
        QVERIFY(!saveEnabled);
        QCOMPARE(field(dialog, "Server Name")->text(), "Incomplete");
        enterHost(dialog);
        field(dialog, "64242")->setText("0");
        QVERIFY(respond(dialog, QMessageBox::Cancel, [&] { click(dialog, "Add"); }, &saveEnabled));
        QVERIFY(!saveEnabled);
    }

    void everyFieldPromptsAndRevertedEditsDoNot() {
        RadioManagerDialog dialog;
        enterHost(dialog);
        click(dialog, "Save");
        for (auto *edit : dialog.findChildren<QLineEdit *>()) {
            const QString original = edit->text();
            edit->setText(original + "x");
            QVERIFY(respond(dialog, QMessageBox::Cancel, [&] { click(dialog, "Add"); }));
            edit->setText(original);
        }
        for (auto *check : dialog.findChildren<QCheckBox *>()) {
            const bool original = check->isChecked();
            const QString port = field(dialog, "64242")->text();
            check->setChecked(!original);
            QVERIFY(respond(dialog, QMessageBox::Cancel, [&] { click(dialog, "Add"); }));
            check->setChecked(original);
            field(dialog, "64242")->setText(port);
        }
        for (auto *combo : dialog.findChildren<QComboBox *>()) {
            const int original = combo->currentIndex();
            combo->setCurrentIndex((original + 1) % combo->count());
            QVERIFY(respond(dialog, QMessageBox::Cancel, [&] { click(dialog, "Add"); }));
            combo->setCurrentIndex(original);
        }
        QVERIFY(!respond(dialog, QMessageBox::Cancel, [&] { dialog.reject(); }));
    }

    void selectionCancelAndSaveFollowServerAfterSort() {
        auto *settings = RadioSettings::instance();
        RadioEntry first;
        first.name = "Alpha";
        first.host = "alpha.local";
        settings->addRadio(first);
        RadioEntry second = first;
        second.name = "Bravo";
        second.host = "bravo.local";
        settings->addRadio(second);
        settings->setLastSelectedIndex(0);
        RadioManagerDialog dialog;
        auto *list = dialog.findChild<QListWidget *>();
        field(dialog, "Server Name")->setText("Zulu");
        QVERIFY(respond(dialog, QMessageBox::Cancel, [&] { list->setCurrentRow(1); }));
        QCOMPARE(list->currentRow(), 0);
        QCOMPARE(field(dialog, "Server Name")->text(), "Zulu");
        QVERIFY(respond(dialog, QMessageBox::Save, [&] { list->setCurrentRow(1); }));
        QCOMPARE(field(dialog, "Server Name")->text(), "Bravo");
        QCOMPARE(list->currentRow(), 0);
        QCOMPARE(settings->radios().at(1).name, "Zulu");
    }

    void closeAndEscapeRespectCancel() {
        RadioManagerDialog dialog;
        dialog.show();
        enterHost(dialog);
        QSignalSpy finished(&dialog, &QDialog::finished);
        QVERIFY(respond(dialog, QMessageBox::Cancel, [&] { dialog.close(); }));
        QVERIFY(dialog.isVisible());
        QVERIFY(finished.isEmpty());
        QVERIFY(respond(dialog, QMessageBox::Cancel, [&] { QTest::keyClick(&dialog, Qt::Key_Escape); }));
        QVERIFY(dialog.isVisible());
        QVERIFY(respond(dialog, QMessageBox::Save, [&] { dialog.reject(); }));
        QCOMPARE(finished.size(), 1);
        QCOMPARE(RadioSettings::instance()->radios().size(), 1);
    }

    void connectCanSaveDraftBeforeEmitting() {
        RadioManagerDialog dialog;
        enterHost(dialog);
        QSignalSpy requested(&dialog, &RadioManagerDialog::connectRequested);
        QVERIFY(respond(dialog, QMessageBox::Cancel, [&] { click(dialog, "Connect"); }));
        QVERIFY(requested.isEmpty());
        QVERIFY(respond(dialog, QMessageBox::Save, [&] { click(dialog, "Connect"); }));
        QCOMPARE(requested.size(), 1);
        QCOMPARE(RadioSettings::instance()->radios().size(), 1);
    }
};

QTEST_MAIN(RadioManagerDialogTests)
#include "test_radiomanagerdialog.moc"
