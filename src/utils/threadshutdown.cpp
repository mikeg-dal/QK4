#include "utils/threadshutdown.h"

#include <QLoggingCategory>
#include <QObject>
#include <QSemaphore>
#include <QThread>
#include <atomic>
#include <memory>

namespace ThreadShutdown {

namespace {

Outcome finish(QThread *&thread, QDeadlineTimer deadline, const QLoggingCategory &category) {
    QThread *const t = thread;
    thread = nullptr;
    if (!t) {
        return Outcome::Joined;
    }
    Q_ASSERT_X(t != QThread::currentThread(), "ThreadShutdown", "a thread cannot join itself");
    Q_ASSERT_X(t->thread() == QThread::currentThread(), "ThreadShutdown",
               "retire a QThread from the thread its object lives in");

    // Also true for a thread that was never started (QThread::wait).
    if (t->wait(deadline)) {
        delete t;
        return Outcome::Joined;
    }

    const QString name = t->objectName().isEmpty() ? QStringLiteral("(unnamed)") : t->objectName();
    qCWarning(category).noquote().nospace() << name << " thread did not stop in time; leaving it running rather than "
                                            << "destroying it underneath itself";

    t->setParent(nullptr);
    // WHY a once-guard: the thread may finish between the timed-out wait and the connect below, so
    // finished() may or may not reach the connection. Both routes call this; only one deletes.
    auto deleted = std::make_shared<std::atomic_bool>(false);
    auto deleteOnce = [t, deleted]() {
        if (!deleted->exchange(true)) {
            t->deleteLater();
        }
    };
    // Queued to the thread the QThread object lives in: finished() is emitted from the ending
    // thread itself. If that event loop has already stopped the thread object is simply leaked.
    QObject::connect(t, &QThread::finished, t, deleteOnce);
    if (t->isFinished()) {
        deleteOnce();
    }
    return Outcome::Disowned;
}

} // namespace

bool callBlocking(QObject *context, std::function<void()> work, QDeadlineTimer deadline) {
    QThread *const target = context->thread();
    if (!target || target == QThread::currentThread() || !target->isRunning()) {
        work();
        return true;
    }
    // WHY shared: on a timeout this function returns while the call is still queued, and the
    // semaphore must outlive both sides.
    auto done = std::make_shared<QSemaphore>();
    QMetaObject::invokeMethod(
        context,
        [work = std::move(work), done]() {
            work();
            done->release();
        },
        Qt::QueuedConnection);
    return done->tryAcquire(1, deadline);
}

Outcome retire(QThread *&thread, QDeadlineTimer deadline, const QLoggingCategory &category) {
    if (thread) {
        thread->quit();
    }
    return finish(thread, deadline, category);
}

Outcome join(QThread *&thread, QDeadlineTimer deadline, const QLoggingCategory &category) {
    return finish(thread, deadline, category);
}

} // namespace ThreadShutdown
