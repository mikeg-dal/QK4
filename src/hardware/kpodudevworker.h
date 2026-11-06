#ifndef KPODUDEVWORKER_H
#define KPODUDEVWORKER_H

#include <QtGlobal>

#ifdef Q_OS_LINUX

#include <QObject>
#include <atomic>

class KpodUdevWorker : public QObject {
    Q_OBJECT

public:
    explicit KpodUdevWorker(quint16 vendorId, quint16 productId, QObject *parent = nullptr);
    ~KpodUdevWorker() override;

    // Thread-safe; wakes the poll() loop via the self-pipe.
    void stop();

public slots:
    // Runs the udev poll loop until stop() is called. Wired to QThread::started.
    void start();

signals:
    void deviceArrived();
    void deviceRemoved();

private:
    quint16 m_vendorId;
    quint16 m_productId;
    // WHY a request flag the loop never resets: stop() may arrive before start() runs.
    std::atomic<bool> m_stopRequested{false};
    int m_wakePipe[2] = {-1, -1}; // created in the constructor, never changed after
};

#endif // Q_OS_LINUX

#endif // KPODUDEVWORKER_H
