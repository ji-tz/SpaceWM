#include "WindowTracker.h"

#include <QSet>

#include <dwmapi.h>

namespace {
bool hasNoTaskbarIcon(HWND hwnd)
{
    LONG_PTR ex = ::GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    return (ex & WS_EX_TOOLWINDOW) != 0 && (ex & WS_EX_APPWINDOW) == 0;
}

bool isCloakedByShell(HWND hwnd)
{
    DWORD cloaked = 0;
    if (FAILED(::DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))))
        return false;
    // Shell-cloaked (system virtual desktop / UWP internal) — leave alone.
    return (cloaked & 0x2) != 0;
}

// Free function with WINAPI/CALLBACK so the type matches WINEVENTPROC exactly.
void CALLBACK winEventTrampoline(
    HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG /*idChild*/,
    DWORD, DWORD, LPARAM)
{
    if (WindowTracker::s_instance)
        WindowTracker::s_instance->handle(event, hwnd, idObject);
}
} // namespace

WindowTracker *WindowTracker::s_instance = nullptr;

WindowTracker::WindowTracker(QObject *parent)
    : QObject(parent)
{
    s_instance = this;
    auto proc = reinterpret_cast<WINEVENTPROC>(&winEventTrampoline);
    // OUTOFCONTEXT: delivered on the thread that runs the message loop (Qt UI).
    m_createHook = ::SetWinEventHook(
        EVENT_OBJECT_CREATE, EVENT_OBJECT_CREATE, nullptr, proc,
        0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    m_destroyHook = ::SetWinEventHook(
        EVENT_OBJECT_DESTROY, EVENT_OBJECT_DESTROY, nullptr, proc,
        0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    m_locationHook = ::SetWinEventHook(
        EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr, proc,
        0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    m_showHook = ::SetWinEventHook(
        EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, proc,
        0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
}

WindowTracker::~WindowTracker()
{
    if (m_createHook) ::UnhookWinEvent(m_createHook);
    if (m_destroyHook) ::UnhookWinEvent(m_destroyHook);
    if (m_locationHook) ::UnhookWinEvent(m_locationHook);
    if (m_showHook) ::UnhookWinEvent(m_showHook);
    if (s_instance == this)
        s_instance = nullptr;
}

void WindowTracker::handle(DWORD event, HWND hwnd, LONG idObject)
{
    if (idObject != OBJID_WINDOW || !hwnd || !::IsWindow(hwnd))
        return;
    // Only top-level.
    if (::GetAncestor(hwnd, GA_PARENT) != ::GetDesktopWindow())
        return;

    switch (event) {
    case EVENT_OBJECT_CREATE:
    case EVENT_OBJECT_SHOW:
        if (isManageable(hwnd)) {
            if (m_onCreated) m_onCreated(hwnd);
            emit windowCreated(reinterpret_cast<quint64>(hwnd));
        }
        break;
    case EVENT_OBJECT_DESTROY:
        if (m_onDestroyed) m_onDestroyed(hwnd);
        emit windowDestroyed(reinterpret_cast<quint64>(hwnd));
        break;
    case EVENT_OBJECT_LOCATIONCHANGE:
        if (isManageable(hwnd)) {
            if (m_onLocation) m_onLocation(hwnd);
            emit windowMoved(reinterpret_cast<quint64>(hwnd));
        }
        break;
    default:
        break;
    }
}

QVector<HWND> WindowTracker::snapshotManageableWindows()
{
    QVector<HWND> out;
    struct Ctx { QVector<HWND> *out; } ctx{&out};

    ::EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        auto *c = reinterpret_cast<Ctx *>(lp);
        if (isManageable(hwnd))
            c->out->push_back(hwnd);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));

    return out;
}

bool WindowTracker::isManageable(HWND hwnd)
{
    // Only discover currently-visible top-level windows.
    // Never adopt shell-hidden / system-VD-hidden / minimized ghosts —
    // treating those as space members and then "showing" them was the
    // startup bug that flooded the desktop with hidden windows.
    if (!hwnd || !::IsWindow(hwnd) || !::IsWindowVisible(hwnd))
        return false;
    if (::GetAncestor(hwnd, GA_ROOT) != hwnd)
        return false;
    if (hasNoTaskbarIcon(hwnd))
        return false;
    if (isCloakedByShell(hwnd))
        return false;

    wchar_t cls[64]{};
    ::GetClassNameW(hwnd, cls, 64);
    static const QSet<QString> banned = {
        QStringLiteral("Progman"),
        QStringLiteral("WorkerW"),
        QStringLiteral("Shell_TrayWnd"),
        QStringLiteral("Shell_SecondaryTrayWnd"),
        QStringLiteral("NotifyIconOverflowWindow"),
        QStringLiteral("Windows.UI.Core.CoreWindow"),
        QStringLiteral("ApplicationFrameWindow"),
        QStringLiteral("SysListView32"),
        QStringLiteral("SysHeader32"),
        QStringLiteral("ToolTips_Class32"),
        QStringLiteral("DV2ControlHost"),
        QStringLiteral("Button"),
    };
    const QString name = QString::fromWCharArray(cls);
    if (banned.contains(name)) {
        if (name == QLatin1String("ApplicationFrameWindow")) {
            wchar_t title[256]{};
            ::GetWindowTextW(hwnd, title, 256);
            if (title[0] == L'\0')
                return false;
        } else {
            return false;
        }
    }

    RECT rc{};
    if (!::GetWindowRect(hwnd, &rc))
        return false;
    if (rc.right - rc.left <= 0 || rc.bottom - rc.top <= 0)
        return false;

    DWORD pid = 0;
    ::GetWindowThreadProcessId(hwnd, &pid);
    if (pid == ::GetCurrentProcessId())
        return false;

    return true;
}
