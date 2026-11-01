#ifndef KPA1500CLIENT_H
#define KPA1500CLIENT_H

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QMap>
#include <QVector>

/**
 * @brief TCP client for a KPA1500 linear amplifier's remote head server. Polls the amp with
 *        `POLL_COMMANDS` on a user-configurable interval, parses the pipelined responses, and
 *        signals each change (power, SWR, temperature, band, antenna, ATU mode + inline state,
 *        operating state, faults, firmware). Drives Kpa1500MiniPanel + Kpa1500Page.
 */
class KPA1500Client : public QObject {
    Q_OBJECT

public:
    enum ConnectionState { Disconnected, Connecting, Connected };
    Q_ENUM(ConnectionState)

    // Amp operating state (^OS response)
    enum OperatingState { StateUnknown = -1, StateStandby = 0, StateOperate = 1 };
    Q_ENUM(OperatingState)

    // Fault status, from the ^FL fault code
    enum FaultStatus { FaultNone = 0, FaultActive = 1 };
    Q_ENUM(FaultStatus)

    explicit KPA1500Client(QObject *parent = nullptr);
    ~KPA1500Client();

    void connectToHost(const QString &host, quint16 port);
    void disconnectFromHost();
    bool isConnected() const;

    void sendCommand(const QString &command);

    // Step to the next antenna enabled on the current band (^AN+;), exactly like the amp's own
    // ANTENNA button, then read back the result. Never enables or configures an antenna.
    void selectNextAntenna();
    void startPolling(int intervalMs);
    void stopPolling();

    QString bandName() const { return m_bandName; }
    FaultStatus faultStatus() const { return m_faultStatus; }
    QString faultCode() const { return m_faultCode; }
    int antenna() const { return m_antenna; }
    // Connector (1 = ANT1, 2 = ANT2) an antenna is routed through on the current band, from the
    // amp's ^AEbbALL enable map. 0 when disabled or the map has not been read yet.
    int antennaConnector(int antenna) const;
    QString serialNumber() const { return m_serialNumber; }
    QString firmwareVersion() const { return m_firmwareVersion; }

signals:
    void stateChanged(ConnectionState state);
    void connected();
    void disconnected();
    void errorOccurred(const QString &error);

    // Data update signals
    void powerChanged(double forward, double reflected, double drive);
    void swrChanged(double swr);
    void paTemperatureChanged(double tempC);
    void operatingStateChanged(OperatingState state);
    void faultStatusChanged(FaultStatus status, const QString &faultCode);
    void atuModeChanged(bool inline_);   // ^AM: ATU mode toggled
    void atuInlineChanged(bool inline_); // ^AI: ATU relay state changed
    void antennaChanged(int antenna);
    void antennaConnectorChanged(int connector); // connector of the active antenna became known/changed
    void bandNumberChanged(int bandNumber);
    void deviceInfoChanged(); // ^SN serial number or ^RVM firmware version changed

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);
    void onPollTimer();

private:
    void setState(ConnectionState state);
    void parseResponse(const QString &response);
    void parseSingleResponse(const QString &response);
    void requestEnableMap();
    // Forget every cached reading, so the first report after a (re)connect is emitted even when the
    // amplifier's value has not changed since the last session.
    void resetCachedState();

    QTcpSocket *m_socket;
    QTimer *m_pollTimer;
    QString m_receiveBuffer;

    QString m_host;
    quint16 m_port;
    ConnectionState m_state;

    // Poll command string
    static const QString POLL_COMMANDS;

    // WHY: every reading is emitted only when it changes, while the mini panel clears its display on
    // disconnect. A cache that outlived the connection kept a steady value (the PA temperature) from
    // ever reaching the panel again after a reconnect. "Unknown" is a value no report can match.
    static constexpr double kUnknownReading = -999.0;

    // Cached state values
    QString m_bandName;
    int m_bandNumber = -1;
    QVector<int> m_enableMap; // connector per antenna 1-32 for m_bandNumber; empty until read
    double m_forwardPower = kUnknownReading;
    double m_reflectedPower = kUnknownReading;
    double m_drivePower = kUnknownReading;
    double m_swr = kUnknownReading;
    double m_paTemperature = kUnknownReading;
    OperatingState m_operatingState = StateUnknown;
    FaultStatus m_faultStatus = FaultNone;
    QString m_faultCode;
    bool m_atuModeInline = false; // ^AM: ATU mode (I=inline/enabled, B=bypassed/disabled)
    bool m_atuInline = false;     // ^AI: ATU relay state (1=relays inline, 0=relays bypassed)
    int m_antenna = 1;
    QString m_serialNumber;
    QString m_firmwareVersion;
};

#endif // KPA1500CLIENT_H
