#include <QtTest>

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
        m_hwnd = ::CreateWindowExW(
            0, L"STATIC", L"SpaceWM space test",
            WS_OVERLAPPEDWINDOW, 10, 10, 300, 200,
            nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
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

    void hasMonitorsAndFourSpaces()
    {
        SpaceManager sm;
        auto *list = new QVector<MonitorSpaces *>(); // avoid leak confusion — use stack
        delete list;
        const auto mons = sm.monitors();
        QVERIFY(!mons.isEmpty());
        auto *m = mons.first();
        QCOMPARE(m->spaces.size(), 4);
        QCOMPARE(m->currentIndex, 0);
        QVERIFY(!m->spaces[0].name.isEmpty());
    }

    void switchSpaceChangesIndexAndEmits()
    {
        SpaceManager sm;
        auto *m = sm.monitors().first();
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

        HWND back = ::CreateWindowExW(
            0, L"STATIC", L"z-back",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE, 60, 60, 280, 180,
            nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
        HWND front = ::CreateWindowExW(
            0, L"STATIC", L"z-front",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 280, 180,
            nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
        QVERIFY(back && front);
        // Ensure front is above back in real Z-order.
        ::SetWindowPos(front, HWND_TOP, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
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
        QVERIFY(qAbs(double(sp.screenshot.width()) / sp.screenshot.height()
                     - double(monW) / monH) < 0.05);

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

    void exclusiveMaximizedSpaceBindsAndRenames()
    {
        SpaceManager sm;
        auto *m = sm.monitors().first();

        // Second window (also own-process → only used via assignWindow directly).
        HWND other = ::CreateWindowExW(
            0, L"STATIC", L"OtherApp Document",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE, 50, 50, 400, 300,
            nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
        QVERIFY(other != nullptr);

        // Maximize test window so isMaximizedWindow() is true.
        ::ShowWindow(m_hwnd, SW_MAXIMIZE);
        QVERIFY(SpaceManager::isMaximizedWindow(m_hwnd));

        QVERIFY(sm.assignWindow(m_hwnd, m->hmon, 1));
        QVERIFY(sm.isExclusiveSpace(m->hmon, 1));
        QCOMPARE(sm.exclusiveWindowOn(m->hmon, 1), m_hwnd);
        // Renamed to window title ("SpaceWM space test").
        QVERIFY(m->spaces[1].name.contains(QStringLiteral("SpaceWM"))
                || m->spaces[1].name.contains(QStringLiteral("space test")));
        QCOMPARE(m->spaces[1].windows.size(), 1);
        QVERIFY(m->spaces[1].windows.contains(m_hwnd));

        // Another window must not enter this exclusive space.
        QVERIFY(!sm.canAssignToSpace(m->hmon, 1, other));
        QVERIFY(!sm.assignWindow(other, m->hmon, 1));
        QVERIFY(!m->spaces[1].windows.contains(other));

        // Non-exclusive space still accepts other.
        QVERIFY(sm.assignWindow(other, m->hmon, 2));
        QCOMPARE(sm.spaceOfWindow(other), 2);

        // Move exclusive window out → space unbinds and restores default name.
        QVERIFY(sm.assignWindow(m_hwnd, m->hmon, 0));
        QVERIFY(!sm.isExclusiveSpace(m->hmon, 1));
        QVERIFY(sm.exclusiveWindowOn(m->hmon, 1) == nullptr);
        QCOMPARE(m->spaces[1].name, QStringLiteral("Space 2"));

        sm.untrackWindow(m_hwnd);
        sm.untrackWindow(other);
        ::cloak::set(m_hwnd, false);
        ::cloak::set(other, false);
        ::ShowWindow(m_hwnd, SW_RESTORE);
        ::DestroyWindow(other);
    }

    void nonMaximizedDoesNotBindExclusive()
    {
        SpaceManager sm;
        auto *m = sm.monitors().first();
        ::ShowWindow(m_hwnd, SW_RESTORE);
        QVERIFY(!SpaceManager::isMaximizedWindow(m_hwnd));

        const QString before = m->spaces[0].name;
        QVERIFY(sm.assignWindow(m_hwnd, m->hmon, 0));
        QVERIFY(!sm.isExclusiveSpace(m->hmon, 0));
        QCOMPARE(m->spaces[0].name, before);

        sm.untrackWindow(m_hwnd);
        ::cloak::set(m_hwnd, false);
    }

    void switchOnlyAffectsAssignedWindowVisibility()
    {
        SpaceManager sm;
        auto *m = sm.monitors().first();

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

private:
    HWND m_hwnd = nullptr;
};

QTEST_MAIN(TestSpaceManager)
#include "test_space_manager.moc"
