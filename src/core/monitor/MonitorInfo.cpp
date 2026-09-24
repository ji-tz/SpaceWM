#include "core/monitor/MonitorInfo.h"

#include <dwmapi.h>
#include <shellscalingapi.h>

#include <algorithm>

#pragma comment(lib, "Shcore.lib")
#pragma comment(lib, "dwmapi.lib")

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

QRect logicalWorkArea(HMONITOR hmon)
{
    if (!hmon)
        return {};
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!::GetMonitorInfoW(hmon, &mi))
        return logicalGeometry(hmon);
    return toLogical(mi.rcWork, hmon);
}

UINT dpiFor(HMONITOR hmon)
{
    return effectiveDpiX(hmon);
}

double scaleFactor(HMONITOR hmon)
{
    const UINT dpi = effectiveDpiX(hmon);
    return dpi > 0 ? double(dpi) / 96.0 : 1.0;
}

QSize toLogicalSize(int physW, int physH, UINT dpiX, UINT dpiY)
{
    if (physW <= 0 || physH <= 0)
        return {};
    if (dpiX == 0)
        dpiX = 96;
    if (dpiY == 0)
        dpiY = 96;
    const int w = ::MulDiv(physW, 96, int(dpiX));
    const int h = ::MulDiv(physH, 96, int(dpiY));
    return QSize(std::max(1, w), std::max(1, h));
}

QSize logicalWindowSize(HWND hwnd)
{
    if (!hwnd || !::IsWindow(hwnd))
        return {};
    RECT wr{};
    // Prefer visible DWM frame (excludes invisible resize borders).
    const HRESULT hr = ::DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS,
                                               &wr, sizeof(wr));
    if (FAILED(hr) || (wr.right - wr.left) <= 0 || (wr.bottom - wr.top) <= 0) {
        if (!::GetWindowRect(hwnd, &wr))
            return {};
    }
    const int pw = wr.right - wr.left;
    const int ph = wr.bottom - wr.top;
    if (pw <= 0 || ph <= 0)
        return {};
    const HMONITOR hmon = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    UINT dx = 96, dy = 96;
    ::GetDpiForMonitor(hmon, MDT_EFFECTIVE_DPI, &dx, &dy);
    return toLogicalSize(pw, ph, dx, dy);
}

} // namespace monitors
