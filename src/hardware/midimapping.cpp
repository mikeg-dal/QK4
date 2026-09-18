#include "midimapping.h"

#include <QJsonArray>
#include <algorithm>

namespace MidiMapping {
namespace {

// Revision 1 maps were written before the built-in K4-Control profile was
// corrected from WheelA to SliderA for CC101-107.  This is separate from the
// file-format version: it identifies which factory defaults a saved map was
// based on without invalidating user mapping files.
constexpr int Ctr2DefaultsRevision = 3;

QString outputName(KnobOutput output) {
    switch (output) {
    case KnobOutput::WheelA:
        return QStringLiteral("wheelA");
    case KnobOutput::WheelB:
        return QStringLiteral("wheelB");
    case KnobOutput::WheelBReverse:
        return QStringLiteral("wheelB-r");
    case KnobOutput::SliderA:
        return QStringLiteral("sliderA");
    case KnobOutput::SliderB:
        return QStringLiteral("sliderB");
    case KnobOutput::Button:
        return QStringLiteral("button");
    }
    return QStringLiteral("wheelA");
}

bool parseOutput(const QString &name, KnobOutput *output) {
    static const QMap<QString, KnobOutput> values = {
        {QStringLiteral("wheelA"), KnobOutput::WheelA},          {QStringLiteral("wheelB"), KnobOutput::WheelB},
        {QStringLiteral("wheelB-r"), KnobOutput::WheelBReverse}, {QStringLiteral("sliderA"), KnobOutput::SliderA},
        {QStringLiteral("sliderB"), KnobOutput::SliderB},        {QStringLiteral("button"), KnobOutput::Button},
    };
    const auto it = values.constFind(name);
    if (it == values.cend())
        return false;
    *output = it.value();
    return true;
}

ButtonBinding action(const char *id) {
    ButtonBinding binding;
    binding.action = QString::fromLatin1(id);
    return binding;
}

KnobBinding knob(const char *id, KnobOutput output) {
    KnobBinding binding;
    binding.action = QString::fromLatin1(id);
    binding.output = output;
    return binding;
}

struct Ctr2ButtonDescriptor {
    QString buttonLabel;
    QString pressType;
    QString knobMode;
};

Ctr2ButtonDescriptor ctr2ButtonDescriptor(bool extendedButtons, int note) {
    if (!extendedButtons) {
        if (note >= 1 && note <= 6)
            return {QStringLiteral("Button %1").arg(note), QStringLiteral("short"), QStringLiteral("All knob modes")};
        if (note >= 11 && note <= 16)
            return {QStringLiteral("Button %1").arg(note - 10), QStringLiteral("long"),
                    QStringLiteral("All knob modes")};
        return {};
    }

    const bool shortPress = note >= 1 && note <= 24;
    const bool longPress = note >= 25 && note <= 48;
    if (!shortPress && !longPress)
        return {};
    const int index = shortPress ? note - 1 : note - 25;
    const int mode = index / 6;
    return {QStringLiteral("Button %1").arg(index % 6 + 1),
            shortPress ? QStringLiteral("short") : QStringLiteral("long"),
            mode == 0 ? QStringLiteral("Home") : QStringLiteral("Knob mode %1").arg(mode)};
}

struct Ctr2KnobDescriptor {
    QString controlLabel;
    QString knobMode;
    QString gesture;
};

Ctr2KnobDescriptor ctr2KnobDescriptor(int cc) {
    if (cc < 100 || cc > 107)
        return {};
    const int index = cc - 100;
    const int mode = index / 2;
    const QString knobMode = mode == 0 ? QStringLiteral("Home") : QStringLiteral("Knob mode %1").arg(mode);
    const QString gesture = (index % 2) == 0 ? QStringLiteral("turn") : QStringLiteral("push and turn");
    return {QStringLiteral("%1 %2").arg(knobMode, gesture), knobMode, gesture};
}

} // namespace

bool DeviceMapping::operator==(const DeviceMapping &other) const {
    return name == other.name && profile == other.profile && keyingMode == other.keyingMode &&
           straightKeyInput == other.straightKeyInput && extendedButtons == other.extendedButtons &&
           tipRingSwapped == other.tipRingSwapped && cwInputEnabled == other.cwInputEnabled &&
           customDitStatus == other.customDitStatus && customDitData1 == other.customDitData1 &&
           customDahStatus == other.customDahStatus && customDahData1 == other.customDahData1 && knobs == other.knobs &&
           buttons == other.buttons && macros == other.macros;
}

DeviceMapping ctr2Default() {
    DeviceMapping mapping;
    mapping.name = QStringLiteral("K4-Control Default");
    mapping.profile = Profile::Ctr2;

    // CTR2 Map 1 defines only CC100 as speed-sensitive WheelA. The other seven
    // knob modes are absolute SliderA controls. Pickup converts each changed
    // position report into one signed step and ignores the first sample and
    // duplicates, preventing jumps when modes are selected or reports skip.
    mapping.knobs.insert(100, knob("active_vfo_frequency", KnobOutput::WheelA));
    mapping.knobs.insert(101, knob("main_volume", KnobOutput::SliderA));
    mapping.knobs.insert(102, knob("other_vfo_frequency", KnobOutput::SliderA));
    mapping.knobs.insert(103, knob("filter_bandwidth", KnobOutput::SliderA));
    mapping.knobs.insert(104, knob("rit_xit_frequency", KnobOutput::SliderA));
    mapping.knobs.insert(105, knob("nr_level", KnobOutput::SliderA));
    mapping.knobs.insert(106, knob("rf_power", KnobOutput::SliderA));
    mapping.knobs.insert(107, knob("cw_speed", KnobOutput::SliderA));

    mapping.buttons.insert(1, action("mode_next"));
    mapping.buttons.insert(11, action("mode_previous"));
    mapping.buttons.insert(2, action("band_up"));
    mapping.buttons.insert(12, action("band_down"));
    mapping.buttons.insert(3, action("main_mute"));
    mapping.buttons.insert(13, action("nr_toggle"));
    mapping.buttons.insert(4, action("rit_toggle"));
    mapping.buttons.insert(14, action("split_toggle"));
    mapping.buttons.insert(5, action("pan_zoom_in"));
    mapping.buttons.insert(15, action("pan_zoom_out"));
    mapping.buttons.insert(6, action("tune_step"));
    mapping.buttons.insert(16, action("tune"));
    return mapping;
}

DeviceMapping ctr2ExtendedDefault() {
    DeviceMapping mapping = withCtr2ButtonMode(ctr2Default(), true);
    mapping.name = QStringLiteral("K4-Control Extended Default");
    return mapping;
}

QVector<int> ctr2ButtonNotes(bool extendedButtons) {
    QVector<int> notes;
    if (extendedButtons) {
        notes.reserve(48);
        for (int mode = 0; mode < 4; ++mode) {
            for (int button = 0; button < 6; ++button) {
                notes.append(mode * 6 + button + 1);  // Short press: MIDI 1-24
                notes.append(mode * 6 + button + 25); // Long press: MIDI 25-48
            }
        }
        return notes;
    }

    notes.reserve(12);
    for (int button = 1; button <= 6; ++button) {
        notes.append(button);      // Short press: MIDI 1-6
        notes.append(button + 10); // Long press: MIDI 11-16
    }
    return notes;
}

DeviceMapping withCtr2ButtonMode(const DeviceMapping &mapping, bool extendedButtons) {
    if (mapping.profile != Profile::Ctr2 || mapping.extendedButtons == extendedButtons)
        return mapping;

    DeviceMapping converted = mapping;
    converted.extendedButtons = extendedButtons;
    converted.buttons.clear();
    converted.macros.clear();

    const auto copyBinding = [&mapping, &converted, extendedButtons](int targetNote, int sourceNote) {
        ButtonBinding binding = mapping.buttons.value(sourceNote, {QStringLiteral("disabled"), QString()});
        if (binding.action == QStringLiteral("macro")) {
            const MacroDefinition definition = mapping.macros.value(binding.macroId);
            binding.macroId = QStringLiteral("button-%1").arg(targetNote);
            converted.macros.insert(binding.macroId,
                                    {ctr2ButtonLabel(extendedButtons, targetNote), definition.command});
        }
        converted.buttons.insert(targetNote, binding);
    };

    if (extendedButtons) {
        // The existing shared assignments become the Extended Home mode. The
        // three newly exposed mode banks are intentionally unassigned; silently
        // cloning twelve controls into all four banks is surprising and can
        // cause unintended radio actions.
        for (int note : ctr2ButtonNotes(true))
            converted.buttons.insert(note, action("disabled"));
        for (int button = 0; button < 6; ++button) {
            copyBinding(button + 1, button + 1);
            copyBinding(button + 25, button + 11);
        }
    } else {
        // Normal mode is shared by every knob mode, so retain the Extended Home
        // assignments when reducing the layout back to twelve controls.
        for (int button = 0; button < 6; ++button) {
            copyBinding(button + 1, button + 1);
            copyBinding(button + 11, button + 25);
        }
    }
    return converted;
}

QString ctr2ButtonLabel(bool extendedButtons, int note) {
    const Ctr2ButtonDescriptor descriptor = ctr2ButtonDescriptor(extendedButtons, note);
    if (descriptor.buttonLabel.isEmpty())
        return QString();
    if (!extendedButtons)
        return QStringLiteral("%1 %2").arg(descriptor.buttonLabel, descriptor.pressType);
    const QString modeLabel =
        descriptor.knobMode == QStringLiteral("Home") ? QStringLiteral("Home knob mode") : descriptor.knobMode;
    return QStringLiteral("%1 %2 %3").arg(modeLabel, descriptor.buttonLabel, descriptor.pressType);
}

QPair<int, int> ctr2KnobButtonNotes(bool extendedButtons, int cc) {
    if (cc < 100 || cc > 107)
        return {-1, -1};
    // The published manual documents the normal sequential pairs at 40-55.
    // Lynovation's Extended BTN clarification relocates the knob Button block
    // to 60-95 so it cannot collide with physical-button notes 1-48. The
    // eight current CC100-107 controls occupy the first eight pairs, 60-75.
    const int firstNote = extendedButtons ? 60 : 40;
    const int counterClockwise = firstNote + ((cc - 100) * 2);
    return {counterClockwise, counterClockwise + 1};
}

static void upgradeKnownCtr2Default(DeviceMapping *mapping) {
    if (!mapping || mapping->profile != Profile::Ctr2)
        return;
    const DeviceMapping corrected = mapping->extendedButtons ? ctr2ExtendedDefault() : ctr2Default();
    if (mapping->name != corrected.name)
        return;

    // Upgrade only unchanged factory knob bindings.  Buttons, macros, keying
    // choices, and customized knob actions remain exactly as the operator
    // saved them.  The old whole-map comparison incorrectly skipped this
    // correction as soon as the operator changed even one button assignment.
    for (int cc = 101; cc <= 107; ++cc) {
        auto saved = mapping->knobs.find(cc);
        const auto factory = corrected.knobs.constFind(cc);
        if (saved != mapping->knobs.end() && factory != corrected.knobs.cend() && saved->action == factory->action &&
            saved->output == KnobOutput::WheelA) {
            saved->output = KnobOutput::SliderA;
        }
    }
}

static void upgradeDuplicatedExtendedButtonBanks(DeviceMapping *mapping, int defaultsRevision) {
    if (!mapping || mapping->profile != Profile::Ctr2 || !mapping->extendedButtons || defaultsRevision >= 3)
        return;

    const auto equivalent = [mapping](int firstNote, int secondNote) {
        const ButtonBinding first = mapping->buttons.value(firstNote, {QStringLiteral("disabled"), QString()});
        const ButtonBinding second = mapping->buttons.value(secondNote, {QStringLiteral("disabled"), QString()});
        if (first.action != second.action)
            return false;
        if (first.action != QStringLiteral("macro"))
            return true;
        return mapping->macros.value(first.macroId).command == mapping->macros.value(second.macroId).command;
    };

    // The first 1.0.4.1 test candidate cloned Home into every mode when the
    // checkbox was enabled. Repair only an exact semantic four-bank clone; if
    // the operator changed even one expanded assignment, preserve the map.
    for (int mode = 1; mode < 4; ++mode) {
        for (int button = 0; button < 6; ++button) {
            if (!equivalent(button + 1, mode * 6 + button + 1) || !equivalent(button + 25, 25 + mode * 6 + button))
                return;
        }
    }

    for (int note = 7; note <= 24; ++note)
        mapping->buttons[note] = action("disabled");
    for (int note = 31; note <= 48; ++note)
        mapping->buttons[note] = action("disabled");

    QMap<QString, MacroDefinition> usedMacros;
    for (auto it = mapping->buttons.cbegin(); it != mapping->buttons.cend(); ++it) {
        if (it->action == QStringLiteral("macro") && mapping->macros.contains(it->macroId))
            usedMacros.insert(it->macroId, mapping->macros.value(it->macroId));
    }
    mapping->macros = usedMacros;
}

DeviceMapping tinyMidiDefault() {
    DeviceMapping mapping;
    mapping.name = QStringLiteral("TinyMIDI");
    mapping.profile = Profile::TinyMidi;
    return mapping;
}

DeviceMapping haliKeyDefault() {
    DeviceMapping mapping;
    mapping.name = QStringLiteral("HaliKey MIDI");
    mapping.profile = Profile::HaliKey;
    return mapping;
}

QStringList supportedKnobActions() {
    return {QStringLiteral("disabled"),
            QStringLiteral("selected_adjustment"),
            QStringLiteral("active_vfo_frequency"),
            QStringLiteral("other_vfo_frequency"),
            QStringLiteral("main_volume"),
            QStringLiteral("sub_volume"),
            QStringLiteral("rit_xit_frequency"),
            QStringLiteral("filter_bandwidth"),
            QStringLiteral("filter_shift"),
            QStringLiteral("attenuator_level"),
            QStringLiteral("noise_blanker_level"),
            QStringLiteral("nr_level"),
            QStringLiteral("manual_notch_pitch"),
            QStringLiteral("main_squelch"),
            QStringLiteral("sub_squelch"),
            QStringLiteral("main_rf_gain"),
            QStringLiteral("sub_rf_gain"),
            QStringLiteral("rf_power"),
            QStringLiteral("cw_speed"),
            QStringLiteral("pan_zoom"),
            QStringLiteral("pan_reference_level"),
            QStringLiteral("waterfall_brightness")};
}

QStringList supportedButtonActions() {
    QStringList predefined = {QStringLiteral("disabled"),
                              QStringLiteral("mode_next"),
                              QStringLiteral("mode_previous"),
                              QStringLiteral("band_up"),
                              QStringLiteral("band_down"),
                              QStringLiteral("main_mute"),
                              QStringLiteral("nr_toggle"),
                              QStringLiteral("attenuator_toggle"),
                              QStringLiteral("noise_blanker_toggle"),
                              QStringLiteral("manual_notch_toggle"),
                              QStringLiteral("khz"),
                              QStringLiteral("rit_toggle"),
                              QStringLiteral("split_toggle"),
                              QStringLiteral("tx_rx_toggle"),
                              QStringLiteral("adjust_ft8_rx_tx"),
                              QStringLiteral("set_ft8_frequency"),
                              QStringLiteral("pan_zoom_in"),
                              QStringLiteral("pan_zoom_out"),
                              QStringLiteral("tune_step"),
                              QStringLiteral("tune")};
    QStringList adjustments = {QStringLiteral("adjust_active_vfo_frequency"),
                               QStringLiteral("adjust_other_vfo_frequency"),
                               QStringLiteral("adjust_main_volume"),
                               QStringLiteral("adjust_sub_volume"),
                               QStringLiteral("adjust_rit_xit_frequency"),
                               QStringLiteral("adjust_filter_bandwidth"),
                               QStringLiteral("adjust_filter_shift"),
                               QStringLiteral("adjust_attenuator_level"),
                               QStringLiteral("adjust_noise_blanker_level"),
                               QStringLiteral("adjust_nr_level"),
                               QStringLiteral("adjust_manual_notch_pitch"),
                               QStringLiteral("adjust_main_squelch"),
                               QStringLiteral("adjust_sub_squelch"),
                               QStringLiteral("adjust_main_rf_gain"),
                               QStringLiteral("adjust_sub_rf_gain"),
                               QStringLiteral("adjust_rf_power"),
                               QStringLiteral("adjust_cw_speed"),
                               QStringLiteral("adjust_pan_zoom"),
                               QStringLiteral("adjust_pan_reference_level"),
                               QStringLiteral("adjust_waterfall_brightness")};

    const auto byLabel = [](const QString &left, const QString &right) {
        return QString::compare(buttonActionLabel(left), buttonActionLabel(right), Qt::CaseInsensitive) < 0;
    };
    std::sort(predefined.begin(), predefined.end(), byLabel);
    std::sort(adjustments.begin(), adjustments.end(), byLabel);

    QStringList ordered{QStringLiteral("macro")};
    ordered.append(predefined);
    ordered.append(adjustments);
    return ordered;
}

bool isSupportedKnobAction(const QString &actionId) {
    return supportedKnobActions().contains(actionId);
}

bool isSupportedButtonAction(const QString &actionId) {
    return supportedButtonActions().contains(actionId);
}

QString knobActionForButtonAction(const QString &buttonAction) {
    static const QMap<QString, QString> actions = {
        {QStringLiteral("adjust_active_vfo_frequency"), QStringLiteral("active_vfo_frequency")},
        {QStringLiteral("adjust_other_vfo_frequency"), QStringLiteral("other_vfo_frequency")},
        {QStringLiteral("adjust_main_volume"), QStringLiteral("main_volume")},
        {QStringLiteral("adjust_sub_volume"), QStringLiteral("sub_volume")},
        {QStringLiteral("adjust_rit_xit_frequency"), QStringLiteral("rit_xit_frequency")},
        {QStringLiteral("adjust_filter_bandwidth"), QStringLiteral("filter_bandwidth")},
        {QStringLiteral("adjust_filter_shift"), QStringLiteral("filter_shift")},
        {QStringLiteral("adjust_attenuator_level"), QStringLiteral("attenuator_level")},
        {QStringLiteral("adjust_noise_blanker_level"), QStringLiteral("noise_blanker_level")},
        {QStringLiteral("adjust_nr_level"), QStringLiteral("nr_level")},
        {QStringLiteral("adjust_manual_notch_pitch"), QStringLiteral("manual_notch_pitch")},
        {QStringLiteral("adjust_main_squelch"), QStringLiteral("main_squelch")},
        {QStringLiteral("adjust_sub_squelch"), QStringLiteral("sub_squelch")},
        {QStringLiteral("adjust_main_rf_gain"), QStringLiteral("main_rf_gain")},
        {QStringLiteral("adjust_sub_rf_gain"), QStringLiteral("sub_rf_gain")},
        {QStringLiteral("adjust_rf_power"), QStringLiteral("rf_power")},
        {QStringLiteral("adjust_cw_speed"), QStringLiteral("cw_speed")},
        {QStringLiteral("adjust_pan_zoom"), QStringLiteral("pan_zoom")},
        {QStringLiteral("adjust_pan_reference_level"), QStringLiteral("pan_reference_level")},
        {QStringLiteral("adjust_waterfall_brightness"), QStringLiteral("waterfall_brightness")},
    };
    return actions.value(buttonAction);
}

bool isValidK4Command(const QString &command, QString *error) {
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (command.trimmed().isEmpty())
        return fail(QStringLiteral("K4 command is empty"));
    if (command != command.trimmed())
        return fail(QStringLiteral("K4 command cannot start or end with whitespace"));
    if (command.size() > 4096)
        return fail(QStringLiteral("K4 command is too long"));
    if (!command.endsWith(QLatin1Char(';')))
        return fail(QStringLiteral("K4 command must end with a semicolon"));
    for (const QChar character : command) {
        const ushort value = character.unicode();
        if (value < 0x20 || value > 0x7e)
            return fail(QStringLiteral("K4 command must contain printable ASCII only"));
    }
    if (error)
        error->clear();
    return true;
}

QString knobActionLabel(const QString &actionId) {
    static const QMap<QString, QString> labels = {{"disabled", "Disabled"},
                                                  {"selected_adjustment", "Selected adjustment (button)"},
                                                  {"active_vfo_frequency", "Active VFO frequency"},
                                                  {"other_vfo_frequency", "Other VFO frequency"},
                                                  {"main_volume", "Main volume"},
                                                  {"sub_volume", "Sub volume"},
                                                  {"rit_xit_frequency", "RIT/XIT frequency"},
                                                  {"filter_bandwidth", "Filter bandwidth"},
                                                  {"filter_shift", "Filter shift"},
                                                  {"attenuator_level", "Attenuator level"},
                                                  {"noise_blanker_level", "Noise blanker level"},
                                                  {"nr_level", "Noise reduction level"},
                                                  {"manual_notch_pitch", "Manual notch pitch"},
                                                  {"main_squelch", "Main squelch"},
                                                  {"sub_squelch", "Sub squelch"},
                                                  {"main_rf_gain", "Main RF gain"},
                                                  {"sub_rf_gain", "Sub RF gain"},
                                                  {"rf_power", "RF power"},
                                                  {"cw_speed", "CW speed"},
                                                  {"pan_zoom", "Panadapter zoom"},
                                                  {"pan_reference_level", "Panadapter reference"},
                                                  {"waterfall_brightness", "Waterfall brightness"}};
    return labels.value(actionId, actionId);
}

QString buttonActionLabel(const QString &actionId) {
    static const QMap<QString, QString> labels = {{"disabled", "Disabled"},
                                                  {"macro", "Custom Command"},
                                                  {"mode_next", "Mode next"},
                                                  {"mode_previous", "Mode previous"},
                                                  {"band_up", "Band up"},
                                                  {"band_down", "Band down"},
                                                  {"main_mute", "Main mute"},
                                                  {"nr_toggle", "NR toggle"},
                                                  {"attenuator_toggle", "Attenuator toggle"},
                                                  {"noise_blanker_toggle", "Noise blanker toggle"},
                                                  {"manual_notch_toggle", "Manual notch toggle"},
                                                  {"rit_toggle", "RIT toggle"},
                                                  {"split_toggle", "Split toggle"},
                                                  {"tx_rx_toggle", "TX/RX toggle"},
                                                  {"adjust_ft8_rx_tx", "FT8/FT4: Switch RX/TX tone"},
                                                  {"set_ft8_frequency", "FT8/FT4: Set tone frequency"},
                                                  {"pan_zoom_in", "Pan zoom in"},
                                                  {"pan_zoom_out", "Pan zoom out"},
                                                  {"tune_step", "Rate"},
                                                  {"khz", "KHZ"},
                                                  {"tune", "TUNE"},
                                                  {"adjust_active_vfo_frequency", "Adjust: Active VFO frequency"},
                                                  {"adjust_other_vfo_frequency", "Adjust: Other VFO frequency"},
                                                  {"adjust_main_volume", "Adjust: Main volume"},
                                                  {"adjust_sub_volume", "Adjust: Sub volume"},
                                                  {"adjust_rit_xit_frequency", "Adjust: RIT/XIT frequency"},
                                                  {"adjust_filter_bandwidth", "Adjust: Filter bandwidth"},
                                                  {"adjust_filter_shift", "Adjust: Filter shift"},
                                                  {"adjust_attenuator_level", "Adjust: Attenuator"},
                                                  {"adjust_noise_blanker_level", "Adjust: Noise blanker"},
                                                  {"adjust_nr_level", "Adjust: Noise reduction"},
                                                  {"adjust_manual_notch_pitch", "Adjust: Manual notch"},
                                                  {"adjust_main_squelch", "Adjust: Main squelch"},
                                                  {"adjust_sub_squelch", "Adjust: Sub squelch"},
                                                  {"adjust_main_rf_gain", "Adjust: Main RF gain"},
                                                  {"adjust_sub_rf_gain", "Adjust: Sub RF gain"},
                                                  {"adjust_rf_power", "Adjust: RF power"},
                                                  {"adjust_cw_speed", "Adjust: CW speed"},
                                                  {"adjust_pan_zoom", "Adjust: Panadapter zoom"},
                                                  {"adjust_pan_reference_level", "Adjust: Panadapter reference"},
                                                  {"adjust_waterfall_brightness", "Adjust: Waterfall brightness"}};
    return labels.value(actionId, actionId);
}

QString knobOutputLabel(KnobOutput output) {
    switch (output) {
    case KnobOutput::WheelA:
        return QStringLiteral("Wheel A (relative)");
    case KnobOutput::WheelB:
        return QStringLiteral("Wheel B (relative)");
    case KnobOutput::WheelBReverse:
        return QStringLiteral("Wheel B reversed");
    case KnobOutput::SliderA:
        return QStringLiteral("Slider A (pickup)");
    case KnobOutput::SliderB:
        return QStringLiteral("Slider B (pickup)");
    case KnobOutput::Button:
        return QStringLiteral("Button direction pair");
    }
    return QString();
}

QString knobOutputId(KnobOutput output) {
    return outputName(output);
}

QString knobOutputDescription(KnobOutput output) {
    switch (output) {
    case KnobOutput::WheelA:
        return QStringLiteral("Relative CC centered on 64: values above 64 are positive, values below 64 are negative, "
                              "and the distance from 64 preserves acceleration. Use when that CTR2 knob mode is "
                              "configured as Wheel A. Map 1 uses this for CC100.");
    case KnobOutput::WheelB:
        return QStringLiteral("Relative CC direction: value 1 is a positive step and value 126 is a negative step. Use "
                              "when that CTR2 knob mode is configured as Wheel B.");
    case KnobOutput::WheelBReverse:
        return QStringLiteral(
            "Reversed Wheel B direction: value 1 is a negative step and value 126 is a positive step.");
    case KnobOutput::SliderA:
        return QStringLiteral("Absolute CC values 0 through 127 from a CTR2 Slider A output. The first report "
                              "establishes position; later movement produces fine signed steps, including across 0/127 "
                              "wrap. Map 1 uses this for CC101 through CC107.");
    case KnobOutput::SliderB:
        return QStringLiteral("Absolute CC values 0 through 127 from a CTR2 Slider B output. It is decoded like "
                              "sliderA; use it when that CTR2 knob mode is configured as Slider B.");
    case KnobOutput::Button:
        return QStringLiteral("Directional NoteOn pair from a CTR2 MIDI Button output, not a CC value. In normal mode "
                              "CC100 uses notes 40/41 through CC107 at 54/55. Extended Button Mode relocates those "
                              "eight pairs to 60/61 through 74/75 within the manufacturer-defined 60-95 knob Button "
                              "range. The first note is counter-clockwise and the second is clockwise.");
    }
    return QString();
}

bool knobOutputFromId(const QString &id, KnobOutput *output) {
    return parseOutput(id, output);
}

QJsonObject toJson(const DeviceMapping &mapping) {
    QJsonObject root;
    QJsonArray comments;
    comments.append(QStringLiteral("This is a user-editable QK4 CTR2 mapping. Action keywords are case-sensitive."));
    comments.append(
        QStringLiteral("Top-level names beginning with an underscore are documentation only. QK4 ignores those "
                       "sections when loading the mapping; actual assignments are in buttons and knobs."));
    comments.append(QStringLiteral("For a predefined button function, copy a keyword from _buttonActions into the "
                                   "button's action field and remove its macro field."));
    comments.append(
        QStringLiteral("For a custom K4 programmer command, use action \"macro\", set the button's macro field to an "
                       "id, and define that id in macros. Commands must end with a semicolon."));
    comments.append(
        QStringLiteral("The _buttonActions reference includes immediate button functions and adjust_* selection "
                       "functions. An adjust_* button selects what a knob mapped to selected_adjustment will control; "
                       "the button does not perform the continuous adjustment by itself. See _buttonActionGuide."));
    comments.append(QStringLiteral("Set buttonMode to \"normal\" or \"extended\" to match the Extended BTN setting in "
                                   "CTR2-MIDI. QK4 cannot detect that device setting automatically."));
    comments.append(QStringLiteral("Each buttons entry identifies its physical button, MIDI note, press type, knob "
                                   "mode, and mapped action or macro. buttonLabel, pressType, and knobMode are "
                                   "explanatory fields; QK4 derives the control from note when loading."));
    comments.append(QStringLiteral(
        "A knob's output describes the MIDI messages emitted by that CTR2 knob mode; it does not select the radio "
        "action. The output keyword must match the output configured on the CTR2. See _knobOutputs."));
    comments.append(QStringLiteral("Loading replaces the complete CTR2 mapping; mappings are never merged."));
    root.insert(QStringLiteral("_comments"), comments);

    QJsonArray buttonModeGuide;
    buttonModeGuide.append(QStringLiteral("normal: the same 12 functions are used in every knob mode. Short presses "
                                          "are MIDI notes 1-6; long presses are notes 11-16."));
    buttonModeGuide.append(QStringLiteral(
        "extended: each device knob mode has its own 12 functions. Home uses short notes 1-6 and long notes 25-30; "
        "Knob mode 1 uses 7-12 and 31-36; Knob mode 2 uses 13-18 and 37-42; Knob mode 3 uses 19-24 and 43-48."));
    buttonModeGuide.append(QStringLiteral(
        "When extended mode is first enabled in QK4, the 12 normal assignments become the Home assignments. The 36 "
        "newly exposed Knob mode 1-3 assignments start disabled; they are not copies of Home."));
    buttonModeGuide.append(
        QStringLiteral("When a knob control uses MIDI Button output, normal mode uses directional notes 40-55. "
                       "Extended mode relocates CC100-107 to notes 60-75 within the manufacturer-defined 60-95 range "
                       "so notes 40-48 remain available to physical buttons."));
    buttonModeGuide.append(QStringLiteral("Every listed button can use a predefined action keyword or action \"macro\" "
                                          "with a supported K4 Programmer's Reference command."));
    root.insert(QStringLiteral("_buttonModeGuide"), buttonModeGuide);

    QJsonArray buttonActionGuide;
    buttonActionGuide.append(QStringLiteral(
        "_buttonActions is a reference list, divided into immediateActions and adjustmentSelectors. Copy a keyword "
        "into an entry in the actual buttons array; the reference list does not assign any controls."));
    buttonActionGuide.append(QStringLiteral("Immediate button actions, such as band_up, nr_toggle, and tx_rx_toggle, "
                                            "perform their function as soon as the mapped button is released."));
    buttonActionGuide.append(QStringLiteral(
        "Actions beginning with adjust_, such as adjust_nr_level, are adjustment selectors. Pressing that button "
        "selects a function, opens the related QK4 control or feedback where available, and waits for knob movement."));
    buttonActionGuide.append(
        QStringLiteral("To use an adjust_* button, assign selected_adjustment to one entry in the actual knobs array. "
                       "That knob then controls whichever adjust_* button was pressed most recently."));
    buttonActionGuide.append(
        QStringLiteral("Example: assign adjust_nr_level to a button and selected_adjustment to CC100. Press the button "
                       "to select NR and open its control, then turn CC100 to change the NR level."));
    buttonActionGuide.append(
        QStringLiteral("The button and knob entries are independent entries in separate arrays. They do not need to be "
                       "adjacent or appear in any particular order; QK4 links them by the adjust_* and "
                       "selected_adjustment action types at runtime."));
    buttonActionGuide.append(
        QStringLiteral("For a knob that should always control one function without a selection button, copy a direct "
                       "keyword such as nr_level from _knobActions into that knob's action field instead."));
    root.insert(QStringLiteral("_buttonActionGuide"), buttonActionGuide);

    QJsonObject selectedAdjustmentExample;
    selectedAdjustmentExample.insert(
        QStringLiteral("purpose"),
        QStringLiteral("Button 3 selects NR; the Home knob then adjusts the selected function."));
    selectedAdjustmentExample.insert(QStringLiteral("pairingRule"),
                                     QStringLiteral("No note-to-CC pairing exists. Any button using an adjust_* action "
                                                    "selects the function for any knob using selected_adjustment."));
    selectedAdjustmentExample.insert(QStringLiteral("ordering"),
                                     QStringLiteral("The entries belong in separate buttons and knobs arrays. Their "
                                                    "order and physical proximity in this file do not matter."));
    selectedAdjustmentExample.insert(
        QStringLiteral("buttonArrayEntry"),
        QJsonObject{{QStringLiteral("note"), 3}, {QStringLiteral("action"), QStringLiteral("adjust_nr_level")}});
    selectedAdjustmentExample.insert(QStringLiteral("knobArrayEntry"),
                                     QJsonObject{{QStringLiteral("cc"), 100},
                                                 {QStringLiteral("action"), QStringLiteral("selected_adjustment")},
                                                 {QStringLiteral("output"), QStringLiteral("wheelA")}});
    selectedAdjustmentExample.insert(
        QStringLiteral("operatorSequence"),
        QJsonArray{
            QStringLiteral("Press Button 3 to select NR and open its QK4 control."),
            QStringLiteral("Turn the Home knob (CC100) to adjust NR."),
            QStringLiteral("Press another adjust_* button to make that same knob control a different function.")});
    root.insert(QStringLiteral("_selectedAdjustmentExample"), selectedAdjustmentExample);

    QJsonArray knobOutputGuide;
    knobOutputGuide.append(
        QStringLiteral("Stock K4-Control Map 1: leave CC100 set to wheelA and CC101 through CC107 set to sliderA. No "
                       "output selection is required unless you reprogram those modes in CTR2-MIDI."));
    knobOutputGuide.append(QStringLiteral(
        "To choose an output, inspect that knob mode in the CTR2-MIDI map/setup and copy its output type here: Wheel A "
        "= wheelA, Wheel B = wheelB, Slider A = sliderA, Slider B = sliderB, or MIDI Button = button."));
    knobOutputGuide.append(
        QStringLiteral("Use wheelA when the CTR2 mode emits relative values centered on 64. It is the best choice for "
                       "VFO tuning or another action where faster turns should produce larger changes, but only when "
                       "the CTR2 mode itself is configured as Wheel A."));
    knobOutputGuide.append(QStringLiteral(
        "Use wheelB when the CTR2 mode emits relative direction values 1 and 126. It produces one fine step per "
        "report. Use wheelB-r instead only when Wheel B moves the selected QK4 action in the wrong direction."));
    knobOutputGuide.append(QStringLiteral(
        "Use sliderA or sliderB when the CTR2 mode emits absolute values from 0 through 127. QK4 converts changes in "
        "those values into fine directional steps; it does not jump the radio control to an absolute position."));
    knobOutputGuide.append(
        QStringLiteral("Do not choose slider merely because the QK4 control is drawn as a slider, and do not choose "
                       "wheel merely because the CTR2 has a physical knob. The MIDI message format configured in "
                       "CTR2-MIDI is what determines this field."));
    root.insert(QStringLiteral("_knobOutputGuide"), knobOutputGuide);

    QJsonObject immediateButtonActions;
    QJsonObject adjustmentSelectorButtonActions;
    for (const QString &action : supportedButtonActions()) {
        if (action == QStringLiteral("macro"))
            continue;
        QJsonObject &group =
            action.startsWith(QStringLiteral("adjust_")) ? adjustmentSelectorButtonActions : immediateButtonActions;
        group.insert(action, buttonActionLabel(action));
    }
    QJsonObject buttonActionReference;
    buttonActionReference.insert(QStringLiteral("immediateActions"), immediateButtonActions);
    buttonActionReference.insert(QStringLiteral("adjustmentSelectors"), adjustmentSelectorButtonActions);
    root.insert(QStringLiteral("_buttonActions"), buttonActionReference);

    QJsonObject knobActionReference;
    for (const QString &action : supportedKnobActions())
        knobActionReference.insert(action, knobActionLabel(action));
    root.insert(QStringLiteral("_knobActions"), knobActionReference);

    QJsonObject knobOutputReference;
    for (int value = static_cast<int>(KnobOutput::WheelA); value <= static_cast<int>(KnobOutput::Button); ++value) {
        const auto output = static_cast<KnobOutput>(value);
        knobOutputReference.insert(knobOutputId(output), knobOutputDescription(output));
    }
    root.insert(QStringLiteral("_knobOutputs"), knobOutputReference);

    root.insert(QStringLiteral("format"), QStringLiteral("qk4-ctr2-midi-mapping"));
    root.insert(QStringLiteral("version"), FileVersion);
    root.insert(QStringLiteral("defaultsRevision"), Ctr2DefaultsRevision);
    root.insert(QStringLiteral("name"), mapping.name);
    root.insert(QStringLiteral("profile"), static_cast<int>(mapping.profile));
    root.insert(QStringLiteral("keyingMode"), static_cast<int>(mapping.keyingMode));
    root.insert(QStringLiteral("straightKeyInput"), static_cast<int>(mapping.straightKeyInput));
    root.insert(QStringLiteral("buttonMode"),
                mapping.extendedButtons ? QStringLiteral("extended") : QStringLiteral("normal"));
    root.insert(QStringLiteral("tipRingSwapped"), mapping.tipRingSwapped);
    root.insert(QStringLiteral("cwInputEnabled"), mapping.cwInputEnabled);
    if (mapping.profile == Profile::Custom) {
        root.insert(QStringLiteral("customDitStatus"), mapping.customDitStatus);
        root.insert(QStringLiteral("customDitData1"), mapping.customDitData1);
        root.insert(QStringLiteral("customDahStatus"), mapping.customDahStatus);
        root.insert(QStringLiteral("customDahData1"), mapping.customDahData1);
    }

    QJsonArray knobs;
    for (auto it = mapping.knobs.cbegin(); it != mapping.knobs.cend(); ++it) {
        QJsonObject entry;
        const Ctr2KnobDescriptor descriptor = ctr2KnobDescriptor(it.key());
        if (!descriptor.controlLabel.isEmpty()) {
            entry.insert(QStringLiteral("controlLabel"), descriptor.controlLabel);
            entry.insert(QStringLiteral("knobMode"), descriptor.knobMode);
            entry.insert(QStringLiteral("gesture"), descriptor.gesture);
            const QPair<int, int> normalNotes = ctr2KnobButtonNotes(false, it.key());
            const QPair<int, int> extendedNotes = ctr2KnobButtonNotes(true, it.key());
            QJsonObject normalPair;
            normalPair.insert(QStringLiteral("counterClockwise"), normalNotes.first);
            normalPair.insert(QStringLiteral("clockwise"), normalNotes.second);
            QJsonObject extendedPair;
            extendedPair.insert(QStringLiteral("counterClockwise"), extendedNotes.first);
            extendedPair.insert(QStringLiteral("clockwise"), extendedNotes.second);
            QJsonObject buttonOutputNotes;
            buttonOutputNotes.insert(QStringLiteral("normal"), normalPair);
            buttonOutputNotes.insert(QStringLiteral("extended"), extendedPair);
            entry.insert(QStringLiteral("buttonOutputNotes"), buttonOutputNotes);
        }
        entry.insert(QStringLiteral("cc"), it.key());
        entry.insert(QStringLiteral("action"), it->action);
        entry.insert(QStringLiteral("output"), outputName(it->output));
        knobs.append(entry);
    }
    root.insert(QStringLiteral("knobs"), knobs);

    QJsonArray buttons;
    for (auto it = mapping.buttons.cbegin(); it != mapping.buttons.cend(); ++it) {
        QJsonObject entry;
        const Ctr2ButtonDescriptor descriptor = ctr2ButtonDescriptor(mapping.extendedButtons, it.key());
        if (!descriptor.buttonLabel.isEmpty()) {
            entry.insert(QStringLiteral("buttonLabel"), descriptor.buttonLabel);
            entry.insert(QStringLiteral("pressType"), descriptor.pressType);
            entry.insert(QStringLiteral("knobMode"), descriptor.knobMode);
        }
        entry.insert(QStringLiteral("note"), it.key());
        entry.insert(QStringLiteral("action"), it->action);
        if (!it->macroId.isEmpty())
            entry.insert(QStringLiteral("macro"), it->macroId);
        buttons.append(entry);
    }
    root.insert(QStringLiteral("buttons"), buttons);

    QJsonArray macros;
    for (auto it = mapping.macros.cbegin(); it != mapping.macros.cend(); ++it) {
        QJsonObject entry;
        entry.insert(QStringLiteral("id"), it.key());
        entry.insert(QStringLiteral("label"), it->label);
        entry.insert(QStringLiteral("command"), it->command);
        macros.append(entry);
    }
    root.insert(QStringLiteral("macros"), macros);
    return root;
}

bool fromJson(const QJsonObject &root, DeviceMapping *mapping, QString *error) {
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (!mapping)
        return fail(QStringLiteral("No destination mapping was provided"));
    if (root.value(QStringLiteral("format")).toString() != QStringLiteral("qk4-ctr2-midi-mapping"))
        return fail(QStringLiteral("Not a QK4 CTR2 mapping file"));
    const int fileVersion = root.value(QStringLiteral("version")).toInt(-1);
    if (fileVersion < 1 || fileVersion > FileVersion)
        return fail(QStringLiteral("Unsupported CTR2 mapping version"));

    DeviceMapping parsed;
    parsed.name = root.value(QStringLiteral("name")).toString(QStringLiteral("Imported Mapping"));
    const int profile = root.value(QStringLiteral("profile")).toInt(static_cast<int>(Profile::Ctr2));
    if (profile < static_cast<int>(Profile::TinyMidi) || profile > static_cast<int>(Profile::Ctr2))
        return fail(QStringLiteral("Invalid MIDI profile"));
    parsed.profile = static_cast<Profile>(profile);
    parsed.keyingMode =
        root.value(QStringLiteral("keyingMode")).toInt() == 1 ? KeyingMode::StraightKey : KeyingMode::Paddles;
    parsed.straightKeyInput =
        root.value(QStringLiteral("straightKeyInput")).toInt() == 1 ? PhysicalInput::Right : PhysicalInput::Left;
    const QJsonValue buttonMode = root.value(QStringLiteral("buttonMode"));
    if (buttonMode.isUndefined()) {
        // v1 files written before buttonMode used this boolean. Keep them loadable.
        if (fileVersion >= 2)
            return fail(QStringLiteral("CTR2 mapping is missing buttonMode"));
        parsed.extendedButtons = root.value(QStringLiteral("extendedButtons")).toBool(false);
    } else if (!buttonMode.isString() || (buttonMode.toString() != QStringLiteral("normal") &&
                                          buttonMode.toString() != QStringLiteral("extended"))) {
        return fail(QStringLiteral("Invalid CTR2 button mode; use normal or extended"));
    } else {
        parsed.extendedButtons = buttonMode.toString() == QStringLiteral("extended");
    }
    parsed.tipRingSwapped = root.value(QStringLiteral("tipRingSwapped")).toBool(false);
    parsed.cwInputEnabled = root.value(QStringLiteral("cwInputEnabled")).toBool(true);
    parsed.customDitStatus = root.value(QStringLiteral("customDitStatus")).toInt(0x90) & 0xf0;
    parsed.customDitData1 = qBound(0, root.value(QStringLiteral("customDitData1")).toInt(20), 127);
    parsed.customDahStatus = root.value(QStringLiteral("customDahStatus")).toInt(0x90) & 0xf0;
    parsed.customDahData1 = qBound(0, root.value(QStringLiteral("customDahData1")).toInt(21), 127);

    for (const QJsonValue &value : root.value(QStringLiteral("knobs")).toArray()) {
        const QJsonObject entry = value.toObject();
        const int cc = entry.value(QStringLiteral("cc")).toInt(-1);
        const QString actionId = entry.value(QStringLiteral("action")).toString();
        KnobOutput output;
        if (cc < 0 || cc > 127 || !isSupportedKnobAction(actionId) ||
            !parseOutput(entry.value(QStringLiteral("output")).toString(), &output))
            return fail(QStringLiteral("Invalid knob mapping"));
        parsed.knobs.insert(cc, KnobBinding{actionId, output});
    }

    const QVector<int> validCtr2ButtonNotes =
        parsed.profile == Profile::Ctr2 ? ctr2ButtonNotes(parsed.extendedButtons) : QVector<int>();
    for (const QJsonValue &value : root.value(QStringLiteral("buttons")).toArray()) {
        const QJsonObject entry = value.toObject();
        const int note = entry.value(QStringLiteral("note")).toInt(-1);
        QString actionId = entry.value(QStringLiteral("action")).toString();
        // Migrate retired tone selectors into the two-action workflow. CTR2
        // short presses select RX/TX; long presses apply the preview.
        if (actionId == QStringLiteral("ft8_rx") || actionId == QStringLiteral("ft8_tx")) {
            const bool longPress =
                parsed.profile == Profile::Ctr2 &&
                ctr2ButtonDescriptor(parsed.extendedButtons, note).pressType == QStringLiteral("long");
            actionId = longPress ? QStringLiteral("set_ft8_frequency") : QStringLiteral("adjust_ft8_rx_tx");
        }
        const QString macroId = entry.value(QStringLiteral("macro")).toString();
        if (note < 0 || note > 127 || !isSupportedButtonAction(actionId) ||
            (actionId == QStringLiteral("macro") && macroId.isEmpty()))
            return fail(QStringLiteral("Invalid button mapping"));
        if (parsed.profile == Profile::Ctr2 && !validCtr2ButtonNotes.contains(note)) {
            return fail(QStringLiteral("MIDI note %1 is not valid in CTR2 %2 button mode")
                            .arg(note)
                            .arg(parsed.extendedButtons ? QStringLiteral("extended") : QStringLiteral("normal")));
        }
        parsed.buttons.insert(note, ButtonBinding{actionId, macroId});
    }

    for (const QJsonValue &value : root.value(QStringLiteral("macros")).toArray()) {
        const QJsonObject entry = value.toObject();
        const QString id = entry.value(QStringLiteral("id")).toString();
        const QString command = entry.value(QStringLiteral("command")).toString();
        QString commandError;
        if (id.isEmpty() || !isValidK4Command(command, &commandError))
            return fail(commandError.isEmpty() ? QStringLiteral("Invalid macro definition") : commandError);
        parsed.macros.insert(id, MacroDefinition{entry.value(QStringLiteral("label")).toString(), command});
    }
    for (auto it = parsed.buttons.cbegin(); it != parsed.buttons.cend(); ++it) {
        if (it->action == QStringLiteral("macro") && !parsed.macros.contains(it->macroId))
            return fail(QStringLiteral("Button references a missing macro"));
    }

    // A legacy built-in map may contain customized buttons or macros while its
    // unchanged factory knob bindings still need the SliderA correction.
    // Current-revision maps preserve an explicit operator choice of WheelA.
    const int defaultsRevision = root.value(QStringLiteral("defaultsRevision")).toInt(0);
    upgradeDuplicatedExtendedButtonBanks(&parsed, defaultsRevision);
    if (defaultsRevision < Ctr2DefaultsRevision)
        upgradeKnownCtr2Default(&parsed);
    *mapping = parsed;
    if (error)
        error->clear();
    return true;
}

KnobValue interpretKnobValue(KnobOutput output, int midiValue) {
    KnobValue result;
    if (midiValue < 0 || midiValue > 127)
        return result;
    switch (output) {
    case KnobOutput::WheelA:
        result.valid = midiValue != 64;
        result.value = midiValue - 64;
        break;
    case KnobOutput::WheelB:
        result.valid = midiValue == 1 || midiValue == 126;
        result.value = midiValue == 1 ? 1 : -1;
        break;
    case KnobOutput::WheelBReverse:
        result.valid = midiValue == 1 || midiValue == 126;
        result.value = midiValue == 1 ? -1 : 1;
        break;
    case KnobOutput::SliderA:
    case KnobOutput::SliderB:
        result.valid = true;
        result.absolute = true;
        result.value = midiValue;
        break;
    case KnobOutput::Button:
        break;
    }
    return result;
}

bool &InputAggregator::stateRef(SourceState &state, LogicalInput input) {
    switch (input) {
    case LogicalInput::Dit:
        return state.dit;
    case LogicalInput::Dah:
        return state.dah;
    case LogicalInput::StraightKey:
        return state.straight;
    case LogicalInput::Ptt:
        return state.ptt;
    }
    return state.dit;
}

bool InputAggregator::stateValue(const SourceState &state, LogicalInput input) {
    switch (input) {
    case LogicalInput::Dit:
        return state.dit;
    case LogicalInput::Dah:
        return state.dah;
    case LogicalInput::StraightKey:
        return state.straight;
    case LogicalInput::Ptt:
        return state.ptt;
    }
    return false;
}

bool InputAggregator::aggregateState(LogicalInput input) const {
    for (const SourceState &state : m_sources) {
        if (stateValue(state, input))
            return true;
    }
    return false;
}

QVector<InputTransition> InputAggregator::setInput(const QString &sourceId, LogicalInput input, bool pressed) {
    const bool before = aggregateState(input);
    SourceState &source = m_sources[sourceId];
    stateRef(source, input) = pressed;
    const bool after = aggregateState(input);
    if (before == after)
        return {};
    return {{input, after}};
}

QVector<InputTransition> InputAggregator::clearSource(const QString &sourceId) {
    QVector<InputTransition> transitions;
    if (!m_sources.contains(sourceId))
        return transitions;
    const bool before[] = {aggregateState(LogicalInput::Dit), aggregateState(LogicalInput::Dah),
                           aggregateState(LogicalInput::StraightKey), aggregateState(LogicalInput::Ptt)};
    m_sources.remove(sourceId);
    const LogicalInput inputs[] = {LogicalInput::Dit, LogicalInput::Dah, LogicalInput::StraightKey, LogicalInput::Ptt};
    for (int i = 0; i < 4; ++i) {
        const bool after = aggregateState(inputs[i]);
        if (before[i] != after)
            transitions.append({inputs[i], after});
    }
    return transitions;
}

} // namespace MidiMapping
