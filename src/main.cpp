#include <QApplication>
#include <QMessageBox>
#include <QSharedMemory>
#include <QTimer>
#include <QWinEventNotifier>

#include "core/space/SpaceManager.h"
#include "core/log/Log.h"
#include "core/window/CloakController.h"
#include "core/window/WindowTracker.h"
#include "hotkeys/HotkeyManager.h"
#include "ui/effects/SwitchFlashOverlay.h"
#include "ui/overview/OverviewHost.h"
#include "ui/overview/OverviewWindow.h"
#include "ui/settings/SettingsDialog.h"
#include "ui/tray/TrayIcon.h"

#include <Windows.h>

// Named event: another process (build script) can request a graceful quit
// so cloak::showAllHidden() runs before the process exits.
static constexpr wchar_t kQuitEventName[] = L"SpaceWM-quit";

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

static void signalExistingInstanceQuit()
{
    if (HANDLE h = ::OpenEventW(EVENT_MODIFY_STATE, FALSE, kQuitEventName)) {
        ::SetEvent(h);
        ::CloseHandle(h);
    }
}

int main(int argc, char *argv[])
{
    // External graceful quit (scripts/stop-spacewm.ps1, SpaceWM.exe --quit).
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--quit") == 0) {
            signalExistingInstanceQuit();
            return 0;
        }
    }

    enableDpiAwareness();

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("SpaceWM"));
    app.setOrganizationName(QStringLiteral("SpaceWM"));
    app.setQuitOnLastWindowClosed(false);

    // TR/EH: spdlog → %AppData%\SpaceWM\logs\{trace,error}.log
    spacelog::init();
    spacelog::installCrashHandlers();
    spacelog::info(QStringLiteral("SpaceWM starting"));

    // Single instance — avoid double-hooking hotkeys.
    QSharedMemory guard(QStringLiteral("SpaceWM-single-instance"));
    if (guard.attach()) {
        // Already running: --quit already handled above; plain launch just exits.
        QMessageBox::information(nullptr, QStringLiteral("SpaceWM"),
                                 QStringLiteral("SpaceWM is already running (check the tray)."));
        spacelog::info(QStringLiteral("exit: another instance already running"));
        spacelog::shutdown();
        return 0;
    }
    guard.create(1);

    // Graceful stop path used by build scripts before replacing the exe.
    HANDLE quitEvent = ::CreateEventW(nullptr, TRUE, FALSE, kQuitEventName);
    QWinEventNotifier *quitNotifier = nullptr;
    if (quitEvent) {
        quitNotifier = new QWinEventNotifier(quitEvent, &app);
        QObject::connect(quitNotifier, &QWinEventNotifier::activated, &app,
                         [&app]() {
                             cloak::showAllHidden();
                             app.quit();
                         });
    }

    SpaceManager manager;
    WindowTracker tracker;
    HotkeyManager hotkeys;
    TrayIcon tray;
    SwitchFlashOverlay flash;
    OverviewHost overview(&manager);
    SettingsDialog settings;

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
        const int owned = manager.spaceOfWindow(hwnd);
        if (owned < 0) {
            manager.trackWindow(hwnd);
            return;
        }
        if (manager.ownerMonitorOf(hwnd) != target) {
            manager.assignWindow(hwnd, target, m->currentIndex);
        } else {
            // Same monitor: size/position changed — re-render that space preview.
            manager.refreshWindowAfterUpdate(hwnd);
        }
    });

    // Taskbar / Alt+Tab while overview is open → land on the CURRENT space
    // and close the overlay so the app is visible on the live desktop.
    QObject::connect(&tracker, &WindowTracker::windowForeground, &app, [&](quint64 h) {
        HWND hwnd = reinterpret_cast<HWND>(h);
        if (!overview.isOpen() || !hwnd || !::IsWindow(hwnd))
            return;
        const int ownedSpace = manager.spaceOfWindow(hwnd);
        // Own overview tool windows are unmanaged — ignore those foreground events.
        if (ownedSpace < 0 && !WindowTracker::isManageable(hwnd))
            return;

        HMONITOR mon = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        auto *m = manager.monitorOf(mon);
        if (!m)
            return;
        if (ownedSpace < 0)
            manager.trackWindow(hwnd);
        if (manager.ownerMonitorOf(hwnd) != mon
            || manager.spaceOfWindow(hwnd) != m->currentIndex)
            manager.assignWindow(hwnd, mon, m->currentIndex);

        // Leave preview; stay on the real current space with the app shown.
        overview.closeAll(false);
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

    // --- overview: every monitor at once (Mission Control style) ---
    auto openOverview = [&]() {
        overview.openAll();
    };
    auto toggleOverview = [&]() {
        if (overview.isOpen())
            overview.closeAll(false);
        else
            overview.openAll();
    };

    QObject::connect(&overview, &OverviewHost::spaceChosen, &manager,
                     [&](quint64 hmon, int space) {
        manager.switchSpace(reinterpret_cast<HMONITOR>(hmon), space, /*animateHint=*/false);
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
            toggleOverview();
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
    QObject::connect(&tray, &TrayIcon::settingsRequested, &app, [&]() {
        settings.reload();
        settings.show();
        settings.raise();
        settings.activateWindow();
    });
    QObject::connect(&settings, &SettingsDialog::settingsApplied, &app, [&]() {
        if (!hotkeys.registerDefaults()) {
            tray.showMessage(QObject::tr("SpaceWM"),
                             QObject::tr("Some hotkeys failed to register (maybe in use)."));
        }
    });
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
        // Graceful quit: uncloak every window WE hid in this process.
        // Never shows windows we did not hide.
        cloak::showAllHidden();
        app.quit();
    });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
        cloak::showAllHidden();
        spacelog::info(QStringLiteral("SpaceWM exiting (showAllHidden)"));
        spacelog::shutdown();
    });

    // Close named event handle on the way out (notifier first).
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [quitEvent, quitNotifier]() {
        if (quitNotifier)
            quitNotifier->setEnabled(false);
        if (quitEvent)
            ::CloseHandle(quitEvent);
    });

    if (!hotkeys.registerDefaults()) {
        tray.showMessage(QObject::tr("SpaceWM"),
                         QObject::tr("Some hotkeys failed to register (maybe in use)."));
    }

    // Initial adoption of open windows.
    manager.adoptExistingWindows();
    {
        int tracked = 0;
        for (MonitorSpaces *m : manager.monitors())
            for (const Space &sp : m->spaces)
                tracked += int(sp.windows.size());
        tray.showMessage(QObject::tr("SpaceWM"),
                         QObject::tr("Tracking %1 window(s). Ctrl+Alt+←/→ switch, Ctrl+Alt+Space overview.")
                             .arg(tracked));
    }

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
