#ifndef UTILS_THREADSHUTDOWN_H
#define UTILS_THREADSHUTDOWN_H

#include <QDeadlineTimer>
#include <functional>

class QLoggingCategory;
class QObject;
class QThread;

// Stopping a thread at shutdown without hanging and without crashing. Every QThread owner in QK4
// goes through here (CONVENTIONS Rule 11 and Rule 14), so the timeout handling exists once.
//
// The two failures this prevents, both from the Qt documentation (~QThread):
//   - "Deleting a running QThread (i.e. isFinished() returns false) will result in a program
//     crash." A thread parented to its owner and left running when the owner dies is exactly that.
//   - Since Qt 6.3, deleting a running thread made by QThread::create() instead "will block until
//     the thread has finished" - a hang on quit rather than a crash.
//
// So a thread that misses its deadline is DISOWNED: unparented, reported once, and left running
// with everything that lives on it. It deletes itself if it ever does finish. At quit that leak
// costs nothing; destroying it underneath itself costs the operator a crash or a frozen window.
//
// What the caller still owns after a Disowned result: anything the thread's code uses that is not
// a QObject living on it (a keyer's clock, a mutex borrowed from another worker). The caller must
// leak those too - this helper cannot see them.
namespace ThreadShutdown {

enum class Outcome {
    Joined,   // The thread finished; it and every deferred deletion on it are done.
    Disowned, // The deadline passed. The thread runs on, unparented, and was reported.
};

// Runs `work` on `context`'s thread and waits for it until `deadline`: the bounded form of a
// Qt::BlockingQueuedConnection, which has no timeout at all. True when `work` has finished.
//
// False means the thread did not get to it in time. `work` may then still run later, or never
// (Qt drops it if `context` is destroyed first), so it must capture only objects that live on that
// thread - never the caller's `this`. If the thread is not running, `work` runs here and now:
// nothing can be in flight on a thread that is not running, and a queued call would never arrive.
// Called on `context`'s own thread, `work` also runs inline.
bool callBlocking(QObject *context, std::function<void()> work, QDeadlineTimer deadline);

// Ends a thread that runs an event loop (the Rule 14 worker pattern): quit(), then join until
// `deadline`. Objects living on the thread are expected to be deleted by the Rule 14
// `finished -> deleteLater` connection: Qt runs deferred deletions on the thread after its loop has
// stopped and before wait() returns (QThread::finished), so on Joined they are gone, destroyed on
// their own thread.
//
// Joined: the QThread is deleted. Disowned: see above. `thread` is set to nullptr either way -
// after this call the owner no longer has a thread. Call it from the thread the QThread object
// itself lives in (normally the main thread). A null `thread` is Joined.
Outcome retire(QThread *&thread, QDeadlineTimer deadline, const QLoggingCategory &category);

// As retire(), for a thread with no event loop that the caller has already told to stop by its own
// means (QThread::create() running a loop with a stop flag: the keyer). quit() would do nothing
// there ("This function does nothing if the thread does not have an event loop", QThread::quit).
Outcome join(QThread *&thread, QDeadlineTimer deadline, const QLoggingCategory &category);

} // namespace ThreadShutdown

#endif // UTILS_THREADSHUTDOWN_H
