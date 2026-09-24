#pragma once

#include <QRect>
#include <QVector>
#include <Windows.h>

struct MonitorEntry {
    HMONITOR handle = nullptr;
    RECT physRect{};        // physical pixels (GetMonitorInfo)
    QRect geometry;         // Qt logical pixels (safe for QWidget::setGeometry)
    bool primary = false;
    QString deviceName;     // e.g. \\.\DISPLAY1
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

} // namespace monitors
