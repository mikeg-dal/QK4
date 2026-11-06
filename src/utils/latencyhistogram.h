// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fixed-bucket latency histogram in microseconds. One writer records, any thread reads: every
// counter is a relaxed atomic, so recording never locks or allocates and a reader sees each bucket
// whole (the set of buckets may be a sample or two apart, which a once-a-minute summary tolerates).

#ifndef LATENCYHISTOGRAM_H
#define LATENCYHISTOGRAM_H

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>

class LatencyHistogram {
public:
    // Upper bounds, inclusive, in µs. The last bucket is everything above the previous bound.
    static constexpr std::array<std::int64_t, 11> kBoundsUs{50,    100,    200,    500,    1'000,    2'000,
                                                            5'000, 10'000, 20'000, 50'000, INT64_MAX};
    static constexpr int kBuckets = static_cast<int>(kBoundsUs.size());

    struct Counts {
        std::array<std::uint64_t, kBuckets> buckets{};
        std::uint64_t total = 0;
        std::int64_t maxUs = 0;

        // Upper bound of the bucket holding the q-quantile (0 < q <= 1), or 0 with no samples.
        std::int64_t quantileBoundUs(double q) const {
            if (total == 0)
                return 0;
            const auto rank = static_cast<std::uint64_t>(q * static_cast<double>(total) + 0.999999);
            std::uint64_t seen = 0;
            for (int i = 0; i < kBuckets; ++i) {
                seen += buckets[static_cast<size_t>(i)];
                if (seen >= std::max<std::uint64_t>(rank, 1))
                    return i == kBuckets - 1 ? maxUs : kBoundsUs[static_cast<size_t>(i)];
            }
            return maxUs;
        }
    };

    static int bucketFor(std::int64_t us) {
        int i = 0;
        while (i < kBuckets - 1 && us > kBoundsUs[static_cast<size_t>(i)])
            ++i;
        return i;
    }

    // Writer thread only.
    void record(std::int64_t us) {
        us = std::max<std::int64_t>(us, 0);
        m_buckets[static_cast<size_t>(bucketFor(us))].fetch_add(1, std::memory_order_relaxed);
        if (us > m_maxUs.load(std::memory_order_relaxed))
            m_maxUs.store(us, std::memory_order_relaxed);
    }

    // Writer thread only: forget everything (a new session).
    void reset() {
        for (auto &b : m_buckets)
            b.store(0, std::memory_order_relaxed);
        m_maxUs.store(0, std::memory_order_relaxed);
    }

    Counts snapshot() const {
        Counts c;
        for (int i = 0; i < kBuckets; ++i) {
            c.buckets[static_cast<size_t>(i)] = m_buckets[static_cast<size_t>(i)].load(std::memory_order_relaxed);
            c.total += c.buckets[static_cast<size_t>(i)];
        }
        c.maxUs = m_maxUs.load(std::memory_order_relaxed);
        return c;
    }

private:
    std::array<std::atomic<std::uint64_t>, kBuckets> m_buckets{};
    std::atomic<std::int64_t> m_maxUs{0};
};

#endif // LATENCYHISTOGRAM_H
