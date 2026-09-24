#pragma once

#include <Windows.h>

// Hide/show a window without destroying it (per-monitor space switching).
//
// CRITICAL: never show a window unless WE previously hid it.
// Showing arbitrary invisible windows would un-hide system/shell windows.
namespace cloak {

// Hide (enable=true) or show again (enable=false).
// show is a no-op (returns false) if this process did not hide the window.
bool set(HWND hwnd, bool enable);

// True if we hid it, or DWM reports cloaked.
bool isCloaked(HWND hwnd);

// True only if SpaceWM's ShowWindow backend currently hides this HWND.
bool isHiddenByUs(HWND hwnd);

enum class Backend { None, ImmersiveView, DwmAttribute, ShowWindow };
Backend lastBackend();

// Test/monitoring: how many windows we currently hold hidden.
int hiddenCount();

// Normal exit: reverse EVERY hide this process performed (all backends).
// Only touches HWNDs we recorded — never shell-hidden windows.
// Returns how many windows were restored.
int showAllHidden();

} // namespace cloak
