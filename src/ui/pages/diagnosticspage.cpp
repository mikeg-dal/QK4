#include "ui/pages/diagnosticspage.h"

#include "logging/logrules.h"
#include "logging/logsession.h"
#include "settings/radiosettings.h"
#include "ui/pages/optionspagelayout.h"
#include "ui/styling/k4styles.h"

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {

QPushButton *smallButton(const QString &text, QWidget *parent) {
    auto *button = new QPushButton(text, parent);
    button->setStyleSheet(K4Styles::Dialog::actionButtonSmall());
    button->setCursor(Qt::PointingHandCursor);
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    return button;
}

// WHY: a path has no spaces, so a word-wrapped label cannot break it and clips it instead. A
// zero-width space after each separator gives it break points; Copy Path hands out the real path.
QString breakablePath(const QString &path) {
    QString native = QDir::toNativeSeparators(path);
    const QChar separator = QDir::separator();
    native.replace(separator, QString(separator) + QChar(0x200B));
    return native;
}

QWidget *checkColumn(const std::vector<Logging::Group> &items, std::vector<QCheckBox *> &checks, QWidget *parent) {
    auto *column = new QWidget(parent);
    auto *layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);
    for (const Logging::Group &item : items) {
        auto *check = new QCheckBox(QString::fromUtf8(item.label), column);
        check->setStyleSheet(K4Styles::Dialog::checkBox());
        check->setProperty("logId", QString::fromLatin1(item.id));
        layout->addWidget(check);
        checks.push_back(check);
    }
    return column;
}

QStringList checkedIds(const std::vector<QCheckBox *> &checks) {
    QStringList ids;
    for (const QCheckBox *check : checks) {
        if (check->isChecked())
            ids << check->property("logId").toString();
    }
    return ids;
}

void setChecked(const std::vector<QCheckBox *> &checks, const QStringList &ids) {
    for (QCheckBox *check : checks) {
        const QSignalBlocker block(check);
        check->setChecked(ids.contains(check->property("logId").toString()));
    }
}

} // namespace

DiagnosticsPage::DiagnosticsPage(QWidget *parent) : QWidget(parent) {
    RadioSettings *settings = RadioSettings::instance();

    auto *layout = OptionsPageLayout::createPage(this);
    OptionsPageLayout::addTitle(layout, this, "Diagnostics");
    OptionsPageLayout::addDescription(
        layout, this,
        "A log file for bug reports. It records what QK4 did - connection attempts with the radio's address, "
        "audio device names and, with bench diagnostics on, every CAT command. It never records a password or "
        "PSK. Attach qk4.log, and qk4.1.log if QK4 crashed, to a bug report.");

    auto *form = OptionsPageLayout::addForm(layout);

    OptionsPageLayout::addSection(form, this, "Log File");
    m_fileCheck = new QCheckBox("Write a log file", this);
    m_fileCheck->setStyleSheet(K4Styles::Dialog::checkBox());
    form->addRow(QString(), m_fileCheck);

    auto *buttons = new QWidget(this);
    auto *buttonRow = new QHBoxLayout(buttons);
    buttonRow->setContentsMargins(0, 0, 0, 0);
    m_openFolderButton = smallButton("Open Log Folder", buttons);
    m_copyPathButton = smallButton("Copy Path", buttons);
    buttonRow->addWidget(m_openFolderButton);
    buttonRow->addWidget(m_copyPathButton);
    buttonRow->addStretch();
    form->addRow(QString(), buttons);
    m_fileStatus = OptionsPageLayout::addFieldHelp(form, this, QString());

    OptionsPageLayout::addSection(form, this, "Detail");
    m_levelCombo = new QComboBox(this);
    m_levelCombo->setStyleSheet(K4Styles::Dialog::comboBox());
    m_levelCombo->addItem("Warnings only", Logging::levelId(Logging::Level::Warnings));
    m_levelCombo->addItem("Normal", Logging::levelId(Logging::Level::Normal));
    m_levelCombo->addItem("Everything", Logging::levelId(Logging::Level::Everything));
    OptionsPageLayout::capWidth(m_levelCombo, K4Styles::Dimensions::OptionsFieldMaxWidth);
    OptionsPageLayout::addRow(form, this, "Level", m_levelCombo);
    OptionsPageLayout::addRow(form, this, "Subsystems", checkColumn(Logging::groups(), m_groupChecks, this));
    OptionsPageLayout::addFieldHelp(form, this,
                                    "Adds debug lines for the checked subsystems. Everything adds them for all of "
                                    "them. Changes apply at once.");

    OptionsPageLayout::addSection(form, this, "Bench Diagnostics");
    form->addRow(QString(), checkColumn(Logging::benchItems(), m_benchChecks, this));
    OptionsPageLayout::addFieldHelp(form, this,
                                    "High volume. Use only while reproducing a problem. These switch themselves off "
                                    "when QK4 restarts.");

    m_overrideNote = OptionsPageLayout::addHelp(
        layout, this,
        "Logging rules from QT_LOGGING_RULES, QT_LOGGING_CONF or --log-rules are applied after these settings and "
        "override them.");

    layout->addStretch();

    refresh();

    connect(m_fileCheck, &QCheckBox::toggled, settings, &RadioSettings::setLogFileEnabled);
    connect(m_levelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index >= 0)
            RadioSettings::instance()->setLogLevel(m_levelCombo->itemData(index).toString());
    });
    for (QCheckBox *check : m_groupChecks)
        connect(check, &QCheckBox::toggled, this, &DiagnosticsPage::saveGroups);
    for (QCheckBox *check : m_benchChecks)
        connect(check, &QCheckBox::toggled, this, &DiagnosticsPage::saveBench);
    connect(m_openFolderButton, &QPushButton::clicked, this, [] {
        const QString dir = Logging::logDirectory();
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    connect(m_copyPathButton, &QPushButton::clicked, this, [] {
        const QString file = Logging::currentLogFile();
        QGuiApplication::clipboard()->setText(
            QDir::toNativeSeparators(file.isEmpty() ? Logging::logDirectory() : file));
    });
    connect(settings, &RadioSettings::loggingChanged, this, &DiagnosticsPage::refresh);
}

void DiagnosticsPage::refresh() {
    const RadioSettings *settings = RadioSettings::instance();
    {
        const QSignalBlocker block(m_fileCheck);
        m_fileCheck->setChecked(settings->logFileEnabled());
    }
    {
        const QSignalBlocker block(m_levelCombo);
        const int index = m_levelCombo->findData(settings->logLevel());
        m_levelCombo->setCurrentIndex(index >= 0 ? index : 1);
    }
    setChecked(m_groupChecks, settings->logGroups());
    setChecked(m_benchChecks, settings->logBench());

    const QString file = Logging::currentLogFile();
    QString status =
        file.isEmpty()
            ? QStringLiteral("Not writing a log file. Folder: %1").arg(breakablePath(Logging::logDirectory()))
            : QStringLiteral("Writing %1").arg(breakablePath(file));
    if (Logging::commandLine().forceFile)
        status += QStringLiteral(" --log-file is on the command line, so this session writes one regardless.");
    m_fileStatus->setText(status);
    m_overrideNote->setVisible(Logging::externalRulesInEffect());
}

void DiagnosticsPage::saveGroups() {
    RadioSettings::instance()->setLogGroups(checkedIds(m_groupChecks));
}

void DiagnosticsPage::saveBench() {
    RadioSettings::instance()->setLogBench(checkedIds(m_benchChecks));
}
