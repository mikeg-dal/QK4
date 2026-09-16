#include "network/cwmacro.h"

#include <QtGlobal>

namespace CwMacro {

QString unescape(const QString &tciText) {
    // TCI reserves ':' ',' and ';' for its own framing, so a macro carries them replaced. Undoing
    // that is the FIRST step and must stay first: '*' becomes ';' here, while |SK| becomes '*'
    // later in CatFrames. Run the other way round, a genuine SK prosign would turn into a
    // semicolon and truncate the CAT command carrying it.
    QString text = tciText;
    text.replace(QLatin1Char('^'), QLatin1Char(':'));
    text.replace(QLatin1Char('~'), QLatin1Char(','));
    text.replace(QLatin1Char('*'), QLatin1Char(';'));
    return text;
}

QVector<CwMacroSegment> parse(const QString &tciText, int baseWpm) {
    QVector<CwMacroSegment> segments;
    const QString text = unescape(tciText);

    // A radio that has not reported a speed yet leaves the snapshot at -1; keying at that would be
    // meaningless, so fall back to something sendable rather than emitting a bad KS.
    int wpm = qBound(kMinWpm, baseWpm > 0 ? baseWpm : 20, kMaxWpm);
    QString current;

    for (const QChar &ch : text) {
        if (ch == QLatin1Char('>') || ch == QLatin1Char('<')) {
            // The text so far belongs to the OLD speed, so it closes here.
            if (!current.isEmpty()) {
                segments.append({wpm, current});
                current.clear();
            }
            // Cumulative, per the spec's own example: '>' is +5 and '>>' a further +10, reading as
            // +15 overall.
            const int step = (ch == QLatin1Char('>')) ? kSpeedStepWpm : -kSpeedStepWpm;
            wpm = qBound(kMinWpm, wpm + step, kMaxWpm);
            continue; // CONSUMED - never reaches the radio. See the header.
        }
        current.append(ch);
    }

    if (!current.isEmpty()) {
        segments.append({wpm, current});
    }
    return segments;
}

} // namespace CwMacro
