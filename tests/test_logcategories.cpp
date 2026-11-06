#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QtTest/QtTest>
#include <functional>

#include "logging/logcategories.h"

namespace {

// Source with comments removed and the contents of string and character literals blanked (the
// quotes stay), so a scan sees only code. Raw strings R"d(...)d" are handled.
QString codeOnly(const QString &src) {
    QString out;
    out.reserve(src.size());
    enum { Code, Line, Block, Str, Chr, Raw } state = Code;
    QString rawEnd;
    for (qsizetype i = 0; i < src.size(); ++i) {
        const QChar c = src[i];
        const QChar n = i + 1 < src.size() ? src[i + 1] : QChar();
        switch (state) {
        case Code:
            if (c == '/' && n == '/') {
                state = Line;
                ++i;
            } else if (c == '/' && n == '*') {
                state = Block;
                ++i;
            } else if (c == 'R' && n == '"' && (i == 0 || !(src[i - 1].isLetterOrNumber() || src[i - 1] == '_'))) {
                const qsizetype open = src.indexOf('(', i + 2);
                if (open < 0) {
                    out += c;
                    break;
                }
                rawEnd = QStringLiteral(")") + src.mid(i + 2, open - i - 2) + QStringLiteral("\"");
                out += QStringLiteral("R\"");
                i = open;
                state = Raw;
            } else {
                if (c == '"')
                    state = Str;
                else if (c == '\'')
                    state = Chr;
                out += c;
            }
            break;
        case Line:
            if (c == '\n') {
                state = Code;
                out += c;
            }
            break;
        case Block:
            if (c == '*' && n == '/') {
                state = Code;
                ++i;
            } else if (c == '\n') {
                out += c;
            }
            break;
        case Str:
        case Chr:
            if (c == '\\') {
                ++i;
            } else if ((state == Str && c == '"') || (state == Chr && c == '\'')) {
                out += c;
                state = Code;
            } else if (c == '\n') {
                out += c;
            }
            break;
        case Raw:
            if (src.mid(i, rawEnd.size()) == rawEnd) {
                out += QLatin1Char('"');
                i += rawEnd.size() - 1;
                state = Code;
            } else if (c == '\n') {
                out += c;
            }
            break;
        }
    }
    return out;
}

struct Violation {
    QString file;
    int line;
    QString text;
};

QString relative(const QString &path) {
    return QString(path).remove(QStringLiteral(QK4_SOURCE_DIR "/"));
}

int lineOf(const QString &code, qsizetype offset) {
    return int(code.left(offset).count(QLatin1Char('\n'))) + 1;
}

// Every match of `pattern` in the code (comments and literal contents removed) of every .cpp/.h
// under src/, except files for which `skip` returns true.
QList<Violation> scanSources(const QRegularExpression &pattern, const std::function<bool(const QString &)> &skip,
                             int *scanned = nullptr) {
    QList<Violation> found;
    int count = 0;
    QDirIterator it(QStringLiteral(QK4_SOURCE_DIR), {"*.cpp", "*.h"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString rel = relative(path);
        if (skip(rel))
            continue;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        ++count;
        const QString code = codeOnly(QString::fromUtf8(f.readAll()));
        auto matches = pattern.globalMatch(code);
        while (matches.hasNext()) {
            const auto m = matches.next();
            found.append({rel, lineOf(code, m.capturedStart()), m.captured().simplified()});
        }
    }
    if (scanned)
        *scanned = count;
    return found;
}

QString describe(const QList<Violation> &violations) {
    QStringList lines;
    for (const Violation &v : violations)
        lines << QStringLiteral("%1:%2: %3").arg(v.file).arg(v.line).arg(v.text);
    return lines.join(QLatin1Char('\n'));
}

const QRegularExpression &categoryDefinition() {
    static const QRegularExpression re(QStringLiteral(R"(\bQ_(DECLARE_)?LOGGING_CATEGORY\s*\()"));
    return re;
}

// qDebug()/qInfo()/qWarning()/qCritical() log to the "default" category, which no rule or Options
// switch can address.
const QRegularExpression &uncategorisedCall() {
    static const QRegularExpression re(QStringLiteral(R"(\bq(Debug|Info|Warning|Critical)\s*\()"));
    return re;
}

// The operands of every log statement: from qCDebug( / qCInfo( / qCWarning( / qCCritical( to the
// statement's semicolon, in code with comments and literal contents removed.
QList<Violation> logStatementsStreaming(const QString &code, const QString &file, const QRegularExpression &forbidden) {
    static const QRegularExpression start(QStringLiteral(R"(\bqC(Debug|Info|Warning|Critical)\s*\()"));
    // Only the emptiness or length of a secret may be logged.
    static const QRegularExpression harmless(
        QStringLiteral(R"(\b\w+\s*(\.|->)\s*(isEmpty|isNull|size|length|count)\s*\(\s*\))"));
    QList<Violation> found;
    auto it = start.globalMatch(code);
    while (it.hasNext()) {
        const auto m = it.next();
        const qsizetype end = code.indexOf(QLatin1Char(';'), m.capturedEnd());
        QString statement = code.mid(m.capturedStart(), end < 0 ? -1 : end - m.capturedStart());
        statement.remove(harmless);
        const auto hit = forbidden.match(statement);
        if (hit.hasMatch())
            found.append({file, lineOf(code, m.capturedStart()), hit.captured()});
    }
    return found;
}

// An identifier that names a secret: one that ends in password, psk, secret, identity or
// presharedkey, in any case (m_password, pskIdentity, preSharedKey).
const QRegularExpression &secretIdentifier() {
    static const QRegularExpression re(QStringLiteral(R"(\b\w*(password|psk|secret|identity|presharedkey)\b)"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

bool inLoggingDir(const QString &rel) {
    return rel.startsWith(QStringLiteral("logging/"));
}

} // namespace

class LogCategoriesTests : public QObject {
    Q_OBJECT

private slots:
    void namesFollowTheScheme() {
        const QRegularExpression scheme(QStringLiteral(R"(^qk4(\.[a-z][a-z0-9]*)*$)"));
        QSet<QString> seen;
        for (const auto category : Logging::allCategories()) {
            const QString name = QString::fromLatin1(category().categoryName());
            QVERIFY2(scheme.match(name).hasMatch(), qPrintable(name));
            QVERIFY2(!seen.contains(name), qPrintable(QStringLiteral("duplicate category ") + name));
            seen.insert(name);
        }
        QVERIFY(seen.size() >= 30);
    }

    void debugIsOffAndInfoOnByDefault() {
        QLoggingCategory::setFilterRules(QString());
        for (const auto category : Logging::allCategories()) {
            const QLoggingCategory &c = category();
            QVERIFY2(!c.isDebugEnabled(), c.categoryName());
            QVERIFY2(c.isInfoEnabled(), c.categoryName());
            QVERIFY2(c.isWarningEnabled(), c.categoryName());
        }
    }

    void categoriesAreDefinedOnlyInTheLoggingModule() {
        int scanned = 0;
        const auto violations = scanSources(categoryDefinition(), inLoggingDir, &scanned);
        QVERIFY2(scanned > 50, "source scan found too few files - is QK4_SOURCE_DIR right?");
        QVERIFY2(violations.isEmpty(), qPrintable(QStringLiteral("define categories in src/logging/logcategories:\n") +
                                                  describe(violations)));
    }

    void everyLogCallNamesACategory() {
        const auto violations = scanSources(uncategorisedCall(), inLoggingDir);
        QVERIFY2(violations.isEmpty(),
                 qPrintable(QStringLiteral("use qCDebug/qCInfo/qCWarning/qCCritical with a category from "
                                           "logging/logcategories.h:\n") +
                            describe(violations)));
    }

    void uncategorisedScanSeesARawCallButNotACategorisedOne() {
        const QString code = codeOnly(QStringLiteral("qCWarning(lcApp) << \"qWarning() in text\";\n"
                                                     "qWarning() << 1;\n"
                                                     "qInfo (\"x\");\n"
                                                     "// qDebug() in a comment\n"));
        int hits = 0;
        auto it = uncategorisedCall().globalMatch(code);
        while (it.hasNext()) {
            it.next();
            ++hits;
        }
        QCOMPARE(hits, 2);
    }

    void noLogStatementStreamsASecret() {
        QList<Violation> violations;
        QDirIterator it(QStringLiteral(QK4_SOURCE_DIR), {"*.cpp", "*.h"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly))
                continue;
            violations +=
                logStatementsStreaming(codeOnly(QString::fromUtf8(f.readAll())), relative(path), secretIdentifier());
        }
        QVERIFY2(violations.isEmpty(),
                 qPrintable(QStringLiteral("never log a password, PSK or PSK identity - log whether it is set:\n") +
                            describe(violations)));
    }

    void secretScanCatchesSecretsButNotTheirEmptiness() {
        const auto scan = [](const char *source) {
            return logStatementsStreaming(codeOnly(QString::fromUtf8(source)), QStringLiteral("x.cpp"),
                                          secretIdentifier())
                .size();
        };
        QCOMPARE(scan("qCDebug(lcLinkTcp) << \"identity:\" << m_identity;"), 1);
        QCOMPARE(scan("qCWarning(lcApp) << entry.password\n    << 1;"), 1);
        QCOMPARE(scan("qCInfo(lcApp) << preSharedKey;"), 1);
        QCOMPARE(scan("qCDebug(lcLinkTcp) << (m_identity.isEmpty() ? \"empty\" : \"set\");"), 0);
        QCOMPARE(scan("qCDebug(lcLinkTcp) << \"password\" << m_password.size();"), 0);
        QCOMPARE(scan("qCDebug(lcLinkTcp) << tls12PskCiphers.size() << identityHint;"), 0);
        QCOMPARE(scan("log(m_password); qCDebug(lcApp) << 1;"), 0);
    }

    void scannerIgnoresCommentsAndStringsButSeesCode() {
        const QString code = codeOnly(QStringLiteral("// Q_LOGGING_CATEGORY(a)\n"
                                                     "/* Q_LOGGING_CATEGORY(b) */\n"
                                                     "auto s = \"Q_LOGGING_CATEGORY(c)\";\n"
                                                     "auto r = R\"x(Q_LOGGING_CATEGORY(d))x\";\n"
                                                     "Q_LOGGING_CATEGORY(e, \"qk4.e\")\n"));
        int hits = 0;
        auto it = categoryDefinition().globalMatch(code);
        while (it.hasNext()) {
            it.next();
            ++hits;
        }
        QCOMPARE(hits, 1);
        QCOMPARE(lineOf(code, code.indexOf(QStringLiteral("Q_LOGGING"))), 5);
    }
};

QTEST_GUILESS_MAIN(LogCategoriesTests)
#include "test_logcategories.moc"
