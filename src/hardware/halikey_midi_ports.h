#ifndef HALIKEY_MIDI_PORTS_H
#define HALIKEY_MIDI_PORTS_H

#include <QString>
#include <QStringList>

// Port-name matching for the MIDI HaliKey. Pure logic with no RtMidi dependency so it can be
// unit-tested without a MIDI subsystem, in the same spirit as halikey_edge.h.
namespace HalikeyMidiPorts {

// Index of the first port whose name CONTAINS `needle` (case-insensitive), or -1 if none does.
//
// WHY containment and not equality: RTMIDI_DO_NOT_ENSURE_UNIQUE_PORTNAMES is not defined, so the
// WinMM backend appends the port index to the name, and Windows compacts those indices whenever any
// MIDI device is removed. Matching on equality would therefore report our device as gone because an
// unrelated device was unplugged. Containment survives the renumbering.
//
// Used both to open the port and to notice it has disappeared, so the two can never disagree about
// which port is ours.
inline int findPort(const QStringList &portNames, const QString &needle) {
    if (needle.isEmpty()) {
        return -1;
    }
    for (int i = 0; i < portNames.size(); ++i) {
        if (portNames.at(i).contains(needle, Qt::CaseInsensitive)) {
            return i;
        }
    }
    return -1;
}

} // namespace HalikeyMidiPorts

#endif // HALIKEY_MIDI_PORTS_H
