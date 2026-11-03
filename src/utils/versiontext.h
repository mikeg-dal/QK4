#pragma once

#include <QRegularExpression>
#include <QString>

// Short forms of the application version for places too narrow for the whole string. The full
// string (QCoreApplication::applicationVersion()) stays in Help > About and --version.
namespace VersionText {

// The side panel's one-line version. Three shapes reach it:
//   release              "0.8.1-rc1"                      -> "V0.8.1-rc1"
//   signed tester build  "development-262ba2b-0.8.1-rc1"  -> "V0.8.1-rc1 262ba2b"
//   unsigned CI build    "development-262ba2b"            -> "development 262ba2b"
// WHY the CI shapes are parsed here: build-signed.yml and ci-packaging.yml name every package after
// "<branch>-<sha7>[-<suffix>]", so reordering the string in CI would rename every artifact and DMG.
// The branch is dropped when a release number is present; the hash says which commit it is.
inline QString panelLabel(const QString &version) {
    if (version.isEmpty())
        return {};
    static const QRegularExpression ciBuild(QStringLiteral("^(.+)-([0-9a-f]{7})(?:-([0-9].*))?$"));
    const QRegularExpressionMatch m = ciBuild.match(version);
    if (!m.hasMatch())
        return QStringLiteral("V") + version;
    if (m.hasCaptured(3))
        return QStringLiteral("V%1 %2").arg(m.captured(3), m.captured(2));
    return QStringLiteral("%1 %2").arg(m.captured(1), m.captured(2));
}

} // namespace VersionText
