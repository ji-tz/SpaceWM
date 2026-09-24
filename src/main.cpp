#include <QApplication>
#include <QMessageBox>
#include <QSharedMemory>
#include <QTimer>

#include "core/CloakController.h"
#include "core/SpaceManager.h"
#include "core/WindowTracker.h"
#include "hotkeys/HotkeyManager.h"
#include "ui/OverviewWindow.h"
#include "ui/SwitchFlashOverlay.h"
#include "ui/TrayIcon.h"

#include <Windows.h>

// DPI awareness before any window is created — critical for correct
// monitor rects and PrintWindow thumbnails on scaled displays.
static void enableDpiAwareness()
{
    // Per-monitor V2 preferred; fall back gracefully.
    using SetCtx = BOOL(WINAPI *)(DPI_AWARENESS_CONTEXT);
    if (HMODULE user32 = ::GetModuleHandleW(L"user32.dll")) {
        if (auto setCtx = reinterpret_cast<SetCtx>(
                ::GetProcAddress(user32, "SetProcessDpiAwarenessContext"))) {
            setCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            return;
        }
    }
    ::SetProcessDPIAware();
}

int main(int argc, char *argv[])
{
    enableDpiAwareness();

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("SpaceWM"));
    app.setOrganizationName(QStringLiteral("SpaceWM"));
    app.setQuitOnLastWindowClosed(false);

    // Single instance — avoid double-hooking hotkeys.
    QSharedMemory guard(QStringLiteral("SpaceWM-single-instance"));
    if (guard.attach()) {
        QMessageBox::information(nullptr, QStringLiteral("SpaceWM"),
                                 QStringLiteral("SpaceWM is already running (check the tray)."));
        return 0;
    }
    guard.create(1);

    SpaceManager manager;
    WindowTracker tracker;
    HotkeyManager hotkeys;
    TrayIcon tray;
    SwitchFlashOverlay flash;
    auto *overview = new OverviewWindow(&manager);

    // --- wire tracker -> manager ---
    QObject::connect(&tracker, &WindowTracker::windowCreated, &manager, [&](quint64 h) {
        manager.trackWindow(reinterpret_cast<HWND>(h));
    });
    QObject::connect(&tracker, &WindowTracker::windowDestroyed, &manager, [&](quint64 h) {
        manager.untrackWindow(reinterpret_cast<HWND>(h));
    });
    // When a window moves across monitors, re-home it to the new monitor's current space.
    QObject::connect(&tracker, &WindowTracker::windowMoved, &manager, [&](quint64 h) {
        HWND hwnd = reinterpret_cast<HWND>(h);
        if (!WindowTracker::isManageable(hwnd))
            return;
        HMONITOR target = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        auto *m = manager.monitorOf(target);
        if (!m)
            return;
        // If not tracked, adopt; if tracked on another monitor, move to current space there.
        // Simplified: assign to current space of the monitor it now lives on.
        manager.assignWindow(hwnd, target, m->currentIndex);
    });

    // Display change
    QObject::connect(&app, &QGuiApplication::screenAdded, &manager, [&]() {
        manager.refreshMonitors();
    });
    QObject::connect(&app, &QGuiApplication::screenRemoved, &manager, [&]() {
        manager.refreshMonitors();
    });

    // --- switch animation flash ---
    QObject::connect(&manager, &SpaceManager::requestSwitchAnimation, &flash,
                     [&](quint64 hmon, int from, int to) {
        auto *m = manager.monitorOf(reinterpret_cast<HMONITOR>(hmon));
        if (!m)
            return;
        const int dir = (to > from) ? 1 : -1;
        flash.play(m->geometry, dir);
    });

    QObject::connect(&manager, &SpaceManager::spaceChanged, &tray,
                     [&](quint64 hmon, int index) {
        auto *m = manager.monitorOf(reinterpret_cast<HMONITOR>(hmon));
        if (!m)
            return;
        tray.setSpaceLabel(m->deviceName, index, int(m->spaces.size()));
    });

    // --- overview ---
    auto openOverview = [&]() {
        if (overview->isOpen())
            return;
        HMONITOR h = monitors::fromCursor();
        overview->openOnMonitor(h);
    };

    QObject::connect(overview, &OverviewWindow::closed, &manager, [&](int chosen) {
        if (chosen < 0)
            return;
        HMONITOR h = overview->targetMonitor();
        manager.switchSpace(h, chosen, /*animateHint=*/true);
    });

    // --- hotkeys ---
    QObject::connect(&hotkeys, &HotkeyManager::actionTriggered, &app, [&](int action) {
        using A = HotkeyManager::Action;
        HMONITOR h = monitors::fromCursor();

        switch (action) {
        case A::SwitchPrevSpace: {
            auto *m = manager.monitorOf(h);
            if (!m || m->spaces.isEmpty())
                return;
            const int n = int(m->spaces.size());
            const int next = (m->currentIndex - 1 + n) % n;
            manager.switchSpace(h, next, true);
            break;
        }
        case A::SwitchNextSpace: {
            auto *m = manager.monitorOf(h);
            if (!m || m->spaces.isEmpty())
                return;
            const int n = int(m->spaces.size());
            const int next = (m->currentIndex + 1) % n;
            manager.switchSpace(h, next, true);
            break;
        }
        case A::ToggleOverview:
            if (overview->isOpen())
                overview->closeOverview(false);
            else
                openOverview();
            break;
        case A::JumpSpace1:
        case A::JumpSpace2:
        case A::JumpSpace3:
        case A::JumpSpace4: {
            const int idx = action - A::JumpSpace1;
            manager.switchSpace(h, idx, true);
            break;
        }
        default:
            break;
        }
    });

    // --- tray ---
    QObject::connect(&tray, &TrayIcon::overviewRequested, &app, openOverview);
    QObject::connect(&tray, &TrayIcon::nextSpaceRequested, &app, [&]() {
        HMONITOR h = monitors::fromCursor();
        auto *m = manager.monitorOf(h);
        if (!m || m->spaces.isEmpty())
            return;
        manager.switchSpace(h, (m->currentIndex + 1) % int(m->spaces.size()), true);
    });
    QObject::connect(&tray, &TrayIcon::prevSpaceRequested, &app, [&]() {
        HMONITOR h = monitors::fromCursor();
        auto *m = manager.monitorOf(h);
        if (!m || m->spaces.isEmpty())
            return;
        const int n = int(m->spaces.size());
        manager.switchSpace(h, (m->currentIndex - 1 + n) % n, true);
    });
    QObject::connect(&tray, &TrayIcon::refreshMonitorsRequested, &manager, &SpaceManager::refreshMonitors);
    QObject::connect(&tray, &TrayIcon::quitRequested, &app, [&]() {
        // Uncloak everything so no windows are stuck hidden on exit.
        for (MonitorSpaces *m : manager.monitors()) {
            for (int s = 0; s < m->spaces.size(); ++s) {
                for (HWND hwnd : m->spaces[s].windows) {
                    cloak::set(hwnd, false);
                }
            }
        }
        app.quit();
    });

    if (!hotkeys.registerDefaults()) {
        tray.showMessage(QObject::tr("SpaceWM"),
                         QObject::tr("Some hotkeys failed to register (maybe in use)."));
    }

    // Initial adoption of open windows.
    manager.adoptExistingWindows();

    {
        auto *m = manager.monitorFromCursor();
        if (m)
            tray.setSpaceLabel(m->deviceName, m->currentIndex, int(m->spaces.size()));
    }

    // Periodic cleanup of dead HWNDs (cheap safety net).
    QTimer cleanup;
    QObject::connect(&cleanup, &QTimer::timeout, &manager, [&]() {
        for (MonitorSpaces *m : manager.monitors()) {
            for (int s = 0; s < m->spaces.size(); ++s) {
                const auto copy = m->spaces[s].windows;
                for (HWND h : copy) {
                    if (!::IsWindow(h))
                        manager.untrackWindow(h);
                }
            }
        }
    });
    cleanup.start(5000);

    return app.exec();
}
