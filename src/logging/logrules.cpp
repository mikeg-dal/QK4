#include "logging/logrules.h"

#include <QtGlobal>

namespace Logging {

const std::vector<Group> &groups() {
    static const std::vector<Group> table{
        {"app", "Application", {"qk4.app"}},
        {"ui",
         "User interface",
         {"qk4.ui", "qk4.ui.menu", "qk4.ui.bandnav", "qk4.ui.modepopup", "qk4.ui.textdecode", "qk4.ui.macro"}},
        {"link", "Radio connection", {"qk4.link", "qk4.link.tcp", "qk4.link.discovery", "qk4.tx.owner"}},
        {"audio", "Audio", {"qk4.audio"}},
        // WHY qk4.kpod.plus here too: the KPOD+ forwards its own keyer's KZ strings, and its device
        // lifecycle decides who owns CW.
        {"cw",
         "CW keying",
         {"qk4.cw.keyer", "qk4.cw.halikey", "qk4.cw.midi", "qk4.cw.vail", "qk4.audio.sidetone", "qk4.kpod.plus"}},
        {"kpod", "KPOD and KPOD+", {"qk4.hardware", "qk4.kpod", "qk4.kpod.udev", "qk4.kpod.plus"}},
        {"catserver", "CAT server", {"qk4.catserver", "qk4.catserver.push"}},
        {"tci", "TCI server", {"qk4.tci", "qk4.tci.ws"}},
        {"dxcluster", "DX cluster", {"qk4.dxcluster"}},
        {"kpa1500", "KPA1500", {"qk4.kpa1500", "qk4.kpa1500.ui"}},
        {"spectrum", "Spectrum", {"qk4.spectrum", "qk4.spectrum.render", "qk4.spectrum.minipan"}},
    };
    return table;
}

const std::vector<Group> &benchItems() {
    static const std::vector<Group> table{
        {"cat", "CAT traffic (every command and reply)", {"qk4.cat.tx", "qk4.cat.rx"}},
        {"audiorx", "Audio RX levels", {"qk4.audio.rx"}},
        {"audiotx", "Audio TX frames", {"qk4.audio.tx"}},
        {"spectrumdiag", "Spectrum render geometry", {"qk4.spectrum.diag"}},
    };
    return table;
}

namespace {

QStringList sanitize(const QStringList &ids, const std::vector<Group> &table) {
    QStringList out;
    for (const Group &group : table) {
        if (ids.contains(QLatin1String(group.id)))
            out << QLatin1String(group.id);
    }
    return out;
}

void addDebug(QStringList &lines, const char *category, bool on) {
    lines << QStringLiteral("%1.debug=%2")
                 .arg(QLatin1String(category), on ? QStringLiteral("true") : QStringLiteral("false"));
}

} // namespace

QStringList sanitizeGroupIds(const QStringList &ids) {
    return sanitize(ids, groups());
}

QStringList sanitizeBenchIds(const QStringList &ids) {
    return sanitize(ids, benchItems());
}

QString levelId(Level level) {
    switch (level) {
    case Level::Warnings:
        return QStringLiteral("warnings");
    case Level::Normal:
        break;
    case Level::Everything:
        return QStringLiteral("everything");
    }
    return QStringLiteral("normal");
}

Level levelFromId(const QString &id) {
    if (id == QLatin1String("warnings"))
        return Level::Warnings;
    if (id == QLatin1String("everything"))
        return Level::Everything;
    return Level::Normal;
}

QString buildRules(const LogConfig &config) {
    QStringList lines;
    // WHY "qk4.*": a trailing wildcard matches every category that starts with "qk4.", and every
    // QK4 category does. Qt's own categories (qt.*) and "default" are left alone.
    if (config.level == Level::Warnings)
        lines << QStringLiteral("qk4.*.info=false");
    else if (config.level == Level::Everything)
        lines << QStringLiteral("qk4.*.debug=true");

    const QStringList selectedGroups = sanitizeGroupIds(config.groups);
    for (const Group &group : groups()) {
        if (!selectedGroups.contains(QLatin1String(group.id)))
            continue;
        for (const char *category : group.categories)
            addDebug(lines, category, true);
    }

    // Later rules win, so the bench lines come after everything that could have turned them on.
    const QStringList selectedBench = sanitizeBenchIds(config.bench);
    for (const Group &item : benchItems()) {
        const bool on = selectedBench.contains(QLatin1String(item.id));
        if (!on && config.level != Level::Everything)
            continue;
        for (const char *category : item.categories)
            addDebug(lines, category, on);
    }

    QString extra = config.extraRules;
    extra.replace(QLatin1Char(';'), QLatin1Char('\n'));
    for (const QString &line : extra.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty())
            lines << trimmed;
    }
    return lines.join(QLatin1Char('\n'));
}

bool rulesOverriddenByEnvironment() {
    return qEnvironmentVariableIsSet("QT_LOGGING_RULES") || qEnvironmentVariableIsSet("QT_LOGGING_CONF");
}

} // namespace Logging
