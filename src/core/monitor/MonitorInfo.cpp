#include "core/monitor/MonitorInfo.h"

#include <shellscalingapi.h>

#include <algorithm>

#pragma comment(lib, "Shcore.lib")

namespace {

UINT effectiveDpiX(HMONITOR hmon)
{
    UINT dpiX = 96, dpiY = 96;
    if (FAILED(::GetDpiForMonitor(hmon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) || dpiX == 0)
        dpiX = 96;
    return dpiX;
}

UINT effectiveDpiY(HMONITOR hmon)
{
    UINT dpiX = 96, dpiY = 96;
    if (FAILED(::GetDpiForMonitor(hmon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) || dpiY == 0)
        dpiY = 96;
    return dpiY;
}

QRect toLogical(const RECT &phys, HMONITOR hmon)
{
    const UINT dx = effectiveDpiX(hmon);
    const UINT dy = effectiveDpiY(hmon);
    return QRect(
        ::MulDiv(phys.left, 96, int(dx)),
        ::MulDiv(phys.top, 96, int(dy)),
        ::MulDiv(phys.right - phys.left, 96, int(dx)),
        ::MulDiv(phys.bottom - phys.top, 96, int(dy)));
}

BOOL CALLBACK enumProc(HMONITOR hmon, HDC, LPRECT, LPARAM data)
{
    auto *out = reinterpret_cast<QVector<MonitorEntry> *>(data);

    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!::GetMonitorInfoW(hmon, &info))
        return TRUE;

    MonitorEntry e;
    e.handle = hmon;
    e.physRect = info.rcMonitor;
    e.geometry = toLogical(info.rcMonitor, hmon);
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
    return pt.x >= mon.physRect.left && pt.x < mon.physRect.right
        && pt.y >= mon.physRect.top && pt.y < mon.physRect.bottom;
}

bool physRectOf(HMONITOR hmon, RECT *out)
{
    if (!hmon || !out)
        return false;
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!::GetMonitorInfoW(hmon, &mi))
        return false;
    *out = mi.rcMonitor;
    return true;
}

QRect logicalGeometry(HMONITOR hmon)
{
    RECT phys{};
    if (!physRectOf(hmon, &phys))
        return {};
    return toLogical(phys, hmon);
}

} // namespace monitors
