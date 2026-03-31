#ifndef HARDWARECONTROLLER_H
#define HARDWARECONTROLLER_H

#include <QObject>
#include <QThread>

class KpodDevice;
class HalikeyDevice;
class IambicKeyer;
class SidetoneGenerator;
class RadioState;
class ConnectionController;

class HardwareController : public QObject {
    Q_OBJECT

public:
    HardwareController(RadioState *radioState, ConnectionController *connController, QObject *parent = nullptr);
    ~HardwareController();

    // Accessors for OptionsDialog integration
    KpodDevice *kpodDevice() const { return m_kpodDevice; }
    HalikeyDevice *halikeyDevice() const { return m_halikeyDevice; }
    IambicKeyer *keyer() const { return m_iambicKeyer; }
    SidetoneGenerator *sidetone() const { return m_sidetoneGenerator; }

signals:
    // KPOD button press → MainWindow dispatches macro
    void macroRequested(const QString &functionId);

private slots:
    void onKpodEncoderRotated(int ticks);
    void onKpodRockerChanged(int position);
    void onKpodPollError(const QString &error);
    void onKpodEnabledChanged(bool enabled);

private:
    RadioState *m_radioState;
    ConnectionController *m_connectionController;

    // KPOD USB tuning knob
    KpodDevice *m_kpodDevice;

    // HaliKey CW paddle device
    HalikeyDevice *m_halikeyDevice;

    // Iambic keyer state machine (HighPriority thread)
    IambicKeyer *m_iambicKeyer;
    QThread *m_keyerThread = nullptr;

    // Local sidetone generator (dedicated thread)
    SidetoneGenerator *m_sidetoneGenerator;
    QThread *m_sidetoneThread = nullptr;
};

#endif // HARDWARECONTROLLER_H
