#ifndef HIDAPIGUARD_H
#define HIDAPIGUARD_H

#include <hidapi/hidapi.h>

#include <mutex>

// hidapi's platform backends own process-global state. In particular, the macOS
// backend may initialize from hid_enumerate(), so even workers using unrelated
// devices must not enter hidapi concurrently. The recursive mutex lets worker
// helpers call one another while retaining one process-wide serialization point.
class HidApiGuard {
public:
    class Lock {
    public:
        Lock() : m_lock(HidApiGuard::mutex()) {}

    private:
        std::lock_guard<std::recursive_mutex> m_lock;
    };

    static bool acquire() {
        Lock lock;
        if (!initialized()) {
            if (hid_init() != 0)
                return false;
            initialized() = true;
        }
        ++refCount();
        return true;
    }

    static void release() {
        Lock lock;
        if (refCount() == 0)
            return;
        // On macOS the IOHIDManager is bound to the run loop/thread that first
        // initialized hidapi. The last worker to stop may be a different one;
        // calling hid_exit there traps in IOHIDManagerUnscheduleFromRunLoop.
        // Keep this process-wide singleton alive until normal process cleanup.
        --refCount();
    }

private:
    static std::recursive_mutex &mutex() {
        static std::recursive_mutex instance;
        return instance;
    }

    static unsigned &refCount() {
        static unsigned count = 0;
        return count;
    }

    static bool &initialized() {
        static bool value = false;
        return value;
    }
};

#endif // HIDAPIGUARD_H
