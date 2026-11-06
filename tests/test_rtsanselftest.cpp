// SPDX-License-Identifier: GPL-3.0-or-later
//
// Proof that the RealtimeSanitizer build checks anything. Built only with QK4_TESTS_RTSAN. Without
// it, a build in which QK4_NONBLOCKING expanded to nothing, or -fsanitize=realtime was dropped,
// would pass every suite while checking nothing.
//
//   test_rtsanselftest malloc  - allocates in a QK4_NONBLOCKING function: must be reported
//   test_rtsanselftest mutex   - locks a std::mutex there: must be reported
//   test_rtsanselftest allowed - RtSan::Allow, RtSan::BoundedMutex, RingDeque within its capacity
//                                and FunctionRef there: must not be

#include "utils/functionref.h"
#include "utils/ringdeque.h"
#include "utils/rtsan.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>

#ifndef QK4_HAS_RTSAN
#error "test_rtsanselftest is built only with -fsanitize=realtime (QK4_TESTS_RTSAN)"
#endif

namespace {

std::mutex g_plainMutex;
RtSan::BoundedMutex g_boundedMutex;

// WHY volatile and opaque: the allocation must reach malloc, not be optimised away.
int allocates() noexcept QK4_NONBLOCKING {
    auto p = std::make_unique<int[]>(64);
    p[0] = 1;
    return static_cast<volatile int *>(p.get())[0];
}

int locks() noexcept QK4_NONBLOCKING {
    std::lock_guard lock(g_plainMutex);
    return 1;
}

int sum(FunctionRef<int(int)> f) noexcept QK4_NONBLOCKING {
    return f(1) + f(2);
}

int allowed(RingDeque<int> &queue) noexcept QK4_NONBLOCKING {
    int total = 0;
    {
        RtSan::Allow allow;
        total += allocates();
    }
    {
        std::lock_guard lock(g_boundedMutex);
        ++total;
    }
    for (int i = 0; i < 100; ++i) {
        queue.push_back(i);
        queue.insert(0, i);
        total += queue.front();
        queue.pop_front();
        queue.pop_front();
    }
    const int k = 3;
    total += sum([k](int x) { return x * k; });
    return total;
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s malloc|mutex|allowed\n", argv[0]);
        return 2;
    }
    if (std::strcmp(argv[1], "malloc") == 0)
        return allocates() == 1 ? 0 : 3; // reaching the return means RTSan saw nothing
    if (std::strcmp(argv[1], "mutex") == 0)
        return locks() == 1 ? 0 : 3;
    if (std::strcmp(argv[1], "allowed") == 0) {
        RingDeque<int> queue(8);
        allowed(queue);
        std::printf("allowed: no report\n");
        return 0;
    }
    return 2;
}
