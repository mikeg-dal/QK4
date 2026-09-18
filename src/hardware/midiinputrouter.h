#ifndef MIDIINPUTROUTER_H
#define MIDIINPUTROUTER_H

#include "midimapping.h"

#include <QObject>
#include <QMap>

class MidiInputRouter : public QObject {
    Q_OBJECT

public:
    explicit MidiInputRouter(QObject *parent = nullptr);

    void setMapping(const QString &sourceId, const MidiMapping::DeviceMapping &mapping);
    MidiMapping::DeviceMapping mapping(const QString &sourceId) const;
    void setLogicalInput(const QString &sourceId, MidiMapping::LogicalInput input, bool pressed);
    bool logicalInputActive(MidiMapping::LogicalInput input) const;
    void clearSourceState(const QString &sourceId);
    void removeSource(const QString &sourceId);
    void processEvent(const QString &sourceId, int status, int data1, int data2);

signals:
    void ditStateChanged(bool pressed);
    void dahStateChanged(bool pressed);
    void straightKeyStateChanged(bool pressed);
    void pttStateChanged(bool pressed);
    void knobActionRequested(const QString &action, int value, bool absolute);
    void buttonActionRequested(const QString &action);
    void macroRequested(const QString &macroId, const QString &command);

private:
    void applyInputTransitions(const QVector<MidiMapping::InputTransition> &transitions);
    void routeNote(const QString &sourceId, const MidiMapping::DeviceMapping &mapping, int note, bool pressed);

    QMap<QString, MidiMapping::DeviceMapping> m_mappings;
    QMap<QString, QMap<int, int>> m_sliderValues;
    MidiMapping::InputAggregator m_inputs;
};

#endif // MIDIINPUTROUTER_H
