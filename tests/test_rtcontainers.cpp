// SPDX-License-Identifier: GPL-3.0-or-later
//
// The non-allocating building blocks the keyer and sidetone use on real-time threads:
// RingDeque (src/utils/ringdeque.h), the std::deque subset on one preallocated ring, and
// FunctionRef (src/utils/functionref.h), the non-owning callable reference.

#include "utils/functionref.h"
#include "utils/ringdeque.h"

#include <QtTest/QtTest>
#include <deque>
#include <random>
#include <vector>

namespace {

template <typename T> std::vector<T> contents(const RingDeque<T> &q) {
    std::vector<T> out;
    for (std::size_t i = 0; i < q.size(); ++i)
        out.push_back(q[i]);
    return out;
}

int callTwice(FunctionRef<int(int)> f) {
    return f(1) + f(2);
}

} // namespace

class TestRtContainers : public QObject {
    Q_OBJECT

private slots:
    void capacityRoundsUpToAPowerOfTwo() {
        QCOMPARE(RingDeque<int>(1).capacity(), std::size_t(1));
        QCOMPARE(RingDeque<int>(5).capacity(), std::size_t(8));
        QCOMPARE(RingDeque<int>(128).capacity(), std::size_t(128));
    }

    void pushAndPopKeepOrderAcrossTheWrap() {
        RingDeque<int> q(4);
        int next = 0;
        int expected = 0;
        for (int round = 0; round < 50; ++round) {
            q.push_back(next++);
            q.push_back(next++);
            QCOMPARE(q.front(), expected++);
            q.pop_front();
            QCOMPARE(q.front(), expected++);
            q.pop_front();
        }
        QVERIFY(q.empty());
        QCOMPARE(q.capacity(), std::size_t(4)); // never needed to grow
    }

    void insertAtFrontMiddleAndBack() {
        RingDeque<int> q(8);
        q.push_back(1);
        q.push_back(3);
        q.insert(1, 2);
        q.insert(0, 0);
        q.insert(q.size(), 4);
        QCOMPARE(contents(q), (std::vector<int>{0, 1, 2, 3, 4}));
    }

    // The keyer's release inserts two edges at the same position: DahUp, then DitUp before it.
    void twoInsertsAtOnePositionKeepTheSecondFirst() {
        RingDeque<int> q(8);
        q.push_back(10);
        q.push_back(40);
        q.insert(1, 30);
        q.insert(1, 20);
        QCOMPARE(contents(q), (std::vector<int>{10, 20, 30, 40}));
    }

    void growsInsteadOfDroppingAndKeepsOrder() {
        RingDeque<int> q(2);
        q.push_back(0);
        q.pop_front(); // head off zero, so the growth has a wrapped ring to unwrap
        for (int i = 1; i <= 9; ++i)
            q.push_back(i);
        QCOMPARE(q.capacity(), std::size_t(16));
        QCOMPARE(contents(q), (std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8, 9}));
        q.insert(0, 0);
        QCOMPARE(q.front(), 0);
    }

    void clearEmptiesAndKeepsCapacity() {
        RingDeque<int> q(4);
        q.push_back(1);
        q.push_back(2);
        q.clear();
        QVERIFY(q.empty());
        QCOMPARE(q.capacity(), std::size_t(4));
        q.push_back(7);
        QCOMPARE(q.front(), 7);
    }

    // Every operation the keyer and sidetone use, at random, against std::deque.
    void matchesStdDequeUnderRandomUse() {
        std::mt19937 rng(1234);
        RingDeque<int> q(4);
        std::deque<int> ref;
        for (int step = 0; step < 20000; ++step) {
            const int op = static_cast<int>(rng() % 4);
            if (op == 0 || ref.empty()) {
                q.push_back(step);
                ref.push_back(step);
            } else if (op == 1) {
                q.pop_front();
                ref.pop_front();
            } else if (op == 2) {
                const std::size_t at = rng() % (ref.size() + 1);
                q.insert(at, -step);
                ref.insert(ref.begin() + static_cast<std::ptrdiff_t>(at), -step);
            } else {
                while (ref.size() > 64) {
                    q.pop_front();
                    ref.pop_front();
                }
            }
            QCOMPARE(q.size(), ref.size());
            if (!ref.empty())
                QCOMPARE(q.front(), ref.front());
        }
        QCOMPARE(contents(q), std::vector<int>(ref.begin(), ref.end()));
    }

    void functionRefCallsTheLambdaItWasMadeFrom() {
        int calls = 0;
        const int scale = 10;
        auto f = [&calls, scale](int x) {
            ++calls;
            return x * scale;
        };
        QCOMPARE(callTwice(f), 30);
        QCOMPARE(calls, 2);
    }

    void functionRefSeesTheCallableNotACopy() {
        int state = 0;
        auto bump = [&state](int x) { return state += x; };
        FunctionRef<int(int)> ref(bump);
        ref(1);
        ref(2);
        QCOMPARE(state, 3);
    }
};

QTEST_GUILESS_MAIN(TestRtContainers)
#include "test_rtcontainers.moc"
