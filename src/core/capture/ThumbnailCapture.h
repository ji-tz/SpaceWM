#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QVector>
#include <Windows.h>

// Capture / render helpers for overview previews.
//
// Space previews are ALWAYS rendered (wallpaper + PrintWindow composite),
// never BitBlt of the screen (overview overlay would be in the shot).
namespace thumbs {

// Window thumbnail via PrintWindow at FULL window size, then scaled to maxSize.
// (A pre-shrunk DC would clip PrintWindow's 1:1 draw to the top-left corner.)
QImage capture(HWND hwnd, const QSize &maxSize = QSize(480, 270));

// Full monitor screenshot in physical pixels (BitBlt). Not used for space cards.
QImage captureMonitor(const RECT &physRect, const QSize &maxSize = QSize(640, 360));

// Desktop wallpaper scaled to fit physRect (cold-start / empty-space preview).
QImage desktopWallpaper(const RECT &physRect, const QSize &maxSize = QSize(640, 360));

// Wallpaper (or solid fill) forced to exactly canvas — background for space render.
QImage wallpaperFilled(const RECT &physRect, const QSize &canvas);

// Render-only space background: wallpaperFilled, no screen BitBlt.
QImage spacePreview(const RECT &physRect, const QSize &maxSize = QSize(640, 360));

} // namespace thumbs
