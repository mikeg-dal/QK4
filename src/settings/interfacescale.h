#ifndef INTERFACESCALE_H
#define INTERFACESCALE_H

class QWidget;

// Applies the operator's interface scale, and in Auto fits the interface to the screen.
//
// The layout is a fixed front panel and is never rearranged; on a screen too small for it the whole
// interface is scaled down uniformly instead, and on one with room for 125% Auto scales it up to
// that. Qt's supported way to do that is QT_SCALE_FACTOR, which Qt
// reads once, when QApplication is constructed - so the scale is applied before that, and changing
// it means starting the app again.
//
// Moving the window between monitors with different OS scaling needs none of this: Qt follows the
// OS factor per monitor, live, and this factor rides on top of it as a fixed multiplier.
namespace InterfaceScale {

// Before QApplication exists: sets QT_SCALE_FACTOR from the saved setting, unless the environment
// already carries one - an operator who set it themselves is never overridden.
void applyBeforeApp();

// The factor this run started with, and whether it came from the environment rather than QK4.
double applied();
bool environmentOverride();

// Auto mode only, with the window built but not yet shown. If the window does not fit its screen
// at the applied factor - or would fit at a larger one - saves the factor that fits and returns
// true: the caller then exits without showing the window and relaunches. At most once per launch,
// so a screen it cannot satisfy can never cause a loop.
bool fitNeedsRelaunch(const QWidget &window);

// "Restart now" from Options. The relaunch happens after the event loop has exited and the window
// has shut down - the new instance must not race this one for the CAT/TCI ports or USB devices.
void requestRestart();
bool restartRequested();

// Starts a new instance with the same arguments. Call only once this one has shut down.
bool relaunch();

} // namespace InterfaceScale

#endif // INTERFACESCALE_H
