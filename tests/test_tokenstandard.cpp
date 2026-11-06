// The typography and colour standard (src/ui/styling/k4constants.h).
//
// Fonts come from six roles and colours from named tokens. The static_asserts in k4constants.h hold the
// roles to the floor; these tests hold the rest of src/ to the tokens, and the tokens to WCAG contrast.

#include "ui/styling/k4styles.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QTest>

namespace {

// WCAG 2.1 SC 1.4.3 (AA): text needs 4.5:1 against its background.
constexpr double kMinTextContrast = 4.5;

double luminance(const QColor &c) {
    auto channel = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) + 0.0722 * channel(c.blueF());
}

double contrast(const QColor &a, const QColor &b) {
    const double la = luminance(a);
    const double lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

// `top` drawn at `alpha` over `under`.
QColor over(const char *top, int alpha, const QColor &under) {
    const QColor t(top);
    const double a = alpha / 255.0;
    return QColor::fromRgbF(float(t.redF() * a + under.redF() * (1 - a)),
                            float(t.greenF() * a + under.greenF() * (1 - a)),
                            float(t.blueF() * a + under.blueF() * (1 - a)));
}

QColor mix(const char *a, const char *b, double t) {
    const QColor ca(a);
    const QColor cb(b);
    return QColor::fromRgbF(float(ca.redF() + (cb.redF() - ca.redF()) * t),
                            float(ca.greenF() + (cb.greenF() - ca.greenF()) * t),
                            float(ca.blueF() + (cb.blueF() - ca.blueF()) * t));
}

// Source with comments removed, string and character literals kept.
QString stripComments(const QString &src) {
    QString out;
    out.reserve(src.size());
    enum { Code, Line, Block, Str, Chr } state = Code;
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
            out += c;
            if (c == '\\') {
                out += n;
                ++i;
            } else if ((state == Str && c == '"') || (state == Chr && c == '\'')) {
                state = Code;
            }
            break;
        }
    }
    return out;
}

struct Rule {
    const char *name;
    QRegularExpression re;
};

struct Allowed {
    const char *fileSuffix;
    const char *rule;
    const char *why;
};

// Each entry excuses one rule in one file.
// Empty: nothing in src/ needs an exception today.
const std::array<Allowed, 0> kAllowed{};

bool allowed(const QString &path, const char *rule) {
    return std::any_of(std::begin(kAllowed), std::end(kAllowed), [&](const Allowed &a) {
        return path.endsWith(QLatin1String(a.fileSuffix)) && qstrcmp(a.rule, rule) == 0;
    });
}

} // namespace

class TestTokenStandard : public QObject {
    Q_OBJECT

private slots:
    // Every (text, background) pair the UI draws. Composite backgrounds are computed the way they are
    // painted, so a scrim or a translucent fill is judged by what ends up behind the glyphs.
    void textOnBackgroundMeetsAa_data() {
        using namespace K4Styles::Colors;
        QTest::addColumn<QColor>("text");
        QTest::addColumn<QColor>("background");

        // WHY 0.75 of the way from GradientTop to GradientMid1: a gradient button's glyphs span roughly
        // 0.3-0.7 of its height, and the lightest face behind them is at the top of that band.
        const QColor buttonFace = mix(GradientTop, GradientMid1, 0.75);
        const QColor white(White);
        const QColor kpaLcd = over(Black, Alpha::LcdShade, over(White, Alpha::CardTint, QColor(Background)));

        const struct {
            const char *name;
            QColor text;
            QColor background;
        } pairs[] = {
            {"TextPrimary on Background", QColor(TextPrimary), QColor(Background)},
            {"TextPrimary on DarkBackground", QColor(TextPrimary), QColor(DarkBackground)},
            {"TextPrimary on PopupBackground", QColor(TextPrimary), QColor(PopupBackground)},
            {"TextPrimary on button face", QColor(TextPrimary), buttonFace},
            // Light keys are judged at their lightest stop, not the glyph band: two-line labels reach the top
            // edge, and the pressed state reverses the gradient so the top stop sits under the second line.
            {"TextPrimary on LightGradientTop (light side-panel keys)", QColor(TextPrimary), QColor(LightGradientTop)},
            {"TextPrimary on LightGradientMid1", QColor(TextPrimary), QColor(LightGradientMid1)},
            {"TextPrimary on TxActiveFill (PTT transmitting)", QColor(TextPrimary), QColor(TxActiveFill)},
            {"TextPrimary on HoverTop", QColor(TextPrimary), QColor(HoverTop)},
            {"TextPrimary on OverlayNavButton", QColor(TextPrimary), QColor(OverlayNavButton)},
            {"TextPrimary on OverlayItemBg", QColor(TextPrimary), QColor(OverlayItemBg)},
            {"TextPrimary on SelectionDark", QColor(TextPrimary), QColor(SelectionDark)},
            {"TextPrimary on band-plan tag", QColor(TextPrimary), QColor(BandPlanTag)},
            {"TextPrimary on span-button scrim over white", QColor(TextPrimary),
             over(Black, Alpha::SpanButtonScrim, white)},
            {"TextSecondary on Background", QColor(TextSecondary), QColor(Background)},
            {"TextSecondary on BadgeOffBackground (SUB/DIV off)", QColor(TextSecondary), QColor(BadgeOffBackground)},
            {"TextSecondary on DarkBackground", QColor(TextSecondary), QColor(DarkBackground)},
            {"TextSecondary on PopupBackground", QColor(TextSecondary), QColor(PopupBackground)},
            {"TextSecondary on OverlayContentBg", QColor(TextSecondary), QColor(OverlayContentBg)},
            {"TextSecondary on OverlayItemBg", QColor(TextSecondary), QColor(OverlayItemBg)},
            {"TextSecondary on OverlayHeaderBg", QColor(TextSecondary), QColor(OverlayHeaderBg)},
            {"TextSecondary on OverlayColumnHeaderBg", QColor(TextSecondary), QColor(OverlayColumnHeaderBg)},
            {"TextSecondary on mini-pan label scrim over white", QColor(TextSecondary),
             over(Black, Alpha::LabelScrim, white)},
            {"TextSecondary on out-of-band band plan", QColor(TextSecondary),
             over(BandPlanOutOfBand, Alpha::BandPlanOutOfBand, QColor(SpectrumBackground))},
            {"TextSecondaryOnRaised on button face", QColor(TextSecondaryOnRaised), buttonFace},
            {"TextSecondaryOnRaised on OverlayNavButton", QColor(TextSecondaryOnRaised), QColor(OverlayNavButton)},
            {"TextOnLight on White", QColor(TextOnLight), white},
            {"TextOnLight on SelectedBottom", QColor(TextOnLight), QColor(SelectedBottom)},
            {"TextOnLight on SelectionLight", QColor(TextOnLight), QColor(SelectionLight)},
            {"TextOnAccent on StatusGreen", QColor(TextOnAccent), QColor(StatusGreen)},
            {"TextOnAccent on VfoACyan", QColor(TextOnAccent), QColor(VfoACyan)},
            {"TextOnAccent on OverlayBackground", QColor(TextOnAccent), QColor(OverlayBackground)},
            {"TextRed on Background", QColor(TextRed), QColor(Background)},
            {"TextRed on DarkBackground", QColor(TextRed), QColor(DarkBackground)},
            {"TextRed on ErrorBgDark", QColor(TextRed), QColor(ErrorBgDark)},
            {"AccentAmber on Background", QColor(AccentAmber), QColor(Background)},
            {"AccentAmber on DarkBackground", QColor(AccentAmber), QColor(DarkBackground)},
            {"AccentAmber on PopupBackground", QColor(AccentAmber), QColor(PopupBackground)},
            {"AccentAmber on OverlayContentBg", QColor(AccentAmber), QColor(OverlayContentBg)},
            {"StatusGreen on Background", QColor(StatusGreen), QColor(Background)},
            {"VfoACyan on Background", QColor(VfoACyan), QColor(Background)},
            {"FilterIndicatorGold on Background", QColor(FilterIndicatorGold), QColor(Background)},
            {"MeterOrange on Background (status-bar warning)", QColor(MeterOrange), QColor(Background)},
            {"TextPrimary on OverlayNavButtonPressed", QColor(TextPrimary), QColor(OverlayNavButtonPressed)},
            {"TextPrimary on DialogBorder (menu-bar item)", QColor(TextPrimary), QColor(DialogBorder)},
            {"TextPrimary on GradientBottom (list hover)", QColor(TextPrimary), QColor(GradientBottom)},
            {"DarkBackground on AccentAmber (list and combo selection)", QColor(DarkBackground), QColor(AccentAmber)},
            {"DarkBackground on VfoACyan (text-decode title bar)", QColor(DarkBackground), QColor(VfoACyan)},
            {"DarkBackground on VfoBGreen (text-decode title bar)", QColor(DarkBackground), QColor(VfoBGreen)},
            {"TextRed on KPA1500 status box", QColor(TextRed), kpaLcd},
            {"StatusGreen on KPA1500 status box", QColor(StatusGreen), kpaLcd},
            {"AccentAmber on KPA1500 status box", QColor(AccentAmber), kpaLcd},
            {"TextPrimary on KPA1500 status box", QColor(TextPrimary), kpaLcd},
        };
        for (const auto &p : pairs)
            QTest::newRow(p.name) << p.text << p.background;
    }

    void textOnBackgroundMeetsAa() {
        QFETCH(QColor, text);
        QFETCH(QColor, background);
        const double ratio = contrast(text, background);
        QVERIFY2(
            ratio >= kMinTextContrast,
            qPrintable(
                QString("%1 on %2 is %3:1, under 4.5:1").arg(text.name(), background.name()).arg(ratio, 0, 'f', 2)));
    }

    // Pairs in use that do not meet 4.5:1. Only disabled-control text belongs here. A pair that starts
    // passing should leave this list, so the test fails then too.
    void exemptPairsAreStillBelowAa_data() {
        using namespace K4Styles::Colors;
        QTest::addColumn<QColor>("text");
        QTest::addColumn<QColor>("background");
        // Disabled controls are exempt from SC 1.4.3.
        QTest::newRow("TextDisabled on Background (disabled)") << QColor(TextDisabled) << QColor(Background);
        QTest::newRow("TextDisabled on button face (disabled)")
            << QColor(TextDisabled) << mix(GradientTop, GradientMid1, 0.75);
        QTest::newRow("TextDisabled on OverlayNavButton (disabled NORM)")
            << QColor(TextDisabled) << QColor(OverlayNavButton);
    }

    void exemptPairsAreStillBelowAa() {
        QFETCH(QColor, text);
        QFETCH(QColor, background);
        QVERIFY2(contrast(text, background) < kMinTextContrast, "this pair now passes; move it to the checked list");
    }

    // TextRed exists because TxRed is too dark for small text; if the two ever converge, the
    // over-S9 marks are back to the contrast they were fixed for.
    void txRedIsNotATextColor() {
        QVERIFY(contrast(QColor(K4Styles::Colors::TxRed), QColor(K4Styles::Colors::Background)) < kMinTextContrast);
        QVERIFY(QColor(K4Styles::Colors::TextRed) != QColor(K4Styles::Colors::TxRed));
    }

    // Every colour value is written once in k4constants.h; a second name for it aliases the first.
    void noHexValueIsRepeated() {
        QFile f(QStringLiteral(QK4_SOURCE_DIR "/ui/styling/k4constants.h"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString text = stripComments(QString::fromUtf8(f.readAll()));
        const QRegularExpression def(QStringLiteral("(\\w+)\\s*=\\s*\"(#[0-9A-Fa-f]{6})\""));
        QHash<QString, QString> seen;
        QStringList repeats;
        int count = 0;
        for (auto it = def.globalMatch(text); it.hasNext();) {
            const auto m = it.next();
            ++count;
            const QString hex = m.captured(2).toUpper();
            if (seen.contains(hex))
                repeats << QString("%1 repeats %2 (%3)").arg(m.captured(1), seen[hex], hex);
            else
                seen.insert(hex, m.captured(1));
        }
        QVERIFY2(count > 40, "found too few colour definitions - has k4constants.h moved?");
        QVERIFY2(repeats.isEmpty(), qPrintable(repeats.join('\n')));
    }

    // Outside k4constants.h, sizes and colours come from the tokens. Comments are not scanned.
    void sourceUsesTokens() {
        const Rule rules[] = {
            {"setPointSize", QRegularExpression(QStringLiteral("\\bsetPointSizeF?\\("))},
            {"literal pixel size",
             QRegularExpression(QStringLiteral("\\b(setPixelSize|paintFont|dataFont)\\(\\s*\\d"))},
            {"literal style-sheet font-size", QRegularExpression(QStringLiteral("font-size:\\s*\\d"))},
            {"hex colour literal", QRegularExpression(QStringLiteral("#[0-9A-Fa-f]{3,8}\\b"))},
            // Also a named variable or member, `QColor bg(26, ...)` / `QColor m_x{0, 255, 0}`, which the
            // bare-constructor form alone let through.
            {"QColor literal", QRegularExpression(QStringLiteral("\\bQColor(::fromRgbF?)?(\\s+\\w+\\s*)?[({]\\s*\\d"))},
            {"Qt::white/black", QRegularExpression(QStringLiteral("\\bQt::(white|black)\\b"))},
            {"style-sheet rgb() literal", QRegularExpression(QStringLiteral("\\brgba?\\(\\s*\\d"))},
            {"style-sheet named colour", QRegularExpression(QStringLiteral(":\\s*(black|white)\\b"))},
        };
        QStringList offenders;
        int scanned = 0;
        QDirIterator it(QStringLiteral(QK4_SOURCE_DIR), {"*.cpp", "*.h"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            if (path.endsWith(QLatin1String("ui/styling/k4constants.h")))
                continue;
            QFile f(path);
            QVERIFY(f.open(QIODevice::ReadOnly));
            const QString raw = QString::fromUtf8(f.readAll());
            ++scanned;
            const QString code = stripComments(raw);
            const QStringList lines = code.split('\n');
            for (const Rule &rule : rules) {
                for (int i = 0; i < lines.size(); ++i) {
                    if (!lines[i].contains(rule.re) || allowed(path, rule.name))
                        continue;
                    offenders << QString("%1:%2: %3: %4")
                                     .arg(QString(path).remove(QStringLiteral(QK4_SOURCE_DIR "/")))
                                     .arg(i + 1)
                                     .arg(rule.name, lines[i].trimmed());
                }
            }
        }
        QVERIFY2(scanned > 50, "source scan found too few files - is QK4_SOURCE_DIR right?");
        QVERIFY2(offenders.isEmpty(), qPrintable("use a K4Styles token instead:\n" + offenders.join('\n')));
    }

    // The scan must still see what it forbids, or it passes by going blind.
    void sourceScanDetectsLiterals() {
        const QString code = stripComments(QStringLiteral("p.setPen(QColor(255, 0, 0)); // QColor(1\n"
                                                          "QColor bg(0x1A, 0x1A, 0x1A); QColor m_x{0, 255, 0};\n"
                                                          "s = \"color: #aabbcc; font-size: 9px;\";\n"
                                                          "/* Qt::white */ f.setPixelSize(9);"));
        QVERIFY(code.contains(QRegularExpression(QStringLiteral("\\bQColor\\(\\s*\\d"))));
        QVERIFY(code.contains(QRegularExpression(QStringLiteral("#[0-9A-Fa-f]{3,8}\\b"))));
        QVERIFY(code.contains(QRegularExpression(QStringLiteral("font-size:\\s*\\d"))));
        QVERIFY(code.contains(QRegularExpression(QStringLiteral("setPixelSize\\(\\s*\\d"))));
        QVERIFY(!code.contains(QStringLiteral("Qt::white")));
        QCOMPARE(code.count(QStringLiteral("QColor(")), 1);
        const QRegularExpression named(QStringLiteral("\\bQColor(::fromRgbF?)?(\\s+\\w+\\s*)?[({]\\s*\\d"));
        QCOMPARE(code.count(named), 3); // QColor(255, bg(0x1A, m_x{0
        QVERIFY(!QStringLiteral("QColor c(K4Styles::Colors::Background);").contains(named));
    }
};

QTEST_GUILESS_MAIN(TestTokenStandard)

#include "test_tokenstandard.moc"
