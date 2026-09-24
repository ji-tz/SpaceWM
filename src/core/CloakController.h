#pragma once

#include <Windows.h>

// Hide/show a window without destroying it (per-monitor space switching).
//
// Backend order:
//   1) IApplicationView::SetCloak via ImmersiveShell (shell-equivalent)
//   2) DwmSetWindowAttribute(DWMWA_CLOAK) — often E_ACCESSDENIED for other PIDs
//   3) ShowWindow(SW_HIDE/SW_SHOW) — always works across processes
namespace cloak {

bool set(HWND hwnd, bool enable);
bool isCloaked(HWND hwnd);

enum class Backend { None, ImmersiveView, DwmAttribute, ShowWindow };
Backend lastBackend();

} // namespace cloak
