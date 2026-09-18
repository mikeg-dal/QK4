#ifndef MIDIMAPPING_H
#define MIDIMAPPING_H

#include <QJsonObject>
#include <QMap>
#include <QPair>
#include <QString>
#include <QVector>

namespace MidiMapping {

constexpr int FileVersion = 2;

enum class Profile { TinyMidi = 0, HaliKey = 1, Custom = 2, Ctr2 = 3 };

enum class KeyingMode { Paddles = 0, StraightKey = 1 };

enum class PhysicalInput { Left = 0, Right = 1 };

enum class KnobOutput { WheelA = 0, WheelB = 1, WheelBReverse = 2, SliderA = 3, SliderB = 4, Button = 5 };

struct KnobBinding {
    QString action;
    KnobOutput output = KnobOutput::WheelA;

    bool operator==(const KnobBinding &other) const { return action == other.action && output == other.output; }
};

struct ButtonBinding {
    QString action;
    QString macroId;

    bool operator==(const ButtonBinding &other) const { return action == other.action && macroId == other.macroId; }
};

struct MacroDefinition {
    QString label;
    QString command;

    bool operator==(const MacroDefinition &other) const { return label == other.label && command == other.command; }
};

struct DeviceMapping {
    QString name = QStringLiteral("CTR2 Default");
    Profile profile = Profile::Ctr2;
    KeyingMode keyingMode = KeyingMode::Paddles;
    PhysicalInput straightKeyInput = PhysicalInput::Left;
    bool extendedButtons = false;
    bool tipRingSwapped = false;
    bool cwInputEnabled = true;
    int customDitStatus = 0x90;
    int customDitData1 = 20;
    int customDahStatus = 0x90;
    int customDahData1 = 21;
    QMap<int, KnobBinding> knobs;
    QMap<int, ButtonBinding> buttons;
    QMap<QString, MacroDefinition> macros;

    bool operator==(const DeviceMapping &other) const;
};

DeviceMapping ctr2Default();
DeviceMapping ctr2ExtendedDefault();
DeviceMapping withCtr2ButtonMode(const DeviceMapping &mapping, bool extendedButtons);
DeviceMapping tinyMidiDefault();
DeviceMapping haliKeyDefault();

QString knobActionLabel(const QString &action);
QString buttonActionLabel(const QString &action);
QVector<int> ctr2ButtonNotes(bool extendedButtons);
QString ctr2ButtonLabel(bool extendedButtons, int note);
QPair<int, int> ctr2KnobButtonNotes(bool extendedButtons, int cc);
QString knobOutputLabel(KnobOutput output);
QString knobOutputId(KnobOutput output);
QString knobOutputDescription(KnobOutput output);
bool knobOutputFromId(const QString &id, KnobOutput *output);

QStringList supportedKnobActions();
QStringList supportedButtonActions();
bool isSupportedKnobAction(const QString &action);
bool isSupportedButtonAction(const QString &action);
// A radio-like adjustment button can select what a knob mapped to
// "selected_adjustment" controls. An empty result means the button is not an
// adjustment selector.
QString knobActionForButtonAction(const QString &buttonAction);
bool isValidK4Command(const QString &command, QString *error = nullptr);

QJsonObject toJson(const DeviceMapping &mapping);
bool fromJson(const QJsonObject &object, DeviceMapping *mapping, QString *error = nullptr);

struct KnobValue {
    bool valid = false;
    bool absolute = false;
    int value = 0;
};

KnobValue interpretKnobValue(KnobOutput output, int midiValue);

enum class LogicalInput { Dit, Dah, StraightKey, Ptt };

struct InputTransition {
    LogicalInput input = LogicalInput::Dit;
    bool pressed = false;
};

// Tracks every physical source independently. The aggregate state changes only
// when the first source presses or the last source releases an input.
class InputAggregator {
public:
    QVector<InputTransition> setInput(const QString &sourceId, LogicalInput input, bool pressed);
    QVector<InputTransition> clearSource(const QString &sourceId);
    bool aggregateState(LogicalInput input) const;

private:
    struct SourceState {
        bool dit = false;
        bool dah = false;
        bool straight = false;
        bool ptt = false;
    };

    static bool &stateRef(SourceState &state, LogicalInput input);
    static bool stateValue(const SourceState &state, LogicalInput input);
    QMap<QString, SourceState> m_sources;
};

} // namespace MidiMapping

#endif // MIDIMAPPING_H
