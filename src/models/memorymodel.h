#ifndef MEMORYMODEL_H
#define MEMORYMODEL_H

#include <QList>
#include <QMap>
#include <QObject>
#include <QString>

// The K4's 200 general-purpose frequency memories, as reported by MCDF (R40).
//
// WIRE FORMAT. Rev D12 of the Programmer's Reference — the latest published — lists MC as
// "[Pending] / TBD", so this is measured, on a K4D at R41.FT1 on 2026-11-25
// (docs/k4-protocol-quirks.md and tools/probe_memories.py):
//
//   MCDF001,,2.8,1.5,14.227,USB,14.221,50,100,,,,,,,;
//   MCDF021,*60m Ch1,,0,5.3305,USB,,,,,0,5.332,CW,,,;
//
// A channel number, then a label, then two symmetric seven-field groups — VFO A then VFO B.
// A blank channel answers with all fifteen fields empty, which is how a blank channel is told
// apart from one holding only a label (the radio keeps those: "A label with no frequency stays
// stored"). A refusal is the command echoed with a question mark, `MCDF?;`, not an ER code.
//
// WHY the fields are held as text: only four of the seven are understood well enough to show.
// The eighth field read 100 in every one of the sixteen samples captured and nothing identifies
// it, so every field is kept exactly as the radio sent it and written back unchanged rather than
// being parsed into something that would quietly rewrite it.
struct MemoryChannel {
    // One receiver's seven fields.
    struct Side {
        QString bandwidthKhz; // filter bandwidth, e.g. "2.8"
        QString shiftKhz;     // IF shift, e.g. "1.5"
        QString frequencyMhz; // VFO frequency; empty means this receiver is unused in the channel
        QString mode;         // "USB", "CW", "DATA"...
        QString panCentreMhz; // panadapter centre
        QString spanKhz;      // panadapter span
        QString unidentified; // always "100" in every sample measured; kept verbatim

        bool isSet() const { return !frequencyMhz.isEmpty(); }
        // The frequency in Hz, or 0 when unset or unparsable.
        qint64 frequencyHz() const;
    };

    int number = 0; // 1-200
    QString label;  // empty when the channel carries no name
    Side a;
    Side b;

    // A channel the radio holds nothing for. MCDF lists only non-blank channels, but a channel
    // read by number always answers, blank or not.
    bool isBlank() const { return label.isEmpty() && !a.isSet() && !b.isSet(); }

    // What a list or a band-page cell calls this channel: its label, or "#001" when it has none.
    // The K4's own MEM page shows an unnamed channel that way.
    QString displayLabel() const;

    // The channel's fields in wire order, so a write can send back what was read.
    QStringList fields() const;
};

// Holds the channels the radio has reported and the selected channel (MC). Cleared on disconnect,
// exactly like MenuModel: the radio is the only source, and a stale channel list after a reconnect
// would describe a different radio.
class MemoryModel : public QObject {
    Q_OBJECT

public:
    explicit MemoryModel(QObject *parent = nullptr);

    static constexpr int MinChannel = 1;
    static constexpr int MaxChannel = 200;

    // Parse one MCDF response. Returns false for anything that is not a well-formed MCDF line,
    // including the `MCDF?;` refusal and an out-of-range channel number.
    bool parseMCDF(const QString &line);

    // Parse MC / MCNF / MCNU, each of which reports a selected channel. Returns false if the line
    // carries no channel number.
    bool parseSelection(const QString &line);

    void clear();

    // The channel, or nullptr when the radio has not reported it.
    const MemoryChannel *channel(int number) const;
    bool hasChannel(int number) const { return m_channels.contains(number); }

    // Reported channels that are not blank, ascending.
    QList<int> channelsInUse() const;

    // The first @p count non-blank channels whose VFO A frequency is inside a ham band (the MEM
    // page) or outside every ham band (the GEN page), ascending by channel number. The K4 fills
    // its own pages the same way, "The search starts at memory #001".
    QList<int> firstInHamBands(int count) const;
    QList<int> firstOutsideHamBands(int count) const;

    int selectedChannel() const { return m_selectedChannel; }

signals:
    void channelChanged(int number);
    void selectedChannelChanged(int number);
    void modelCleared();

private:
    QList<int> firstMatching(int count, bool inHamBand) const;

    QMap<int, MemoryChannel> m_channels;
    int m_selectedChannel = -1; // -1 until the radio reports one
};

#endif // MEMORYMODEL_H
