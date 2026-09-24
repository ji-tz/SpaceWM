#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QVector>
#include <Windows.h>

// Capture / render helpers for overview previews.
//
// Window previews: ONE PrintWindow per HWND is cached (windowShot); callers
// scale the shared image. Space previews: rendered composites stored on
// Space::screenshot — rebuilt only when that space's membership changes.
namespace thumbs {

// Uncached full-window capture (PrintWindow at full size, then scale to maxSize).
QImage capture(HWND hwnd, const QSize &maxSize = QSize(480, 270));

// Cached window shot: first call captures once; later calls scale the shared
// image. One source image per HWND — reuse for strip tiles and space composites.
// maxSize empty → return/cache the full (post-capture) image.
QImage windowShot(HWND hwnd, const QSize &maxSize = QSize());

// Drop one HWND from the window-shot cache (window closed / content must refresh).
void invalidateWindow(HWND hwnd);
void clearWindowCache();
// Number of cached window shots (tests).
int windowCacheCount();

// Full monitor screenshot in physical pixels (BitBlt). Not used for space cards.
QImage captureMonitor(const RECT &physRect, const QSize &maxSize = QSize(640, 360));

// Desktop wallpaper scaled to fit physRect (cold-start / empty-space preview).
QImage desktopWallpaper(const RECT &physRect, const QSize &maxSize = QSize(640, 360));

// Wallpaper (or solid fill) forced to exactly canvas — background for space render.
QImage wallpaperFilled(const RECT &physRect, const QSize &canvas);

// Render-only space background: wallpaperFilled, no screen BitBlt.
QImage spacePreview(const RECT &physRect, const QSize &maxSize = QSize(640, 360));

} // namespace thumbs
