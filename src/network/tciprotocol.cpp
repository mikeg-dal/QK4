#include "network/tciprotocol.h"

namespace {

// Commands WSJT-X sends without a receiver index that everything downstream expects to carry one.
// Only split_enable is observed in practice; the list exists so adding another is a one-line change
// rather than a new code path.
const QStringList &globalFormCommands() {
    static const QStringList names{QStringLiteral("split_enable")};
    return names;
}

bool looksLikeBoolean(const QString &s) {
    return s.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0 ||
           s.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0;
}

QString sanitize(const QString &value) {
    QString out = value;
    out.remove(QLatin1Char(';'));
    out.remove(QLatin1Char(','));
    return out;
}

} // namespace

namespace TciProtocol {

bool Command::argAsInt(int i, int *out) const {
    bool ok = false;
    const int v = arg(i).toInt(&ok);
    if (ok && out) {
        *out = v;
    }
    return ok;
}

bool Command::argAsLongLong(int i, qint64 *out) const {
    bool ok = false;
    const qint64 v = arg(i).toLongLong(&ok);
    if (ok && out) {
        *out = v;
    }
    return ok;
}

bool Command::argAsBool(int i, bool *out) const {
    const QString s = arg(i);
    if (!looksLikeBoolean(s)) {
        // WHY refuse rather than default to false: the reference server reads anything that is not
        // "true" as false, so "split_enable:0,yes" silently turns split off and "trx:0,yes"
        // silently unkeys. A refusal is visible; a wrong answer is not.
        return false;
    }
    if (out) {
        *out = (s.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);
    }
    return true;
}

Command parseOne(const QString &text) {
    Command cmd;
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return cmd;
    }

    const int colon = trimmed.indexOf(QLatin1Char(':'));
    if (colon < 0) {
        cmd.name = trimmed.toLower();
        cmd.valid = !cmd.name.isEmpty();
        return cmd;
    }

    cmd.name = trimmed.left(colon).trimmed().toLower();
    if (cmd.name.isEmpty()) {
        return cmd;
    }

    const QString argsPart = trimmed.mid(colon + 1);
    // Qt::KeepEmptyParts: an empty argument is a distinct (if usually invalid) value, and dropping
    // it would shift every later argument into the wrong position.
    const QStringList raw = argsPart.split(QLatin1Char(','), Qt::KeepEmptyParts);
    for (const QString &a : raw) {
        cmd.args.append(a.trimmed());
    }
    cmd.valid = true;
    return cmd;
}

QVector<Command> Parser::feed(const QString &text) {
    QVector<Command> out;
    m_buffer += text;

    // A peer that never terminates a command must not grow this without bound.
    if (m_buffer.size() > MAX_BUFFERED_CHARS) {
        m_buffer.clear();
        return out;
    }

    int start = 0;
    for (;;) {
        const int end = m_buffer.indexOf(QLatin1Char(';'), start);
        if (end < 0) {
            break;
        }
        const Command cmd = parseOne(m_buffer.mid(start, end - start));
        if (cmd.valid) {
            out.append(cmd);
        }
        start = end + 1;
    }
    if (start > 0) {
        m_buffer.remove(0, start);
    }
    return out;
}

QString message(const QString &name, const QStringList &args) {
    QString out = name.toLower();
    if (!args.isEmpty()) {
        QStringList clean;
        clean.reserve(args.size());
        for (const QString &a : args) {
            clean.append(sanitize(a));
        }
        out += QLatin1Char(':') + clean.join(QLatin1Char(','));
    }
    out += QLatin1Char(';');
    return out;
}

QString message(const QString &name, const QString &a) {
    return message(name, QStringList{a});
}

QString message(const QString &name, const QString &a, const QString &b) {
    return message(name, QStringList{a, b});
}

QString message(const QString &name, const QString &a, const QString &b, const QString &c) {
    return message(name, QStringList{a, b, c});
}

QString messageFreeText(const QString &name, const QString &value) {
    return name.toLower() + QLatin1Char(':') + value + QLatin1Char(';');
}

QString boolText(bool value) {
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

Command expandGlobalForm(const Command &cmd) {
    if (!cmd.valid || cmd.args.size() != 1 || !globalFormCommands().contains(cmd.name)) {
        return cmd;
    }
    // Only a lone boolean is the global form. "split_enable:0" is a GET for receiver 0 and must
    // not be rewritten into a SET.
    if (!looksLikeBoolean(cmd.args.first())) {
        return cmd;
    }
    Command expanded = cmd;
    expanded.args.prepend(QStringLiteral("0"));
    return expanded;
}

} // namespace TciProtocol
