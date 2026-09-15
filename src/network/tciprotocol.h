#ifndef NETWORK_TCIPROTOCOL_H
#define NETWORK_TCIPROTOCOL_H

#include <QString>
#include <QStringList>
#include <QVector>

// TCI grammar: "name[:arg1,arg2,...];". Pure text handling, no sockets and no radio.
//
// Every command is terminated by ';'. Names are case-insensitive on the wire (ExpertSDR's own
// documentation writes them uppercase, AetherSDR lowercases on parse) and always lowercase on the
// way out. Arguments split on ',' and are trimmed. There is no escaping mechanism at all, which is
// why formatting has to defend the delimiters — see message() below.
//
// See docs/tci-server-design.md for the wire contract this implements.
namespace TciProtocol {

struct Command {
    QString name;     // lowercased, never empty when valid
    QStringList args; // trimmed; empty for a bare "name;"
    bool valid = false;

    // TCI's convention: no arguments, or a bare receiver index, is a GET; two or more is a SET.
    // Commands that legitimately take one argument are the exception and must be declared, never
    // inferred — AetherSDR's global "argc >= 2" rule makes every such SET unreachable.
    int argCount() const { return args.size(); }
    QString arg(int i) const { return (i >= 0 && i < args.size()) ? args.at(i) : QString(); }
    bool argAsInt(int i, int *out) const;
    bool argAsBool(int i, bool *out) const;
    bool argAsLongLong(int i, qint64 *out) const;
};

// Incremental tokeniser. A single WebSocket text frame may hold several commands, and a command
// may be split across frames, so a trailing partial is buffered for the next feed.
class Parser {
public:
    // Returns every complete command in what has been fed so far.
    QVector<Command> feed(const QString &text);
    void reset() { m_buffer.clear(); }

    // Guards against a peer that never sends a ';'.
    static constexpr int MAX_BUFFERED_CHARS = 64 * 1024;

private:
    QString m_buffer;
};

// Parse exactly one command body (no trailing ';' required). Exposed for tests and for callers
// that already have a single command in hand.
Command parseOne(const QString &text);

// Format "name:a,b;" with every argument sanitised.
//
// WHY sanitise: a value containing ';' or ',' silently corrupts framing for every client on the
// socket, because the grammar has no escape. Ours are numeric or enum today; enforcing it centrally
// keeps that true when someone later formats a string.
QString message(const QString &name, const QStringList &args = QStringList());
QString message(const QString &name, const QString &a);
QString message(const QString &name, const QString &a, const QString &b);
QString message(const QString &name, const QString &a, const QString &b, const QString &c);

// Format without sanitising the value.
//
// WHY this exists: exactly two identity values are legitimately comma-bearing —
// "modulations_list:usb,lsb,cw,..." is a list, and "protocol:ExpertSDR3,1.5" is a two-field value.
// Scrubbing them produced "protocol:expertsdr3_1.5;" in TR4W, which is the string WSJT-X fails to
// match before it halves transmit amplitude. Never use this for a value derived from user or radio
// input.
QString messageFreeText(const QString &name, const QString &value);

// TCI booleans are the literal words, and anything else must be refused rather than coerced:
// reading "yes" as false would silently unkey a transmitter or tear down a split.
QString boolText(bool value);

// WSJT-X sends the global one-argument form "split_enable:false;". Expanding it to
// "split_enable:0,false;" at the parse boundary gives one shape downstream; without it the command
// reads as a GET for receiver -1 and is answered with silence.
Command expandGlobalForm(const Command &cmd);

} // namespace TciProtocol

#endif // NETWORK_TCIPROTOCOL_H
