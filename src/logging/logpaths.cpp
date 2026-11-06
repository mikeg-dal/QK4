#include "logging/logpaths.h"

#include <QDir>
#include <QtGlobal>

namespace Logging {

namespace {

QString slashes(QString path) {
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (path.size() > 1 && path.endsWith(QLatin1Char('/')))
        path.chop(1);
    return path;
}

// WHY: the XDG Base Directory spec says a relative path in an XDG variable is invalid and must be
// ignored, and an empty one is the same as unset.
QString xdgDir(const PathInputs &inputs, const char *name) {
    const QString value = inputs.env.value(QLatin1String(name));
    return value.startsWith(QLatin1Char('/')) ? slashes(value) : QString();
}

} // namespace

QString logDirectory(const PathInputs &inputs) {
    const QString home = slashes(inputs.home);
    switch (inputs.platform) {
    case Platform::MacOS:
        return home + QStringLiteral("/Library/Logs/QK4");
    case Platform::Windows: {
        const QString localAppData = slashes(inputs.env.value(QStringLiteral("LOCALAPPDATA")));
        const QString base = localAppData.isEmpty() ? home + QStringLiteral("/AppData/Local") : localAppData;
        return base + QStringLiteral("/QK4/Logs");
    }
    case Platform::Linux:
        break;
    }

    const QString state = xdgDir(inputs, "XDG_STATE_HOME");
    if (!state.isEmpty())
        return state + QStringLiteral("/QK4/logs");
    // WHY: Flatpak maps XDG_STATE_HOME into the app's persistent ~/.var/app/<id> only from 1.13.
    // Before that it is unset, and ~/.local/state inside the sandbox does not survive a restart;
    // the data directory does.
    if (!inputs.env.value(QStringLiteral("FLATPAK_ID")).isEmpty()) {
        const QString data = xdgDir(inputs, "XDG_DATA_HOME");
        return (data.isEmpty() ? home + QStringLiteral("/.local/share") : data) + QStringLiteral("/QK4/logs");
    }
    return home + QStringLiteral("/.local/state/QK4/logs");
}

PathInputs currentPathInputs() {
    PathInputs inputs;
#if defined(Q_OS_MACOS)
    inputs.platform = Platform::MacOS;
#elif defined(Q_OS_WIN)
    inputs.platform = Platform::Windows;
#else
    inputs.platform = Platform::Linux;
#endif
    inputs.home = QDir::homePath();
    for (const char *name : {"LOCALAPPDATA", "XDG_STATE_HOME", "XDG_DATA_HOME", "FLATPAK_ID"}) {
        if (qEnvironmentVariableIsSet(name))
            inputs.env.insert(QLatin1String(name), qEnvironmentVariable(name));
    }
    return inputs;
}

} // namespace Logging
