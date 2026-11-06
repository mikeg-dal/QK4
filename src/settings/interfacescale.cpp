#include "settings/interfacescale.h"
#include "logging/logcategories.h"
#include "settings/radiosettings.h"
#include "utils/windowfit.h"

#include <QCoreApplication>
#include <QDebug>
#include <QProcess>
#include <QRect>
#include <QScreen>
#include <QWidget>
#include <cmath>

namespace {

constexpr char kScaleEnv[] = "QT_SCALE_FACTOR";
// Set on the relaunched instance only, so Auto fits at most once per launch.
constexpr char kRelaunchedEnv[] = "QK4_SCALE_RELAUNCHED";

// WHY: Qt cannot know the native title bar and frame until the window is shown, and the fit has to
// be decided before that. This allows for the tallest common decoration (Windows 11 title bar plus
// borders, GNOME's headerbar-sized titles) rather than the shortest, so the fitted window is never
// a few pixels too tall for the screen it was fitted to.
const QSize kFrameAllowance(16, 40);

double g_applied = 1.0;
bool g_environmentOverride = false;
bool g_restartRequested = false;
// Remembered before this process set QT_SCALE_FACTOR itself, so a relaunch does not mistake our
// own value for one the operator set.
bool g_weSetScaleEnv = false;

} // namespace

namespace InterfaceScale {

void applyBeforeApp() {
    if (qEnvironmentVariableIsSet(kScaleEnv)) {
        g_environmentOverride = true;
        bool ok = false;
        const double fromEnv = qEnvironmentVariable(kScaleEnv).toDouble(&ok);
        g_applied = ok && fromEnv > 0.0 ? fromEnv : 1.0;
        return;
    }
    g_applied = RadioSettings::startupInterfaceScale();
    if (g_applied != 1.0) {
        qputenv(kScaleEnv, QByteArray::number(g_applied, 'f', 2));
        g_weSetScaleEnv = true;
    }
}

double applied() {
    return g_applied;
}

bool environmentOverride() {
    return g_environmentOverride;
}

bool fitNeedsRelaunch(const QWidget &window) {
    if (g_environmentOverride || qEnvironmentVariableIsSet(kRelaunchedEnv))
        return false;
    if (RadioSettings::instance()->interfaceScale() != RadioSettings::kInterfaceScaleAuto)
        return false;
    const QScreen *screen = window.screen();
    if (!screen)
        return false;

    const QSize required = window.minimumSizeHint() + kFrameAllowance;
    const QRect availableGeometry = screen->availableGeometry();
    const QSize available = availableGeometry.size();
    const double ceiling = WindowFit::autoScaleCeiling(screen->geometry(), availableGeometry);
    const double fitted = WindowFit::fitScale(required, available, g_applied, ceiling);
    if (std::abs(fitted - g_applied) < WindowFit::kScaleStep / 2)
        return false;

    qCInfo(lcApp) << "Interface scale: window needs" << required << "on a" << available << "screen at" << g_applied
                  << "- relaunching at" << fitted;
    RadioSettings::instance()->setAutoScaleFactor(fitted);
    return true;
}

void requestRestart() {
    g_restartRequested = true;
}

bool restartRequested() {
    return g_restartRequested;
}

bool relaunch() {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (g_weSetScaleEnv)
        env.remove(kScaleEnv);
    // WHY: an operator's "Restart now" must be free to fit again, so only a relaunch that Auto
    // itself asked for carries the guard.
    if (g_restartRequested)
        env.remove(kRelaunchedEnv);
    else
        env.insert(kRelaunchedEnv, QStringLiteral("1"));

    QProcess process;
    process.setProgram(QCoreApplication::applicationFilePath());
    process.setArguments(QCoreApplication::arguments().mid(1));
    process.setProcessEnvironment(env);
    const bool started = process.startDetached();
    if (!started)
        qCWarning(lcApp) << "Interface scale: could not relaunch" << QCoreApplication::applicationFilePath();
    return started;
}

} // namespace InterfaceScale
