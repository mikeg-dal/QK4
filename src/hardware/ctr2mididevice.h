#ifndef CTR2MIDIDEVICE_H
#define CTR2MIDIDEVICE_H

#include <QObject>
#include <QStringList>
#include <memory>
#include <vector>

class RtMidiIn;

class Ctr2MidiDevice final : public QObject {
    Q_OBJECT
public:
    explicit Ctr2MidiDevice(QObject *parent = nullptr);
    ~Ctr2MidiDevice() override;
    bool openPort(const QString &portName);
    void closePort();
    bool isConnected() const { return m_connected; }
    QString portName() const { return m_portName; }
    QString statusMessage() const { return m_status; }
    static QStringList availableMidiDevices();
    static void startMidiScan() {}

signals:
    void connected();
    void disconnected();
    void connectionError(const QString &error);
    void rawMidiEvent(int status, int data1, int data2);

private:
    static void midiCallback(double, std::vector<unsigned char> *message, void *userData);
    std::unique_ptr<RtMidiIn> m_input;
    QString m_portName;
    QString m_status = QStringLiteral("Not connected");
    bool m_connected = false;
};
#endif
