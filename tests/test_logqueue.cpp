#include <QtTest/QtTest>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "logging/logqueue.h"

using Logging::LogQueue;
using Logging::LogRecord;

namespace {

LogRecord record(int producer, int sequence) {
    LogRecord r;
    r.line = sequence;
    r.wallMs = producer;
    r.message = QStringLiteral("p%1 #%2").arg(producer).arg(sequence);
    return r;
}

} // namespace

class LogQueueTests : public QObject {
    Q_OBJECT

private slots:
    void fullQueueDropsAndCounts() {
        LogQueue queue(4);
        for (int i = 0; i < 4; ++i)
            QVERIFY(queue.push(record(0, i), false));
        QVERIFY(!queue.push(record(0, 4), false));
        QVERIFY(!queue.push(record(0, 5), true));
        QCOMPARE(queue.takeDropped(), quint64(2));
        QCOMPARE(queue.takeDropped(), quint64(0));

        std::vector<LogRecord> spare(queue.capacity());
        QCOMPARE(queue.take(spare), std::size_t(4));
        QCOMPARE(spare[3].line, 3);
        LogQueue::release(spare, 4);
        QVERIFY(spare[3].message.isNull());
        QVERIFY(queue.push(record(0, 6), false));
    }

    void realtimePushNeverWaitsForTheLock() {
        LogQueue queue(16);
        std::atomic<bool> holding{false};
        std::atomic<bool> release{false};
        // Keep the queue's lock contended from another thread with back-to-back takes.
        std::thread hog([&] {
            std::vector<LogRecord> spare(queue.capacity());
            holding = true;
            while (!release.load())
                queue.take(spare);
        });
        while (!holding.load())
            std::this_thread::yield();

        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 10000; ++i)
            queue.push(record(1, i), true);
        const auto elapsed = std::chrono::steady_clock::now() - start;
        release = true;
        hog.join();
        // 10k try_lock pushes against a contended lock: bounded, never parked on the mutex.
        QVERIFY2(elapsed < std::chrono::seconds(2), "realtime pushes blocked");
    }

    void everyRecordIsDeliveredOrCounted() {
        constexpr int producers = 8;
        constexpr int perProducer = 100000;
        LogQueue queue(1024);
        std::atomic<int> running{producers};
        std::vector<std::thread> threads;
        for (int p = 0; p < producers; ++p) {
            threads.emplace_back([&, p] {
                for (int i = 0; i < perProducer; ++i)
                    queue.push(record(p, i), p % 2 == 0);
                running.fetch_sub(1);
            });
        }

        quint64 received = 0;
        std::vector<int> lastSeen(producers, -1);
        bool ordered = true;
        std::vector<LogRecord> spare(queue.capacity());
        auto collect = [&] {
            const std::size_t n = queue.take(spare);
            for (std::size_t i = 0; i < n; ++i) {
                const int p = int(spare[i].wallMs);
                if (spare[i].line <= lastSeen[p])
                    ordered = false;
                lastSeen[p] = spare[i].line;
            }
            received += n;
            LogQueue::release(spare, n);
        };
        while (running.load() > 0) {
            collect();
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        for (auto &t : threads)
            t.join();
        collect();

        const quint64 dropped = queue.takeDropped();
        QCOMPARE(received + dropped, quint64(producers) * perProducer);
        QVERIFY(ordered);
        QVERIFY(received > 0);
    }
};

QTEST_GUILESS_MAIN(LogQueueTests)
#include "test_logqueue.moc"
