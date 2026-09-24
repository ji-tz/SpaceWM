#pragma once

#include <QRect>
#include <QVector>
#include <Windows.h>

struct MonitorEntry {
    HMONITOR handle = nullptr;
    QRect geometry;       // virtual-desktop coordinates
    bool primary = false;
    QString deviceName;   // e.g. \\.\DISPLAY1
};

// Enumerates physical monitors via EnumDisplayMonitors.
namespace monitors {

QVector<MonitorEntry> enumerate();

// Monitor that contains the majority of the window (or the cursor's monitor).
HMONITOR fromWindow(HWND hwnd);
HMONITOR fromPoint(POINT pt);
HMONITOR fromCursor();

// True if pt lies inside mon's geometry.
bool contains(const MonitorEntry &mon, POINT pt);

} // namespace monitors
