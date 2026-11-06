#include "kpodudevworker.h"
#include "logging/logcategories.h"

#include <QtGlobal>

#ifdef Q_OS_LINUX

#include "hardware/kpodudevmatch.h"
#include <QLoggingCategory>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <libudev.h>
#include <poll.h>
#include <unistd.h>

// WHY: hidapi's hid_enumerate() on Linux calls libusb_get_device_list() and opens each
// candidate device to read descriptor strings. Polling that every 2 s for hotplug detection
// dominated idle CPU on Linux laptops with several USB devices (~23 % observed). udev's
// netlink monitor is kernel-event-driven: zero idle CPU, sub-100 ms hotplug latency, and
// the subsystem/devtype filter is applied in the kernel so unrelated USB traffic never
// crosses to userspace.

KpodUdevWorker::KpodUdevWorker(quint16 vendorId, quint16 productId, QObject *parent)
    : QObject(parent), m_vendorId(vendorId), m_productId(productId) {
    // WHY here, not in start(): stop() writes to the pipe from another thread, so the descriptors
    // must exist before the worker thread does and never change afterwards.
    if (::pipe2(m_wakePipe, O_CLOEXEC | O_NONBLOCK) < 0) {
        qCWarning(lcKpodUdev) << "pipe2 failed:" << ::strerror(errno);
        m_wakePipe[0] = m_wakePipe[1] = -1;
    }
}

KpodUdevWorker::~KpodUdevWorker() {
    if (m_wakePipe[0] >= 0)
        ::close(m_wakePipe[0]);
    if (m_wakePipe[1] >= 0)
        ::close(m_wakePipe[1]);
}

void KpodUdevWorker::stop() {
    m_stopRequested.store(true, std::memory_order_release);
    if (m_wakePipe[1] >= 0) {
        const char b = 'x';
        ssize_t n = ::write(m_wakePipe[1], &b, 1);
        (void)n; // best-effort wakeup; loop also checks m_stopRequested
    }
}

void KpodUdevWorker::start() {
    if (m_stopRequested.load(std::memory_order_acquire) || m_wakePipe[0] < 0)
        return;

    udev *udev = udev_new();
    if (!udev) {
        qCWarning(lcKpodUdev) << "udev_new failed";
        return;
    }

    udev_monitor *monitor = udev_monitor_new_from_netlink(udev, "udev");
    if (!monitor) {
        qCWarning(lcKpodUdev) << "udev_monitor_new_from_netlink failed";
        udev_unref(udev);
        return;
    }

    if (udev_monitor_filter_add_match_subsystem_devtype(monitor, "usb", "usb_device") < 0) {
        qCWarning(lcKpodUdev) << "udev_monitor_filter_add_match_subsystem_devtype failed";
        udev_monitor_unref(monitor);
        udev_unref(udev);
        return;
    }

    if (udev_monitor_enable_receiving(monitor) < 0) {
        qCWarning(lcKpodUdev) << "udev_monitor_enable_receiving failed";
        udev_monitor_unref(monitor);
        udev_unref(udev);
        return;
    }

    int monitorFd = udev_monitor_get_fd(monitor);
    if (monitorFd < 0) {
        qCWarning(lcKpodUdev) << "udev_monitor_get_fd returned" << monitorFd;
        udev_monitor_unref(monitor);
        udev_unref(udev);
        return;
    }

    while (!m_stopRequested.load(std::memory_order_acquire)) {
        struct pollfd fds[2];
        fds[0].fd = monitorFd;
        fds[0].events = POLLIN;
        fds[0].revents = 0;
        fds[1].fd = m_wakePipe[0];
        fds[1].events = POLLIN;
        fds[1].revents = 0;

        int rc = ::poll(fds, 2, -1);
        if (rc < 0) {
            if (errno == EINTR)
                continue;
            qCWarning(lcKpodUdev) << "poll failed:" << ::strerror(errno);
            break;
        }

        if (fds[1].revents & POLLIN) {
            char drain[64];
            while (::read(m_wakePipe[0], drain, sizeof(drain)) > 0) {
            }
            // Either stop() was called or spurious — re-check loop condition.
            continue;
        }

        if (fds[0].revents & POLLIN) {
            // Drain all pending events (multiple may arrive between wakeups).
            while (udev_device *dev = udev_monitor_receive_device(monitor)) {
                const char *action = udev_device_get_action(dev);
                // WHY event properties, not sysattrs: on "remove" the sysfs node is already gone,
                // so idVendor/idProduct read as null and an unplug was never reported.
                const bool ours = KpodUdevMatch::eventMatches(
                    udev_device_get_property_value(dev, "PRODUCT"), udev_device_get_property_value(dev, "ID_VENDOR_ID"),
                    udev_device_get_property_value(dev, "ID_MODEL_ID"), m_vendorId, m_productId);
                if (action && ours) {
                    if (std::strcmp(action, "add") == 0) {
                        emit deviceArrived();
                    } else if (std::strcmp(action, "remove") == 0) {
                        emit deviceRemoved();
                    }
                }
                udev_device_unref(dev);
            }
        }
    }

    udev_monitor_unref(monitor);
    udev_unref(udev);
}

#endif // Q_OS_LINUX
