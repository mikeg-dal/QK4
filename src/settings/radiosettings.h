#ifndef RADIOSETTINGS_H
#define RADIOSETTINGS_H

#include <QMap>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QVector>

// Macro entry for programmable function keys
struct MacroEntry {
    QString functionId; // "PF1", "Fn.F1", "K-pod.1T", etc.
    QString label;      // Custom label or empty
    QString command;    // CAT command or empty

    bool isEmpty() const { return command.isEmpty(); }
    QString displayLabel() const {
        if (command.isEmpty())
            return "Unused";
        return label.isEmpty() ? "Mapped" : label;
    }
};

// RX EQ preset entry (8-band graphic equalizer)
struct EqPreset {
    QString name;       // User-defined name ("SSB", "CW", etc.)
    QVector<int> bands; // 8 values, -16 to +16 dB

    bool isEmpty() const { return bands.isEmpty() || name.isEmpty(); }
    QString displayName() const { return isEmpty() ? "---" : name; }
};

struct DxClusterEntry {
    QString host;
    quint16 port = 7000;
    QString callsign;
    bool autoConnect = false;
};

struct RadioEntry {
    QString name;
    QString host;
    QString password; // Password (used as PSK when TLS enabled)
    quint16 port;
    bool useTls = false; // Use TLS/PSK encryption (port 9204)
    QString identity;    // TLS-PSK identity (optional, empty = default)
    // Audio encode mode. See docs/k4-protocol-quirks.md -> "Audio encode modes EM0-EM3".
    int encodeMode =
        3; // 0=RAW S32LE (24-bit), 1=RAW S16LE, 2/3=Opus (same bitstream, int vs float decode); EM3 default
    int streamingLatency = 3; // Remote streaming audio latency: 0-7 (default 3)
    int displayFps = 15;      // Display FPS: 12-30 (default 15, good balance for large monitors)

    // Connect to this radio automatically when QK4 starts.
    //
    // AT MOST ONE radio may have this set. The rule is enforced in RadioSettings rather than in
    // the dialog, because settings can also be edited by hand or restored from a backup, and a
    // second flagged radio would make startup depend on list order.
    bool connectAtStartup = false;

    bool operator==(const RadioEntry &other) const {
        return name == other.name && host == other.host && port == other.port;
    }
};

class RadioSettings : public QObject {
    Q_OBJECT

public:
    static RadioSettings *instance();

    QVector<RadioEntry> radios() const;

    // The radio to connect to at startup, or -1 if none is flagged. Returns the FIRST flagged
    // entry; setConnectAtStartupRadio keeps that unique, and load() repairs a file that is not.
    int connectAtStartupIndex() const;

    // Index of the saved radio with this name, or -1. Case-insensitive, because the name is typed
    // on a command line or into a desktop shortcut, where matching the stored capitalisation
    // exactly is a needless way to fail.
    int indexOfRadioNamed(const QString &name) const;

    // Flags one radio and clears every other. Pass -1 to disable auto-connect entirely.
    void setConnectAtStartupRadio(int index);
    void addRadio(const RadioEntry &radio);
    void removeRadio(int index);
    void updateRadio(int index, const RadioEntry &radio);

    int lastSelectedIndex() const;
    void setLastSelectedIndex(int index);

    bool kpodEnabled() const;
    void setKpodEnabled(bool enabled);

    // KPA1500 Amplifier settings
    QString kpa1500Host() const;
    void setKpa1500Host(const QString &host);
    quint16 kpa1500Port() const;
    void setKpa1500Port(quint16 port);
    bool kpa1500Enabled() const;
    void setKpa1500Enabled(bool enabled);
    int kpa1500PollInterval() const;
    void setKpa1500PollInterval(int intervalMs);

    // Audio output settings
    int volume() const;
    void setVolume(int value); // 0-100, default 45
    int subVolume() const;
    void setSubVolume(int value); // 0-100, default 45 (Sub RX / VFO B)

    // Audio input (microphone) settings
    int micGain() const;
    void setMicGain(int value); // 0-100, default 25
    QString micDevice() const;
    void setMicDevice(const QString &deviceId);

    // Audio output (speaker) settings
    QString speakerDevice() const;
    void setSpeakerDevice(const QString &deviceId);

    // CAT Server settings (local TCP server for external apps)
    bool catServerEnabled() const;
    void setCatServerEnabled(bool enabled);
    quint16 catServerPort() const;
    void setCatServerPort(quint16 port);

    // TCI server settings (WebSocket server carrying both CAT and audio for WSJT-X and friends).
    // Off by default: an always-on listener would change behaviour for every user, and TCI is
    // additive - the CAT server on 9299 keeps working untouched.
    bool tciServerEnabled() const;
    void setTciServerEnabled(bool enabled);
    quint16 tciServerPort() const;
    void setTciServerPort(quint16 port);
    // Audio is the expensive half and CAT-only is a legitimate configuration, so it is separately
    // switchable. Defaults on, because carrying audio is the reason the TCI server exists.
    bool tciAudioEnabled() const;
    void setTciAudioEnabled(bool enabled);

    // Macro settings
    QMap<QString, MacroEntry> macros() const;
    MacroEntry macro(const QString &functionId) const;
    void setMacro(const QString &functionId, const QString &label, const QString &command);

    // HaliKey CW Keyer settings
    QString halikeyPortName() const;
    void setHalikeyPortName(const QString &portName);
    bool halikeyEnabled() const;
    void setHalikeyEnabled(bool enabled);
    int halikeyDeviceType() const;
    void setHalikeyDeviceType(int type); // 0=V14, 1=MiDi
    int sidetoneVolume() const;
    void setSidetoneVolume(int value); // 0-100, default 30

    // DX Cluster settings
    QVector<DxClusterEntry> dxClusters() const;
    void addDxCluster(const DxClusterEntry &entry);
    void removeDxCluster(int index);
    void updateDxCluster(int index, const DxClusterEntry &entry);
    int dxClusterSpotAge() const;
    void setDxClusterSpotAge(int seconds);
    QString dxClusterCallsign() const;
    void setDxClusterCallsign(const QString &callsign);
    int dxClusterSpotFontSize() const;
    void setDxClusterSpotFontSize(int sizePx);

    // RX EQ Presets (4 slots)
    EqPreset rxEqPreset(int index) const;                  // Get preset 0-3
    void setRxEqPreset(int index, const EqPreset &preset); // Set preset 0-3
    void clearRxEqPreset(int index);                       // Clear preset 0-3

    // TX EQ Presets (4 slots)
    EqPreset txEqPreset(int index) const;                  // Get preset 0-3
    void setTxEqPreset(int index, const EqPreset &preset); // Set preset 0-3
    void clearTxEqPreset(int index);                       // Clear preset 0-3

    // KPOD+ encode mode (KZ/KX). Keyer speed, CW pitch, iambic mode and paddle
    // orientation are no longer stored — the KPOD+ mirrors the connected K4.
    int kpodPlusEncodeMode() const;
    void setKpodPlusEncodeMode(int mode); // 0=KZ, 1=KX

    // CW/data text-decode popup font size (per receiver, pixel value)
    int textDecodeFontSize(bool subRx) const;
    void setTextDecodeFontSize(bool subRx, int sizePx);

    // Station / operator info
    int iaruRegion() const; // 1, 2, or 3 — drives the panadapter band-plan overlay
    void setIaruRegion(int region);
    QString callSign() const;
    void setCallSign(const QString &callSign);
    QString gridSquare() const;
    void setGridSquare(const QString &grid);
    QString operatorName() const;
    void setOperatorName(const QString &name);
    QString qth() const;
    void setQth(const QString &qth);
    bool bandPlanOverlayEnabled() const;
    void setBandPlanOverlayEnabled(bool enabled);

signals:
    void radiosChanged();
    void kpodEnabledChanged(bool enabled);
    void kpa1500EnabledChanged(bool enabled);
    void kpa1500SettingsChanged();
    void kpa1500PollIntervalChanged(int intervalMs);
    void micGainChanged(int value);
    void micDeviceChanged(const QString &deviceId);
    void speakerDeviceChanged(const QString &deviceId);
    void catServerEnabledChanged(bool enabled);
    void catServerPortChanged(quint16 port);
    void tciServerEnabledChanged(bool enabled);
    void tciServerPortChanged(quint16 port);
    void tciAudioEnabledChanged(bool enabled);
    void macrosChanged();
    void halikeyEnabledChanged(bool enabled);
    void halikeyPortNameChanged(const QString &portName);
    void halikeyDeviceTypeChanged(int type);
    void sidetoneVolumeChanged(int value);
    void rxEqPresetsChanged();
    void txEqPresetsChanged();
    void dxClusterSettingsChanged();
    void kpodPlusSettingsChanged();
    void iaruRegionChanged(int region);
    void bandPlanOverlayEnabledChanged(bool enabled);

private:
    explicit RadioSettings(QObject *parent = nullptr);
    void load();
    void save();
    void sortRadios();

    QVector<RadioEntry> m_radios;
    int m_lastSelectedIndex;
    bool m_kpodEnabled;

    // KPA1500 settings
    QString m_kpa1500Host;
    quint16 m_kpa1500Port = 1500;
    bool m_kpa1500Enabled = false;
    int m_kpa1500PollInterval = 300; // Default: 300ms for responsive meters

    // CAT Server settings
    bool m_catServerEnabled = false;
    quint16 m_catServerPort = 9299;

    // TCI server settings. 50001 is the TCI convention and what WSJT-X defaults to.
    bool m_tciServerEnabled = false;
    quint16 m_tciServerPort = 50001;
    bool m_tciAudioEnabled = true;

    // HaliKey settings
    QString m_halikeyPortName;
    bool m_halikeyEnabled = false;
    int m_halikeyDeviceType = 0; // 0=V14, 1=MiDi
    int m_sidetoneVolume = 30;   // Default 30%

    // Macro settings
    QMap<QString, MacroEntry> m_macros;

    // RX EQ Presets (4 slots)
    EqPreset m_rxEqPresets[4];

    // TX EQ Presets (4 slots)
    EqPreset m_txEqPresets[4];

    // DX Cluster settings
    QVector<DxClusterEntry> m_dxClusters;
    int m_dxClusterSpotAge = 600; // Default 10 minutes
    QString m_dxClusterCallsign;
    int m_dxClusterSpotFontSize = 11; // K4Styles::Dimensions::FontSizeSpot default; clamped to [8, 16]

    // KPOD+ encode mode (0=KZ, 1=KX). Keyer speed / CW pitch / iambic mode /
    // paddle orientation are not stored — the KPOD+ mirrors the K4.
    int m_kpodPlusEncodeMode = 0;

    QSettings m_settings;
};

#endif // RADIOSETTINGS_H
