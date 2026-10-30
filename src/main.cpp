#include <QApplication>
#include <QElapsedTimer>
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

    // TR/EH: spdlog → <projectRoot>/TR/trace.log + <projectRoot>/EH/error.log
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
        QObject::connect(quitNotifier, &QWinEventNotifier::activated, &app, [&app]() {
            cloak::showAllHidden();
            app.quit();
        });
    }

    SpaceManager manager;
    WindowTracker tracker;
    HotkeyManager hotkeys;
    TrayIcon tray;
    SpaceSwitchAnimator switchAnimator(&manager);
    OverviewHost overview(&manager);
    SettingsDialog settings;

    // --- wire tracker -> manager ---
    QObject::connect(&tracker, &WindowTracker::windowCreated, &manager,
                     [&](quint64 h) { manager.trackWindow(reinterpret_cast<HWND>(h)); });
    QObject::connect(&tracker, &WindowTracker::windowDestroyed, &manager,
                     [&](quint64 h) { manager.untrackWindow(reinterpret_cast<HWND>(h)); });
    // Apps close transient windows by hiding them — auto-untrack so they
    // leave the preview (re-show re-tracks via the SHOW path).
    QObject::connect(&tracker, &WindowTracker::windowHidden, &manager,
                     [&](quint64 h) { manager.onWindowHidden(reinterpret_cast<HWND>(h)); });
    // When a window moves across monitors, re-home it to the new monitor's
    // current space (shared entry with the integration flow — see
    // SpaceManager::onWindowMoved).
    QObject::connect(&tracker, &WindowTracker::windowMoved, &manager,
                     [&](quint64 h) { manager.onWindowMoved(reinterpret_cast<HWND>(h)); });

    // Taskbar / Alt+Tab follows the activated window's owning space.
    // While overview is open, filter automatic activation from warming shots.
    // EVENT_SYSTEM_FOREGROUND carries no cause info (MSDN: "the foreground
    // window has changed"), so act ONLY on the two documented user-initiated
    // switches and ignore everything else (uncloak steals, activation reverts):
    //   - Alt+Tab — our LL hook announced the chord (switchPending);
    //   - mouse click — the button is still down when the event arrives
    //     (GetAsyncKeyState), which system side-effects never are.
    bool switchPending = false;
    QObject::connect(&hotkeys, &HotkeyManager::taskbarPressed, &switchAnimator,
                     [&]() { switchAnimator.prepareTaskbarSwitch(); });
    auto updateTaskbarGuard = [&]() {
        bool hasOtherWindows = false;
        if (!overview.isOpen())
            for (auto *mon : manager.monitors())
                for (int i = 0; i < mon->spaces.size(); ++i)
                    if (i != mon->currentIndex && manager.spaceHasWindows(mon->hmon, i))
                        hasOtherWindows = true;
        hotkeys.setTaskbarGuardEnabled(hasOtherWindows);
    };
    QObject::connect(&manager, &SpaceManager::windowTracked, &hotkeys, updateTaskbarGuard);
    QObject::connect(&manager, &SpaceManager::windowUntracked, &hotkeys, updateTaskbarGuard);
    QObject::connect(&manager, &SpaceManager::spaceChanged, &hotkeys, updateTaskbarGuard);
    QObject::connect(&manager, &SpaceManager::monitorLayoutChanged, &hotkeys, updateTaskbarGuard);
    QObject::connect(&overview, &OverviewHost::allClosed, &hotkeys, updateTaskbarGuard);
    QObject::connect(&hotkeys, &HotkeyManager::windowSwitchChord, &app, [&]() {
        if (overview.isOpen())
            switchPending = true;
    });
    QObject::connect(&tracker, &WindowTracker::windowForeground, &app, [&](quint64 h) {
        HWND hwnd = reinterpret_cast<HWND>(h);
        // OUTOFCONTEXT events can arrive after a newer activation. Do not
        // switch back to a window that is no longer in the foreground.
        if (!hwnd || !::IsWindow(hwnd) || ::GetForegroundWindow() != hwnd)
            return;
        if (!overview.isOpen()) {
            if (manager.onWindowForeground(hwnd))
                switchAnimator.finishTaskbarPress();
            return;
        }
        const int ownedSpace = manager.spaceOfWindow(hwnd);
        // Own overview tool windows are unmanaged — ignore those foreground events.
        if (ownedSpace < 0 && !WindowTracker::isManageable(hwnd))
            return;

        const bool userClick =
            (::GetAsyncKeyState(VK_LBUTTON) & 0x8000) || (::GetAsyncKeyState(VK_RBUTTON) & 0x8000);
        if (!switchPending && !userClick) {
            wchar_t cls[64]{};
            ::GetClassNameW(hwnd, cls, 64);
            spacelog::info(
                QStringLiteral("overview foreground ignored (no user switch): hwnd=0x%1 class=%2")
                    .arg(quintptr(hwnd), 0, 16)
                    .arg(QString::fromWCharArray(cls)));
            return;
        }
        const bool altTab = switchPending;
        switchPending = false;

        if (ownedSpace < 0)
            manager.trackWindow(hwnd);
        if (!manager.onWindowForeground(hwnd))
            return;

        // Leave preview on the activated app's owning space.
        wchar_t cls[64]{};
        ::GetClassNameW(hwnd, cls, 64);
        spacelog::info(
            QStringLiteral("overview auto-close: foreground hwnd=0x%1 class=%2 space=%3 via=%4")
                .arg(quintptr(hwnd), 0, 16)
                .arg(QString::fromWCharArray(cls))
                .arg(manager.spaceOfWindow(hwnd))
                .arg(altTab ? QStringLiteral("altTab") : QStringLiteral("click")));
        overview.closeAll(false);
    });

    // Display change
    QObject::connect(&app, &QGuiApplication::screenAdded, &manager,
                     [&]() { manager.refreshMonitors(); });
    QObject::connect(&app, &QGuiApplication::screenRemoved, &manager,
                     [&]() { manager.refreshMonitors(); });

    QObject::connect(&manager, &SpaceManager::spaceChanged, &tray, [&](quint64 hmon, int index) {
        auto *m = manager.monitorOf(reinterpret_cast<HMONITOR>(hmon));
        if (!m)
            return;
        tray.setSpaceLabel(m->deviceName, index, int(m->spaces.size()));
    });

    // --- overview: every monitor at once (Mission Control style) ---
    QObject::connect(&hotkeys, &HotkeyManager::winHoldChanged, &overview, [&](bool held) {
        overview.postWinPreviewEvent(held ? WinPreviewEvents::Event::WinDown
                                          : WinPreviewEvents::Event::WinUp);
    });
    auto openOverview = [&]() {
        QElapsedTimer timer;
        timer.start();
        hotkeys.setTaskbarGuardEnabled(false);
        const qint64 guardNs = timer.nsecsElapsed();
        switchAnimator.stopAll();
        const qint64 stopNs = timer.nsecsElapsed();
        spacelog::info(
            QStringLiteral("overview open dispatch guard_ms=%1 stopAnimations_ms=%2 total_ms=%3")
                .arg(double(guardNs) / 1000000.0, 0, 'f', 3)
                .arg(double(stopNs - guardNs) / 1000000.0, 0, 'f', 3)
                .arg(double(stopNs) / 1000000.0, 0, 'f', 3));
        overview.openAll();
        if (overview.isOpen())
            hotkeys.setConsumeEscape(true); // Esc closes globally (hook swallows)
    };
    auto toggleOverview = [&]() {
        if (overview.isOpen()) {
            hotkeys.setConsumeEscape(false);
            overview.closeAll(false);
        } else {
            openOverview();
        }
    };
    QObject::connect(&hotkeys, &HotkeyManager::tabPressed, &overview, [&]() {
        overview.postWinPreviewEvent(WinPreviewEvents::Event::TabDown);
    });
    QObject::connect(&overview, &OverviewHost::previewRequested, &app, toggleOverview);
    // Any full close (cancel, commit, foreground/Alt+Tab) ends with allClosed.
    QObject::connect(&overview, &OverviewHost::allClosed, &hotkeys, [&]() {
        hotkeys.setConsumeEscape(false);
        switchPending = false;
    });
    // Esc pressed while the overview is open — works even when the panel
    // lost focus (the hook swallows it, so no leak to the foreground app).
    QObject::connect(&hotkeys, &HotkeyManager::escapeRequested, &app, [&]() {
        if (overview.isOpen())
            overview.closeAll(false);
    });

    QObject::connect(&overview, &OverviewHost::spaceChosen, &manager, [&](quint64 hmon, int space) {
        manager.switchSpace(reinterpret_cast<HMONITOR>(hmon), space, /*animateHint=*/false);
    });

    // --- hotkeys ---
    QObject::connect(&hotkeys, &HotkeyManager::actionTriggered, &app, [&](int action) {
        using A = HotkeyManager::Action;
        HMONITOR h = monitors::fromCursor();

        switch (action) {
        case A::SwitchPrevSpace:
            manager.switchAdjacentSpace(h, -1);
            break;
        case A::SwitchNextSpace:
            manager.switchAdjacentSpace(h, 1);
            break;
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
        manager.switchAdjacentSpace(monitors::fromCursor(), 1);
    });
    QObject::connect(&tray, &TrayIcon::prevSpaceRequested, &app, [&]() {
        manager.switchAdjacentSpace(monitors::fromCursor(), -1);
    });
    QObject::connect(&tray, &TrayIcon::refreshMonitorsRequested, &manager,
                     &SpaceManager::refreshMonitors);
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
        tray.showMessage(
            QObject::tr("SpaceWM"),
            QObject::tr("Tracking %1 window(s). Ctrl+Alt+←/→ switch, Ctrl+Alt+Space overview.")
                .arg(tracked));
    }

    {
        auto *m = manager.monitorFromCursor();
        if (m)
            tray.setSpaceLabel(m->deviceName, m->currentIndex, int(m->spaces.size()));
    }

    // Periodic cleanup of dead HWNDs and app-hidden windows (cheap safety net
    // in case a hide event was missed; onWindowHidden is the live path).
    QTimer cleanup;
    QObject::connect(&cleanup, &QTimer::timeout, &manager, [&]() {
        for (MonitorSpaces *m : manager.monitors()) {
            for (int s = 0; s < m->spaces.size(); ++s) {
                const auto copy = m->spaces[s].windows;
                for (HWND h : copy) {
                    if (!::IsWindow(h))
                        manager.untrackWindow(h);
                    else
                        manager.onWindowHidden(h);
                }
            }
        }
    });
    cleanup.start(5000);

    return app.exec();
}
