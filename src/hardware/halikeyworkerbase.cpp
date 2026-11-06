#include "halikeyworkerbase.h"

HaliKeyWorkerBase::HaliKeyWorkerBase(const QString &portName, QObject *parent)
    : QObject(parent), m_portName(portName) {}

void HaliKeyWorkerBase::stop() {
    m_stopRequested.store(true, std::memory_order_release);
}
