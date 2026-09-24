#pragma once

#include <QObject>
#include <Windows.h>
#include <functional>

// Watches top-level window create/destroy/show/hide via WinEvent hooks.
// Deliberately Win32-only: hooks fire on the UI thread that installs them
// when using WINEVENT_OUTOFCONTEXT, which matches our single-threaded Qt app.
class WindowTracker : public QObject {
    Q_OBJECT
public:
    using WindowCallback = std::function<void(HWND)>;

    explicit WindowTracker(QObject *parent = nullptr);
    ~WindowTracker() override;

    void setOnCreated(WindowCallback cb) { m_onCreated = std::move(cb); }
    void setOnDestroyed(WindowCallback cb) { m_onDestroyed = std::move(cb); }
    void setOnLocationChanged(WindowCallback cb) { m_onLocation = std::move(cb); }

    // Snapshot of currently visible, manageable top-level windows.
    static QVector<HWND> snapshotManageableWindows();

    // Heuristic: app window we may place into a space.
    static bool isManageable(HWND hwnd);

    // SetWinEventHook has no user-data slot; one instance per process.
    // Public so the free-function trampoline can dispatch to it.
    static WindowTracker *s_instance;

    // Called from the WinEvent trampoline (friend-equivalent via public).
    void handle(DWORD event, HWND hwnd, LONG idObject);

signals:
    void windowCreated(quint64 hwnd);
    void windowDestroyed(quint64 hwnd);
    void windowMoved(quint64 hwnd);
    // Top-level window came to the foreground (taskbar click, Alt+Tab, …).
    void windowForeground(quint64 hwnd);

private:
    HWINEVENTHOOK m_createHook = nullptr;
    HWINEVENTHOOK m_destroyHook = nullptr;
    HWINEVENTHOOK m_locationHook = nullptr;
    HWINEVENTHOOK m_showHook = nullptr;
    HWINEVENTHOOK m_foregroundHook = nullptr;

    WindowCallback m_onCreated;
    WindowCallback m_onDestroyed;
    WindowCallback m_onLocation;
};
