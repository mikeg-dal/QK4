#include "panelscale.h"

#include <QRect>
#include <QScreen>

#include <algorithm>

#ifdef Q_OS_MACOS
#include <CoreGraphics/CoreGraphics.h>
#include <IOKit/graphics/IOGraphicsTypes.h>
#endif

namespace PanelScale {

qreal framebufferPixelsPerPanelPixel(const QScreen *screen) {
    if (!screen)
        return 1.0;
#ifdef Q_OS_MACOS
    // WHY matched by position: Qt exposes no Cocoa native interface for QScreen (only Android,
    // Wayland and Windows ones), so the CoreGraphics display is the one under the screen's centre.
    // Both use the same global coordinates: points, origin at the main display's top-left.
    const QRect geometry = screen->geometry();
    const CGPoint centre = CGPointMake(geometry.center().x(), geometry.center().y());
    CGDirectDisplayID display = kCGNullDirectDisplay;
    uint32_t count = 0;
    if (CGGetDisplaysWithPoint(centre, 1, &display, &count) != kCGErrorSuccess || count == 0)
        return 1.0;

    CGDisplayModeRef current = CGDisplayCopyDisplayMode(display);
    if (!current)
        return 1.0;
    const size_t framebufferWidth = CGDisplayModeGetPixelWidth(current);
    CGDisplayModeRelease(current);

    // The panel's own resolution is the widest mode the display marks native. The duplicate
    // low-resolution option is what makes macOS list the HiDPI variants at all.
    const void *keys[] = {kCGDisplayShowDuplicateLowResolutionModes};
    const void *values[] = {kCFBooleanTrue};
    CFDictionaryRef options =
        CFDictionaryCreate(nullptr, keys, values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFArrayRef modes = CGDisplayCopyAllDisplayModes(display, options);
    if (options)
        CFRelease(options);
    if (!modes)
        return 1.0;
    size_t panelWidth = 0;
    for (CFIndex i = 0; i < CFArrayGetCount(modes); ++i) {
        auto mode = static_cast<CGDisplayModeRef>(const_cast<void *>(CFArrayGetValueAtIndex(modes, i)));
        if (CGDisplayModeGetIOFlags(mode) & kDisplayModeNativeFlag)
            panelWidth = (std::max)(panelWidth, CGDisplayModeGetPixelWidth(mode));
    }
    CFRelease(modes);

    if (panelWidth == 0 || framebufferWidth == 0)
        return 1.0;
    return static_cast<qreal>(framebufferWidth) / static_cast<qreal>(panelWidth);
#else
    return 1.0;
#endif
}

} // namespace PanelScale
