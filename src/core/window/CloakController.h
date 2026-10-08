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

// True while WE force-disabled DWM show/hide transitions on this HWND
// (set on cloak, restored on show — no animation during space switches).
// Bookkeeping only: DWMWA_TRANSITIONS_FORCEDISABLED is [set]-only, the
// attribute itself cannot be read back from DWM.
bool transitionsForced(HWND hwnd);

enum class Backend { None, ImmersiveView, DwmAttribute, ShowWindow };
Backend lastBackend();

// Test/monitoring: how many windows we currently hold hidden.
int hiddenCount();

// Which backend currently hides hwnd (None = not held by us). Lets tests pin
// the shell SetCloak path — the only backend that keeps the taskbar button.
Backend backendOf(HWND hwnd);

// True when the ImmersiveShell IApplicationViewCollection proxy resolves —
// i.e. the shell SetCloak backend is usable at all in this session. When it
// is, cloak() MUST pick ImmersiveView; DWM/ShowWindow are last-resort only.
bool shellBackendAvailable();

// Graceful exit: reverse EVERY hide this process performed (all backends).
// Only touches HWNDs we recorded in this process — never shell-hidden windows.
// Returns how many windows were restored.
int showAllHidden();

} // namespace cloak
