#include "radiosettings.h"
#include "logging/logrules.h"
#include "logging/logsink.h"
#include "audio/rxoutputstage.h"
#include "ui/styling/k4constants.h"
#include "utils/windowfit.h"
#include <numeric>
#include <algorithm>
#include <cmath>

// Starting point for the TCI transmit level: 25% on the cubic curve is 0.0156x, measured on a K4 as
// keeping ALC at or below 5 on an injected signal, WITH WSJT-X'S OWN OUTPUT AT 80%.
//
// That pairing is the whole measurement - 25% on its own means nothing, because there are two gain
// stages in series and the client owns the first one. 80% is recorded because it is a common
// setting, not a required one; an operator running their client hotter or quieter moves this to
// suit, which is why the control is a slider and why the help text gives them the ALC criterion
// instead of a number to copy.
//
// WHY QK4 NEEDS ITS OWN CONTROL AT ALL, when WSJT-X already has an output slider: the client's
// slider is not QK4's to spend. The same WSJT-X installation is commonly used against other
// programs and other radios, where its output level is already set to suit them. Asking the
// operator to retune it for QK4 would break those setups every time they switch. This control is
// the one that belongs to this path, so the client's level can stay where the rest of their
// station needs it.
//
// WHY THIS DEFAULT CARRIES MORE WEIGHT THAN MIC GAIN'S: the K4 has a settable line-in level for its
// soundcard input and another for the rear 3.5 mm jack, but NONE for LAN audio. There is no
// radio-side trim on this path, so this gain and the client's own output level are the only two
// controls in the chain. That also means a bad default cannot be absorbed at the radio the way a
// hot line input can.
//
// It coincides with the Mic Gain default by measurement, not derivation - the two are tuned against
// different sources and different meters, and either may move without the other.
static constexpr int kTciTxGainDefault = 25;

namespace {

// One range rule per field, applied by its setter and by load() alike: a settings file can hold
// anything - a hand edit, another build's value, a type QSettings cannot convert. Values from the
// file are read wide, so an out-of-range number is caught before it is narrowed.

qint64 storedInt(const QVariant &value, qint64 fallback) {
    bool ok = false;
    const qint64 number = value.toLongLong(&ok);
    return ok ? number : fallback;
}

// The CAT and TCI listeners: unprivileged ports only.
quint16 listenPort(qint64 port) {
    return static_cast<quint16>(std::clamp<qint64>(port, 1024, 65535));
}

// A port on another host (K4, KPA1500, DX cluster). 0 would mean "any port", so it is refused.
quint16 remotePort(qint64 port, quint16 fallback) {
    return (port >= 1 && port <= 65535) ? static_cast<quint16>(port) : fallback;
}

int kpa1500PollIntervalMs(qint64 ms) {
    return static_cast<int>(std::clamp<qint64>(ms, 100, 5000));
}

int dxClusterSpotAgeSeconds(qint64 seconds) {
    return static_cast<int>(std::clamp<qint64>(seconds, 300, 1800)); // 5-30 minutes
}

int dxClusterSpotFontSizePx(qint64 sizePx) {
    return static_cast<int>(
        std::clamp<qint64>(sizePx, K4Styles::Dimensions::FontSizeSpotMin, K4Styles::Dimensions::FontSizeSpotMax));
}

int percent(qint64 value) {
    return static_cast<int>(std::clamp<qint64>(value, 0, 100));
}

int halikeyDeviceTypeIndex(qint64 type) {
    return static_cast<int>(std::clamp<qint64>(type, 0, 2)); // 0 = V1.4 serial, 1 = MIDI, 2 = Vail
}

int halikeyKeyerModeIndex(qint64 mode) {
    return static_cast<int>(std::clamp<qint64>(mode, 0, 2));
}

int kpodPlusEncodeModeIndex(qint64 mode) {
    return static_cast<int>(std::clamp<qint64>(mode, 0, 1));
}

} // namespace

static const QByteArray obfuscationKey = "K4RemoteObfuscation";

static QString obfuscatePassword(const QString &password) {
    if (password.isEmpty())
        return QString();
    QByteArray utf8 = password.toUtf8();
    for (int i = 0; i < utf8.size(); ++i)
        utf8[i] = utf8[i] ^ obfuscationKey[i % obfuscationKey.size()];
    return QStringLiteral("obf:") + QString::fromLatin1(utf8.toBase64());
}

static QString deobfuscatePassword(const QString &stored) {
    if (!stored.startsWith(QStringLiteral("obf:")))
        return stored;
    QByteArray decoded = QByteArray::fromBase64(stored.mid(4).toLatin1());
    for (int i = 0; i < decoded.size(); ++i)
        decoded[i] = decoded[i] ^ obfuscationKey[i % obfuscationKey.size()];
    return QString::fromUtf8(decoded);
}

RadioSettings *RadioSettings::instance() {
    static RadioSettings instance;
    return &instance;
}

RadioSettings::RadioSettings(QObject *parent)
    : QObject(parent), m_lastSelectedIndex(-1), m_kpodEnabled(false),
      // Same store as QSettings("QK4", "QK4") - on every platform the two resolve to the identical
      // file - but named through defaultFormat() so it can be REDIRECTED.
      //
      // WHY THAT MATTERS: Qt's two-argument QSettings(org, app) constructor ignores
      // QSettings::setDefaultFormat() and always uses NativeFormat, which on macOS is the
      // CFPreferences domain. A test therefore had NO way to point this singleton anywhere else,
      // and one that tried ran against a developer's real preferences and deleted every saved
      // radio. Spelling the format out is the whole fix: setDefaultFormat() now reaches this.
      m_settings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("QK4"), QStringLiteral("QK4")) {
    load();
}

QVector<RadioEntry> RadioSettings::radios() const {
    return m_radios;
}

int RadioSettings::connectAtStartupIndex() const {
    for (int i = 0; i < m_radios.size(); ++i) {
        if (m_radios[i].connectAtStartup) {
            return i;
        }
    }
    return -1;
}

int RadioSettings::indexOfRadioNamed(const QString &name) const {
    if (name.isEmpty()) {
        return -1;
    }
    for (int i = 0; i < m_radios.size(); ++i) {
        if (m_radios[i].name.compare(name, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return -1;
}

void RadioSettings::setConnectAtStartupRadio(int index) {
    // Clearing every other entry is the whole point: the flag means "the radio QK4 opens with",
    // which only has meaning for one. Enforcing it here rather than in the dialog covers a
    // settings file edited by hand or restored from a backup.
    bool changed = false;
    for (int i = 0; i < m_radios.size(); ++i) {
        const bool wanted = (i == index);
        if (m_radios[i].connectAtStartup != wanted) {
            m_radios[i].connectAtStartup = wanted;
            changed = true;
        }
    }
    if (changed) {
        save();
        emit radiosChanged();
    }
}

// WHY the saved radio becomes the selected one: the Radio Manager reselects lastSelectedIndex when
// radiosChanged() refreshes it, and the radio just saved is the one the operator is working on.
// Before this, the list re-sorted under a stale index and the form refilled from another entry -
// an SL3 save appeared to bounce back to SL0 when the row under the old index still said SL0.
void RadioSettings::addRadio(const RadioEntry &radio) {
    // The same radio (name, host, port) saved again is an update, not a second copy.
    const int existing = m_radios.indexOf(radio);
    if (existing >= 0) {
        m_radios[existing] = sanitized(radio);
        m_lastSelectedIndex = existing;
    } else {
        m_radios.append(sanitized(radio));
        m_lastSelectedIndex = static_cast<int>(m_radios.size()) - 1;
    }
    sortRadios();
    save();
    emit radiosChanged();
}

void RadioSettings::removeRadio(int index) {
    if (index >= 0 && index < m_radios.size()) {
        m_radios.removeAt(index);
        // The selection follows its radio: an earlier removal moves it up one. Removing the selected
        // radio selects its neighbour (the Radio Manager shows that entry next), none if the list is empty.
        if (index == m_lastSelectedIndex) {
            m_lastSelectedIndex = m_radios.isEmpty() ? -1 : std::min(index, static_cast<int>(m_radios.size()) - 1);
        } else if (index < m_lastSelectedIndex) {
            --m_lastSelectedIndex;
        }
        save();
        emit radiosChanged();
    }
}

void RadioSettings::updateRadio(int index, const RadioEntry &radio) {
    if (index >= 0 && index < m_radios.size()) {
        m_radios[index] = sanitized(radio);
        m_lastSelectedIndex = index; // see addRadio
        sortRadios();
        save();
        emit radiosChanged();
    }
}

int RadioSettings::lastSelectedIndex() const {
    return m_lastSelectedIndex;
}

void RadioSettings::setLastSelectedIndex(int index) {
    if (m_lastSelectedIndex != index) {
        m_lastSelectedIndex = index;
        save();
    }
}

bool RadioSettings::kpodEnabled() const {
    return m_kpodEnabled;
}

void RadioSettings::setKpodEnabled(bool enabled) {
    if (m_kpodEnabled != enabled) {
        m_kpodEnabled = enabled;
        save();
        emit kpodEnabledChanged(enabled);
    }
}

QString RadioSettings::kpa1500Host() const {
    return m_kpa1500Host;
}

void RadioSettings::setKpa1500Host(const QString &host) {
    if (m_kpa1500Host != host) {
        m_kpa1500Host = host;
        save();
        emit kpa1500SettingsChanged();
    }
}

quint16 RadioSettings::kpa1500Port() const {
    return m_kpa1500Port;
}

void RadioSettings::setKpa1500Port(quint16 port) {
    port = remotePort(port, m_kpa1500Port);
    if (m_kpa1500Port != port) {
        m_kpa1500Port = port;
        save();
        emit kpa1500SettingsChanged();
    }
}

bool RadioSettings::kpa1500Enabled() const {
    return m_kpa1500Enabled;
}

void RadioSettings::setKpa1500Enabled(bool enabled) {
    if (m_kpa1500Enabled != enabled) {
        m_kpa1500Enabled = enabled;
        save();
        emit kpa1500EnabledChanged(enabled);
    }
}

int RadioSettings::kpa1500PollInterval() const {
    return m_kpa1500PollInterval;
}

void RadioSettings::setKpa1500PollInterval(int intervalMs) {
    intervalMs = kpa1500PollIntervalMs(intervalMs);
    if (m_kpa1500PollInterval != intervalMs) {
        m_kpa1500PollInterval = intervalMs;
        save();
        emit kpa1500PollIntervalChanged(intervalMs);
    }
}

int RadioSettings::volume() const {
    return m_settings.value("audio/volume", 45).toInt();
}

void RadioSettings::setVolume(int value) {
    value = qBound(0, value, 100);
    m_settings.setValue("audio/volume", value);
    m_settings.sync();
}

int RadioSettings::subVolume() const {
    return m_settings.value("audio/subVolume", 45).toInt();
}

void RadioSettings::setSubVolume(int value) {
    value = qBound(0, value, 100);
    m_settings.setValue("audio/subVolume", value);
    m_settings.sync();
}

int RadioSettings::textDecodeFontSize(bool subRx) const {
    using namespace K4Styles::Dimensions;
    const int value =
        m_settings.value(subRx ? "textDecode/subFontSize" : "textDecode/mainFontSize", FontSizeTextDecode).toInt();
    return qBound(FontSizeTextDecodeMin, value, FontSizeTextDecodeMax);
}

void RadioSettings::setTextDecodeFontSize(bool subRx, int sizePx) {
    sizePx = qBound(K4Styles::Dimensions::FontSizeTextDecodeMin, sizePx, K4Styles::Dimensions::FontSizeTextDecodeMax);
    m_settings.setValue(subRx ? "textDecode/subFontSize" : "textDecode/mainFontSize", sizePx);
    m_settings.sync();
}

int RadioSettings::iaruRegion() const {
    // 1/2/3 = IARU regions, 4 = US (FCC). Default Region 2 (Americas).
    return qBound(1, m_settings.value("station/iaruRegion", 2).toInt(), 4);
}

void RadioSettings::setIaruRegion(int region) {
    region = qBound(1, region, 4);
    if (iaruRegion() != region) {
        m_settings.setValue("station/iaruRegion", region);
        m_settings.sync();
        emit iaruRegionChanged(region);
    }
}

QString RadioSettings::callSign() const {
    return m_settings.value("station/callSign", "").toString();
}

void RadioSettings::setCallSign(const QString &callSign) {
    m_settings.setValue("station/callSign", callSign.trimmed().toUpper());
    m_settings.sync();
}

QString RadioSettings::gridSquare() const {
    return m_settings.value("station/gridSquare", "").toString();
}

void RadioSettings::setGridSquare(const QString &grid) {
    m_settings.setValue("station/gridSquare", grid.trimmed());
    m_settings.sync();
}

QString RadioSettings::operatorName() const {
    return m_settings.value("station/operatorName", "").toString();
}

void RadioSettings::setOperatorName(const QString &name) {
    m_settings.setValue("station/operatorName", name.trimmed());
    m_settings.sync();
}

QString RadioSettings::qth() const {
    return m_settings.value("station/qth", "").toString();
}

void RadioSettings::setQth(const QString &qth) {
    m_settings.setValue("station/qth", qth.trimmed());
    m_settings.sync();
}

bool RadioSettings::bandPlanOverlayEnabled() const {
    return m_settings.value("station/bandPlanOverlay", false).toBool();
}

void RadioSettings::setBandPlanOverlayEnabled(bool enabled) {
    if (bandPlanOverlayEnabled() != enabled) {
        m_settings.setValue("station/bandPlanOverlay", enabled);
        m_settings.sync();
        emit bandPlanOverlayEnabledChanged(enabled);
    }
}

bool RadioSettings::temperatureInFahrenheit() const {
    return m_settings.value("ui/temperatureFahrenheit", false).toBool();
}

void RadioSettings::setTemperatureInFahrenheit(bool fahrenheit) {
    if (temperatureInFahrenheit() != fahrenheit) {
        m_settings.setValue("ui/temperatureFahrenheit", fahrenheit);
        m_settings.sync();
        emit temperatureInFahrenheitChanged(fahrenheit);
    }
}

namespace {

// An unknown saved value (hand-edited, or a step since removed) falls back to Auto: the one
// setting that cannot leave the window larger than the screen.
int sanitizeInterfaceScale(int percent) {
    for (int allowed : RadioSettings::kInterfaceScales)
        if (percent == allowed)
            return percent;
    return RadioSettings::kInterfaceScaleAuto;
}

double sanitizeAutoScaleFactor(double factor) {
    return std::isfinite(factor) ? std::clamp(factor, WindowFit::kMinScale, WindowFit::kMaxScale)
                                 : WindowFit::kNativeScale;
}

} // namespace

int RadioSettings::interfaceScale() const {
    return sanitizeInterfaceScale(m_settings.value("ui/interfaceScale", kInterfaceScaleAuto).toInt());
}

void RadioSettings::setInterfaceScale(int percent) {
    percent = sanitizeInterfaceScale(percent);
    if (interfaceScale() != percent) {
        m_settings.setValue("ui/interfaceScale", percent);
        m_settings.sync();
        emit interfaceScaleChanged(percent);
    }
}

bool RadioSettings::logFileEnabled() const {
    return m_settings.value("Logging/fileEnabled", false).toBool();
}

void RadioSettings::setLogFileEnabled(bool enabled) {
    if (logFileEnabled() != enabled) {
        m_settings.setValue("Logging/fileEnabled", enabled);
        m_settings.sync();
        emit loggingChanged();
    }
}

QString RadioSettings::logLevel() const {
    return Logging::levelId(Logging::levelFromId(m_settings.value("Logging/level").toString()));
}

void RadioSettings::setLogLevel(const QString &level) {
    const QString sanitized = Logging::levelId(Logging::levelFromId(level));
    if (logLevel() != sanitized) {
        m_settings.setValue("Logging/level", sanitized);
        m_settings.sync();
        emit loggingChanged();
    }
}

QStringList RadioSettings::logGroups() const {
    return Logging::sanitizeGroupIds(m_settings.value("Logging/groups").toStringList());
}

void RadioSettings::setLogGroups(const QStringList &groups) {
    const QStringList sanitized = Logging::sanitizeGroupIds(groups);
    if (logGroups() != sanitized) {
        m_settings.setValue("Logging/groups", sanitized);
        m_settings.sync();
        emit loggingChanged();
    }
}

Logging::LogConfig RadioSettings::logConfig() const {
    Logging::LogConfig config;
    config.level = Logging::levelFromId(logLevel());
    config.groups = logGroups();
    config.bench = logBench();
    return config;
}

QStringList RadioSettings::logBench() const {
    return m_logBench;
}

void RadioSettings::setLogBench(const QStringList &bench) {
    const QStringList sanitized = Logging::sanitizeBenchIds(bench);
    if (m_logBench != sanitized) {
        m_logBench = sanitized;
        emit loggingChanged();
    }
}

double RadioSettings::autoScaleFactor() const {
    return sanitizeAutoScaleFactor(m_settings.value("ui/autoScaleFactor", WindowFit::kNativeScale).toDouble());
}

void RadioSettings::setAutoScaleFactor(double factor) {
    m_settings.setValue("ui/autoScaleFactor", sanitizeAutoScaleFactor(factor));
    m_settings.sync();
}

double RadioSettings::startupInterfaceScale(bool *isAuto) {
    const QSettings store(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("QK4"),
                          QStringLiteral("QK4"));
    const int percent = sanitizeInterfaceScale(store.value("ui/interfaceScale", kInterfaceScaleAuto).toInt());
    if (isAuto)
        *isAuto = percent == kInterfaceScaleAuto;
    if (percent == kInterfaceScaleAuto)
        return sanitizeAutoScaleFactor(store.value("ui/autoScaleFactor", WindowFit::kNativeScale).toDouble());
    return percent / 100.0;
}

int RadioSettings::micGain() const {
    return m_settings.value("audio/micGain", 25).toInt();
}

void RadioSettings::setMicGain(int value) {
    value = qBound(0, value, 100);
    int oldValue = m_settings.value("audio/micGain", 25).toInt();
    if (oldValue != value) {
        m_settings.setValue("audio/micGain", value);
        m_settings.sync();
        emit micGainChanged(value);
    }
}

QString RadioSettings::micDevice() const {
    return m_settings.value("audio/micDevice", "").toString();
}

void RadioSettings::setMicDevice(const QString &deviceId) {
    QString oldDevice = m_settings.value("audio/micDevice", "").toString();
    if (oldDevice != deviceId) {
        m_settings.setValue("audio/micDevice", deviceId);
        m_settings.sync();
        emit micDeviceChanged(deviceId);
    }
}

QString RadioSettings::speakerDevice() const {
    return m_settings.value("audio/speakerDevice", "").toString();
}

void RadioSettings::setSpeakerDevice(const QString &deviceId) {
    QString oldDevice = m_settings.value("audio/speakerDevice", "").toString();
    if (oldDevice != deviceId) {
        m_settings.setValue("audio/speakerDevice", deviceId);
        m_settings.sync();
        emit speakerDeviceChanged(deviceId);
    }
}

bool RadioSettings::catServerEnabled() const {
    return m_catServerEnabled;
}

void RadioSettings::setCatServerEnabled(bool enabled) {
    if (m_catServerEnabled != enabled) {
        m_catServerEnabled = enabled;
        save();
        emit catServerEnabledChanged(enabled);
    }
}

quint16 RadioSettings::catServerPort() const {
    return m_catServerPort;
}

void RadioSettings::setCatServerPort(quint16 port) {
    port = listenPort(port);
    if (m_catServerPort != port) {
        m_catServerPort = port;
        save();
        emit catServerPortChanged(port);
    }
}

bool RadioSettings::tciServerEnabled() const {
    return m_tciServerEnabled;
}

void RadioSettings::setTciServerEnabled(bool enabled) {
    if (m_tciServerEnabled != enabled) {
        m_tciServerEnabled = enabled;
        save();
        emit tciServerEnabledChanged(enabled);
    }
}

quint16 RadioSettings::tciServerPort() const {
    return m_tciServerPort;
}

void RadioSettings::setTciServerPort(quint16 port) {
    port = listenPort(port);
    if (m_tciServerPort != port) {
        m_tciServerPort = port;
        save();
        emit tciServerPortChanged(port);
    }
}

bool RadioSettings::tciAudioEnabled() const {
    return m_tciAudioEnabled;
}

void RadioSettings::setTciAudioEnabled(bool enabled) {
    if (m_tciAudioEnabled != enabled) {
        m_tciAudioEnabled = enabled;
        save();
        emit tciAudioEnabledChanged(enabled);
    }
}

int RadioSettings::tciTxGain() const {
    return m_settings.value("tci/txGain", kTciTxGainDefault).toInt();
}

void RadioSettings::setTciTxGain(int value) {
    value = qBound(0, value, 100);
    const int oldValue = m_settings.value("tci/txGain", kTciTxGainDefault).toInt();
    if (oldValue != value) {
        m_settings.setValue("tci/txGain", value);
        m_settings.sync();
        emit tciTxGainChanged(value);
    }
}

int RadioSettings::rxAudioBoostDb() const {
    return RxOutputStage::snapBoostDb(m_settings.value("audio/rxBoostDb", 0).toInt());
}

void RadioSettings::setRxAudioBoostDb(int db) {
    db = RxOutputStage::snapBoostDb(db);
    if (rxAudioBoostDb() != db) {
        m_settings.setValue("audio/rxBoostDb", db);
        m_settings.sync();
        emit rxAudioBoostDbChanged(db);
    }
}

QMap<QString, MacroEntry> RadioSettings::macros() const {
    return m_macros;
}

MacroEntry RadioSettings::macro(const QString &functionId) const {
    return m_macros.value(functionId);
}

void RadioSettings::setMacro(const QString &functionId, const QString &label, const QString &command) {
    MacroEntry entry;
    entry.functionId = functionId;
    entry.label = label;
    entry.command = command;

    if (command.isEmpty()) {
        // Remove empty macros
        if (m_macros.contains(functionId)) {
            m_macros.remove(functionId);
            save();
            emit macrosChanged();
        }
    } else {
        // Add or update macro
        const auto existing = m_macros.constFind(functionId);
        const bool changed = existing == m_macros.cend() || existing->label != label || existing->command != command;
        if (changed) {
            m_macros[functionId] = entry;
            save();
            emit macrosChanged();
        }
    }
}

QString RadioSettings::halikeyPortName() const {
    return m_halikeyPortName;
}

void RadioSettings::setHalikeyPortName(const QString &portName) {
    if (m_halikeyPortName != portName) {
        m_halikeyPortName = portName;
        save();
        emit halikeyPortNameChanged(portName);
    }
}

bool RadioSettings::halikeyEnabled() const {
    return m_halikeyEnabled;
}

void RadioSettings::setHalikeyEnabled(bool enabled) {
    if (m_halikeyEnabled != enabled) {
        m_halikeyEnabled = enabled;
        save();
        emit halikeyEnabledChanged(enabled);
    }
}

int RadioSettings::halikeyDeviceType() const {
    return m_halikeyDeviceType;
}

void RadioSettings::setHalikeyDeviceType(int type) {
    type = halikeyDeviceTypeIndex(type);
    if (m_halikeyDeviceType != type) {
        m_halikeyDeviceType = type;
        save();
        emit halikeyDeviceTypeChanged(type);
    }
}

int RadioSettings::sidetoneVolume() const {
    return m_sidetoneVolume;
}

void RadioSettings::setSidetoneVolume(int value) {
    value = percent(value);
    if (m_sidetoneVolume != value) {
        m_sidetoneVolume = value;
        save();
        emit sidetoneVolumeChanged(value);
    }
}

int RadioSettings::halikeyKeyerMode() const {
    return m_halikeyKeyerMode;
}

void RadioSettings::setHalikeyKeyerMode(int mode) {
    mode = halikeyKeyerModeIndex(mode);
    if (m_halikeyKeyerMode != mode) {
        m_halikeyKeyerMode = mode;
        save();
        emit halikeyKeyerModeChanged(mode);
    }
}

bool RadioSettings::halikeyStraightKey() const {
    return m_halikeyStraightKey;
}

void RadioSettings::setHalikeyStraightKey(bool straight) {
    if (m_halikeyStraightKey != straight) {
        m_halikeyStraightKey = straight;
        save();
        emit halikeyStraightKeyChanged(straight);
    }
}

int RadioSettings::halikeyDitMemoryDelay() const {
    return m_halikeyDitMemoryDelay;
}

void RadioSettings::setHalikeyDitMemoryDelay(int percent) {
    percent = ::percent(percent);
    if (m_halikeyDitMemoryDelay != percent) {
        m_halikeyDitMemoryDelay = percent;
        save();
        emit halikeyDitMemoryDelayChanged(percent);
    }
}

bool RadioSettings::halikeyMuteRadioMonitor() const {
    return m_halikeyMuteRadioMonitor;
}

void RadioSettings::setHalikeyMuteRadioMonitor(bool mute) {
    if (m_halikeyMuteRadioMonitor != mute) {
        m_halikeyMuteRadioMonitor = mute;
        save();
        emit halikeyMuteRadioMonitorChanged(mute);
    }
}

bool RadioSettings::vailComputerSidetone() const {
    return m_vailComputerSidetone;
}

void RadioSettings::setVailComputerSidetone(bool enabled) {
    if (m_vailComputerSidetone != enabled) {
        m_vailComputerSidetone = enabled;
        save();
        emit vailComputerSidetoneChanged(enabled);
    }
}

// =============================================================================
// KPOD+ Keyer Settings
// =============================================================================

int RadioSettings::kpodPlusEncodeMode() const {
    return m_kpodPlusEncodeMode;
}

void RadioSettings::setKpodPlusEncodeMode(int mode) {
    mode = kpodPlusEncodeModeIndex(mode);
    if (m_kpodPlusEncodeMode != mode) {
        m_kpodPlusEncodeMode = mode;
        save();
        emit kpodPlusSettingsChanged();
    }
}

EqPreset RadioSettings::rxEqPreset(int index) const {
    if (index >= 0 && index < 4) {
        return m_rxEqPresets[index];
    }
    return EqPreset();
}

void RadioSettings::setRxEqPreset(int index, const EqPreset &preset) {
    if (index >= 0 && index < 4) {
        m_rxEqPresets[index] = preset;
        save();
        emit rxEqPresetsChanged();
    }
}

void RadioSettings::clearRxEqPreset(int index) {
    if (index >= 0 && index < 4) {
        m_rxEqPresets[index] = EqPreset();
        save();
        emit rxEqPresetsChanged();
    }
}

EqPreset RadioSettings::txEqPreset(int index) const {
    if (index >= 0 && index < 4) {
        return m_txEqPresets[index];
    }
    return EqPreset();
}

void RadioSettings::setTxEqPreset(int index, const EqPreset &preset) {
    if (index >= 0 && index < 4) {
        m_txEqPresets[index] = preset;
        save();
        emit txEqPresetsChanged();
    }
}

void RadioSettings::clearTxEqPreset(int index) {
    if (index >= 0 && index < 4) {
        m_txEqPresets[index] = EqPreset();
        save();
        emit txEqPresetsChanged();
    }
}

QVector<DxClusterEntry> RadioSettings::dxClusters() const {
    return m_dxClusters;
}

void RadioSettings::addDxCluster(const DxClusterEntry &entry) {
    m_dxClusters.append(sanitized(entry));
    save();
    emit dxClusterSettingsChanged();
}

void RadioSettings::removeDxCluster(int index) {
    if (index >= 0 && index < m_dxClusters.size()) {
        m_dxClusters.removeAt(index);
        save();
        emit dxClusterSettingsChanged();
    }
}

void RadioSettings::updateDxCluster(int index, const DxClusterEntry &entry) {
    if (index >= 0 && index < m_dxClusters.size()) {
        m_dxClusters[index] = sanitized(entry);
        save();
        emit dxClusterSettingsChanged();
    }
}

int RadioSettings::dxClusterSpotAge() const {
    return m_dxClusterSpotAge;
}

void RadioSettings::setDxClusterSpotAge(int seconds) {
    seconds = dxClusterSpotAgeSeconds(seconds);
    if (m_dxClusterSpotAge != seconds) {
        m_dxClusterSpotAge = seconds;
        save();
        emit dxClusterSettingsChanged();
    }
}

QString RadioSettings::dxClusterCallsign() const {
    return m_dxClusterCallsign;
}

void RadioSettings::setDxClusterCallsign(const QString &callsign) {
    QString upper = callsign.trimmed().toUpper();
    if (m_dxClusterCallsign != upper) {
        m_dxClusterCallsign = upper;
        save();
        emit dxClusterSettingsChanged();
    }
}

int RadioSettings::dxClusterSpotFontSize() const {
    return m_dxClusterSpotFontSize;
}

void RadioSettings::setDxClusterSpotFontSize(int sizePx) {
    sizePx = dxClusterSpotFontSizePx(sizePx);
    if (m_dxClusterSpotFontSize != sizePx) {
        m_dxClusterSpotFontSize = sizePx;
        save();
        emit dxClusterSettingsChanged();
    }
}

quint16 RadioSettings::defaultRadioPort(bool useTls) {
    return useTls ? K4Protocol::TLS_PORT : K4Protocol::DEFAULT_PORT;
}

RadioEntry RadioSettings::sanitized(RadioEntry radio) {
    Logging::registerSecret(radio.password);
    Logging::registerSecret(radio.identity);
    radio.port = remotePort(radio.port, defaultRadioPort(radio.useTls));
    return radio;
}

DxClusterEntry RadioSettings::sanitized(DxClusterEntry entry) {
    entry.port = remotePort(entry.port, kDefaultDxClusterPort);
    return entry;
}

void RadioSettings::load() {
    int count = m_settings.beginReadArray("radios");
    m_radios.clear();
    for (int i = 0; i < count; ++i) {
        m_settings.setArrayIndex(i);
        RadioEntry entry;
        entry.name = m_settings.value("name").toString();
        entry.host = m_settings.value("host").toString();
        entry.password = deobfuscatePassword(m_settings.value("password").toString());
        entry.useTls = m_settings.value("useTls", false).toBool();
        entry.port = remotePort(storedInt(m_settings.value("port"), 0), defaultRadioPort(entry.useTls));
        entry.identity = m_settings.value("identity").toString();
        entry.encodeMode = m_settings.value("encodeMode", 3).toInt();             // Default EM3 (Opus)
        entry.streamingLatency = m_settings.value("streamingLatency", 3).toInt(); // Default SL3
        entry.displayFps = m_settings.value("displayFps", 15).toInt();            // Default 15 FPS
        entry.connectAtStartup = m_settings.value("connectAtStartup", false).toBool();
        Logging::registerSecret(entry.password);
        Logging::registerSecret(entry.identity);
        m_radios.append(entry);
    }
    m_settings.endArray();
    sortRadios();

    // Keep the "at most one" rule true even for a settings file that was not written by us - a
    // hand edit, or a backup from before the rule existed. First flagged entry wins; the rest are
    // cleared so startup cannot depend on list order.
    bool seenStartupRadio = false;
    for (RadioEntry &entry : m_radios) {
        if (!entry.connectAtStartup) {
            continue;
        }
        if (seenStartupRadio) {
            entry.connectAtStartup = false;
        }
        seenStartupRadio = true;
    }

    m_lastSelectedIndex = m_settings.value("lastSelectedIndex", -1).toInt();
    m_kpodEnabled = m_settings.value("kpodEnabled", false).toBool();

    // KPA1500 settings
    m_kpa1500Host = m_settings.value("kpa1500/host", "").toString();
    m_kpa1500Port = remotePort(storedInt(m_settings.value("kpa1500/port"), kDefaultKpa1500Port), kDefaultKpa1500Port);
    m_kpa1500Enabled = m_settings.value("kpa1500/enabled", false).toBool();
    m_kpa1500PollInterval = kpa1500PollIntervalMs(storedInt(m_settings.value("kpa1500/pollInterval"), 300));

    // CAT Server settings (migrate from old rigctld keys if present)
    m_catServerEnabled = m_settings.value("catServer/enabled", m_settings.value("rigctld/enabled", false)).toBool();
    m_catServerPort = listenPort(storedInt(m_settings.value("catServer/port", m_settings.value("rigctld/port")), 9299));
    m_tciServerEnabled = m_settings.value("tciServer/enabled", false).toBool();
    m_tciServerPort = listenPort(storedInt(m_settings.value("tciServer/port"), 50001));
    m_tciAudioEnabled = m_settings.value("tciServer/audio", true).toBool();

    // DX Cluster settings
    int dxCount = m_settings.beginReadArray("dxClusters");
    m_dxClusters.clear();
    for (int i = 0; i < dxCount; ++i) {
        m_settings.setArrayIndex(i);
        DxClusterEntry entry;
        entry.host = m_settings.value("host").toString();
        entry.port = remotePort(storedInt(m_settings.value("port"), kDefaultDxClusterPort), kDefaultDxClusterPort);
        entry.autoConnect = m_settings.value("autoConnect", false).toBool();
        m_dxClusters.append(entry);
    }
    m_settings.endArray();
    m_dxClusterSpotAge = dxClusterSpotAgeSeconds(storedInt(m_settings.value("dxCluster/spotAge"), 600));
    m_dxClusterCallsign = m_settings.value("dxCluster/callsign", "").toString();
    m_dxClusterSpotFontSize = dxClusterSpotFontSizePx(storedInt(m_settings.value("dxCluster/spotFontSize"), 11));

    // Seed default cluster entry on first run
    if (m_dxClusters.isEmpty()) {
        DxClusterEntry rbn;
        rbn.host = "telnet.reversebeacon.net";
        rbn.port = 7300;
        m_dxClusters.append(rbn);
    }

    // HaliKey settings
    m_halikeyPortName = m_settings.value("halikey/portName", "").toString();
    m_halikeyEnabled = m_settings.value("halikey/enabled", false).toBool();
    m_halikeyDeviceType = halikeyDeviceTypeIndex(storedInt(m_settings.value("halikey/deviceType"), 0));
    m_sidetoneVolume = percent(storedInt(m_settings.value("halikey/sidetoneVolume"), 30));
    m_halikeyKeyerMode = halikeyKeyerModeIndex(storedInt(m_settings.value("halikey/keyerMode"), 0));
    m_halikeyStraightKey = m_settings.value("halikey/straightKey", false).toBool();
    m_halikeyDitMemoryDelay = percent(storedInt(m_settings.value("halikey/ditMemoryDelay"), 33));
    m_halikeyMuteRadioMonitor = m_settings.value("halikey/muteRadioMonitor", false).toBool();
    m_vailComputerSidetone = m_settings.value("vail/computerSidetone", false).toBool();

    // KPOD+ keyer settings
    m_kpodPlusEncodeMode = kpodPlusEncodeModeIndex(storedInt(m_settings.value("kpodPlus/encodeMode"), 0));

    // Macro settings
    int macroCount = m_settings.beginReadArray("macros");
    m_macros.clear();
    for (int i = 0; i < macroCount; ++i) {
        m_settings.setArrayIndex(i);
        MacroEntry entry;
        entry.functionId = m_settings.value("functionId").toString();
        entry.label = m_settings.value("label").toString();
        entry.command = m_settings.value("command").toString();
        if (!entry.functionId.isEmpty()) {
            m_macros[entry.functionId] = entry;
        }
    }
    m_settings.endArray();

    // RX EQ Presets (4 slots)
    for (int i = 0; i < 4; ++i) {
        QString prefix = QString("rxEqPresets/%1/").arg(i);
        m_rxEqPresets[i].name = m_settings.value(prefix + "name", "").toString();
        QString bandsStr = m_settings.value(prefix + "bands", "").toString();
        m_rxEqPresets[i].bands.clear();
        if (!bandsStr.isEmpty()) {
            QStringList bandsList = bandsStr.split(",");
            for (const QString &val : bandsList) {
                bool ok;
                int dB = val.toInt(&ok);
                if (ok) {
                    m_rxEqPresets[i].bands.append(qBound(-16, dB, 16));
                }
            }
        }
    }

    // TX EQ Presets (4 slots)
    for (int i = 0; i < 4; ++i) {
        QString prefix = QString("txEqPresets/%1/").arg(i);
        m_txEqPresets[i].name = m_settings.value(prefix + "name", "").toString();
        QString bandsStr = m_settings.value(prefix + "bands", "").toString();
        m_txEqPresets[i].bands.clear();
        if (!bandsStr.isEmpty()) {
            QStringList bandsList = bandsStr.split(",");
            for (const QString &val : bandsList) {
                bool ok;
                int dB = val.toInt(&ok);
                if (ok) {
                    m_txEqPresets[i].bands.append(qBound(-16, dB, 16));
                }
            }
        }
    }
}

// Sorts by display name and keeps lastSelectedIndex on the same entry. WHY by position and not by
// RadioEntry::operator==: two entries can compare equal (same name, host and port), and the
// selection must stay on the one it was on.
void RadioSettings::sortRadios() {
    QVector<int> order(m_radios.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [this](int a, int b) {
        const QString nameA = m_radios[a].name.isEmpty() ? m_radios[a].host : m_radios[a].name;
        const QString nameB = m_radios[b].name.isEmpty() ? m_radios[b].host : m_radios[b].name;
        return nameA.compare(nameB, Qt::CaseInsensitive) < 0;
    });
    QVector<RadioEntry> sorted;
    sorted.reserve(m_radios.size());
    int selected = -1;
    for (int pos = 0; pos < order.size(); ++pos) {
        sorted.append(m_radios[order[pos]]);
        if (order[pos] == m_lastSelectedIndex) {
            selected = pos;
        }
    }
    m_radios = std::move(sorted);
    m_lastSelectedIndex = selected;
}

void RadioSettings::save() {
    m_settings.beginWriteArray("radios");
    for (int i = 0; i < m_radios.size(); ++i) {
        m_settings.setArrayIndex(i);
        m_settings.setValue("name", m_radios[i].name);
        m_settings.setValue("host", m_radios[i].host);
        m_settings.setValue("password", obfuscatePassword(m_radios[i].password));
        m_settings.setValue("port", m_radios[i].port);
        m_settings.setValue("useTls", m_radios[i].useTls);
        m_settings.setValue("identity", m_radios[i].identity);
        m_settings.setValue("encodeMode", m_radios[i].encodeMode);
        m_settings.setValue("streamingLatency", m_radios[i].streamingLatency);
        m_settings.setValue("displayFps", m_radios[i].displayFps);
        m_settings.setValue("connectAtStartup", m_radios[i].connectAtStartup);
    }
    m_settings.endArray();

    m_settings.setValue("lastSelectedIndex", m_lastSelectedIndex);
    m_settings.setValue("kpodEnabled", m_kpodEnabled);

    // KPA1500 settings
    m_settings.setValue("kpa1500/host", m_kpa1500Host);
    m_settings.setValue("kpa1500/port", m_kpa1500Port);
    m_settings.setValue("kpa1500/enabled", m_kpa1500Enabled);
    m_settings.setValue("kpa1500/pollInterval", m_kpa1500PollInterval);

    // CAT Server settings
    m_settings.setValue("catServer/enabled", m_catServerEnabled);
    m_settings.setValue("catServer/port", m_catServerPort);
    m_settings.setValue("tciServer/enabled", m_tciServerEnabled);
    m_settings.setValue("tciServer/port", m_tciServerPort);
    m_settings.setValue("tciServer/audio", m_tciAudioEnabled);

    // HaliKey settings
    m_settings.setValue("halikey/portName", m_halikeyPortName);
    m_settings.setValue("halikey/enabled", m_halikeyEnabled);
    m_settings.setValue("halikey/deviceType", m_halikeyDeviceType);
    m_settings.setValue("halikey/sidetoneVolume", m_sidetoneVolume);
    m_settings.setValue("halikey/keyerMode", m_halikeyKeyerMode);
    m_settings.setValue("halikey/straightKey", m_halikeyStraightKey);
    m_settings.setValue("halikey/ditMemoryDelay", m_halikeyDitMemoryDelay);
    m_settings.setValue("halikey/muteRadioMonitor", m_halikeyMuteRadioMonitor);
    m_settings.setValue("vail/computerSidetone", m_vailComputerSidetone);

    // KPOD+ keyer settings
    m_settings.setValue("kpodPlus/encodeMode", m_kpodPlusEncodeMode);

    // Macro settings
    m_settings.beginWriteArray("macros");
    int i = 0;
    for (auto it = m_macros.constBegin(); it != m_macros.constEnd(); ++it, ++i) {
        m_settings.setArrayIndex(i);
        m_settings.setValue("functionId", it->functionId);
        m_settings.setValue("label", it->label);
        m_settings.setValue("command", it->command);
    }
    m_settings.endArray();

    // DX Cluster settings
    m_settings.beginWriteArray("dxClusters");
    for (int j = 0; j < m_dxClusters.size(); ++j) {
        m_settings.setArrayIndex(j);
        m_settings.setValue("host", m_dxClusters[j].host);
        m_settings.setValue("port", m_dxClusters[j].port);
        m_settings.setValue("autoConnect", m_dxClusters[j].autoConnect);
    }
    m_settings.endArray();
    m_settings.setValue("dxCluster/spotAge", m_dxClusterSpotAge);
    m_settings.setValue("dxCluster/callsign", m_dxClusterCallsign);
    m_settings.setValue("dxCluster/spotFontSize", m_dxClusterSpotFontSize);

    // RX EQ Presets (4 slots)
    for (int j = 0; j < 4; ++j) {
        QString prefix = QString("rxEqPresets/%1/").arg(j);
        m_settings.setValue(prefix + "name", m_rxEqPresets[j].name);
        // Convert bands to comma-separated string
        QStringList bandsList;
        for (int dB : m_rxEqPresets[j].bands) {
            bandsList.append(QString::number(dB));
        }
        m_settings.setValue(prefix + "bands", bandsList.join(","));
    }

    // TX EQ Presets (4 slots)
    for (int j = 0; j < 4; ++j) {
        QString prefix = QString("txEqPresets/%1/").arg(j);
        m_settings.setValue(prefix + "name", m_txEqPresets[j].name);
        // Convert bands to comma-separated string
        QStringList bandsList;
        for (int dB : m_txEqPresets[j].bands) {
            bandsList.append(QString::number(dB));
        }
        m_settings.setValue(prefix + "bands", bandsList.join(","));
    }

    m_settings.sync();
}
