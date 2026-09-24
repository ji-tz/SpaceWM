#pragma once

#include <Windows.h>
#include <dwmapi.h>
#include <optional>

// Thin wrappers around DWM cloak. Cloak hides a window without destroying it —
// the same mechanism the shell uses when switching system virtual desktops.
namespace cloak {

// DWMWA_CLOAK = 13 (dwmapi.h); set to cloak, clear to show again.
inline bool set(HWND hwnd, bool enable)
{
    if (!hwnd || !IsWindow(hwnd))
        return false;
    BOOL value = enable ? TRUE : FALSE;
    const HRESULT hr = ::DwmSetWindowAttribute(hwnd, 13 /*DWMWA_CLOAK*/, &value, sizeof(value));
    return SUCCEEDED(hr);
}

// DWMWA_CLOAKED = 14; non-zero if currently cloaked (by app, shell, or inherited).
inline bool isCloaked(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd))
        return false;
    DWORD cloaked = 0;
    const HRESULT hr = ::DwmGetWindowAttribute(hwnd, 14 /*DWMWA_CLOAKED*/, &cloaked, sizeof(cloaked));
    return SUCCEEDED(hr) && cloaked != 0;
}

} // namespace cloak
