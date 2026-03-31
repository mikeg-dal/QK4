#include "radiosettings.h"

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
    : QObject(parent), m_lastSelectedIndex(-1), m_kpodEnabled(false), m_settings("QK4", "QK4") {
    load();
}

QVector<RadioEntry> RadioSettings::radios() const {
    return m_radios;
}

void RadioSettings::addRadio(const RadioEntry &radio) {
    m_radios.append(radio);
    save();
    emit radiosChanged();
}

void RadioSettings::removeRadio(int index) {
    if (index >= 0 && index < m_radios.size()) {
        m_radios.removeAt(index);
        if (m_lastSelectedIndex >= m_radios.size()) {
            m_lastSelectedIndex = m_radios.isEmpty() ? -1 : m_radios.size() - 1;
        }
        save();
        emit radiosChanged();
    }
}

void RadioSettings::updateRadio(int index, const RadioEntry &radio) {
    if (index >= 0 && index < m_radios.size()) {
        m_radios[index] = radio;
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
    // Clamp to reasonable range: 100ms - 5000ms
    intervalMs = qBound(100, intervalMs, 5000);
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
    // Clamp to valid port range (1024-65535)
    port = qBound(quint16(1024), port, quint16(65535));
    if (m_catServerPort != port) {
        m_catServerPort = port;
        save();
        emit catServerPortChanged(port);
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
        bool changed = !m_macros.contains(functionId) || m_macros[functionId].label != label ||
                       m_macros[functionId].command != command;
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
    value = qBound(0, value, 100);
    if (m_sidetoneVolume != value) {
        m_sidetoneVolume = value;
        save();
        emit sidetoneVolumeChanged(value);
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

void RadioSettings::load() {
    int count = m_settings.beginReadArray("radios");
    m_radios.clear();
    for (int i = 0; i < count; ++i) {
        m_settings.setArrayIndex(i);
        RadioEntry entry;
        entry.name = m_settings.value("name").toString();
        entry.host = m_settings.value("host").toString();
        entry.password = deobfuscatePassword(m_settings.value("password").toString());
        entry.port = m_settings.value("port").toUInt();
        entry.useTls = m_settings.value("useTls", false).toBool();
        entry.identity = m_settings.value("identity").toString();
        entry.encodeMode = m_settings.value("encodeMode", 3).toInt();             // Default EM3 (Opus Float)
        entry.streamingLatency = m_settings.value("streamingLatency", 3).toInt(); // Default SL3
        entry.displayFps = m_settings.value("displayFps", 15).toInt();            // Default 15 FPS
        m_radios.append(entry);
    }
    m_settings.endArray();

    m_lastSelectedIndex = m_settings.value("lastSelectedIndex", -1).toInt();
    m_kpodEnabled = m_settings.value("kpodEnabled", false).toBool();

    // KPA1500 settings
    m_kpa1500Host = m_settings.value("kpa1500/host", "").toString();
    m_kpa1500Port = m_settings.value("kpa1500/port", 1500).toUInt();
    m_kpa1500Enabled = m_settings.value("kpa1500/enabled", false).toBool();
    m_kpa1500PollInterval = m_settings.value("kpa1500/pollInterval", 300).toInt();

    // CAT Server settings (migrate from old rigctld keys if present)
    m_catServerEnabled = m_settings.value("catServer/enabled", m_settings.value("rigctld/enabled", false)).toBool();
    m_catServerPort = m_settings.value("catServer/port", m_settings.value("rigctld/port", 9299)).toUInt();

    // HaliKey settings
    m_halikeyPortName = m_settings.value("halikey/portName", "").toString();
    m_halikeyEnabled = m_settings.value("halikey/enabled", false).toBool();
    m_halikeyDeviceType = m_settings.value("halikey/deviceType", 0).toInt();
    m_sidetoneVolume = m_settings.value("halikey/sidetoneVolume", 30).toInt();

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

    // HaliKey settings
    m_settings.setValue("halikey/portName", m_halikeyPortName);
    m_settings.setValue("halikey/enabled", m_halikeyEnabled);
    m_settings.setValue("halikey/deviceType", m_halikeyDeviceType);
    m_settings.setValue("halikey/sidetoneVolume", m_sidetoneVolume);

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
