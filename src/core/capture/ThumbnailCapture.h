#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QVector>
#include <Windows.h>

// Capture / render helpers for overview previews.
//
// Window previews: per-window cascade first — PrintWindow (PW_RENDERFULLCONTENT
// → flags=0 → GetWindowDC), each step rejected on a near-black frame (the
// PrintWindow BOOL stays TRUE for black buffers on GPU/composition windows).
// Screen BitBlt is the last-resort fallback, honest only when sampling is on
// (overview mask down) and the rect is visible / not cloaked / on-screen.
// Preview open: SpaceManager::refreshVisibleShots() runs BEFORE the masks go
// up (recaptures on-screen windows with the screen fallback); behind the mask
// warmWindowShots() uncloaks + fills only MISSING shots, keeping switch-time
// shots for off-space windows. Bottom strip and space composites share that
// cache for one open.
// Space previews: composites on Space::screenshot after the window batch.
namespace thumbs {

// Uncached per-window capture at full size, then scale to maxSize.
QImage capture(HWND hwnd, const QSize &maxSize = QSize(480, 270));

// Cached window shot for the current open: first call captures; later calls
// scale the shared image. maxSize empty → full (post-capture) image.
QImage windowShot(HWND hwnd, const QSize &maxSize = QSize());

// Drop one HWND from the window-shot cache (window closed / content must refresh).
void invalidateWindow(HWND hwnd);
void clearWindowCache();
// Number of cached window shots (tests).
int windowCacheCount();

// True when a screen BitBlt at this window's rect would show THIS window:
// visible, not DWM-cloaked, and intersecting the virtual screen (tray-restored
// windows parked at -32000 have no honest pixels). False for off-space windows.
bool canSampleScreen(HWND hwnd);
// Master switch: SpaceManager clears this while the overview overlay is up.
void setScreenSamplingEnabled(bool on);
bool screenSamplingEnabled();

// Full monitor screenshot in physical pixels (BitBlt). Not used for space cards.
QImage captureMonitor(const RECT &physRect, const QSize &maxSize = QSize(640, 360));

// Desktop wallpaper scaled to fit physRect (cold-start / empty-space preview).
QImage desktopWallpaper(const RECT &physRect, const QSize &maxSize = QSize(640, 360));

// Wallpaper (or solid fill) forced to exactly canvas — background for space render.
QImage wallpaperFilled(const RECT &physRect, const QSize &canvas);

// Render-only space background: wallpaperFilled, no screen BitBlt.
QImage spacePreview(const RECT &physRect, const QSize &maxSize = QSize(640, 360));

} // namespace thumbs
