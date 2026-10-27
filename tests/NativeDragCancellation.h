#pragma once

#include <QWidget>
#include <Windows.h>

namespace dragtest {

// QTest's QWidget mouse events never enter the native Windows queue. In Qt
// 6.8.3, startDoDragDrop first waits in GetMessage for a native WM_MOUSEMOVE;
// that loop cannot service the Qt cancellation timer. Queue a button-free
// native move BEFORE exec(), so it returns without entering an unattended OLE
// drag. The dragStarted refresh and source-lifetime assertions still run.
// Only our own GUI-thread windows receive messages; no global input/cursor change.
inline bool queueNativeRelease(QWidget *owner)
{
    const HWND candidates[] = {::GetCapture(), ::GetFocus(),
                               reinterpret_cast<HWND>(owner->window()->winId())};
    bool queued = false;
    for (HWND hwnd : candidates) {
        DWORD pid = 0;
        const DWORD tid = ::GetWindowThreadProcessId(hwnd, &pid);
        if (!hwnd || pid != ::GetCurrentProcessId() || tid != ::GetCurrentThreadId())
            continue;
        queued = (::PostMessageW(hwnd, WM_MOUSEMOVE, 0, 0) != FALSE) || queued;
    }
    return queued;
}

} // namespace dragtest
