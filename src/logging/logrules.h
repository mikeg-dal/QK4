#ifndef LOGGING_LOGRULES_H
#define LOGGING_LOGRULES_H

#include <QString>
#include <QStringList>
#include <vector>

namespace Logging {

enum class Level { Warnings, Normal, Everything };

// A switch on the Diagnostics page and the categories it turns to debug. Bench items are the
// high-volume categories (every CAT line, every audio frame); nothing else turns them on, not even
// Level::Everything.
struct Group {
    const char *id;
    const char *label;
    std::vector<const char *> categories;
};

const std::vector<Group> &groups();
const std::vector<Group> &benchItems();

struct LogConfig {
    Level level = Level::Normal;
    QStringList groups; // Group ids
    QStringList bench;  // bench item ids
    QString extraRules; // --log-rules, ';' or newline separated; applied last
};

// The rule text for QLoggingCategory::setFilterRules(). Pure.
QString buildRules(const LogConfig &config);

QString levelId(Level level);
Level levelFromId(const QString &id); // unknown → Normal
// Known ids only, in table order, without duplicates.
QStringList sanitizeGroupIds(const QStringList &ids);
QStringList sanitizeBenchIds(const QStringList &ids);

// QT_LOGGING_RULES and QT_LOGGING_CONF are applied after setFilterRules(), so they win.
bool rulesOverriddenByEnvironment();

} // namespace Logging

#endif // LOGGING_LOGRULES_H
