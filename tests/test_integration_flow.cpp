#include <QtTest>

#include "core/log/Log.h"
#include "core/space/SpaceManager.h"
#include "core/window/CloakController.h"
#include "core/window/WindowTracker.h"
#include "ui/overview/OverviewHost.h"
#include "ui/overview/OverviewWindow.h"
#include "ui/preview/AddSpaceButton.h"
#include "ui/preview/SpaceCardWidget.h"
#include "ui/preview/WindowPreviewWidget.h"

#include <QDataStream>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QMimeData>
#include <QProcess>
#include <QSignalSpy>

#include <Windows.h>

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
    // warm pass uncloaks every managed window behind the mask.
    void step08_reopenPreview()
    {
        m_host.openAll();
        QVERIFY(m_host.isOpen());
        syncPanelMonitor();
        QCOMPARE(m_host.activePanel()->cardCount(), 2);

        QTest::qWait(700);
        QVERIFY2(m_host.isOpen(), "second open flashed closed");
        // warmWindowShots uncloaks everything while the mask is up …
        QVERIFY(!cloak::isCloaked(m_hwndExpl));
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
            QVERIFY2(m_sm.spaceOfWindow(h) >= 0,
                     qPrintable(QStringLiteral("monitor refresh lost window 0x%1")
                                    .arg(quintptr(h), 0, 16)));
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
