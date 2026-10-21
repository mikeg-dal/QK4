// SPDX-License-Identifier: GPL-3.0-or-later
//
// KzEgress: the lock-free path from the CW keying threads to the I/O thread's socket. Frames come out
// whole and in order, are never lost while there is room, are refused and counted when there is
// not, and the owning thread is woken across threads, including after a moveToThread. Plus the
// latency histogram the I/O thread keeps. Run under ThreadSanitizer as well as plainly.

#include "network/kzegress.h"
#include "utils/latencyhistogram.h"

#include <QCoreApplication>
#include <QThread>
#include <QtTest/QtTest>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

// Collects what a drain hands over, on whichever thread the egress lives on.
struct Collector {
    std::mutex mutex;
    std::vector<std::string> frames;
    LatencyHistogram latency;

    void connectTo(KzEgress &egress) {
        QObject::connect(&egress, &KzEgress::framesReady, &egress, [this, &egress] {
            egress.drain([this](std::string_view frame, std::int64_t enqueuedNs) {
                latency.record((KzEgress::nowNs() - enqueuedNs) / 1000);
                std::lock_guard lock(mutex);
                frames.emplace_back(frame);
            });
        });
    }
    std::size_t count() {
        std::lock_guard lock(mutex);
        return frames.size();
    }
    std::vector<std::string> taken() {
        std::lock_guard lock(mutex);
        return frames;
    }
};

std::string numbered(char producer, int n) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "KZ%c%05d;", producer, n);
    return buf;
}

} // namespace

class TestKzEgress : public QObject {
    Q_OBJECT

private slots:
    void sourceAndEligibilitySurviveADeviceChangeBeforeDrain() {
        KzEgress egress;
        egress.setSoftwareKeyingAllowed(true);
        const quint64 original = egress.softwareEpoch();
        egress.softwareLink()->send("KZD0000;");
        egress.setSoftwareKeyingAllowed(false);
        egress.setSoftwareKeyingAllowed(true);
        QVERIFY(egress.softwareEpoch() != original);
        egress.softwareLink()->send("KZU0060;");
        QVERIFY(egress.post("KZD0000;KZU0060;"));
        int seen = 0;
        egress.drainTagged(
            [&](std::string_view frame, std::int64_t, KzEgress::Source source, quint64 epoch, bool eligible) {
                if (seen < 2) {
                    QCOMPARE(source, KzEgress::Source::Software);
                    QCOMPARE(epoch, original);
                    QVERIFY(eligible);
                    QCOMPARE(frame, seen == 0 ? std::string_view("KZD0000;") : std::string_view("KZU0060;"));
                } else {
                    QCOMPARE(source, KzEgress::Source::Kpod);
                }
                ++seen;
            });
        QCOMPARE(seen, 3);
    }

    void disabledDownCannotBecomeEligibleWhenTheDeviceIsReenabled() {
        KzEgress egress;
        egress.softwareLink()->send("KZD0000;");
        egress.setSoftwareKeyingAllowed(true);
        egress.softwareLink()->send("KZU0060;");
        int seen = 0;
        egress.drainTagged([&](std::string_view, std::int64_t, KzEgress::Source source, quint64 epoch, bool eligible) {
            QCOMPARE(source, KzEgress::Source::Software);
            QCOMPARE(epoch, quint64(0));
            QCOMPARE(eligible, seen != 0);
            ++seen;
        });
        QCOMPARE(seen, 2);
    }

    void framesArriveWholeAndInOrder() {
        KzEgress egress;
        Collector c;
        c.connectTo(egress);
        QVERIFY(egress.post("KZD0000;"));
        QVERIFY(egress.post("KZU0060;"));
        QVERIFY(egress.post("ML0000;"));
        QTRY_COMPARE(c.count(), std::size_t(3));
        QCOMPARE(c.taken(), (std::vector<std::string>{"KZD0000;", "KZU0060;", "ML0000;"}));
        QCOMPARE(egress.droppedCount(), std::uint64_t(0));
    }

    // The longest KPOD+ batch fits; anything longer, or empty, is refused rather than cut.
    void refusesEmptyAndOverlongFrames() {
        KzEgress egress;
        const std::string longest(KzEgress::kMaxFrameBytes, 'K');
        QVERIFY(egress.post(longest));
        QVERIFY(!egress.post(std::string(KzEgress::kMaxFrameBytes + 1, 'K')));
        QVERIFY(!egress.post(""));
        QCOMPARE(egress.droppedCount(), std::uint64_t(2));
        std::vector<std::string> out;
        egress.drain([&](std::string_view f, std::int64_t) { out.emplace_back(f); });
        QCOMPARE(out, std::vector<std::string>{longest});
    }

    // With the owning thread not draining, every slot fills, the next post is refused and counted,
    // and a later drain still hands over everything that was accepted, in order.
    void fullQueueRefusesAndKeepsWhatItHas() {
        KzEgress egress;
        for (std::size_t i = 0; i < KzEgress::kCapacity; ++i)
            QVERIFY(egress.post(numbered('A', static_cast<int>(i))));
        QVERIFY(!egress.post("KZD0000;"));
        QCOMPARE(egress.droppedCount(), std::uint64_t(1));
        int next = 0;
        bool inOrder = true;
        const int handed =
            egress.drain([&](std::string_view f, std::int64_t) { inOrder = inOrder && f == numbered('A', next++); });
        QCOMPARE(handed, static_cast<int>(KzEgress::kCapacity));
        QVERIFY(inOrder);
        QVERIFY(egress.post("KZD0000;")); // room again
    }

    // The keyer and the KPOD+ EP02 reader post from two threads at once: nothing is lost or torn,
    // and each thread's frames come out in the order it posted them.
    void twoProducersKeepTheirOwnOrder() {
        KzEgress egress;
        Collector c;
        c.connectTo(egress);
        constexpr int kEach = 5000;
        auto produce = [&egress](char who) {
            for (int i = 0; i < kEach; ++i) {
                const std::string f = numbered(who, i);
                while (!egress.post(f)) // only when the test thread has fallen 1024 behind
                    std::this_thread::yield();
            }
        };
        std::thread a(produce, 'A');
        std::thread b(produce, 'B');
        QTRY_COMPARE_WITH_TIMEOUT(c.count(), std::size_t(2 * kEach), 20000);
        a.join();
        b.join();
        int nextA = 0, nextB = 0;
        for (const auto &f : c.taken()) {
            if (f[2] == 'A')
                QCOMPARE(f, numbered('A', nextA++));
            else
                QCOMPARE(f, numbered('B', nextB++));
        }
        QCOMPARE(nextA, kEach);
        QCOMPARE(nextB, kEach);
    }

    // As in the app: the egress is created with its owner on one thread and moved to the I/O
    // thread, and a third thread posts. The wake-up must reach the I/O thread's event loop. Also
    // reports the post-to-drain latency of that wake-up on this machine.
    void wakesTheThreadItWasMovedTo() {
        QThread io;
        auto *egress = new KzEgress;
        Collector c;
        c.connectTo(*egress);
        std::atomic<Qt::HANDLE> drainedOn{nullptr};
        QObject::connect(egress, &KzEgress::framesReady, egress,
                         [&drainedOn] { drainedOn.store(QThread::currentThreadId()); });
        egress->moveToThread(&io);
        io.start();

        constexpr int kFrames = 500;
        std::thread keyer([egress] {
            for (int i = 0; i < kFrames; ++i) {
                egress->post(numbered('K', i));
                std::this_thread::sleep_for(std::chrono::microseconds(1000));
            }
        });
        keyer.join();
        QTRY_COMPARE(c.count(), std::size_t(kFrames));
        QVERIFY(drainedOn.load() != nullptr);
        QVERIFY(drainedOn.load() != QThread::currentThreadId());

        const LatencyHistogram::Counts h = c.latency.snapshot();
        qInfo("KZ egress post -> drain on another thread, %llu frames: median <= %lld us, 99%% <= %lld us, max %lld us",
              static_cast<unsigned long long>(h.total), static_cast<long long>(h.quantileBoundUs(0.5)),
              static_cast<long long>(h.quantileBoundUs(0.99)), static_cast<long long>(h.maxUs));

        QMetaObject::invokeMethod(egress, [egress] { delete egress; }, Qt::BlockingQueuedConnection);
        io.quit();
        io.wait();
    }

    // ---- LatencyHistogram ----

    void histogramBucketsAreInclusiveUpperBounds() {
        QCOMPARE(LatencyHistogram::bucketFor(0), 0);
        QCOMPARE(LatencyHistogram::bucketFor(50), 0);
        QCOMPARE(LatencyHistogram::bucketFor(51), 1);
        QCOMPARE(LatencyHistogram::bucketFor(1000), 4);
        QCOMPARE(LatencyHistogram::bucketFor(50'000), LatencyHistogram::kBuckets - 2);
        QCOMPARE(LatencyHistogram::bucketFor(50'001), LatencyHistogram::kBuckets - 1);
    }

    void histogramQuantilesReportTheBucketBound() {
        LatencyHistogram h;
        QCOMPARE(h.snapshot().quantileBoundUs(0.5), std::int64_t(0));
        for (int i = 0; i < 98; ++i)
            h.record(30);
        h.record(700);
        h.record(80'000);
        const auto c = h.snapshot();
        QCOMPARE(c.total, std::uint64_t(100));
        QCOMPARE(c.quantileBoundUs(0.5), std::int64_t(50));
        QCOMPARE(c.quantileBoundUs(0.99), std::int64_t(1000));
        QCOMPARE(c.quantileBoundUs(1.0), std::int64_t(80'000)); // the open bucket reports the max
        QCOMPARE(c.maxUs, std::int64_t(80'000));
        h.reset();
        QCOMPARE(h.snapshot().total, std::uint64_t(0));
    }
};

QTEST_GUILESS_MAIN(TestKzEgress)
#include "test_kzegress.moc"
