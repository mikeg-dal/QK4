#ifndef WINDOWFIT_H
#define WINDOWFIT_H

#include <QRect>
#include <QSize>
#include <algorithm>
#include <cmath>

// How big the main window opens, and the interface scale at which it fits the screen.
//
// Pure logic on sizes, so every screen can be tested without owning one — mirroring
// audio/audiodeviceselection.h, hardware/usbdevicelifecycle.h and the rest.
//
// WHY this exists. The window once declared a fixed minimum of 1340x840 that was smaller than its
// own layout: the side panel needs more height than 840 leaves once an in-window menu bar (Windows,
// Linux) takes its share, so the TX buttons were squeezed under their orange sub-labels. On a 1080p
// laptop at 150% scaling the screen offers ~640 logical px for a window that needs ~860, and the
// whole left panel collapsed. The layout is a front panel and is never rearranged; the window takes
// its minimum from the layout, and when the screen is too small the whole interface is scaled
// uniformly instead. The same rule runs the other way on a large screen: a 27" 2560x1440 monitor
// has room for the interface at 125%, and Auto uses it.
namespace WindowFit {

// WHY: 5% steps keep the chosen factor stable — a screen a few pixels different must not produce a
// new factor, and therefore a relaunch, every time.
constexpr double kScaleStep = 0.05;
// WHY: below half size even the 10 px floor text (K4Styles FontSizeFloor) renders at 5 px and is
// unreadable at 100% OS scaling. A screen that small gets a window that hangs off the edge rather than one nobody can
// read.
constexpr double kMinScale = 0.5;
constexpr double kNativeScale = 1.0;
// WHY: Auto grows no further than 125%. A large screen usually shares QK4 with a logger or WSJT-X,
// and past 125% the window would claim most of it; an operator who wants more picks 150% in Options.
constexpr double kMaxScale = 1.25;
// WHY: above native size Auto moves in 25% steps only. Qt's High DPI guide: "Integer scale factors
// (for example, 1.0 or 2.0) are preferred for best results. 'Rounding' the scale factor to 25%
// increments can also give good results." Shrinking has no such choice — a small screen gets the
// largest 5% step that fits — but growing is optional, so it takes only factors Qt renders well.
constexpr double kGrowthStep = 0.25;

// The size the window opens at: the preferred size, never below what the layout needs, and never
// larger than the screen offers — unless the layout itself does not fit, in which case the layout
// wins, because a window smaller than its contents is the bug this file exists to prevent.
inline QSize initialWindowSize(QSize layoutMin, QSize preferred, QSize available) {
    const int w = std::max(layoutMin.width(), std::min(preferred.width(), available.width()));
    const int h = std::max(layoutMin.height(), std::min(preferred.height(), available.height()));
    return {w, h};
}

// The interface scale at which a window needing `required` fits `available`. Both sizes are logical
// pixels measured under `currentScale`, the factor already applied to this run. A layout's logical
// size does not depend on the factor; the screen's does, inversely — at 0.75 a screen offers 4/3 the
// logical pixels it does at 1.0. So the answer is currentScale x the tighter of the two ratios.
// Rounded down to a whole step so the result really fits — a 5% step below native size, a 25% step
// above it — and clamped to kMinScale..`maxScale`: it shrinks the interface on a screen too small for
// it and grows it on one with room to spare.
//
// Logical pixels already carry the OS scaling, so this needs no platform cases: a 1920x1080 panel
// at Windows 150% offers 1280x720 and shrinks, a 4K panel at 150% offers 2560x1440 and grows —
// the same as a 1440p panel at 100%, which is the same physical size on the desk.
inline double fitScale(QSize required, QSize available, double currentScale, double maxScale = kMaxScale) {
    if (required.isEmpty() || available.isEmpty() || currentScale <= 0.0)
        return kNativeScale;
    const double ratio =
        std::min(double(available.width()) / required.width(), double(available.height()) / required.height());
    // WHY: the epsilon absorbs floating-point error so an exact fit at a step (0.75 computed as
    // 0.7499999) is not rounded down a whole step.
    const double steps = std::floor(currentScale * ratio / kScaleStep + 1e-9);
    // WHY: steps x 0.05 lands a hair off the decimal (14 x 0.05 = 0.7000000000000001), which is what
    // gets saved and shown. Rounding to whole percent gives the double nearest the step itself.
    double scale = std::round(steps * kScaleStep * 100.0) / 100.0;
    if (scale > kNativeScale)
        scale = std::floor(scale / kGrowthStep + 1e-9) * kGrowthStep;
    return std::clamp(scale, kMinScale, std::max(kMinScale, maxScale));
}

// How far Auto may grow on a screen. Growth fits the window to the brink of the available area, so it
// is only as safe as that area is true. Where the platform does not know its work area it reports the
// whole screen — X11 with several monitors or no _NET_WORKAREA, and Wayland, which has no protocol
// for it — and a panel or dock would cover the bottom of a grown window. There Auto still shrinks
// but never grows past native size.
inline double autoScaleCeiling(QRect screenGeometry, QRect availableGeometry) {
    return availableGeometry == screenGeometry ? kNativeScale : kMaxScale;
}

} // namespace WindowFit

#endif // WINDOWFIT_H
