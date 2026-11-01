#include "utils/spscring.h"

#include <QtTest>

#include <array>
#include <atomic>
#include <thread>

// The lock-free rings that carry decoded RX audio from the I/O thread to the audio thread (in
// place) and the keyer's events and requests (by value). Run under ThreadSanitizer as well as
// plainly: the threaded cases are the ones that matter there.
namespace {

struct Packet {
    int seq = -1;
    int floats = 0;
    std::array<float, 64> samples{};
};

} // namespace

class TestSpscRing : public QObject {
    Q_OBJECT
private slots:
    void emptyRingHasNoFront();
    void fillsToCapacityThenRefuses();
    void keepsOrderAcrossWrap();
    void peekSeesQueuedWithoutPopping();
    void uncommittedWriteIsInvisible();
    void threadedProducerConsumerKeepsEveryPacketIntact();
    void threadedFullRingDropsNewestOnly();
    void byValuePushPopKeepsOrderAndRefusesWhenFull();
    void threadedByValueKeepsEveryItemInOrder();
};

void TestSpscRing::emptyRingHasNoFront() {
    SpscRing<Packet, 4> ring;
    QVERIFY(ring.empty());
    QVERIFY(ring.front() == nullptr);
    QVERIFY(ring.peek(0) == nullptr);
}

void TestSpscRing::fillsToCapacityThenRefuses() {
    SpscRing<Packet, 4> ring;
    for (int i = 0; i < 4; ++i) {
        Packet *p = ring.beginWrite();
        QVERIFY(p);
        p->seq = i;
        ring.commitWrite();
    }
    QCOMPARE(ring.size(), std::size_t(4));
    QVERIFY(ring.beginWrite() == nullptr);
    ring.pop();
    QVERIFY(ring.beginWrite() != nullptr);
}

void TestSpscRing::keepsOrderAcrossWrap() {
    SpscRing<Packet, 4> ring;
    int next = 0;
    for (int round = 0; round < 10; ++round) {
        for (int i = 0; i < 3; ++i) {
            Packet *p = ring.beginWrite();
            QVERIFY(p);
            p->seq = round * 3 + i;
            ring.commitWrite();
        }
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(ring.front()->seq, next++);
            ring.pop();
        }
    }
    QVERIFY(ring.empty());
}

void TestSpscRing::peekSeesQueuedWithoutPopping() {
    SpscRing<Packet, 8> ring;
    for (int i = 0; i < 3; ++i) {
        ring.beginWrite()->seq = i;
        ring.commitWrite();
    }
    QCOMPARE(ring.peek(0)->seq, 0);
    QCOMPARE(ring.peek(2)->seq, 2);
    QVERIFY(ring.peek(3) == nullptr);
    QCOMPARE(ring.size(), std::size_t(3));
}

void TestSpscRing::uncommittedWriteIsInvisible() {
    SpscRing<Packet, 4> ring;
    Packet *p = ring.beginWrite();
    p->seq = 7;
    QVERIFY(ring.front() == nullptr);
    QCOMPARE(ring.beginWrite(), p); // the same slot until it is committed
    ring.commitWrite();
    QCOMPARE(ring.front()->seq, 7);
}

// The real shape: a producer filling packets as fast as it can, a consumer draining them, every
// sample checked. A torn or reordered packet means the memory ordering is wrong.
void TestSpscRing::threadedProducerConsumerKeepsEveryPacketIntact() {
    constexpr int kPackets = 200000;
    SpscRing<Packet, 16> ring;
    std::atomic<bool> bad{false};

    std::thread producer([&ring] {
        for (int seq = 0; seq < kPackets;) {
            Packet *p = ring.beginWrite();
            if (!p) {
                std::this_thread::yield();
                continue;
            }
            p->seq = seq;
            p->floats = 1 + seq % 64;
            for (int i = 0; i < p->floats; ++i)
                p->samples[i] = static_cast<float>(seq + i);
            ring.commitWrite();
            ++seq;
        }
    });

    int expected = 0;
    while (expected < kPackets) {
        Packet *p = ring.front();
        if (!p) {
            std::this_thread::yield();
            continue;
        }
        if (p->seq != expected || p->floats != 1 + expected % 64)
            bad = true;
        for (int i = 0; i < p->floats; ++i) {
            if (p->samples[i] != static_cast<float>(expected + i))
                bad = true;
        }
        p->seq = -1; // the consumer may modify a slot in place (AudioEngine mixes into it)
        ring.pop();
        ++expected;
    }
    producer.join();
    QVERIFY(!bad);
    QVERIFY(ring.empty());
}

// A stalled consumer: the producer drops what does not fit and never blocks. What did get in is
// the oldest run, in order.
void TestSpscRing::threadedFullRingDropsNewestOnly() {
    SpscRing<Packet, 8> ring;
    std::atomic<int> dropped{0};
    std::thread producer([&] {
        for (int seq = 0; seq < 100; ++seq) {
            Packet *p = ring.beginWrite();
            if (!p) {
                ++dropped;
                continue;
            }
            p->seq = seq;
            ring.commitWrite();
        }
    });
    producer.join();
    QCOMPARE(dropped.load(), 92);
    for (int seq = 0; seq < 8; ++seq) {
        QCOMPARE(ring.front()->seq, seq);
        ring.pop();
    }
}

// push()/pop(out), as the keyer's queues use them: the same ring, copied in and out by value.
void TestSpscRing::byValuePushPopKeepsOrderAndRefusesWhenFull() {
    SpscRing<double, 4> ring;
    double out = -1;
    QVERIFY(!ring.pop(out));
    QCOMPARE(out, -1.0);
    for (int round = 0; round < 3; ++round) {
        for (int i = 0; i < 4; ++i)
            QVERIFY(ring.push(round * 10 + i));
        QVERIFY(!ring.push(99));
        QCOMPARE(ring.size(), std::size_t(4));
        for (int i = 0; i < 4; ++i) {
            QVERIFY(ring.pop(out));
            QCOMPARE(out, double(round * 10 + i));
        }
        QVERIFY(ring.empty());
    }
}

void TestSpscRing::threadedByValueKeepsEveryItemInOrder() {
    SpscRing<long long, 16> ring;
    constexpr long long kCount = 200000;
    std::thread producer([&] {
        for (long long v = 0; v < kCount;)
            if (ring.push(v))
                ++v;
    });
    long long expected = 0;
    bool bad = false;
    while (expected < kCount) {
        long long v;
        if (!ring.pop(v))
            continue;
        bad = bad || v != expected;
        ++expected;
    }
    producer.join();
    QVERIFY(!bad);
    QVERIFY(ring.empty());
}

QTEST_GUILESS_MAIN(TestSpscRing)
#include "test_spscring.moc"
