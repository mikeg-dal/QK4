#include <QSet>
#include <QtTest/QtTest>

#include "logging/logcategories.h"
#include "logging/logrules.h"

using Logging::Level;
using Logging::LogConfig;

namespace {

LogConfig config(Level level, const QStringList &groups = {}, const QStringList &bench = {},
                 const QString &extra = {}) {
    LogConfig c;
    c.level = level;
    c.groups = groups;
    c.bench = bench;
    c.extraRules = extra;
    return c;
}

const QLoggingCategory &category(const char *name) {
    for (const auto fn : Logging::allCategories()) {
        if (qstrcmp(fn().categoryName(), name) == 0)
            return fn();
    }
    qFatal("no category %s", name);
}

} // namespace

class LogRulesTests : public QObject {
    Q_OBJECT

private slots:
    void cleanup() { QLoggingCategory::setFilterRules(QString()); }

    void normalAddsNothing() { QCOMPARE(Logging::buildRules(config(Level::Normal)), QString()); }

    void warningsTurnInfoOff() {
        QCOMPARE(Logging::buildRules(config(Level::Warnings)), QStringLiteral("qk4.*.info=false"));
    }

    void aGroupNamesEachOfItsCategories() {
        const QString rules = Logging::buildRules(config(Level::Normal, {"audio", "tci"}));
        QCOMPARE(rules, QStringLiteral("qk4.audio.debug=true\nqk4.tci.debug=true\nqk4.tci.ws.debug=true"));
    }

    void everythingKeepsBenchItemsOff() {
        const QString rules = Logging::buildRules(config(Level::Everything));
        QVERIFY(rules.startsWith(QStringLiteral("qk4.*.debug=true\n")));
        QVERIFY(rules.contains(QStringLiteral("qk4.cat.tx.debug=false")));
        QVERIFY(rules.contains(QStringLiteral("qk4.spectrum.diag.debug=false")));
    }

    void aSelectedBenchItemSurvivesEverything() {
        const QString rules = Logging::buildRules(config(Level::Everything, {}, {"cat"}));
        QVERIFY(rules.contains(QStringLiteral("qk4.cat.rx.debug=true")));
        QVERIFY(!rules.contains(QStringLiteral("qk4.cat.rx.debug=false")));
        QVERIFY(rules.indexOf(QStringLiteral("qk4.cat.rx.debug=true")) > rules.indexOf(QStringLiteral("qk4.*")));
    }

    void commandLineRulesComeLast() {
        const QString rules =
            Logging::buildRules(config(Level::Warnings, {"cw"}, {}, " qk4.cw.keyer.debug=false ; qk4.app.info=true;"));
        const QStringList lines = rules.split('\n');
        QCOMPARE(lines.mid(lines.size() - 2), (QStringList{"qk4.cw.keyer.debug=false", "qk4.app.info=true"}));
    }

    void unknownIdsAreDropped() {
        QCOMPARE(Logging::sanitizeGroupIds({"tci", "nope", "app", "tci"}), (QStringList{"app", "tci"}));
        QCOMPARE(Logging::sanitizeBenchIds({"cw", "cat"}), QStringList{"cat"});
        QCOMPARE(Logging::levelFromId("loud"), Level::Normal);
        QCOMPARE(Logging::levelFromId(Logging::levelId(Level::Everything)), Level::Everything);
    }

    void everyCategoryIsReachableAndBenchItemsOnlyFromTheBench() {
        QSet<QString> real;
        for (const auto fn : Logging::allCategories())
            real.insert(QString::fromLatin1(fn().categoryName()));
        QSet<QString> grouped;
        QSet<QString> bench;
        for (const auto &group : Logging::groups())
            for (const char *name : group.categories)
                grouped.insert(QString::fromLatin1(name));
        for (const auto &item : Logging::benchItems())
            for (const char *name : item.categories)
                bench.insert(QString::fromLatin1(name));
        for (const QString &name : std::as_const(grouped))
            QVERIFY2(real.contains(name), qPrintable(name + " is not a category"));
        for (const QString &name : std::as_const(bench))
            QVERIFY2(real.contains(name), qPrintable(name + " is not a category"));
        QVERIFY(!grouped.intersects(bench));
        QCOMPARE(grouped + bench, real);
    }

    void rulesTakeEffectOnTheRealCategories() {
        QLoggingCategory::setFilterRules(Logging::buildRules(config(Level::Normal, {"cw"})));
        QVERIFY(category("qk4.cw.keyer").isDebugEnabled());
        QVERIFY(category("qk4.audio.sidetone").isDebugEnabled());
        QVERIFY(category("qk4.kpod.plus").isDebugEnabled());
        QVERIFY(!category("qk4.audio").isDebugEnabled());
        QVERIFY(!category("qk4.cat.tx").isDebugEnabled());

        QLoggingCategory::setFilterRules(Logging::buildRules(config(Level::Everything, {}, {"audiotx"})));
        QVERIFY(category("qk4.app").isDebugEnabled());
        QVERIFY(category("qk4.audio.tx").isDebugEnabled());
        QVERIFY(!category("qk4.audio.rx").isDebugEnabled());
        QVERIFY(!category("qk4.cat.rx").isDebugEnabled());

        QLoggingCategory::setFilterRules(Logging::buildRules(config(Level::Warnings)));
        QVERIFY(!category("qk4.link").isInfoEnabled());
        QVERIFY(category("qk4.link").isWarningEnabled());

        QLoggingCategory::setFilterRules(QString());
        QVERIFY(category("qk4.link").isInfoEnabled());
        QVERIFY(!category("qk4.cw.keyer").isDebugEnabled());
    }

    void environmentOverrideIsDetected() {
        const QByteArray saved = qgetenv("QT_LOGGING_RULES");
        const bool wasSet = qEnvironmentVariableIsSet("QT_LOGGING_RULES");
        qunsetenv("QT_LOGGING_RULES");
        const bool confSet = qEnvironmentVariableIsSet("QT_LOGGING_CONF");
        if (!confSet)
            QVERIFY(!Logging::rulesOverriddenByEnvironment());
        qputenv("QT_LOGGING_RULES", "qk4.app.debug=true");
        QVERIFY(Logging::rulesOverriddenByEnvironment());
        if (wasSet)
            qputenv("QT_LOGGING_RULES", saved);
        else
            qunsetenv("QT_LOGGING_RULES");
    }
};

QTEST_GUILESS_MAIN(LogRulesTests)
#include "test_logrules.moc"
