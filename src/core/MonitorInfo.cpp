#include "MonitorInfo.h"

#include <QGuiApplication>
#include <QScreen>

namespace {

BOOL CALLBACK enumProc(HMONITOR hmon, HDC, LPRECT lprc, LPARAM data)
{
    auto *out = reinterpret_cast<QVector<MonitorEntry> *>(data);

    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!::GetMonitorInfoW(hmon, &info))
        return TRUE;

    MonitorEntry e;
    e.handle = hmon;
    e.geometry = QRect(info.rcMonitor.left, info.rcMonitor.top,
                       info.rcMonitor.right - info.rcMonitor.left,
                       info.rcMonitor.bottom - info.rcMonitor.top);
    e.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
    e.deviceName = QString::fromWCharArray(info.szDevice);
    out->push_back(e);
    return TRUE;
}

} // namespace

namespace monitors {

QVector<MonitorEntry> enumerate()
{
    QVector<MonitorEntry> out;
    ::EnumDisplayMonitors(nullptr, nullptr, enumProc, reinterpret_cast<LPARAM>(&out));
    // Stable order: primary first, then left-to-right.
    std::sort(out.begin(), out.end(), [](const MonitorEntry &a, const MonitorEntry &b) {
        if (a.primary != b.primary)
            return a.primary;
        if (a.geometry.x() != b.geometry.x())
            return a.geometry.x() < b.geometry.x();
        return a.geometry.y() < b.geometry.y();
    });
    return out;
}

HMONITOR fromWindow(HWND hwnd)
{
    if (!hwnd)
        return fromCursor();
    return ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
}

HMONITOR fromPoint(POINT pt)
{
    return ::MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
}

HMONITOR fromCursor()
{
    POINT pt{};
    ::GetCursorPos(&pt);
    return ::MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
}

bool contains(const MonitorEntry &mon, POINT pt)
{
    return pt.x >= mon.geometry.x() && pt.x < mon.geometry.x() + mon.geometry.width()
        && pt.y >= mon.geometry.y() && pt.y < mon.geometry.y() + mon.geometry.height();
}

} // namespace monitors
