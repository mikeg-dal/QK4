#ifndef LOGGING_LOGSESSION_H
#define LOGGING_LOGSESSION_H

#include "logging/logrules.h"

#include <QString>
#include <QStringList>

namespace Logging {

// What the command line asked for. Set once in main(), before the first apply().
struct CommandLine {
    QString extraRules;           // --log-rules
    bool forceFile = false;       // --log-file
    bool stderrRequested = false; // --log-stderr
};

void setCommandLine(const CommandLine &commandLine);
const CommandLine &commandLine();

// Lines describing the environment, written at the top of every log file.
void setSessionHeader(const QStringList &lines);

// Applies the operator's choices: filter rules (live, no restart), the log file (opened, rotating
// the previous one, or closed) and whether the console copy is kept. Main thread. Requires
// install(). The command line's rules are appended to config's and --log-file keeps the file on.
void apply(bool fileEnabled, LogConfig config);

QString logDirectory();
QString currentLogFile(); // empty when no file is being written

// Rules the Diagnostics page cannot override: QT_LOGGING_RULES / QT_LOGGING_CONF or --log-rules.
bool externalRulesInEffect();

bool stderrIsTerminal();

} // namespace Logging

#endif // LOGGING_LOGSESSION_H
