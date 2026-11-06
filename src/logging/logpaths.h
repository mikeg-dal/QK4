#ifndef LOGGING_LOGPATHS_H
#define LOGGING_LOGPATHS_H

#include <QHash>
#include <QString>

namespace Logging {

enum class Platform { MacOS, Windows, Linux };

// Everything the log directory depends on, so the choice is a pure function testable on any OS.
struct PathInputs {
    Platform platform = Platform::Linux;
    QString home;                // QDir::homePath()
    QHash<QString, QString> env; // LOCALAPPDATA, XDG_STATE_HOME, XDG_DATA_HOME, FLATPAK_ID
};

// Where QK4 writes qk4.log, with '/' separators:
//   macOS    ~/Library/Logs/QK4                (Console.app lists it)
//   Windows  %LOCALAPPDATA%/QK4/Logs
//   Linux    $XDG_STATE_HOME/QK4/logs, default ~/.local/state/QK4/logs
//   Flatpak  the same; before Flatpak 1.13 (no XDG_STATE_HOME) $XDG_DATA_HOME/QK4/logs
QString logDirectory(const PathInputs &inputs);

// The inputs of the running process.
PathInputs currentPathInputs();

} // namespace Logging

#endif // LOGGING_LOGPATHS_H
