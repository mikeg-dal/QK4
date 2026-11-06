#ifndef KPODUDEVMATCH_H
#define KPODUDEVMATCH_H

#include <QByteArray>
#include <QtGlobal>

// Whether a udev usb_device event is for vendorId:productId, from the event's own properties.
// PRODUCT is the kernel's "vid/pid/bcd" in unpadded hex and is on every event, add and remove;
// ID_VENDOR_ID / ID_MODEL_ID (4-digit hex) are udev's and are the fallback. Platform-neutral so
// it can be tested off Linux; used by KpodUdevWorker.
namespace KpodUdevMatch {
inline bool eventMatches(const char *product, const char *idVendor, const char *idModel, quint16 vendorId,
                         quint16 productId) {
    if (product) {
        const QList<QByteArray> parts = QByteArray(product).split('/');
        if (parts.size() >= 2) {
            bool vidOk = false;
            bool pidOk = false;
            const uint vid = parts[0].toUInt(&vidOk, 16);
            const uint pid = parts[1].toUInt(&pidOk, 16);
            if (vidOk && pidOk)
                return vid == vendorId && pid == productId;
        }
    }
    if (idVendor && idModel) {
        bool vidOk = false;
        bool pidOk = false;
        const uint vid = QByteArray(idVendor).toUInt(&vidOk, 16);
        const uint pid = QByteArray(idModel).toUInt(&pidOk, 16);
        return vidOk && pidOk && vid == vendorId && pid == productId;
    }
    return false;
}
} // namespace KpodUdevMatch

#endif // KPODUDEVMATCH_H
