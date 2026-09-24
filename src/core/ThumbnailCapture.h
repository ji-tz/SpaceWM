#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QVector>
#include <Windows.h>

// Capture helpers for overview cards.
namespace thumbs {

// Window thumbnail via PrintWindow (null if minimized/unavailable).
QImage capture(HWND hwnd, const QSize &maxSize = QSize(480, 270));

// Full monitor screenshot in physical pixels (BitBlt from the screen).
QImage captureMonitor(const RECT &physRect, const QSize &maxSize = QSize(640, 360));

// Desktop wallpaper image scaled to fit physRect (cold-start / empty-space preview).
QImage desktopWallpaper(const RECT &physRect, const QSize &maxSize = QSize(640, 360));

// Best-effort preview for a space: live monitor shot, else wallpaper, else flat fill.
QImage spacePreview(const RECT &physRect, const QSize &maxSize = QSize(640, 360));

} // namespace thumbs
