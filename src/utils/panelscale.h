#ifndef UTILS_PANELSCALE_H
#define UTILS_PANELSCALE_H

#include <QtGlobal>

class QScreen;

// How many framebuffer pixels the operating system squeezes into one physical pixel of the panel.
//
// Qt's device pixels are framebuffer pixels, and Qt does not promise those are the panel's: "The
// device pixel coordinate system is often equivalent to the physical coordinate system of the
// display, but this is not guaranteed. For example, both macOS and Ubuntu may apply additional
// scaling, depending on display settings." (Qt 6.10 docs, High DPI). On macOS a "HiDPI" mode whose
// framebuffer is larger than the panel — a 4K panel driven as "looks like 3840x2160", which renders
// 7680x4320 and lets the display pipeline halve it — still reports devicePixelRatio 2, so nothing
// in Qt reveals that every device pixel is half a physical one.
//
// Returns 2.0 in that case, 1.0 when the framebuffer is the panel (a 5K panel at "looks like
// 2560x1440"), something between for macOS's fractional "scaled" modes, and 1.0 whenever it cannot
// tell: off macOS (Windows and X11 render at the panel's resolution; Wayland is not queried), for a
// null screen, or when the OS does not mark a native mode.
//
// Not cheap on macOS (it enumerates display modes): call it when the screen or its scale changes,
// not per frame.
namespace PanelScale {
qreal framebufferPixelsPerPanelPixel(const QScreen *screen);
} // namespace PanelScale

#endif // UTILS_PANELSCALE_H
