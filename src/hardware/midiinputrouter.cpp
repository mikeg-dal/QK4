#include "midiinputrouter.h"

using namespace MidiMapping;

MidiInputRouter::MidiInputRouter(QObject *parent) : QObject(parent) {}

void MidiInputRouter::setMapping(const QString &sourceId, const DeviceMapping &mapping) {
    if (sourceId.isEmpty())
        return;
    applyInputTransitions(m_inputs.clearSource(sourceId));
    m_sliderValues.remove(sourceId);
    m_mappings.insert(sourceId, mapping);
}

DeviceMapping MidiInputRouter::mapping(const QString &sourceId) const {
    return m_mappings.value(sourceId);
}

void MidiInputRouter::setLogicalInput(const QString &sourceId, LogicalInput input, bool pressed) {
    if (!sourceId.isEmpty())
        applyInputTransitions(m_inputs.setInput(sourceId, input, pressed));
}

bool MidiInputRouter::logicalInputActive(LogicalInput input) const {
    return m_inputs.aggregateState(input);
}

void MidiInputRouter::clearSourceState(const QString &sourceId) {
    applyInputTransitions(m_inputs.clearSource(sourceId));
    m_sliderValues.remove(sourceId);
}

void MidiInputRouter::removeSource(const QString &sourceId) {
    m_mappings.remove(sourceId);
    clearSourceState(sourceId);
}

void MidiInputRouter::applyInputTransitions(const QVector<InputTransition> &transitions) {
    for (const InputTransition &transition : transitions) {
        switch (transition.input) {
        case LogicalInput::Dit:
            emit ditStateChanged(transition.pressed);
            break;
        case LogicalInput::Dah:
            emit dahStateChanged(transition.pressed);
            break;
        case LogicalInput::StraightKey:
            emit straightKeyStateChanged(transition.pressed);
            break;
        case LogicalInput::Ptt:
            emit pttStateChanged(transition.pressed);
            break;
        }
    }
}

void MidiInputRouter::processEvent(const QString &sourceId, int status, int data1, int data2) {
    if (!m_mappings.contains(sourceId))
        return;
    const DeviceMapping &deviceMapping = m_mappings[sourceId];
    const int kind = status & 0xf0;
    if (deviceMapping.profile == Profile::Custom && deviceMapping.cwInputEnabled) {
        const int matchKind = kind == 0x80 ? 0x90 : kind;
        const bool pressed = kind != 0x80 && data2 > 0;
        if (matchKind == deviceMapping.customDitStatus && data1 == deviceMapping.customDitData1) {
            applyInputTransitions(m_inputs.setInput(sourceId, LogicalInput::Dit, pressed));
            return;
        }
        if (matchKind == deviceMapping.customDahStatus && data1 == deviceMapping.customDahData1) {
            applyInputTransitions(m_inputs.setInput(sourceId, LogicalInput::Dah, pressed));
            return;
        }
    }
    if (kind == 0xb0) {
        const auto it = deviceMapping.knobs.constFind(data1);
        if (it == deviceMapping.knobs.cend())
            return;
        KnobValue value = interpretKnobValue(it->output, data2);
        if (value.absolute) {
            QMap<int, int> &sourceValues = m_sliderValues[sourceId];
            if (!sourceValues.contains(data1)) {
                sourceValues.insert(data1, data2);
                return;
            }
            const int previous = sourceValues.value(data1);
            sourceValues.insert(data1, data2);
            // SliderA/SliderB report an absolute controller position.  CTR2
            // uses them as rotary controls, so the position is useful only
            // for determining direction here.  The CTR2 can either repeat an
            // endpoint while the knob keeps turning or wrap 127 -> 0 / 0 ->
            // 127.  Preserve the direction across both cases so an endless
            // physical knob is not artificially limited by a MIDI slider's
            // finite value range.
            int direction = 0;
            if (data2 == previous && data2 == 127)
                direction = 1;
            else if (data2 == previous && data2 == 0)
                direction = -1;
            else if (previous >= 120 && data2 <= 7)
                direction = 1;
            else if (previous <= 7 && data2 >= 120)
                direction = -1;
            else
                direction = qBound(-1, data2 - previous, 1);

            // Do not multiply a skipped or coalesced MIDI position by the
            // radio control's step: one received movement report must produce
            // one fine adjustment.
            value.value = direction;
            value.absolute = false;
            value.valid = value.value != 0;
        }
        if (value.valid && it->action != QStringLiteral("disabled"))
            emit knobActionRequested(it->action, value.value, value.absolute);
        return;
    }
    if (kind != 0x80 && kind != 0x90)
        return;
    const bool pressed = kind == 0x90 && data2 > 0;
    routeNote(sourceId, deviceMapping, data1, pressed);
}

void MidiInputRouter::routeNote(const QString &sourceId, const DeviceMapping &mapping, int note, bool pressed) {
    // In CTR2 "MIDI Button" knob mode, CC100-107 use directional NoteOn
    // pairs 40-55 in normal BTN mode and 60-75 in Extended BTN mode. Keeping
    // the ranges mode-specific leaves extended physical-button notes 40-48
    // available to their assigned actions.
    const int firstKnobButtonNote = mapping.extendedButtons ? 60 : 40;
    const int lastKnobButtonNote = firstKnobButtonNote + 15;
    if (mapping.profile == Profile::Ctr2 && note >= firstKnobButtonNote && note <= lastKnobButtonNote) {
        const int cc = 100 + ((note - firstKnobButtonNote) / 2);
        const auto knob = mapping.knobs.constFind(cc);
        if (knob != mapping.knobs.cend() && knob->output == KnobOutput::Button) {
            if (pressed && knob->action != QStringLiteral("disabled"))
                emit knobActionRequested(knob->action, ((note - firstKnobButtonNote) % 2) == 0 ? -1 : 1, false);
            return;
        }
    }
    if (mapping.cwInputEnabled) {
        if (mapping.profile == Profile::Ctr2) {
            if (mapping.keyingMode == KeyingMode::Paddles) {
                const int left = mapping.extendedButtons ? 96 : 20;
                const int right = mapping.extendedButtons ? 97 : 21;
                const int ptt = mapping.extendedButtons ? 99 : 31;
                if (note == left || note == right) {
                    const bool physicalLeft = note == left;
                    const LogicalInput input =
                        (physicalLeft != mapping.tipRingSwapped) ? LogicalInput::Dit : LogicalInput::Dah;
                    applyInputTransitions(m_inputs.setInput(sourceId, input, pressed));
                    return;
                }
                if (note == ptt) {
                    applyInputTransitions(m_inputs.setInput(sourceId, LogicalInput::Ptt, pressed));
                    return;
                }
            } else {
                const int tip = mapping.extendedButtons ? 98 : 30;
                const int ring = mapping.extendedButtons ? 99 : 31;
                if (note == tip || note == ring) {
                    const bool physicalTip = note == tip;
                    const bool straight = physicalTip != mapping.tipRingSwapped;
                    applyInputTransitions(
                        m_inputs.setInput(sourceId, straight ? LogicalInput::StraightKey : LogicalInput::Ptt, pressed));
                    return;
                }
            }
        } else {
            const int left = 20;
            const int right = 21;
            if (mapping.keyingMode == KeyingMode::Paddles && (note == left || note == right)) {
                applyInputTransitions(
                    m_inputs.setInput(sourceId, note == left ? LogicalInput::Dit : LogicalInput::Dah, pressed));
                return;
            }
            if (mapping.keyingMode == KeyingMode::StraightKey) {
                const int straightNote = mapping.straightKeyInput == PhysicalInput::Left ? left : right;
                if (note == straightNote) {
                    applyInputTransitions(m_inputs.setInput(sourceId, LogicalInput::StraightKey, pressed));
                    return;
                }
            }
        }
    }

    // CTR2 classifies short/long presses in hardware and sends a positive
    // NoteOn on physical release. There need not be a subsequent NoteOff.
    // Keep other profiles' release handling and the CW/PTT edges above intact.
    if (mapping.profile == Profile::Ctr2 ? pressed : !pressed) {
        const auto it = mapping.buttons.constFind(note);
        if (it == mapping.buttons.cend() || it->action == QStringLiteral("disabled"))
            return;
        if (it->action == QStringLiteral("macro")) {
            const auto macro = mapping.macros.constFind(it->macroId);
            if (macro != mapping.macros.cend())
                emit macroRequested(it->macroId, macro->command);
        } else {
            emit buttonActionRequested(it->action);
        }
    }
}
