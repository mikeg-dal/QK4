#include "logging/logsession.h"

#include "logging/logcategories.h"
#include "logging/logpaths.h"
#include "logging/logsink.h"

#include <QDir>
#include <QLoggingCategory>
#include <cstdio>
#ifdef Q_OS_WIN
#include <io.h>
#else
#include <unistd.h>
#endif

namespace Logging {

namespace {
CommandLine g_commandLine;
QStringList g_header;
QString g_rules;
bool g_fileOn = false;
QString g_fileDescription;
} // namespace

void setCommandLine(const CommandLine &commandLine) {
    g_commandLine = commandLine;
}

const CommandLine &commandLine() {
    return g_commandLine;
}

void setSessionHeader(const QStringList &lines) {
    g_header = lines;
}

bool stderrIsTerminal() {
#ifdef Q_OS_WIN
    return _isatty(_fileno(stderr)) != 0;
#else
    return isatty(fileno(stderr)) != 0;
#endif
}

void apply(bool fileEnabled, LogConfig config) {
    LogSink *logSink = sink();
    if (!logSink)
        return;

    config.extraRules = g_commandLine.extraRules;
    const QString rules = buildRules(config);
    if (rules != g_rules) {
        g_rules = rules;
        QLoggingCategory::setFilterRules(rules);
    }

    const bool fileOn = fileEnabled || g_commandLine.forceFile;
    // WHY: with no file the console is the only place a message can go, as it always was. With a
    // file, a Finder or Explorer launch would only send a second copy to the unified log or
    // OutputDebugString, so the console copy is kept for a terminal or on request.
    logSink->setConsoleEnabled(!fileOn || stderrIsTerminal() || g_commandLine.stderrRequested);

    const QString description =
        QStringLiteral("Log level %1, groups [%2], bench [%3]%4")
            .arg(levelId(config.level), config.groups.join(QLatin1Char(',')), config.bench.join(QLatin1Char(',')),
                 rulesOverriddenByEnvironment() ? QStringLiteral(" - QT_LOGGING_RULES/QT_LOGGING_CONF override these")
                                                : QString());
    if (fileOn == g_fileOn) {
        if (fileOn && description != g_fileDescription) {
            g_fileDescription = description;
            qCInfo(lcApp).noquote() << "Logging changed:" << description;
            qCInfo(lcApp).noquote() << "Rules:" << QString(rules).replace(QLatin1Char('\n'), QLatin1Char(';'));
        }
        return;
    }
    g_fileOn = fileOn;
    g_fileDescription = description;

    LogSink::FileOptions file;
    if (fileOn) {
        file.directory = logDirectory();
        file.header = g_header;
        file.header << description;
        file.header << QStringLiteral("Rules: %1")
                           .arg(rules.isEmpty() ? QStringLiteral("(none)")
                                                : QString(rules).replace(QLatin1Char('\n'), QLatin1Char(';')));
        file.header << QStringLiteral("Log file %1").arg(QDir::toNativeSeparators(file.directory));
    }
    if (!logSink->setFile(file)) {
        g_fileOn = false;
        qCWarning(lcApp) << "Could not open a log file in" << QDir::toNativeSeparators(file.directory);
    }
}

QString logDirectory() {
    return Logging::logDirectory(currentPathInputs());
}

QString currentLogFile() {
    LogSink *logSink = sink();
    return logSink ? logSink->currentFile() : QString();
}

bool externalRulesInEffect() {
    return rulesOverriddenByEnvironment() || !g_commandLine.extraRules.trimmed().isEmpty();
}

} // namespace Logging
