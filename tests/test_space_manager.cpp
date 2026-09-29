#include <QtTest>

#include "core/capture/ThumbnailCapture.h"
#include "core/window/CloakController.h"
#include "core/space/SpaceManager.h"
#include "core/window/WindowTracker.h"

#include <Windows.h>

// End-to-end space model against the real primary monitor + a real test window.
class TestSpaceManager : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase()
    {
        m_hwnd =
            ::CreateWindowExW(0, L"STATIC", L"SpaceWM space test", WS_OVERLAPPEDWINDOW, 10, 10, 300,
                              200, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
        QVERIFY(m_hwnd != nullptr);
        ::ShowWindow(m_hwnd, SW_SHOWNORMAL);
        ::UpdateWindow(m_hwnd);
    }

    void cleanupTestCase()
    {
        if (m_hwnd) {
            ::cloak::set(m_hwnd, false);
            ::DestroyWindow(m_hwnd);
            m_hwnd = nullptr;
        }
    }

    void hasMonitorsAndOneSpace()
    {
        SpaceManager sm;
        const auto mons = sm.monitors();
        QVERIFY(!mons.isEmpty());
        auto *m = mons.first();
        // Cold start: one space per monitor; nothing is loaded from disk.
        QCOMPARE(m->spaces.size(), 1);
        QCOMPARE(m->currentIndex, 0);
        QVERIFY(!m->spaces[0].name.isEmpty());
    }

    void switchSpaceChangesIndexAndEmits()
    {
        SpaceManager sm;
        auto *m = sm.monitors().first();
        ensureSpaces(sm, m, 2);
        m->currentIndex = 0;

        QSignalSpy spy(&sm, &SpaceManager::spaceChanged);
        QVERIFY(sm.switchSpace(m->hmon, 1, /*animateHint=*/false));
        QCOMPARE(m->currentIndex, 1);
        QCOMPARE(spy.count(), 1);

        // Same index is a no-op.
        QVERIFY(!sm.switchSpace(m->hmon, 1, false));
        QCOMPARE(spy.count(), 1);

        // Out of range is a no-op.
        QVERIFY(!sm.switchSpace(m->hmon, 99, false));
        QVERIFY(!sm.switchSpace(m->hmon, -1, false));
    }

    void wraparoundHelpersInAppUseModulo()
    {
        // Document expected wrap used by hotkeys: (i + n) % n
        const int n = 4;
        QCOMPARE((0 + n) % n, 0);
        QCOMPARE((3 + 1) % n, 0);
        QCOMPARE((0 - 1 + n) % n, 3);
    }

    void previewSpaceSyncsWhileOverviewOpen()
    {
        SpaceManager sm;
        auto *m = sm.monitors().first();
        ensureSpaces(sm, m, 3);
        m->currentIndex = 0;
        sm.setOverviewOpen(true);

        QVERIFY(sm.previewSpace(m->hmon, 2));
        QCOMPARE(m->currentIndex, 2);

        // Same index still succeeds (refresh path after drop).
        QVERIFY(sm.previewSpace(m->hmon, 2));
        QCOMPARE(m->currentIndex, 2);

        // Invalid indices
        QVERIFY(!sm.previewSpace(m->hmon, -1));
        QVERIFY(!sm.previewSpace(m->hmon, 99));
        QVERIFY(!sm.previewSpace(nullptr, 0));

        // rebuild screenshot under overview must not crash / must produce image
        sm.rebuildSpaceScreenshot(m->hmon, 2);
        QVERIFY(!m->spaces[2].screenshot.isNull());

        sm.setOverviewOpen(false);
        sm.previewSpace(m->hmon, 0);
        QCOMPARE(m->currentIndex, 0);
    }

    void renderedPreviewMatchesMonitorAspect()
    {
        SpaceManager sm;
        auto *m = sm.monitors().first();
        ensureSpaces(sm, m, 2);
        const int monW = m->physRect.right - m->physRect.left;
        const int monH = m->physRect.bottom - m->physRect.top;
        QVERIFY(monW > 0 && monH > 0);

        sm.setOverviewOpen(true);
        // captureSpaceScreenshot is render-only (no BitBlt / overview-safe).
        sm.captureSpaceScreenshot(m->hmon, 1);
        const QImage &img = m->spaces[1].screenshot;
        QVERIFY(!img.isNull());
        QVERIFY(img.width() <= 640);
        QVERIFY(img.height() <= 360);

        const double monAspect = double(monW) / double(monH);
        const double imgAspect = double(img.width()) / double(img.height());
        QVERIFY2(qAbs(monAspect - imgAspect) < 0.05,
                 qPrintable(QStringLiteral("mon=%1 img=%2 (%3x%4)")
                                .arg(monAspect, 0, 'f', 3)
                                .arg(imgAspect, 0, 'f', 3)
                                .arg(img.width())
                                .arg(img.height())));

        // Seed fills empties via render as well.
        for (Space &sp : m->spaces)
            sp.screenshot = QImage();
        sm.seedScreenshots();
        for (int i = 0; i < m->spaces.size(); ++i)
            QVERIFY2(!m->spaces[i].screenshot.isNull(),
                     qPrintable(QStringLiteral("space %1 not seeded").arg(i)));

        sm.setOverviewOpen(false);
    }

    void renderedPreviewUsesWindowZOrder()
    {
        SpaceManager sm;
        auto *m = sm.monitors().first();
        ensureSpaces(sm, m, 4);

        HWND back =
            ::CreateWindowExW(0, L"STATIC", L"z-back", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 60, 60,
                              280, 180, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
        HWND front =
            ::CreateWindowExW(0, L"STATIC", L"z-front", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100,
                              280, 180, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
        QVERIFY(back && front);
        // Ensure front is above back in real Z-order.
        ::SetWindowPos(front, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        ::Sleep(30);

        QVERIFY(sm.assignWindow(back, m->hmon, 3));
        QVERIFY(sm.assignWindow(front, m->hmon, 3));
        sm.setOverviewOpen(true);
        sm.rebuildSpaceScreenshot(m->hmon, 3);

        const Space &sp = m->spaces[3];
        QCOMPARE(sp.windows.size(), 2);
        QVERIFY(!sp.zOrder.isEmpty());
        // zOrder is top → bottom: front window must be first.
        QCOMPARE(sp.zOrder.first(), front);
        QCOMPARE(sp.zOrder.last(), back);
        QVERIFY(!sp.screenshot.isNull());
        // Canvas is monitor-aspect, not a letterboxed 640×360.
        const int monW = m->physRect.right - m->physRect.left;
        const int monH = m->physRect.bottom - m->physRect.top;
        QVERIFY(qAbs(double(sp.screenshot.width()) / sp.screenshot.height() - double(monW) / monH) <
                0.05);

        sm.setOverviewOpen(false);
        sm.untrackWindow(back);
        sm.untrackWindow(front);
        ::cloak::set(back, false);
        ::cloak::set(front, false);
        ::DestroyWindow(back);
        ::DestroyWindow(front);
    }

    void assignAndQueryOwnership()
    {
        SpaceManager sm;
        auto *m = sm.monitors().first();
        ensureSpaces(sm, m, 4);
        m->currentIndex = 0;

        QVERIFY(sm.assignWindow(m_hwnd, m->hmon, 2));
        QCOMPARE(sm.spaceOfWindow(m_hwnd), 2);
        QCOMPARE(sm.ownerMonitorOf(m_hwnd), m->hmon);

        // Moving to current space should uncloak; moving away should cloak.
        QVERIFY(sm.assignWindow(m_hwnd, m->hmon, 0));
        QCOMPARE(sm.spaceOfWindow(m_hwnd), 0);
        // Give DWM a moment.
        ::Sleep(50);
        QVERIFY(!cloak::isCloaked(m_hwnd));

        QVERIFY(sm.assignWindow(m_hwnd, m->hmon, 3));
        ::Sleep(50);
        QVERIFY(cloak::isCloaked(m_hwnd));

        // Clean: put back on space 0 and untrack.
        QVERIFY(sm.assignWindow(m_hwnd, m->hmon, 0));
        sm.untrackWindow(m_hwnd);
        QCOMPARE(sm.spaceOfWindow(m_hwnd), -1);
        QVERIFY(sm.ownerMonitorOf(m_hwnd) == nullptr);
        ::cloak::set(m_hwnd, false);
    }

    void movingWindowRebuildsSourceScreenshot()
    {
        SpaceManager sm;
        auto *m = sm.monitors().first();
        ensureSpaces(sm, m, 3);
        m->currentIndex = 0;

        QVERIFY(sm.assignWindow(m_hwnd, m->hmon, 1));
        QImage marker(16, 9, QImage::Format_ARGB32_Premultiplied);
        marker.fill(QColor(9, 9, 9));
        m->spaces[1].screenshot = marker;

        QVERIFY(sm.assignWindow(m_hwnd, m->hmon, 2));
        QCOMPARE(sm.spaceOfWindow(m_hwnd), 2);
        QVERIFY(!m->spaces[1].screenshot.isNull());
        QVERIFY(m->spaces[1].screenshot.size() != marker.size());

        sm.untrackWindow(m_hwnd);
        ::cloak::set(m_hwnd, false);
    }

    void maximizedWindowSharesSpaceLikeAnyOther()
    {
        SpaceManager sm;
        auto *m = sm.monitors().first();
        ensureSpaces(sm, m, 2);
        const QString nameBefore = m->spaces[1].name;

        // Second window (own-process → only used via assignWindow directly).
        HWND other = ::CreateWindowExW(0, L"STATIC", L"OtherApp Document",
                                       WS_OVERLAPPEDWINDOW | WS_VISIBLE, 50, 50, 400, 300, nullptr,
                                       nullptr, ::GetModuleHandleW(nullptr), nullptr);
        QVERIFY(other != nullptr);

        // Maximized windows no longer create exclusive spaces.
        ::ShowWindow(m_hwnd, SW_MAXIMIZE);
        QVERIFY(sm.assignWindow(m_hwnd, m->hmon, 1));
        QVERIFY(sm.assignWindow(other, m->hmon, 1));
        QCOMPARE(m->spaces[1].windows.size(), 2);
        QVERIFY(m->spaces[1].windows.contains(m_hwnd));
        QVERIFY(m->spaces[1].windows.contains(other));
        QCOMPARE(m->spaces[1].name, nameBefore);

        sm.untrackWindow(m_hwnd);
        sm.untrackWindow(other);
        ::cloak::set(m_hwnd, false);
        ::cloak::set(other, false);
        ::ShowWindow(m_hwnd, SW_RESTORE);
        ::DestroyWindow(other);
    }

    void assignKeepsDefaultSpaceName()
    {
        SpaceManager sm;
        auto *m = sm.monitors().first();
        ::ShowWindow(m_hwnd, SW_RESTORE);

        const QString before = m->spaces[0].name;
        QVERIFY(sm.assignWindow(m_hwnd, m->hmon, 0));
        QCOMPARE(m->spaces[0].name, before);

        sm.untrackWindow(m_hwnd);
        ::cloak::set(m_hwnd, false);
    }

    void switchOnlyAffectsAssignedWindowVisibility()
    {
        SpaceManager sm;
        auto *m = sm.monitors().first();
        ensureSpaces(sm, m, 2);

        QVERIFY(sm.assignWindow(m_hwnd, m->hmon, 0));
        QVERIFY(sm.switchSpace(m->hmon, 1, false));
        ::Sleep(80);
        QVERIFY(cloak::isCloaked(m_hwnd));

        QVERIFY(sm.switchSpace(m->hmon, 0, false));
        ::Sleep(80);
        QVERIFY(!cloak::isCloaked(m_hwnd));

        sm.untrackWindow(m_hwnd);
        ::cloak::set(m_hwnd, false);
    }

    void overviewOpenSuppressesAnimationSignal()
    {
        SpaceManager sm;
        auto *m = sm.monitors().first();
        ensureSpaces(sm, m, 3);
        m->currentIndex = 0;
        sm.setOverviewOpen(true);

        QSignalSpy anim(&sm, &SpaceManager::requestSwitchAnimation);
        QVERIFY(sm.switchSpace(m->hmon, 2, true));
        QCOMPARE(anim.count(), 0);

        sm.setOverviewOpen(false);
        QVERIFY(sm.switchSpace(m->hmon, 0, true));
        QCOMPARE(anim.count(), 1);
    }

    void refreshMonitorsIsIdempotent()
    {
        SpaceManager sm;
        const int before = sm.monitors().size();
        sm.refreshMonitors();
        sm.refreshMonitors();
        QCOMPARE(sm.monitors().size(), before);
    }

    // Committing the CURRENT space (clicking its card right after open) must
    // still re-apply cloak: overview warm uncloaks every managed window and a
    // same-index switch used to early-return without applyVisibility — the
    // desktop then flashed off-space windows when the mask went away.
    void switchSameIndexStillRecloaks()
    {
        SpaceManager sm;
        auto *m = sm.monitors().first();
        ensureSpaces(sm, m, 2);
        m->currentIndex = 0;

        HWND hwnd = ::CreateWindowExW(0, L"STATIC", L"same-index recloak",
                                      WS_OVERLAPPEDWINDOW | WS_VISIBLE, 70, 70, 260, 160, nullptr,
                                      nullptr, ::GetModuleHandleW(nullptr), nullptr);
        QVERIFY(hwnd != nullptr);
        QVERIFY(sm.assignWindow(hwnd, m->hmon, 1)); // lives on the OTHER space

        // Simulate overview warm: visible although it belongs elsewhere.
        ::cloak::set(hwnd, false);
        QTRY_VERIFY_WITH_TIMEOUT(!cloak::isCloaked(hwnd), 1500);

        // Same-index switch: returns false (no switch) but must recloak.
        QVERIFY(!sm.switchSpace(m->hmon, 0, false));
        QTRY_VERIFY_WITH_TIMEOUT(cloak::isCloaked(hwnd), 1500);

        ::cloak::set(hwnd, false);
        sm.untrackWindow(hwnd);
        ::DestroyWindow(hwnd);
    }

    void rebuildEmitsSpacePreviewInvalidated()
    {
        SpaceManager sm;
        auto *m = sm.monitors().first();
        QSignalSpy spy(&sm, &SpaceManager::spacePreviewInvalidated);

        sm.rebuildSpaceScreenshot(m->hmon, 0);
        QVERIFY(spy.count() >= 1);
        QCOMPARE(spy.last().at(0).toULongLong(), quint64(reinterpret_cast<quintptr>(m->hmon)));
        QCOMPARE(spy.last().at(1).toInt(), 0);
    }

    // Open step 1 (pre-mask): on-screen windows are dropped + recaptured so
    // the screen fallback can fill PrintWindow-black frames with honest pixels.
    void refreshVisibleShotsRecapturesOnScreenWindows()
    {
        thumbs::clearWindowCache();
        SpaceManager sm;
        auto *m = sm.monitors().first();
        QVERIFY(sm.assignWindow(m_hwnd, m->hmon, 0));
        QVERIFY(thumbs::canSampleScreen(m_hwnd));

        // NOTE: always constBits() — non-const bits() detaches a shared QImage
        // and would compare two fresh copies instead of the cache buffers.
        const QImage seeded = thumbs::windowShot(m_hwnd);
        QVERIFY(!seeded.isNull());

        sm.refreshVisibleShots();
        const QImage refreshed = thumbs::windowShot(m_hwnd);
        QVERIFY(!refreshed.isNull());
        // Recaptured → a NEW backing buffer, not the seeded one.
        QVERIFY2(refreshed.constBits() != seeded.constBits(),
                 "refreshVisibleShots kept the stale cache entry");

        // Cloaked (off-space) windows are NOT touched — no honest screen
        // source exists for them, their existing shot must survive refresh.
        QVERIFY(::cloak::set(m_hwnd, true));
        ::Sleep(50);
        QVERIFY(!thumbs::canSampleScreen(m_hwnd));
        // Snapshot AFTER the cloak settles (cloak may itself trigger a
        // recapture through other hooks — that is not what we assert here).
        const QImage cloakedKept = thumbs::windowShot(m_hwnd);
        QVERIFY(!cloakedKept.isNull());
        sm.refreshVisibleShots();
        QCOMPARE(thumbs::windowShot(m_hwnd).constBits(), cloakedKept.constBits());
        ::cloak::set(m_hwnd, false);
        ::cloak::set(m_hwnd, false);

        thumbs::clearWindowCache();
    }

    // Open step 2 (behind masks): warm fills MISSING shots only — it must not
    // clear known-good entries the way the old clear+recapture did.
    void warmWindowShotsPreservesCacheAndFillsMissing()
    {
        thumbs::clearWindowCache();
        SpaceManager sm;
        auto *m = sm.monitors().first();
        QVERIFY(sm.assignWindow(m_hwnd, m->hmon, 0));

        const QImage kept = thumbs::windowShot(m_hwnd);
        QVERIFY(!kept.isNull());
        QCOMPARE(thumbs::windowCacheCount(), 1);

        // A tracked window whose shot was invalidated (missing) gets filled.
        HWND fresh = ::CreateWindowExW(0, L"STATIC", L"warm fill target",
                                       WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 300, 260, 160, nullptr,
                                       nullptr, ::GetModuleHandleW(nullptr), nullptr);
        QVERIFY(fresh != nullptr);
        ::ShowWindow(fresh, SW_SHOW);
        ::UpdateWindow(fresh);
        ::Sleep(30);
        QVERIFY(sm.assignWindow(fresh, m->hmon, 0));
        thumbs::invalidateWindow(fresh);
        QCOMPARE(thumbs::windowCacheCount(), 1);

        sm.warmWindowShots();

        // Existing entry preserved (same buffer — no blind clear+recapture)…
        QCOMPARE(thumbs::windowCacheCount(), 2);
        const QImage after = thumbs::windowShot(m_hwnd);
        QVERIFY(!after.isNull());
        QCOMPARE(after.constBits(), kept.constBits());
        // …and the missing one was captured.
        QVERIFY(!thumbs::windowShot(fresh).isNull());

        ::DestroyWindow(fresh);
        thumbs::clearWindowCache();
    }

    // A monitor-handle change (sleep/wake, dock, GPU reset) replaces the
    // MonitorSpaces wholesale. Entries on the gone handle used to be purged
    // WITHOUT re-adoption: the window then appeared in NO overview panel —
    // and stayed stuck cloaked if it was off-space — until it happened to
    // move or get clicked. Survivors must re-home onto a live monitor.
    void monitorHandleChangeRehomesAndUncloaksLostWindows()
    {
        SpaceManager sm;
        auto *m = sm.monitors().first();
        ensureSpaces(sm, m, 2);
        m->currentIndex = 0;
        const HMONITOR realH = m->hmon;

        // Off-space → cloaked (the stuck-hidden case).
        QVERIFY(sm.assignWindow(m_hwnd, m->hmon, 1));
        QTRY_VERIFY_WITH_TIMEOUT(cloak::isCloaked(m_hwnd), 1500);

        // Old handle "vanishes": only a fake monitor survives the refresh.
        MonitorEntry fake;
        fake.handle = reinterpret_cast<HMONITOR>(quintptr(0x00C0FFEE));
        sm.applyMonitorEntries({fake});

        // Survived the purge: owned by the live monitor's current space, and
        // the stale cloak was cleared (visibility restored).
        QCOMPARE(sm.ownerMonitorOf(m_hwnd), fake.handle);
        QCOMPARE(sm.spaceOfWindow(m_hwnd), 0);
        QTRY_VERIFY_WITH_TIMEOUT(!cloak::isCloaked(m_hwnd), 1500);

        // Real handles come back — membership must land on the REAL monitor.
        sm.applyMonitorEntries(monitors::enumerate());
        auto *back = sm.monitorOf(realH);
        QVERIFY(back);
        QCOMPARE(sm.ownerMonitorOf(m_hwnd), realH);
        QCOMPARE(sm.spaceOfWindow(m_hwnd), back->currentIndex);
        QVERIFY(back->spaces[back->currentIndex].windows.contains(m_hwnd));

        sm.untrackWindow(m_hwnd);
        ::cloak::set(m_hwnd, false);
    }

    // Refresh with surviving handles must leave ownership and off-space
    // cloak untouched — recovery only kicks in for vanished handles.
    void monitorRefreshPreservesOwnershipWhenHandlesSurvive()
    {
        SpaceManager sm;
        auto *m = sm.monitors().first();
        ensureSpaces(sm, m, 3);
        QVERIFY(sm.assignWindow(m_hwnd, m->hmon, 2));
        const HMONITOR realH = m->hmon;

        sm.applyMonitorEntries(monitors::enumerate());

        QCOMPARE(sm.ownerMonitorOf(m_hwnd), realH);
        QCOMPARE(sm.spaceOfWindow(m_hwnd), 2);
        // Still off-space → refresh must not have uncloaked it.
        QTRY_VERIFY_WITH_TIMEOUT(cloak::isCloaked(m_hwnd), 1500);

        sm.untrackWindow(m_hwnd);
        ::cloak::set(m_hwnd, false);
    }

    // Refresh must NOT sweep untracked desktop windows into management: batch
    // adoption used to recapture every foreign window per assign (PrintWindow
    // has no timeout — one hung app froze the whole refresh). Only orphans
    // (previously owned, shots cached) are re-homed.
    void monitorRefreshDoesNotSweepUntrackedWindows()
    {
        SpaceManager sm;
        auto *m = sm.monitors().first();
        QVERIFY(sm.assignWindow(m_hwnd, m->hmon, 0)); // owned survives below

        // Every foreign manageable window is untracked in this fresh manager.
        const QVector<HWND> foreign = WindowTracker::snapshotManageableWindows();
        sm.applyMonitorEntries(monitors::enumerate());
        for (HWND h : foreign)
            QVERIFY2(
                sm.spaceOfWindow(h) == -1,
                qPrintable(
                    QStringLiteral("refresh swept untracked window 0x%1").arg(quintptr(h), 0, 16)));
        // The owned window keeps its membership (no-op refresh path).
        QCOMPARE(sm.spaceOfWindow(m_hwnd), 0);

        sm.untrackWindow(m_hwnd);
        ::cloak::set(m_hwnd, false);
    }

    // Cross-monitor move re-homes the OWNED window into the target monitor's
    // current space — even while it is cloaked (off-space): cloak state must
    // never strand a window on the monitor it left (main + flow share
    // SpaceManager::onWindowMoved).
    void windowMovedRehomesAcrossMonitors()
    {
        SpaceManager sm;
        if (sm.monitors().size() < 2)
            QSKIP("cross-monitor re-home needs at least 2 monitors");
        auto *a = sm.monitors().first();
        auto *b = sm.monitors().last();
        QVERIFY(a->hmon != b->hmon);
        ensureSpaces(sm, a, 2);

        RECT before{};
        ::GetWindowRect(m_hwnd, &before);
        QVERIFY(sm.assignWindow(m_hwnd, a->hmon, 1)); // off-space → cloaked
        QTRY_VERIFY_WITH_TIMEOUT(cloak::isCloaked(m_hwnd), 1500);

        // Physically move onto monitor B, then deliver the move event.
        const int cx = (b->physRect.left + b->physRect.right) / 2;
        const int cy = (b->physRect.top + b->physRect.bottom) / 2;
        ::SetWindowPos(m_hwnd, nullptr, cx - 150, cy - 100, 0, 0, SWP_NOZORDER | SWP_NOACTIVATE);
        sm.onWindowMoved(m_hwnd);

        QCOMPARE(sm.ownerMonitorOf(m_hwnd), b->hmon);
        QCOMPARE(sm.spaceOfWindow(m_hwnd), b->currentIndex);
        // Landed in B's CURRENT space → the stale cloak must be cleared.
        QTRY_VERIFY_WITH_TIMEOUT(!cloak::isCloaked(m_hwnd), 1500);

        // Same-monitor repeat is a refresh no-op (ownership unchanged).
        sm.onWindowMoved(m_hwnd);
        QCOMPARE(sm.ownerMonitorOf(m_hwnd), b->hmon);

        sm.untrackWindow(m_hwnd);
        ::cloak::set(m_hwnd, false);
        ::SetWindowPos(m_hwnd, nullptr, before.left, before.top, 0, 0,
                       SWP_NOZORDER | SWP_NOACTIVATE);
    }

  private:
    // Grow the monitor to at least n spaces (cold start is 1 — issue #8).
    static void ensureSpaces(SpaceManager &sm, MonitorSpaces *m, int n)
    {
        while (m->spaces.size() < n)
            sm.addSpace(m->hmon);
    }

    HWND m_hwnd = nullptr;
};

QTEST_MAIN(TestSpaceManager)
#include "test_space_manager.moc"
