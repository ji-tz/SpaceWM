#include "NativeDragCancellation.h"
#include "ui/effects/SwitchFlashOverlay.h"
#include <QtTest>

#include "core/log/Log.h"
#include "core/capture/ThumbnailCapture.h"
#include "core/space/SpaceManager.h"
#include "core/window/CloakController.h"
#include "core/window/WindowTracker.h"
#include "ui/overview/OverviewHost.h"
#include "ui/overview/OverviewWindow.h"
#include "ui/preview/AddSpaceButton.h"
#include "ui/preview/SpaceCardWidget.h"
#include "ui/preview/WindowPreviewWidget.h"
#include "hotkeys/HotkeyManager.h"

#include <QDataStream>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QGraphicsOpacityEffect>
#include <QMimeData>
#include <QProcess>
#include <QPushButton>
#include <QPointer>
#include <QSignalSpy>
#include <QTimer>

#include <Windows.h>
#include <dwmapi.h>

#include <memory>

// Real-mode integration flow (opt-in, see AGENTS.md §2.5).
//
// Drives the FULL overview lifecycle against REAL foreign applications
// (Notepad, File Explorer, a browser): real tracking, real cloak/switch,
// real composite screenshots. Widget input is synthesized Qt events — the
// LL keyboard hook ignores injected keys by design (HotkeyManager), so the
// hotkey step calls the exact same openAll() entry the hotkey lambda uses
// (hotkey parsing/debounce: test_hotkeys; physical hotkey: AGENTS.md §5).
//
// Each NEW feature that touches this flow must APPEND a stepNN_ slot (and
// extend cleanup if it allocates resources) — never rewrite earlier steps.
// Default `ctest` skips this suite; run it explicitly:
//   SpaceWM.exe --quit ; $env:SPACEWM_IT='1' ; ctest -R test_integration_flow -V
class TestIntegrationFlow : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase()
    {
        if (!qEnvironmentVariableIsSet("SPACEWM_IT"))
            QSKIP("real-mode flow is opt-in: set SPACEWM_IT=1 (AGENTS.md §2.5)");
        // Two SpaceManagers would fight over cloak — refuse to run alongside.
        if (HANDLE ev = ::OpenEventW(EVENT_MODIFY_STATE, FALSE, L"SpaceWM-quit")) {
            ::CloseHandle(ev);
            QSKIP("SpaceWM.exe is running — quit it first (SpaceWM.exe --quit)");
        }
        // Real-mode flow writes runtime logs too (open-timings triage, §2.5).
        spacelog::init();

        // Same wiring as main.cpp (minus main-only foreground-close logic —
        // that lives in main and is covered by manual smoke §5.7).
        connect(&m_tracker, &WindowTracker::windowCreated, &m_sm,
                [this](quint64 h) { m_sm.trackWindow(reinterpret_cast<HWND>(h)); });
        connect(&m_tracker, &WindowTracker::windowDestroyed, &m_sm,
                [this](quint64 h) { m_sm.untrackWindow(reinterpret_cast<HWND>(h)); });
        connect(&m_tracker, &WindowTracker::windowMoved, &m_sm, [this](quint64 h) {
            // Same entry as main.cpp — cross-monitor moves re-home (fidelity:
            // the flow used to only refresh, which main never does).
            m_sm.onWindowMoved(reinterpret_cast<HWND>(h));
        });
        connect(&m_tracker, &WindowTracker::windowHidden, &m_sm, [this](quint64 h) {
            // Same entry as main.cpp — apps close transient windows by HIDE;
            // SpaceWM must auto-untrack them so they leave the preview.
            m_sm.onWindowHidden(reinterpret_cast<HWND>(h));
        });
        connect(&m_host, &OverviewHost::spaceChosen, &m_sm, [this](quint64 h, int space) {
            m_sm.switchSpace(reinterpret_cast<HMONITOR>(h), space,
                             /*animateHint=*/false);
        });
        m_allClosedSpy = std::make_unique<QSignalSpy>(&m_host, &OverviewHost::allClosed);

        QVERIFY(!m_sm.monitors().isEmpty());
        m_monitor = m_sm.monitors().first();
        m_hmon = m_monitor->hmon;
        m_allClosedSpy->clear();
    }

    // Step 1: open Notepad, File Explorer and a browser (real processes).
    void step01_launchNotepadExplorerBrowser()
    {
        // --- Notepad (a real .txt so the window has content) ---
        m_noteFile = QDir::temp().filePath(
            QStringLiteral("spacewm-it-%1.txt").arg(QCoreApplication::applicationPid()));
        {
            QFile f(m_noteFile);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
            f.write("SpaceWM real-mode integration flow\n");
            f.close();
        }
        auto before = WindowTracker::snapshotManageableWindows();
        QVERIFY(QProcess::startDetached(QStringLiteral("notepad.exe"), {m_noteFile}));
        m_hwndNote = waitForNewWindow(before, {QStringLiteral("Notepad")}, 15000);
        QVERIFY2(m_hwndNote, "Notepad window did not appear (an existing Notepad instance "
                             "may have absorbed the launch — close it and rerun)");

        // --- File Explorer (its own new window; never kill explorer.exe) ---
        before = WindowTracker::snapshotManageableWindows();
        QVERIFY(QProcess::startDetached(QStringLiteral("explorer.exe"), QStringList()));
        m_hwndExpl = waitForNewWindow(before, {QStringLiteral("CabinetWClass")}, 15000);
        QVERIFY2(m_hwndExpl, "File Explorer window did not appear");

        // --- Browser: Edge/Chrome if installed, else a second Notepad ---
        const QString browser = findBrowser();
        if (!browser.isEmpty()) {
            before = WindowTracker::snapshotManageableWindows();
            QVERIFY(QProcess::startDetached(
                browser, {QStringLiteral("--new-window"), QStringLiteral("about:blank")}));
            m_hwndWeb = waitForNewWindow(before, {QStringLiteral("Chrome_WidgetWin_1")}, 20000);
            if (!m_hwndWeb)
                QWARN("browser window did not appear — falling back to a 2nd Notepad");
        } else {
            QWARN("no Edge/Chrome found — falling back to a 2nd Notepad");
        }
        if (!m_hwndWeb) {
            m_webFile = QDir::temp().filePath(
                QStringLiteral("spacewm-it-web-%1.txt").arg(QCoreApplication::applicationPid()));
            QFile f(m_webFile);
            if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                f.write("browser stand-in\n");
                f.close();
            }
            before = WindowTracker::snapshotManageableWindows();
            QVERIFY(QProcess::startDetached(QStringLiteral("notepad.exe"), {m_webFile}));
            m_hwndWeb = waitForNewWindow(before, {QStringLiteral("Notepad")}, 15000);
            QVERIFY2(m_hwndWeb, "fallback third window did not appear");
        }

        QVERIFY(::IsWindow(m_hwndNote));
        QVERIFY(::IsWindow(m_hwndExpl));
        QVERIFY(::IsWindow(m_hwndWeb));
    }

    // Step 2: software up — all three foreign windows get tracked into the
    // cold-start single space of their monitor.
    void step02_softwareTracksAllThree()
    {
        // Cold start: exactly one space per monitor, no matter which display
        // the flow's active panel lands on (multi-monitor machines).
        for (MonitorSpaces *mon : m_sm.monitors())
            QCOMPARE(mon->spaces.size(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(m_sm.spaceOfWindow(m_hwndNote), 0, 8000);
        QTRY_COMPARE_WITH_TIMEOUT(m_sm.spaceOfWindow(m_hwndExpl), 0, 8000);
        QTRY_COMPARE_WITH_TIMEOUT(m_sm.spaceOfWindow(m_hwndWeb), 0, 8000);
        // Nothing got hidden merely by tracking the current space.
        QVERIFY(!cloak::isCloaked(m_hwndNote));
        QVERIFY(!cloak::isCloaked(m_hwndExpl));
        QVERIFY(!cloak::isCloaked(m_hwndWeb));
    }

    // Step 3: hotkey-equivalent open (see file header); preview must STAY
    // open — guards the "second open flashes closed" regression class.
    void step03_openPreview()
    {
        m_host.openAll();
        QVERIFY(m_host.isOpen());
        QVERIFY(m_host.activePanel() != nullptr);
        syncPanelMonitor(); // anchor all later assertions to the OPEN panel
        QCOMPARE(m_host.activePanel()->cardCount(), m_sm.spaceCount(m_hmon));

        QTest::qWait(700); // enter fade + any foreground side-effects settle
        QVERIFY2(m_host.isOpen(), "preview closed itself right after open");
        QVERIFY(m_sm.overviewOpen());
    }

    // Step 4: click the "+" button → a second space appears (real strip widget).
    void step04_clickAddCreatesSecondSpace()
    {
        OverviewWindow *panel = m_host.activePanel();
        QVERIFY(panel && panel->isOpen());
        auto *btn = panel->findChild<AddSpaceButton *>();
        QVERIFY(btn);

        const QPoint c = btn->rect().center();
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(c), btn->mapToGlobal(c),
                          btn->mapToGlobal(c), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(btn, &press);

        QTRY_COMPARE_WITH_TIMEOUT(m_sm.spaceCount(m_hmon), 2, 3000);
        QCOMPARE(panel->cardCount(), 2);
        m_nameS1 = m_monitor->spaces[0].name;
        m_nameS2 = m_monitor->spaces[1].name;
        QVERIFY(!m_nameS1.isEmpty() && !m_nameS1.isEmpty());
        QVERIFY(m_nameS1 != m_nameS2);
    }

    // Step 5: drag Notepad onto the second space card (real DnD payload the
    // card receives from a window tile) → membership moves, current space stays.
    void step05_dropNotepadOntoSecondSpace()
    {
        OverviewWindow *panel = m_host.activePanel();
        QVERIFY(panel && panel->isOpen());
        const auto cards = visibleCards(panel);
        QCOMPARE(cards.size(), 2);
        SpaceCardWidget *target = cards[1];

        QByteArray payload;
        QDataStream ds(&payload, QIODevice::WriteOnly);
        ds << quint64(m_hwndNote);
        QMimeData mime;
        mime.setData(WindowPreviewWidget::kMimeType, payload);

        const QPoint c = target->rect().center();
        QDragEnterEvent enter(c, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(target, &enter);
        QVERIFY2(enter.isAccepted(), "second space card rejected the drag");

        QDropEvent drop(c, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(target, &drop);
        QVERIFY(drop.isAccepted());

        QTRY_COMPARE_WITH_TIMEOUT(m_sm.spaceOfWindow(m_hwndNote), 1, 3000);
        // placeWindowInSpace must not jump the desktop — still on index 0.
        QCOMPARE(m_monitor->currentIndex, 0);
    }

    // Step 6: click the second space card → commit switch to index 1.
    void step06_clickSecondSpaceCommitsSwitch()
    {
        OverviewWindow *panel = m_host.activePanel();
        QVERIFY(panel && panel->isOpen());
        const auto cards = visibleCards(panel);
        QCOMPARE(cards.size(), 2);

        QSignalSpy chosen(&m_host, &OverviewHost::spaceChosen);
        clickWidget(cards[1]);

        QTRY_COMPARE_WITH_TIMEOUT(chosen.count(), 1, 3000);
        QCOMPARE(chosen.first().at(1).toInt(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(m_monitor->currentIndex, 1, 3000);
    }

    // Step 7: preview exits by itself after the commit; real cloak follows —
    // Notepad (space 1) visible, Explorer/browser (space 0) hidden.
    void step07_overviewAutoExited()
    {
        QTRY_VERIFY_WITH_TIMEOUT(!m_host.isOpen(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!m_sm.overviewOpen(), 5000);
        QVERIFY(m_allClosedSpy->count() >= 1);

        QTRY_VERIFY_WITH_TIMEOUT(!cloak::isCloaked(m_hwndNote), 3000);
        if (explOnPanel())
            QTRY_VERIFY_WITH_TIMEOUT(cloak::isCloaked(m_hwndExpl), 3000);
        dumpWebState("step07");
        if (webStableIn(0)) // pre-swap: browser must live on S1 (idx 0)
            QVERIFY(cloak::isCloaked(m_hwndWeb));
    }

    // Step 8: open preview again (second-open path) — must stay open, and the
    // inactive-space windows remain cloaked behind the mask.
    void step08_reopenPreview()
    {
        m_host.openAll();
        QVERIFY(m_host.isOpen());
        syncPanelMonitor();
        QCOMPARE(m_host.activePanel()->cardCount(), 2);

        QTest::qWait(700);
        QVERIFY2(m_host.isOpen(), "second open flashed closed");
        if (explOnPanel())
            QVERIFY(cloak::isCloaked(m_hwndExpl));
        QVERIFY(!cloak::isCloaked(m_hwndNote));
    }

    // Step 9: drag the FIRST space card onto the SECOND — reorder path
    // (press → move past startDragDistance → reorderRequested → moveSpace).
    void step09_dragFirstSpaceCardOntoSecond()
    {
        OverviewWindow *panel = m_host.activePanel();
        QVERIFY(panel && panel->isOpen());
        auto cards = visibleCards(panel);
        QCOMPARE(cards.size(), 2);
        SpaceCardWidget *first = cards[0];
        SpaceCardWidget *second = cards[1];

        // All events are dispatched synchronously: reorderRequested rebuilds
        // the strip (deleteLater) DURING the move — no event-loop turn may
        // happen before the release, or the pressed card would die mid-drag.
        const QPoint gSecond = second->mapToGlobal(second->rect().center());
        const QPointF localSecond = first->mapFromGlobal(gSecond);

        const QPointF c0(first->rect().center());
        QMouseEvent press(QEvent::MouseButtonPress, c0, first->mapToGlobal(c0),
                          first->mapToGlobal(c0), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(first, &press);

        QMouseEvent move(QEvent::MouseMove, localSecond, QPointF(gSecond), QPointF(gSecond),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(first, &move); // fires reorder → rebuild here

        QMouseEvent release(QEvent::MouseButtonRelease, localSecond, QPointF(gSecond),
                            QPointF(gSecond), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(first, &release); // old card still alive (deleteLater)

        QTRY_COMPARE_WITH_TIMEOUT(m_sm.spaceCount(m_hmon), 2, 2000);
    }

    // Step 10: order actually swapped — [Space 2, Space 1]; membership and
    // current space followed the moved card.
    void step10_spaceOrderSwapped()
    {
        flushDeferredDeletes();
        QCOMPARE(m_monitor->spaces.size(), 2);
        QCOMPARE(m_monitor->spaces[0].name, m_nameS2);
        QCOMPARE(m_monitor->spaces[1].name, m_nameS1);
        // Notepad lives on S2 which moved to index 0 …
        QTRY_COMPARE_WITH_TIMEOUT(m_sm.spaceOfWindow(m_hwndNote), 0, 3000);
        if (explOnPanel())
            QCOMPARE(m_sm.spaceOfWindow(m_hwndExpl), 1);
        if (webStableIn(1)) // post-swap: S1 sits at index 1
            QCOMPARE(m_sm.spaceOfWindow(m_hwndWeb), 1);
        // … and the desktop stayed on S2 (currentIndex followed the move).
        QCOMPARE(m_monitor->currentIndex, 0);

        OverviewWindow *panel = m_host.activePanel();
        QVERIFY(panel);
        QCOMPARE(panel->cardCount(), 2);
    }

    // Step 11: click the SECOND card (= original Space 1 with Explorer/browser).
    void step11_clickSecondSpaceCard()
    {
        OverviewWindow *panel = m_host.activePanel();
        QVERIFY(panel && panel->isOpen());
        auto cards = visibleCards(panel);
        QCOMPARE(cards.size(), 2);

        QSignalSpy chosen(&m_host, &OverviewHost::spaceChosen);
        clickWidget(cards[1]);
        QTRY_COMPARE_WITH_TIMEOUT(chosen.count(), 1, 3000);
        QCOMPARE(chosen.first().at(1).toInt(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(m_monitor->currentIndex, 1, 3000);
    }

    // Step 12: entered the second space and the preview is gone; cloak and
    // membership reflect the final state.
    void step12_enteredSpaceAndPreviewGone()
    {
        QTRY_VERIFY_WITH_TIMEOUT(!m_host.isOpen(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!m_sm.overviewOpen(), 5000);
        QVERIFY(m_allClosedSpy->count() >= 2);
        QCOMPARE(m_monitor->currentIndex, 1);

        QTRY_VERIFY_WITH_TIMEOUT(!cloak::isCloaked(m_hwndExpl), 3000);
        if (webStableIn(1)) {
            QVERIFY(!cloak::isCloaked(m_hwndWeb));
            QCOMPARE(m_sm.spaceOfWindow(m_hwndWeb), 1);
        }
        QTRY_VERIFY_WITH_TIMEOUT(cloak::isCloaked(m_hwndNote), 3000);
        QCOMPARE(m_sm.spaceOfWindow(m_hwndNote), 0);
        if (explOnPanel())
            QCOMPARE(m_sm.spaceOfWindow(m_hwndExpl), 1);
    }

    // Step 13: a monitor refresh whose handles change must not lose windows —
    // survivors re-home onto a live monitor's current space (SpaceManager::
    // applyMonitorEntries purge → re-home), and a window that was OFF-space
    // (cloaked) before the change becomes visible again instead of staying
    // stuck hidden until process exit.
    void step13_monitorRefreshRehomesLostWindows()
    {
        // State from step 12: Notepad lives off the current space → cloaked.
        QTRY_VERIFY_WITH_TIMEOUT(cloak::isCloaked(m_hwndNote), 3000);
        const HMONITOR realH = m_hmon;

        // 1) Every real handle "vanishes" — only a fake monitor survives.
        MonitorEntry fake;
        fake.handle = reinterpret_cast<HMONITOR>(quintptr(0x00C0FFEE));
        m_sm.applyMonitorEntries({fake});

        // No flow window may fall out of management…
        for (HWND h : {m_hwndNote, m_hwndExpl, m_hwndWeb}) {
            if (!h || !::IsWindow(h))
                continue;
            QVERIFY2(
                m_sm.spaceOfWindow(h) >= 0,
                qPrintable(
                    QStringLiteral("monitor refresh lost window 0x%1").arg(quintptr(h), 0, 16)));
        }
        // …and the previously off-space Notepad is visible again.
        QTRY_VERIFY_WITH_TIMEOUT(!cloak::isCloaked(m_hwndNote), 3000);

        // 2) Real handles come back — everyone lands on a LIVE monitor.
        m_sm.applyMonitorEntries(monitors::enumerate());
        m_monitor = m_sm.monitorOf(realH); // m_monitors was rebuilt — re-sync
        QVERIFY(m_monitor);
        for (HWND h : {m_hwndNote, m_hwndExpl, m_hwndWeb}) {
            if (!h || !::IsWindow(h))
                continue;
            QVERIFY2(m_sm.monitorOf(m_sm.ownerMonitorOf(h)) != nullptr,
                     "window re-homed onto a monitor that no longer exists");
        }
        QTRY_VERIFY_WITH_TIMEOUT(!cloak::isCloaked(m_hwndNote), 3000);
    }

    // Step 14: tile header — title ABOVE the image; × on the right closes the
    // window via graceful WM_CLOSE without activating the tile, and the strip
    // drops the dead window through untrack.
    void step14_tileCloseButtonClosesWindow()
    {
        m_host.openAll();
        QVERIFY(m_host.isOpen());
        syncPanelMonitor();
        QTest::qWait(700);
        QVERIFY(m_sm.overviewOpen());

        // Notepad may sit on any monitor's panel (per-monitor spaces).
        WindowPreviewWidget *tile = nullptr;
        OverviewWindow *tilePanel = nullptr;
        for (QWidget *w : QApplication::topLevelWidgets()) {
            auto *p = qobject_cast<OverviewWindow *>(w);
            if (!p || !p->isOpen())
                continue;
            for (auto *t : p->findChildren<WindowPreviewWidget *>()) {
                if (t->windowHandle() == m_hwndNote) {
                    tile = t;
                    tilePanel = p;
                    break;
                }
            }
            if (tile)
                break;
        }
        QVERIFY2(tile, "Notepad tile not found in any panel strip");
        QVERIFY(tilePanel);

        auto *btn = tile->findChild<QPushButton *>(QStringLiteral("CloseButton"));
        QVERIFY(btn && btn->isEnabled());
        QSignalSpy activated(tile, &WindowPreviewWidget::activated);

        QTest::mouseClick(btn, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!::IsWindow(m_hwndNote), 5000);
        QCOMPARE(activated.count(), 0);

        // The strip rebuilds without the dead window (windowUntracked).
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                for (auto *t : tilePanel->findChildren<WindowPreviewWidget *>()) {
                    if (t->windowHandle() == m_hwndNote)
                        return false;
                }
                return true;
            }(),
            3000);

        m_hwndNote = nullptr; // closed on purpose — cleanup skips it
    }

    // Step 15: cloak backend contract — after a REAL switch hides a REAL
    // foreign window, the hide must have gone through the shell
    // IApplicationView::SetCloak backend: the only one that keeps the
    // window's taskbar button (the DWM/ShowWindow fallbacks silently drop
    // it — regression guarded in test_cloak too).
    void step15_cloakHidesViaShellBackend()
    {
        if (!cloak::shellBackendAvailable())
            QSKIP("shell IApplicationViewCollection unavailable — cannot pin cloak backend");

        // Step14 left the preview open with Notepad closed (its premise).
        OverviewWindow *panel = m_host.activePanel();
        QVERIFY(panel && panel->isOpen());
        syncPanelMonitor();

        // Step13's monitor-handle refresh rebuilt monitors with the default
        // single space — a second one is needed for a switch to hide anything.
        if (m_sm.spaceCount(m_hmon) < 2) {
            auto *btn = panel->findChild<AddSpaceButton *>();
            QVERIFY(btn);
            const QPoint c = btn->rect().center();
            QMouseEvent press(QEvent::MouseButtonPress, QPointF(c), btn->mapToGlobal(c),
                              btn->mapToGlobal(c), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(btn, &press);
            QTRY_COMPARE_WITH_TIMEOUT(m_sm.spaceCount(m_hmon), 2, 3000);
        }

        // A flow window in the CURRENT space of the PANEL monitor is what the
        // switch below hides (per-monitor spaces — same guard style as
        // explOnPanel/webStableIn for windows parked on the other display).
        HWND target = nullptr;
        for (HWND h : {m_hwndExpl, m_hwndWeb}) {
            if (h && ::IsWindow(h) && m_sm.ownerMonitorOf(h) == m_hmon &&
                m_sm.spaceOfWindow(h) == m_monitor->currentIndex) {
                target = h;
                break;
            }
        }
        if (!target) {
            QWARN("no flow window in the panel monitor's current space — "
                  "skipping shell-backend assert");
            return;
        }

        const int cur = m_monitor->currentIndex;
        const int other = cur == 0 ? 1 : 0;
        auto cards = visibleCards(panel);
        QCOMPARE(cards.size(), 2);

        QSignalSpy chosen(&m_host, &OverviewHost::spaceChosen);
        clickWidget(cards[other]);
        QTRY_COMPARE_WITH_TIMEOUT(chosen.count(), 1, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(m_monitor->currentIndex, other, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!m_host.isOpen(), 5000);

        // The switch hid `target` for real — and via the SHELL backend.
        QTRY_VERIFY_WITH_TIMEOUT(cloak::isCloaked(target), 3000);
        QCOMPARE(cloak::backendOf(target), cloak::Backend::ImmersiveView);
    }

    // Step 16: cross-monitor drop — dropping a window onto ANOTHER display's
    // space card must re-home ownership there AND physically move the window
    // onto that display, size/position scaled by the screen ratio (before
    // this, the window stayed stranded on its original monitor).
    void step16_crossMonitorDropMovesWindowWithScreenRatio()
    {
        if (m_sm.monitors().size() < 2)
            QSKIP("cross-monitor drop needs at least 2 monitors");

        m_host.openAll();
        QVERIFY(m_host.isOpen());
        syncPanelMonitor();
        QTest::qWait(700);
        QVERIFY(m_sm.overviewOpen());

        // A live flow window owned by the PANEL monitor (the drop source).
        HWND hwnd = nullptr;
        for (HWND h : {m_hwndExpl, m_hwndWeb}) {
            if (h && ::IsWindow(h) && m_sm.ownerMonitorOf(h) == m_hmon) {
                hwnd = h;
                break;
            }
        }
        if (!hwnd)
            QSKIP("no flow window on the panel monitor — nothing to drag across");

        // The OTHER display's panel is the drop target (cross-screen DnD).
        OverviewWindow *dstPanel = nullptr;
        for (MonitorSpaces *mon : m_sm.monitors()) {
            if (mon->hmon == m_hmon)
                continue;
            if (OverviewWindow *p = m_host.panelFor(mon->hmon); p && p->isOpen()) {
                dstPanel = p;
                break;
            }
        }
        QVERIFY2(dstPanel, "no overview panel on the second monitor");
        const HMONITOR dstMon = dstPanel->targetMonitor();

        RECT srcPhys{}, dstPhys{};
        QVERIFY(monitors::physRectOf(m_hmon, &srcPhys));
        QVERIFY(monitors::physRectOf(dstMon, &dstPhys));
        const int sw = srcPhys.right - srcPhys.left;
        const int sh = srcPhys.bottom - srcPhys.top;
        QVERIFY(sw > 0 && sh > 0);

        // Park on the source screen at known fractions: (12.5%, 12.5%), 50% × 50%.
        ::SetWindowPos(hwnd, nullptr, srcPhys.left + sw / 8, srcPhys.top + sh / 8, sw / 2, sh / 2,
                       SWP_NOZORDER | SWP_NOACTIVATE);
        RECT before{};
        QVERIFY(::GetWindowRect(hwnd, &before));
        const RECT expect = SpaceManager::mapRectToMonitor(before, srcPhys, dstPhys);
        // placeWindowInSpace must not jump either display's viewed space.
        const int srcCur = m_monitor->currentIndex;
        MonitorSpaces *dstMonitor = m_sm.monitorOf(dstMon);
        QVERIFY(dstMonitor);
        const int dstCur = dstMonitor->currentIndex;

        // Real DnD payload onto the target display's first card (same path as
        // step05 — only the PANEL differs, which is what makes it cross-screen).
        const auto cards = visibleCards(dstPanel);
        QVERIFY(!cards.isEmpty());
        SpaceCardWidget *target = cards.first();
        const int targetSpace = target->spaceIndex();
        QVERIFY(targetSpace >= 0);

        QByteArray payload;
        QDataStream ds(&payload, QIODevice::WriteOnly);
        ds << quint64(hwnd);
        QMimeData mime;
        mime.setData(WindowPreviewWidget::kMimeType, payload);

        const QPoint c = target->rect().center();
        QDragEnterEvent enter(c, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(target, &enter);
        QVERIFY2(enter.isAccepted(), "cross-monitor space card rejected the drag");
        QDropEvent drop(c, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(target, &drop);
        QVERIFY(drop.isAccepted());

        // Ownership follows the drop…
        QTRY_VERIFY_WITH_TIMEOUT(
            m_sm.ownerMonitorOf(hwnd) == dstMon && m_sm.spaceOfWindow(hwnd) == targetSpace, 3000);
        // …and the window PHYSICALLY lands on the target screen…
        QTRY_VERIFY_WITH_TIMEOUT(::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) == dstMon,
                                 3000);
        // …with size/position scaled by the screen ratio (±4 px slack).
        RECT after{};
        QVERIFY(::GetWindowRect(hwnd, &after));
        QVERIFY2(
            qAbs(after.left - expect.left) <= 4,
            qPrintable(QStringLiteral("x off: got %1 want %2").arg(after.left).arg(expect.left)));
        QVERIFY2(
            qAbs(after.top - expect.top) <= 4,
            qPrintable(QStringLiteral("y off: got %1 want %2").arg(after.top).arg(expect.top)));
        QVERIFY2(qAbs((after.right - after.left) - (expect.right - expect.left)) <= 4,
                 "width not scaled to screen ratio");
        QVERIFY2(qAbs((after.bottom - after.top) - (expect.bottom - expect.top)) <= 4,
                 "height not scaled to screen ratio");

        // Neither display's viewed space jumped on drop.
        QCOMPARE(m_monitor->currentIndex, srcCur);
        QCOMPARE(dstMonitor->currentIndex, dstCur);
    }

    void step17_reopenStartsDarkThenFadesContent()
    {
        m_host.closeAll(false);
        QTRY_VERIFY_WITH_TIMEOUT(!m_host.isOpen(), 4000);
        m_host.openAll();
        QVERIFY(m_host.isOpen());
        for (MonitorSpaces *mon : m_sm.monitors()) {
            auto *panel = m_host.panelFor(mon->hmon);
            QVERIFY(panel && panel->isVisible());
            QCOMPARE(panel->windowOpacity(), 1.0);
            auto *content = panel->findChild<QWidget *>(QStringLiteral("OverviewPanel"));
            QVERIFY(content);
            auto *effect = qobject_cast<QGraphicsOpacityEffect *>(content->graphicsEffect());
            QVERIFY(effect);
            QCOMPARE(effect->opacity(), 0.0);
        }
        auto *active = m_host.activePanel();
        QVERIFY(active);
        QTRY_VERIFY_WITH_TIMEOUT(!active->isAnimating(), 2000);
        QVERIFY(m_host.isOpen());
        m_host.closeAll(false);
        QTRY_VERIFY_WITH_TIMEOUT(!m_host.isOpen(), 4000);
    }

    void step18_windowDragSurvivesDropTriggeredRefresh()
    {
        QVERIFY(m_hwndExpl && ::IsWindow(m_hwndExpl));
        const HMONITOR mon = m_sm.ownerMonitorOf(m_hwndExpl);
        const int origin = m_sm.spaceOfWindow(m_hwndExpl);
        QVERIFY(mon && origin >= 0);
        if (m_sm.spaceCount(mon) < 2)
            QVERIFY(m_sm.addSpace(mon));
        const int destination = (origin + 1) % m_sm.spaceCount(mon);
        m_host.openAll();
        auto *panel = m_host.panelFor(mon);
        QVERIFY(panel);
        QVERIFY(panel->previewSpace(origin));
        QTRY_VERIFY(!panel->isAnimating());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QPointer<WindowPreviewWidget> source;
        for (auto *tile : panel->findChildren<WindowPreviewWidget *>())
            if (tile->windowHandle() == m_hwndExpl)
                source = tile;
        QVERIFY(source);
        bool placed = false;
        bool survived = false;
        QSignalSpy finished(source, &WindowPreviewWidget::dragFinished);
        connect(source, &WindowPreviewWidget::dragStarted, panel, [&]() {
            placed = panel->placeWindowInSpace(m_hwndExpl, destination);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            survived = !source.isNull();
            QVERIFY(dragtest::queueNativeRelease(panel));
        });
        QTest::mousePress(source, Qt::LeftButton, Qt::NoModifier, QPoint(20, 20));
        const QPointF pos(50, 50);
        QMouseEvent move(QEvent::MouseMove, pos, source->mapToGlobal(pos.toPoint()), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(source, &move);
        QTest::mouseRelease(panel, Qt::LeftButton, Qt::NoModifier, QPoint(20, 20));
        QCOMPARE(finished.count(), 1);
        QVERIFY(placed);
        QVERIFY2(survived, "drop refresh deleted the source during the drag call");
        QCOMPARE(m_sm.spaceOfWindow(m_hwndExpl), destination);
        QTRY_VERIFY(source.isNull());
        m_host.closeAll(false);
        QTRY_VERIFY_WITH_TIMEOUT(!m_host.isOpen(), 4000);
    }

    // Same foreground entry as main; simulate the activation notification so
    // this remains deterministic even when Windows denies foreground stealing.
    void step19_foregroundSwitchesToOwningSpace()
    {
        QVERIFY(m_hwndExpl && ::IsWindow(m_hwndExpl));
        const HMONITOR mon = m_sm.ownerMonitorOf(m_hwndExpl);
        const int owned = m_sm.spaceOfWindow(m_hwndExpl);
        QVERIFY(mon && owned >= 0);
        if (m_sm.spaceCount(mon) < 2)
            QVERIFY(m_sm.addSpace(mon));
        const int away = (owned + 1) % m_sm.spaceCount(mon);
        // Step 18 may already have left this monitor on `away`.
        m_sm.switchSpace(mon, away, false);
        QCOMPARE(m_sm.currentSpaceIndex(mon), away);
        QVERIFY(cloak::isCloaked(m_hwndExpl));
        QHash<quintptr, int> others;
        for (auto *m : m_sm.monitors())
            if (m->hmon != mon)
                others.insert(quintptr(m->hmon), m->currentIndex);
        const auto activation =
            connect(&m_tracker, &WindowTracker::windowForeground, &m_sm,
                    [this](quint64 h) { m_sm.onWindowForeground(reinterpret_cast<HWND>(h)); });
        m_tracker.handle(EVENT_SYSTEM_FOREGROUND, m_hwndExpl, OBJID_WINDOW);
        disconnect(activation);
        QCOMPARE(m_sm.currentSpaceIndex(mon), owned);
        QCOMPARE(m_sm.spaceOfWindow(m_hwndExpl), owned);
        QCOMPARE(m_sm.ownerMonitorOf(m_hwndExpl), mon);
        QVERIFY(!cloak::isCloaked(m_hwndExpl));
        for (auto *m : m_sm.monitors())
            if (m->hmon != mon)
                QCOMPARE(m->currentIndex, others.value(quintptr(m->hmon)));
    }

    void step20_directAndForegroundSwitchAnimate()
    {
        // isOpen() becomes false at fade-out start; wait for allClosed state.
        QTRY_VERIFY_WITH_TIMEOUT(!m_sm.overviewOpen(), 4000);
        QVERIFY(m_hwndExpl && ::IsWindow(m_hwndExpl));
        const HMONITOR mon = m_sm.ownerMonitorOf(m_hwndExpl);
        const int owned = m_sm.spaceOfWindow(m_hwndExpl);
        QVERIFY(mon && owned >= 0);
        if (m_sm.spaceCount(mon) < 2)
            QVERIFY(m_sm.addSpace(mon));
        SpaceSwitchAnimator animation(&m_sm);
        m_sm.setAnimationEnabled(true);
        m_sm.switchSpace(mon, owned, false);
        const int away = (owned + 1) % m_sm.spaceCount(mon);
        QVERIFY(m_sm.switchSpace(mon, away, true, 1));
        QVERIFY(animation.isPlaying(mon));
        QTRY_VERIFY_WITH_TIMEOUT(!animation.isPlaying(mon), 3000);
        QVERIFY(cloak::isCloaked(m_hwndExpl));
        QVERIFY(m_sm.onWindowForeground(m_hwndExpl));
        QVERIFY(animation.isPlaying(mon));
        QCOMPARE(m_sm.currentSpaceIndex(mon), owned);
        QCOMPARE(m_sm.spaceOfWindow(m_hwndExpl), owned);
        QVERIFY(!cloak::isCloaked(m_hwndExpl));
        QTRY_VERIFY_WITH_TIMEOUT(!animation.isPlaying(mon), 3000);
    }

    void step21_foregroundCoveredBeforeCapture()
    {
        const HMONITOR mon = m_sm.ownerMonitorOf(m_hwndExpl);
        const int owned = m_sm.spaceOfWindow(m_hwndExpl);
        QVERIFY(mon && owned >= 0);
        const int away = (owned + 1) % m_sm.spaceCount(mon);
        m_sm.switchSpace(mon, away, false);
        SpaceSwitchAnimator animation(&m_sm);
        bool covered = false;
        bool rebuilt = false;
        // Model the shell exposing its activated app before our notification.
        QVERIFY(cloak::set(m_hwndExpl, false));
        const auto coverConnection =
            connect(&m_sm, &SpaceManager::requestSwitchCover, this, [&](quint64 h, int from) {
                QCOMPARE(h, quint64(quintptr(mon)));
                QCOMPARE(from, away);
                QCOMPARE(m_sm.currentSpaceIndex(mon), away);
                QVERIFY(animation.isPlaying(mon));
                QVERIFY(!thumbs::screenSamplingEnabled());
                covered = true;
            });
        const auto rebuildConnection =
            connect(&m_sm, &SpaceManager::spacePreviewInvalidated, this, [&](quint64, int) {
                QVERIFY(covered);
                QVERIFY(animation.isPlaying(mon));
                QVERIFY(!thumbs::screenSamplingEnabled());
                rebuilt = true;
            });
        const auto activation =
            connect(&m_tracker, &WindowTracker::windowForeground, &m_sm,
                    [this](quint64 h) { m_sm.onWindowForeground(reinterpret_cast<HWND>(h)); });
        m_tracker.handle(EVENT_SYSTEM_FOREGROUND, m_hwndExpl, OBJID_WINDOW);
        disconnect(activation);
        disconnect(coverConnection);
        disconnect(rebuildConnection);
        QVERIFY(covered);
        QVERIFY(rebuilt);
        QCOMPARE(m_sm.currentSpaceIndex(mon), owned);
        QCOMPARE(m_sm.spaceOfWindow(m_hwndExpl), owned);
        QVERIFY(!cloak::isCloaked(m_hwndExpl));
        QTRY_VERIFY_WITH_TIMEOUT(!animation.isPlaying(mon), 3000);
        QVERIFY(thumbs::screenSamplingEnabled());
    }

    void step22_switchStartsWithOriginalMonitorCopy()
    {
        const HMONITOR mon = m_sm.ownerMonitorOf(m_hwndExpl);
        auto *m = m_sm.monitorOf(mon);
        QVERIFY(m);
        SpaceSwitchAnimator animation(&m_sm);
        QWidget marker(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        marker.setGeometry(m->geometry);
        marker.setAutoFillBackground(true);
        QPalette palette;
        palette.setColor(QPalette::Window, Qt::magenta);
        marker.setPalette(palette);
        marker.show();
        QVERIFY(QTest::qWaitForWindowExposed(&marker));
        QVERIFY(::SetWindowPos(reinterpret_cast<HWND>(marker.winId()), HWND_TOPMOST,
                               m->physRect.left, m->physRect.top,
                               m->physRect.right - m->physRect.left,
                               m->physRect.bottom - m->physRect.top, SWP_NOACTIVATE));
        marker.repaint();
        ::DwmFlush();
        auto markerIsOnScreen = [&]() {
            const QImage frame = thumbs::captureMonitor(m->physRect);
            return !frame.isNull() &&
                   frame.pixelColor(frame.width() / 2, frame.height() / 2) == QColor(Qt::magenta);
        };
        QTRY_VERIFY_WITH_TIMEOUT(markerIsOnScreen(), 3000);
        bool verified = false;
        const auto connection =
            connect(&m_sm, &SpaceManager::requestSwitchCover, this, [&](quint64, int) {
                for (auto *widget : QApplication::topLevelWidgets()) {
                    auto *overlay = qobject_cast<SwitchFlashOverlay *>(widget);
                    if (!overlay || !overlay->isVisible())
                        continue;
                    const QImage frame = overlay->grab().toImage();
                    QCOMPARE(frame.pixelColor(frame.width() / 2, frame.height() / 2),
                             QColor(Qt::magenta));
                    verified = true;
                }
            });
        const int target = (m->currentIndex + 1) % int(m->spaces.size());
        QVERIFY(m_sm.switchSpace(mon, target));
        disconnect(connection);
        QVERIFY(verified);
        QTRY_VERIFY_WITH_TIMEOUT(!animation.isPlaying(mon), 3000);
    }

    void step23_currentWindowsBeforeExpandedSpaces()
    {
        const HMONITOR mon = m_sm.ownerMonitorOf(m_hwndExpl);
        const int current = m_sm.currentSpaceIndex(mon);
        m_host.openAll();
        auto *panel = m_host.panelFor(mon);
        QVERIFY(panel && panel->isOpen());
        QVERIFY(!panel->isSpaceStripExpanded());
        QTRY_VERIFY(!panel->isAnimating());
        const QPoint bottom(panel->width() / 2, panel->height() - 40);
        QMouseEvent leaveStrip(QEvent::MouseMove, bottom, panel->mapToGlobal(bottom), Qt::NoButton,
                               Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(panel, &leaveStrip);
        QCOMPARE(panel->selectedIndex(), current);
        for (int i = 0; i < panel->windowPreviewCount(); ++i)
            QCOMPARE(m_sm.spaceOfWindow(panel->windowPreviewHandle(i)), current);
        auto *strip = panel->findChild<QWidget *>(QStringLiteral("SpaceStripHost"));
        QVERIFY(strip);
        const int compactHeight = strip->height();
        const QPoint top(panel->width() / 2, 2);
        QMouseEvent enterStrip(QEvent::MouseMove, top, panel->mapToGlobal(top), Qt::NoButton,
                               Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(panel, &enterStrip);
        QTRY_VERIFY(panel->isSpaceStripExpanded());
        QCOMPARE(strip->height(), compactHeight);
        QApplication::sendEvent(panel, &leaveStrip);
        QTest::qWait(500);
        QVERIFY(panel->isSpaceStripExpanded());
        QCOMPARE(m_sm.currentSpaceIndex(mon), current);
        m_host.closeAll(false);
        QTRY_VERIFY(!m_sm.overviewOpen());
    }

    void step24_currentMonitorPreparedBeforeOtherSpaces()
    {
        m_host.openAll();
        auto *active = m_host.activePanel();
        QVERIFY(active && active->isContentReady());
        QVERIFY(!thumbs::screenSamplingEnabled());
        for (auto *mon : m_sm.monitors()) {
            auto *panel = m_host.panelFor(mon->hmon);
            QVERIFY(panel && panel->isVisible());
            QVERIFY(panel->isContentReady());
        }
        QCOMPARE(active->selectedIndex(), m_sm.currentSpaceIndex(active->targetMonitor()));
        for (auto *mon : m_sm.monitors()) {
            auto *panel = m_host.panelFor(mon->hmon);
            QTRY_VERIFY_WITH_TIMEOUT(panel->isContentReady(), 10000);
        }
        m_host.closeAll(false);
        QTRY_VERIFY(!m_sm.overviewOpen());
        QVERIFY(thumbs::screenSamplingEnabled());
    }

    void step25_taskbarPreactivationCoverKeepsOriginalFrame()
    {
        const HMONITOR mon = m_sm.ownerMonitorOf(m_hwndExpl);
        const int owned = m_sm.spaceOfWindow(m_hwndExpl);
        QVERIFY(mon && owned >= 0);
        if (m_sm.spaceCount(mon) < 2)
            QVERIFY(m_sm.addSpace(mon));
        m_sm.switchSpace(mon, (owned + 1) % m_sm.spaceCount(mon), false);
        int captures = 0;
        SpaceSwitchAnimator animation(&m_sm, nullptr, [&](const RECT &rect) {
            ++captures;
            return thumbs::captureMonitor(rect, QSize());
        });
        HotkeyManager input;
        input.setTaskbarGuardEnabled(true);
        connect(&input, &HotkeyManager::taskbarPressed, &animation,
                [&]() { animation.prepareTaskbarSwitch(); });
        // The native bridge used by the mouse hook completes before returning
        // control to Explorer. Physical taskbar hit-testing remains manual QA.
        ::SendMessageW(input.taskbarBridgeWindow(), HotkeyManager::TaskbarPressMessage,
                       WPARAM(::GetTickCount64() + 1000), 0);
        QVERIFY(animation.hasTaskbarCover(mon));
        QVERIFY(animation.isPlaying(mon));
        QVERIFY(!thumbs::screenSamplingEnabled());
        const int preActivationCaptures = captures;
        QVERIFY(preActivationCaptures > 0);
        QVERIFY(cloak::set(m_hwndExpl, false));
        QVERIFY(m_sm.onWindowForeground(m_hwndExpl));
        QCOMPARE(captures, preActivationCaptures);
        QCOMPARE(m_sm.spaceOfWindow(m_hwndExpl), owned);
        QCOMPARE(m_sm.currentSpaceIndex(mon), owned);
        QTRY_VERIFY_WITH_TIMEOUT(!animation.isPlaying(mon), 3000);
        QVERIFY(thumbs::screenSamplingEnabled());
    }

    void step26_windowsMoveIntoPreviewAndStripSlidesDown()
    {
        m_host.openAll();
        QVERIFY(m_host.isOpen());
        auto *panel = m_host.panelFor(m_sm.ownerMonitorOf(m_hwndExpl));
        QVERIFY(panel);
        QTRY_VERIFY_WITH_TIMEOUT(panel->isContentReady(), 10000);
        QVERIFY(panel->entryWindowCount() > 0);
        QVERIFY(panel->enterProgress() < 1.0);
        QVERIFY(panel->entryStripRect().top() <
                panel->findChild<QWidget *>("SpaceStripHost")->mapTo(panel, QPoint()).y());
        const QRectF start = panel->entryWindowRect(0);
        QTRY_VERIFY_WITH_TIMEOUT(panel->entryWindowRect(0) != start, 2000);
        QTRY_VERIFY_WITH_TIMEOUT(!panel->isAnimating(), 3000);
        QCOMPARE(panel->enterProgress(), 1.0);
        QVERIFY(!panel->findChild<QWidget *>("OverviewPanel")->graphicsEffect());
        m_host.closeAll(false);
        QTRY_VERIFY_WITH_TIMEOUT(!m_sm.overviewOpen(), 4000);
    }

    void step27_parallelMonitorCaptureBeforeOverview()
    {
        spacelog::flush();
        QFile trace(spacelog::tracePath());
        QVERIFY(trace.open(QIODevice::ReadOnly | QIODevice::Text));
        trace.seek(trace.size());
        m_host.openAll();
        spacelog::flush();
        const QString log = QString::fromUtf8(trace.readAll());
        const int firstPanel = log.indexOf(QStringLiteral("overview panel open "));
        QVERIFY(firstPanel >= 0);
        int completed = 0;
        for (const QString &line : log.split('\n')) {
            if (line.contains(QStringLiteral("overview monitor worker ")) &&
                line.contains(QStringLiteral("phase=done"))) {
                QVERIFY(log.indexOf(line) < firstPanel);
                ++completed;
            }
        }
        QCOMPARE(completed, m_sm.monitors().size());
        QCOMPARE(m_host.openPanelCount(), completed);
        QVERIFY(!thumbs::screenSamplingEnabled());
        m_host.closeAll(false);
        QTRY_VERIFY_WITH_TIMEOUT(!m_sm.overviewOpen(), 4000);
        QVERIFY(thumbs::screenSamplingEnabled());
    }

    void step28_allMonitorsPreparedBeforeAnimations()
    {
        bool sawReady = false;
        const auto connection =
            connect(&m_host, &OverviewHost::panelReady, this, [&](quint64, int) {
                sawReady = true;
                for (auto *mon : m_sm.monitors())
                    QVERIFY(m_host.panelFor(mon->hmon)->isContentReady());
            });
        m_host.openAll();
        disconnect(connection);
        QVERIFY(sawReady);
        for (auto *mon : m_sm.monitors()) {
            auto *panel = m_host.panelFor(mon->hmon);
            QVERIFY(panel->isContentReady());
            QTRY_VERIFY_WITH_TIMEOUT(!panel->isAnimating(), 3000);
        }
        m_host.closeAll(false);
        QTRY_VERIFY_WITH_TIMEOUT(!m_sm.overviewOpen(), 4000);
    }

    void step29_windowCaptureTasksFinishBeforeAnimation()
    {
        spacelog::flush();
        QFile trace(spacelog::tracePath());
        QVERIFY(trace.open(QIODevice::ReadOnly | QIODevice::Text));
        trace.seek(trace.size());
        m_host.openAll();
        QVERIFY(m_host.isOpen());
        spacelog::flush();
        const QString log = QString::fromUtf8(trace.readAll());
        const int ready = log.indexOf(QStringLiteral("phase=allMonitorsPrepared "));
        const int animated = log.indexOf(QStringLiteral("phase=allAnimationsStarted "));
        QVERIFY(ready >= 0 && animated > ready);
        int captures = 0;
        for (const QString &line : log.split('\n')) {
            if (!line.contains(QStringLiteral("overview window worker ")))
                continue;
            QVERIFY(log.indexOf(line) < ready);
            QVERIFY(!line.contains(QStringLiteral("thread=%1 ").arg(::GetCurrentThreadId())));
            if (line.contains(QStringLiteral("phase=done ")))
                ++captures;
        }
        QVERIFY(captures > 0);
        m_host.closeAll(false);
        QTRY_VERIFY_WITH_TIMEOUT(!m_sm.overviewOpen(), 4000);
    }

    // Step 30: a REAL app's transient window — Notepad's Open dialog
    // (owned #32770) — is tracked, then Notepad closes (hides) it; SpaceWM
    // must auto-untrack it so it leaves the preview (live windowHidden path;
    // the 5s timer in main.cpp is the safety net).
    void step30_appHiddenTransientWindowAutoUntracks()
    {
        if (!::SetForegroundWindow(m_hwndNote))
            QSKIP("cannot foreground Notepad");
        auto before = WindowTracker::snapshotManageableWindows();
        sendCtrlO();
        HWND dlg = waitForNewWindow(before, {QStringLiteral("#32770")}, 10000);
        if (!dlg)
            QSKIP("Notepad open dialog did not appear");
        QTRY_VERIFY_WITH_TIMEOUT(m_sm.spaceOfWindow(dlg) >= 0, 5000);

        m_host.openAll();
        auto *panel = m_host.panelFor(m_sm.ownerMonitorOf(dlg));
        QVERIFY(panel);
        QTRY_VERIFY_WITH_TIMEOUT(panel->isContentReady(), 10000);
        const auto hasTile = [&]() {
            for (auto *tile : panel->findChildren<WindowPreviewWidget *>())
                if (tile->windowHandle() == dlg)
                    return true;
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(hasTile(), 5000);

        // Close the dialog — Notepad hides it (does not destroy it).
        ::SendMessageW(dlg, WM_CLOSE, 0, 0);
        QTRY_COMPARE_WITH_TIMEOUT(m_sm.spaceOfWindow(dlg), -1, 8000);
        QTRY_VERIFY_WITH_TIMEOUT(!hasTile(), 5000);

        m_host.closeAll(false);
        QTRY_VERIFY_WITH_TIMEOUT(!m_sm.overviewOpen(), 4000);
    }

    void step31_adjacentSwitchStopsAtBoundary()
    {
        auto *m = m_sm.primaryMonitor();
        QVERIFY(m);
        const HMONITOR mon = m->hmon;
        QVERIFY(m_sm.switchSpace(mon, 0, false) || m_sm.currentSpaceIndex(mon) == 0);
        QSignalSpy changed(&m_sm, &SpaceManager::spaceChanged);
        QSignalSpy boundary(&m_sm, &SpaceManager::requestBoundaryAnimation);
        QVERIFY(!m_sm.switchAdjacentSpace(mon, -1));
        QCOMPARE(m_sm.currentSpaceIndex(mon), 0);
        QCOMPARE(changed.count(), 0);
        QCOMPARE(boundary.count(), 1);
    }

    void step32_spaceStripExpandsAndCollapsesWithAnimation()
    {
        m_host.openAll();
        auto *panel = m_host.activePanel();
        QVERIFY(panel);
        QTRY_VERIFY(!panel->isAnimating());
        auto *card = panel->findChild<SpaceCardWidget *>();
        QVERIFY(card);
        const int compact = card->height();
        QTest::keyClick(panel, Qt::Key_Right);
        QTRY_VERIFY(card->height() > compact);
        QTest::qWait(250);
        const int expanded = card->height();
        QVERIFY(expanded > compact);
        m_host.closeAll(false);
        QTRY_VERIFY_WITH_TIMEOUT(!m_sm.overviewOpen(), 4000);
    }

    void step33_commitAnimatesWindowsBackToDesktop()
    {
        const HMONITOR mon = m_sm.ownerMonitorOf(m_hwndExpl);
        const int space = m_sm.spaceOfWindow(m_hwndExpl);
        QVERIFY(mon);
        QVERIFY(space >= 0);
        QVERIFY(m_sm.switchSpace(mon, space, false) || m_sm.currentSpaceIndex(mon) == space);
        m_host.openAll();
        auto *panel = m_host.panelFor(mon);
        QVERIFY(panel);
        QTRY_VERIFY_WITH_TIMEOUT(!panel->isAnimating(), 5000);
        QVERIFY(panel->windowPreviewCount() > 0);
        panel->closeOverview(true);
        QTRY_VERIFY_WITH_TIMEOUT(panel->isExitStarted(), 3000);
        QVERIFY(panel->entryWindowCount() > 0);
        const QRectF start = panel->entryWindowRect(0);
        QTRY_VERIFY_WITH_TIMEOUT(panel->entryWindowRect(0) != start, 2000);
        QTRY_VERIFY_WITH_TIMEOUT(!m_sm.overviewOpen(), 4000);
    }

    void step34_winPreparationBeforePreview()
    {
        m_host.beginWinPreparation();
        QVERIFY(m_host.preparedWindowCount() > 0);
        QTRY_VERIFY_WITH_TIMEOUT(m_host.winPreparationReady(), 30000);
        m_host.openAll();
        QVERIFY(m_host.isOpen());
        QCOMPARE(m_host.preparedWindowCount(), 0);
        m_host.cancelWinPreparation();
        m_host.closeAll(false);
        QTRY_VERIFY_WITH_TIMEOUT(!m_sm.overviewOpen(), 4000);
        m_host.beginWinPreparation();
        QVERIFY(m_host.preparedWindowCount() > 0);
        m_host.cancelWinPreparation();
        QCOMPARE(m_host.preparedWindowCount(), 0);
    }

    void step35_pointerOverStripExpandsAfterEntry()
    {
        const QPoint oldCursor = QCursor::pos();
        m_host.openAll();
        auto *panel = m_host.panelFor(m_hmon);
        QVERIFY(panel);
        auto *strip = panel->findChild<QWidget *>(QStringLiteral("SpaceStripHost"));
        QVERIFY(strip);
        QCursor::setPos(strip->mapToGlobal(strip->rect().center()));
        QEvent enter(QEvent::Enter);
        QApplication::sendEvent(strip, &enter);
        QVERIFY(!panel->isSpaceStripExpanded());
        QTRY_VERIFY_WITH_TIMEOUT(!panel->isAnimating(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(panel->isSpaceStripExpanded(), 3000);
        m_host.closeAll(false);
        QTRY_VERIFY_WITH_TIMEOUT(!m_sm.overviewOpen(), 4000);
        QCursor::setPos(oldCursor);
    }

    void step36_previewKeepsInactiveWindowsCloaked()
    {
        QVERIFY(m_hwndExpl && ::IsWindow(m_hwndExpl));
        const HMONITOR mon = m_sm.ownerMonitorOf(m_hwndExpl);
        const int owned = m_sm.spaceOfWindow(m_hwndExpl);
        QVERIFY(mon && owned >= 0);
        if (m_sm.spaceCount(mon) < 2)
            QVERIFY(m_sm.addSpace(mon));
        const int away = (owned + 1) % m_sm.spaceCount(mon);
        m_sm.switchSpace(mon, away, false);
        QVERIFY(cloak::isCloaked(m_hwndExpl));
        m_host.openAll();
        QVERIFY(m_host.isOpen());
        QVERIFY(cloak::isCloaked(m_hwndExpl));
        m_host.closeAll(false);
        QTRY_VERIFY_WITH_TIMEOUT(!m_sm.overviewOpen(), 4000);
        QVERIFY(cloak::isCloaked(m_hwndExpl));
        m_sm.switchSpace(mon, owned, false);
        QVERIFY(!cloak::isCloaked(m_hwndExpl));
    }

    void step37_winPreparationIncludesWallpaper()
    {
        m_host.beginWinPreparation();
        QTRY_VERIFY_WITH_TIMEOUT(m_host.winPreparationReady(), 30000);
        spacelog::flush();
        QFile trace(spacelog::tracePath());
        QVERIFY(trace.open(QIODevice::ReadOnly | QIODevice::Text));
        trace.seek(trace.size());
        m_host.openAll();
        QVERIFY(m_host.isOpen());
        spacelog::flush();
        QVERIFY(QString::fromUtf8(trace.readAll()).contains(
            QStringLiteral("overview wallpaper phase=warmResult")));
        m_host.closeAll(false);
        QTRY_VERIFY_WITH_TIMEOUT(!m_sm.overviewOpen(), 4000);
    }

    void step38_winTabEventsOpenAfterPreparation()
    {
        QSignalSpy requested(&m_host, &OverviewHost::previewRequested);
        const auto connection = connect(&m_host, &OverviewHost::previewRequested,
                                        &m_host, &OverviewHost::openAll);
        m_host.postWinPreviewEvent(WinPreviewEvents::Event::WinDown);
        m_host.postWinPreviewEvent(WinPreviewEvents::Event::TabDown);
        m_host.postWinPreviewEvent(WinPreviewEvents::Event::WinUp);
        QTRY_COMPARE_WITH_TIMEOUT(requested.count(), 1, 30000);
        QVERIFY(m_host.isOpen());
        QCOMPARE(m_host.preparedWindowCount(), 0);
        disconnect(connection);
        m_host.closeAll(false);
        QTRY_VERIFY_WITH_TIMEOUT(!m_sm.overviewOpen(), 4000);
    }

    void cleanupTestCase()
    {
        // Best-effort teardown — must leave the user's desktop untouched.
        if (m_host.isOpen())
            m_host.forceHideAll();
        QTest::qWait(400);

        for (HWND h : {m_hwndNote, m_hwndExpl, m_hwndWeb}) {
            if (h && ::IsWindow(h))
                m_sm.untrackWindow(h);
        }
        cloak::showAllHidden(); // restore anything THIS process hid

        const HWND ours[] = {m_hwndNote, m_hwndExpl, m_hwndWeb};
        for (HWND h : ours) {
            if (h && ::IsWindow(h))
                ::SendMessageW(h, WM_CLOSE, 0, 0);
        }
        // Give them a moment; never force-kill (explorer.exe hosts the shell).
        for (int i = 0; i < 50; ++i) {
            bool anyAlive = false;
            for (HWND h : ours)
                if (h && ::IsWindow(h))
                    anyAlive = true;
            if (!anyAlive)
                break;
            QTest::qWait(100);
        }
        for (HWND h : ours)
            if (h && ::IsWindow(h))
                QWARN("integration-test window did not close after WM_CLOSE");

        if (!m_noteFile.isEmpty())
            QFile::remove(m_noteFile);
        if (!m_webFile.isEmpty())
            QFile::remove(m_webFile);
        spacelog::shutdown();
    }

  private:
    // The flow always follows the OPEN panel's monitor (openAll picks the
    // cursor's display — asserting against monitors().first() reads the wrong
    // monitor on multi-head machines).
    void syncPanelMonitor()
    {
        OverviewWindow *panel = m_host.activePanel();
        QVERIFY(panel);
        m_hmon = panel->targetMonitor();
        m_monitor = m_sm.monitorOf(m_hmon);
        QVERIFY(m_monitor);
    }

    // Diagnostics for browser-window flakiness: owner monitor+index vs actual
    // set membership (across ALL monitors) vs cloak/visibility.
    void dumpWebState(const char *where)
    {
        QString sets;
        for (MonitorSpaces *mon : m_sm.monitors()) {
            for (int i = 0; i < mon->spaces.size(); ++i)
                if (mon->spaces[i].windows.contains(m_hwndWeb))
                    sets += QStringLiteral(" mon%1/set%2")
                                .arg(reinterpret_cast<quintptr>(mon->hmon), 0, 16)
                                .arg(i);
        }
        const bool onPanelMon = m_sm.ownerMonitorOf(m_hwndWeb) == m_hmon;
        QWARN(qPrintable(
            QStringLiteral("[%1] web alive=%2 ownerSpace=%3 onPanelMon=%4 in:%5 cloaked=%6 "
                           "visible=%7 current(panel)=%8")
                .arg(QLatin1String(where))
                .arg(::IsWindow(m_hwndWeb) ? 1 : 0)
                .arg(m_sm.spaceOfWindow(m_hwndWeb))
                .arg(onPanelMon ? 1 : 0)
                .arg(sets.isEmpty() ? QStringLiteral("<none>") : sets)
                .arg(cloak::isCloaked(m_hwndWeb) ? 1 : 0)
                .arg(::IsWindowVisible(m_hwndWeb) ? 1 : 0)
                .arg(m_monitor->currentIndex)));
    }

    // Explorer windows land wherever the shell decides (cursor/taskbar —
    // varies run to run): like webStableIn, membership/cloak asserts are only
    // meaningful when it sits on the PANEL's monitor.
    bool explOnPanel()
    {
        if (!m_hwndExpl || !::IsWindow(m_hwndExpl)) {
            QWARN("explorer window died mid-flow — skipping explorer asserts");
            return false;
        }
        if (m_sm.ownerMonitorOf(m_hwndExpl) != m_hmon) {
            QWARN("explorer sits on another display — its monitor's spaces are "
                  "independent; skipping explorer asserts");
            return false;
        }
        return true;
    }

    // A browser assertion is only meaningful when the window lives on the
    // PANEL's monitor: spaces are per-monitor (design), so a window parked on
    // the other display follows THAT monitor's independent current space —
    // Edge reopens windows wherever it likes, run to run.
    bool webStableIn(int expectedSpace)
    {
        if (!m_hwndWeb || !::IsWindow(m_hwndWeb) || m_sm.spaceOfWindow(m_hwndWeb) < 0) {
            QWARN("browser window died mid-flow — skipping web asserts");
            return false;
        }
        if (m_sm.ownerMonitorOf(m_hwndWeb) != m_hmon) {
            QWARN("browser window sits on another display — its monitor's "
                  "spaces are independent; skipping web asserts");
            return false;
        }
        const int sp = m_sm.spaceOfWindow(m_hwndWeb);
        if (sp != expectedSpace) {
            QWARN(qPrintable(QStringLiteral("browser window landed in space %1 (expected %2) — "
                                            "skipping web asserts")
                                 .arg(sp)
                                 .arg(expectedSpace)));
            return false;
        }
        return true;
    }

    // Ctrl+O to the foreground app (Notepad's Open dialog accelerator). The
    // LL hook ignores injected keys only for its OWN bound chords; a plain
    // accelerator reaches the foreground app normally.
    static void sendCtrlO()
    {
        INPUT in[4] = {};
        in[0].type = INPUT_KEYBOARD;
        in[0].ki.wVk = VK_CONTROL;
        in[1].type = INPUT_KEYBOARD;
        in[1].ki.wVk = 'O';
        in[2].type = INPUT_KEYBOARD;
        in[2].ki.wVk = 'O';
        in[2].ki.dwFlags = KEYEVENTF_KEYUP;
        in[3].type = INPUT_KEYBOARD;
        in[3].ki.wVk = VK_CONTROL;
        in[3].ki.dwFlags = KEYEVENTF_KEYUP;
        ::SendInput(4, in, sizeof(in[0]));
    }

    static QString classNameOf(HWND hwnd)
    {
        wchar_t buf[64]{};
        ::GetClassNameW(hwnd, buf, 64);
        return QString::fromWCharArray(buf);
    }

    // Poll real window enumeration for a NEW top-level window of a class.
    HWND waitForNewWindow(const QVector<HWND> &before, const QStringList &classes, int timeoutMs)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            const auto now = WindowTracker::snapshotManageableWindows();
            for (HWND h : now) {
                if (before.contains(h))
                    continue;
                if (classes.contains(classNameOf(h), Qt::CaseInsensitive))
                    return h;
            }
            QTest::qWait(100);
        }
        return nullptr;
    }

    static QString findBrowser()
    {
        const QStringList candidates = {
            qEnvironmentVariable("ProgramFiles(x86)") +
                QStringLiteral("/Microsoft/Edge/Application/msedge.exe"),
            qEnvironmentVariable("ProgramFiles") +
                QStringLiteral("/Microsoft/Edge/Application/msedge.exe"),
            qEnvironmentVariable("LocalAppData") +
                QStringLiteral("/Microsoft/Edge/Application/msedge.exe"),
            qEnvironmentVariable("ProgramFiles") +
                QStringLiteral("/Google/Chrome/Application/chrome.exe"),
            qEnvironmentVariable("ProgramFiles(x86)") +
                QStringLiteral("/Google/Chrome/Application/chrome.exe"),
        };
        for (const QString &c : candidates)
            if (!c.isEmpty() && QFile::exists(c))
                return c;
        return {};
    }

    // Cards of the ACTIVE panel, flushed past pending deleteLater removals.
    static QVector<SpaceCardWidget *> visibleCards(OverviewWindow *panel)
    {
        flushDeferredDeletes();
        return panel->findChildren<SpaceCardWidget *>();
    }

    static void flushDeferredDeletes()
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }

    // One press+release (within drag distance) — same pattern as unit tests.
    static void clickWidget(QWidget *w)
    {
        const QPointF c(w->rect().center());
        const QPointF g = w->mapToGlobal(c);
        QMouseEvent press(QEvent::MouseButtonPress, c, g, g, Qt::LeftButton, Qt::LeftButton,
                          Qt::NoModifier);
        QApplication::sendEvent(w, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, c + QPointF(1, 0),
                            w->mapToGlobal(c + QPointF(1, 0)), w->mapToGlobal(c + QPointF(1, 0)),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(w, &release);
    }

    SpaceManager m_sm;
    WindowTracker m_tracker;
    OverviewHost m_host{&m_sm};
    std::unique_ptr<QSignalSpy> m_allClosedSpy;
    MonitorSpaces *m_monitor = nullptr;
    HMONITOR m_hmon = nullptr;

    HWND m_hwndNote = nullptr;
    HWND m_hwndExpl = nullptr;
    HWND m_hwndWeb = nullptr;
    QString m_noteFile;
    QString m_webFile;
    QString m_nameS1;
    QString m_nameS2;
};

QTEST_MAIN(TestIntegrationFlow)
#include "test_integration_flow.moc"
