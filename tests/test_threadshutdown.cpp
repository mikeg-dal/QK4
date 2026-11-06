// ThreadShutdown (src/utils/threadshutdown.h): a thread that stops in time is joined and deleted
// with what lives on it; one that does not is disowned - unparented, reported, left running - so
// neither its owner's destruction nor the helper crashes or hangs, and it cleans up after itself if
// it ever finishes.

#include <QDeadlineTimer>
#include <QPointer>
#include <QSemaphore>
#include <QElapsedTimer>
#include <QTest>
#include <QThread>
#include <atomic>
#include <chrono>

#include "logging/logcategories.h"
#include "utils/threadshutdown.h"

using namespace std::chrono_literals;

namespace {

// Records the thread it was destroyed on, so a test can prove the deletion ran there.
class Resident : public QObject {
public:
    explicit Resident(std::atomic<QThread *> *destroyedOn) : m_destroyedOn(destroyedOn) {}
    ~Resident() override { m_destroyedOn->store(QThread::currentThread()); }

private:
    std::atomic<QThread *> *m_destroyedOn;
};

const QRegularExpression kDisownedWarning(QStringLiteral("did not stop in time"));

} // namespace

class TestThreadShutdown : public QObject {
    Q_OBJECT

private slots:
    void aNullThreadIsJoined();
    void aThreadThatWasNeverStartedIsJoinedAndDeleted();
    void anEventLoopThreadIsJoinedAndItsResidentDiesOnIt();
    void aStuckEventLoopThreadIsDisownedAndReported();
    void aDisownedThreadSurvivesItsParentAndDeletesItselfWhenItFinishes();
    void aDisownedResidentIsLeftAliveUntilItsThreadFinishes();
    void aStoppedLooplessThreadIsJoined();
    void aStuckLooplessThreadIsDisownedWithoutBlocking();
    void callBlockingRunsTheWorkOnTheTargetThread();
    void callBlockingGivesUpAtTheDeadline();
    void callBlockingRunsInlineWhenTheThreadIsNotRunning();
};

void TestThreadShutdown::aNullThreadIsJoined() {
    QThread *thread = nullptr;
    QCOMPARE(ThreadShutdown::retire(thread, QDeadlineTimer(10ms), lcApp()), ThreadShutdown::Outcome::Joined);
    QCOMPARE(ThreadShutdown::join(thread, QDeadlineTimer(10ms), lcApp()), ThreadShutdown::Outcome::Joined);
}

void TestThreadShutdown::aThreadThatWasNeverStartedIsJoinedAndDeleted() {
    QObject owner;
    QThread *thread = new QThread(&owner);
    QPointer<QThread> watch(thread);
    QCOMPARE(ThreadShutdown::retire(thread, QDeadlineTimer(100ms), lcApp()), ThreadShutdown::Outcome::Joined);
    QCOMPARE(thread, nullptr);
    QVERIFY(watch.isNull());
}

void TestThreadShutdown::anEventLoopThreadIsJoinedAndItsResidentDiesOnIt() {
    QObject owner;
    std::atomic<QThread *> destroyedOn{nullptr};
    QThread *thread = new QThread(&owner);
    thread->setObjectName(QStringLiteral("Joined"));
    auto *resident = new Resident(&destroyedOn);
    resident->moveToThread(thread);
    QObject::connect(thread, &QThread::finished, resident, &QObject::deleteLater);
    thread->start();
    QPointer<QThread> watch(thread);
    QThread *const raw = thread;

    QCOMPARE(ThreadShutdown::retire(thread, QDeadlineTimer(2s), lcApp()), ThreadShutdown::Outcome::Joined);

    QCOMPARE(thread, nullptr);
    QVERIFY(watch.isNull());
    // Destroyed by the deferred deletion Qt runs after the loop stops, on the worker's own thread,
    // and before wait() returned.
    QCOMPARE(destroyedOn.load(), raw);
}

void TestThreadShutdown::aStuckEventLoopThreadIsDisownedAndReported() {
    QSemaphore release;
    auto *owner = new QObject;
    QThread *thread = new QThread(owner);
    thread->setObjectName(QStringLiteral("Stuck"));
    auto *worker = new QObject;
    worker->moveToThread(thread);
    QObject::connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    thread->start();
    QSemaphore entered;
    QMetaObject::invokeMethod(
        worker,
        [&entered, &release]() {
            entered.release();
            release.acquire();
        },
        Qt::QueuedConnection);
    entered.acquire(); // a quit posted before the slot starts would let the loop exit without it
    QPointer<QThread> watch(thread);

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^Stuck thread did not stop in time")));
    const auto outcome = ThreadShutdown::retire(thread, QDeadlineTimer(50ms), lcApp());
    QCOMPARE(outcome, ThreadShutdown::Outcome::Disowned);
    QCOMPARE(thread, nullptr);
    QVERIFY(!watch.isNull());
    QVERIFY(watch->isRunning());
    QCOMPARE(watch->parent(), nullptr);

    // The whole point: destroying the former owner does not destroy a running QThread.
    delete owner;

    release.release();
    QTRY_VERIFY_WITH_TIMEOUT(watch.isNull(), 5000);
}

void TestThreadShutdown::aDisownedThreadSurvivesItsParentAndDeletesItselfWhenItFinishes() {
    QSemaphore release;
    QPointer<QThread> watch;
    {
        QObject owner;
        QThread *thread = QThread::create([&release]() { release.acquire(); });
        thread->setParent(&owner);
        thread->start();
        watch = thread;
        QTest::ignoreMessage(QtWarningMsg, kDisownedWarning);
        QCOMPARE(ThreadShutdown::join(thread, QDeadlineTimer(30ms), lcApp()), ThreadShutdown::Outcome::Disowned);
    } // ~owner: with the thread still parented this would block (QThread::create) or abort.
    QVERIFY(!watch.isNull());
    release.release();
    QTRY_VERIFY_WITH_TIMEOUT(watch.isNull(), 5000);
}

void TestThreadShutdown::aDisownedResidentIsLeftAliveUntilItsThreadFinishes() {
    QSemaphore release;
    std::atomic<QThread *> destroyedOn{nullptr};
    QThread *thread = new QThread;
    auto *resident = new Resident(&destroyedOn);
    resident->moveToThread(thread);
    QObject::connect(thread, &QThread::finished, resident, &QObject::deleteLater);
    thread->start();
    QSemaphore entered;
    QMetaObject::invokeMethod(
        resident,
        [&entered, &release]() {
            entered.release();
            release.acquire();
        },
        Qt::QueuedConnection);
    entered.acquire();
    QThread *const raw = thread;
    QPointer<QThread> watch(thread);

    QTest::ignoreMessage(QtWarningMsg, kDisownedWarning);
    QCOMPARE(ThreadShutdown::retire(thread, QDeadlineTimer(30ms), lcApp()), ThreadShutdown::Outcome::Disowned);
    // Not deleted from here while its thread is inside one of its slots.
    QCOMPARE(destroyedOn.load(), nullptr);

    release.release();
    QTRY_VERIFY_WITH_TIMEOUT(watch.isNull(), 5000);
    QCOMPARE(destroyedOn.load(), raw);
}

void TestThreadShutdown::aStoppedLooplessThreadIsJoined() {
    std::atomic_bool stop{false};
    QObject owner;
    QThread *thread = QThread::create([&stop]() {
        while (!stop.load())
            QThread::msleep(1);
    });
    thread->setParent(&owner);
    thread->start();
    QPointer<QThread> watch(thread);

    stop.store(true);
    QCOMPARE(ThreadShutdown::join(thread, QDeadlineTimer(2s), lcApp()), ThreadShutdown::Outcome::Joined);
    QVERIFY(watch.isNull());
}

void TestThreadShutdown::aStuckLooplessThreadIsDisownedWithoutBlocking() {
    QSemaphore release;
    QThread *thread = QThread::create([&release]() { release.acquire(); });
    thread->start();
    QPointer<QThread> watch(thread);

    QTest::ignoreMessage(QtWarningMsg, kDisownedWarning);
    QElapsedTimer elapsed;
    elapsed.start();
    QCOMPARE(ThreadShutdown::join(thread, QDeadlineTimer(50ms), lcApp()), ThreadShutdown::Outcome::Disowned);
    // Bounded by the deadline, not by the thread: ~QThread on a running QThread::create() thread
    // would have waited for it.
    QVERIFY2(elapsed.elapsed() < 1000, qPrintable(QString::number(elapsed.elapsed())));

    release.release();
    QTRY_VERIFY_WITH_TIMEOUT(watch.isNull(), 5000);
}

void TestThreadShutdown::callBlockingRunsTheWorkOnTheTargetThread() {
    QThread thread;
    auto *context = new QObject;
    context->moveToThread(&thread);
    QObject::connect(&thread, &QThread::finished, context, &QObject::deleteLater);
    thread.start();

    std::atomic<QThread *> ranOn{nullptr};
    QVERIFY(ThreadShutdown::callBlocking(
        context, [&ranOn]() { ranOn.store(QThread::currentThread()); }, QDeadlineTimer(2s)));
    QCOMPARE(ranOn.load(), &thread);

    thread.quit();
    QVERIFY(thread.wait(2000));
}

void TestThreadShutdown::callBlockingGivesUpAtTheDeadline() {
    QSemaphore release;
    QThread thread;
    auto *context = new QObject;
    context->moveToThread(&thread);
    QObject::connect(&thread, &QThread::finished, context, &QObject::deleteLater);
    thread.start();
    QSemaphore entered;
    QMetaObject::invokeMethod(
        context,
        [&entered, &release]() {
            entered.release();
            release.acquire();
        },
        Qt::QueuedConnection);
    entered.acquire();

    auto ran = std::make_shared<std::atomic_bool>(false);
    QElapsedTimer elapsed;
    elapsed.start();
    QVERIFY(!ThreadShutdown::callBlocking(context, [ran]() { ran->store(true); }, QDeadlineTimer(50ms)));
    QVERIFY(elapsed.elapsed() < 1000);
    QVERIFY(!ran->load());

    // Still delivered once the thread gets to it: the work must only touch what outlives the call.
    release.release();
    QTRY_VERIFY_WITH_TIMEOUT(ran->load(), 5000);
    thread.quit();
    QVERIFY(thread.wait(2000));
}

void TestThreadShutdown::callBlockingRunsInlineWhenTheThreadIsNotRunning() {
    QThread thread; // never started
    QObject context;
    context.moveToThread(&thread);
    bool ran = false;
    QVERIFY(ThreadShutdown::callBlocking(&context, [&ran]() { ran = true; }, QDeadlineTimer(10ms)));
    QVERIFY(ran);
}

QTEST_GUILESS_MAIN(TestThreadShutdown)
#include "test_threadshutdown.moc"
