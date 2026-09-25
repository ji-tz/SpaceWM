#pragma once

#include <QRect>
#include <QSize>
#include <QVector>
#include <Windows.h>

struct MonitorEntry {
    HMONITOR handle = nullptr;
    RECT physRect{}; // physical pixels (GetMonitorInfo)
    QRect geometry;  // Qt logical pixels (safe for QWidget::setGeometry)
    bool primary = false;
    QString deviceName; // e.g. \\.\DISPLAY1
};

// Enumerates monitors; geometry is converted to Qt logical DPI coords.
namespace monitors {

QVector<MonitorEntry> enumerate();

HMONITOR fromWindow(HWND hwnd);
HMONITOR fromPoint(POINT pt);
HMONITOR fromCursor();

bool contains(const MonitorEntry &mon, POINT pt);

// Physical monitor RECT for capture / native SetWindowPos.
bool physRectOf(HMONITOR hmon, RECT *out);

// Qt logical geometry for a monitor (PMv2-safe).
QRect logicalGeometry(HMONITOR hmon);

// Work area (excludes taskbar) in Qt logical pixels — use for overview so
// the taskbar stays visible and clickable.
QRect logicalWorkArea(HMONITOR hmon);

// Per-monitor effective DPI (96 = 100%).
UINT dpiFor(HMONITOR hmon);

// DPI scale factor: 1.0 at 96 DPI, 1.5 at 144, 2.0 at 192.
double scaleFactor(HMONITOR hmon);

// Pure conversion — physical size → Qt logical using DPI (96 base).
QSize toLogicalSize(int physW, int physH, UINT dpiX, UINT dpiY);

// Window's on-screen size in Qt logical pixels (GetWindowRect / that monitor's DPI).
// Use for UI tile sizes so previews match the real window at any DPI.
QSize logicalWindowSize(HWND hwnd);

} // namespace monitors
