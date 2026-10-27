#include <QtTest>
#include "core/space/SpaceManager.h"
#include "core/capture/ThumbnailCapture.h"
#include "ui/effects/SwitchFlashOverlay.h"
#include <dwmapi.h>

class TestSpaceSwitchAnimation : public QObject {
    Q_OBJECT
  private slots:
    void startsFromLiveMonitorRatherThanSpaceComposite()
    {
        SpaceManager sm;
        SpaceSwitchAnimator animation(&sm);
        auto *m = sm.primaryMonitor();
        const HMONITOR mon = m->hmon;
        QVERIFY(sm.addSpace(mon));
        // This untracked surface cannot occur in a managed-window composite.
        QWidget desktop(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        desktop.setGeometry(m->geometry);
        desktop.setAutoFillBackground(true);
        QPalette palette;
        palette.setColor(QPalette::Window, Qt::red);
        desktop.setPalette(palette);
        desktop.show();
        QVERIFY(QTest::qWaitForWindowExposed(&desktop));
        QVERIFY(::SetWindowPos(reinterpret_cast<HWND>(desktop.winId()), HWND_TOPMOST,
                               m->physRect.left, m->physRect.top,
                               m->physRect.right - m->physRect.left,
                               m->physRect.bottom - m->physRect.top, SWP_NOACTIVATE));
        desktop.repaint();
        ::DwmFlush();
        auto markerIsOnScreen = [&]() {
            const QImage frame = thumbs::captureMonitor(m->physRect);
            return !frame.isNull() &&
                   frame.pixelColor(frame.width() / 2, frame.height() / 2) == QColor(Qt::red);
        };
        QTRY_VERIFY_WITH_TIMEOUT(markerIsOnScreen(), 3000);
        m->spaces[0].screenshot.fill(Qt::blue); // deliberately stale model image
        const int cachedWindows = thumbs::windowCacheCount();
        auto verifySource = [&]() {
            SwitchFlashOverlay *visible = nullptr;
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *overlay = qobject_cast<SwitchFlashOverlay *>(widget);
                    overlay && overlay->isVisible())
                    visible = overlay;
            QVERIFY(visible);
            const QImage frame = visible->grab().toImage();
            QCOMPARE(frame.pixelColor(frame.width() / 2, frame.height() / 2), QColor(Qt::red));
        };
        bool covered = false;
        connect(&sm, &SpaceManager::requestSwitchCover, this, [&](quint64, int) {
            QCOMPARE(thumbs::windowCacheCount(), cachedWindows);
            verifySource();
            covered = true;
        });
        connect(&sm, &SpaceManager::requestSwitchAnimation, this,
                [&](quint64, int, int, int) { verifySource(); });
        QVERIFY(sm.switchSpace(mon, 1));
        QVERIFY(covered);
        animation.stopAll();
    }
    void windowCaptureRunsBehindCover()
    {
        SpaceManager sm;
        SpaceSwitchAnimator animation(&sm);
        auto *m = sm.monitors().first();
        const HMONITOR mon = m->hmon;
        QWidget source;
        source.setGeometry(m->geometry.adjusted(30, 30, -30, -30));
        source.show();
        QVERIFY(QTest::qWaitForWindowExposed(&source));
        const HWND hwnd = reinterpret_cast<HWND>(source.winId());
        QVERIFY(sm.assignWindow(hwnd, mon, 0));
        QVERIFY(sm.addSpace(mon));
        QVERIFY(thumbs::refreshWindow(hwnd));
        thumbs::invalidateWindow(hwnd);
        const int beforeCapture = thumbs::windowCacheCount();
        bool covered = false;
        connect(&sm, &SpaceManager::requestSwitchCover, this, [&](quint64, int) {
            // Even the missing source shot must not be captured before cover.
            QCOMPARE(thumbs::windowCacheCount(), beforeCapture);
            QVERIFY(animation.isPlaying(mon));
            QVERIFY(!thumbs::screenSamplingEnabled());
            covered = true;
        });
        QVERIFY(sm.switchSpace(mon, 1));
        QVERIFY(covered);
        QCOMPARE(thumbs::windowCacheCount(), beforeCapture + 1);
        sm.untrackWindow(hwnd);
        animation.stopAll();
    }
    void preparesAllShotsBeforeSwitchAndCleansUp()
    {
        SpaceManager sm;
        SpaceSwitchAnimator animation(&sm);
        auto *m = sm.monitors().first();
        const HMONITOR mon = m->hmon;
        QVERIFY(sm.addSpace(mon));
        QVERIFY(sm.addSpace(mon));
        bool covered = false;
        connect(&sm, &SpaceManager::requestSwitchCover, this, [&](quint64 h, int from) {
            QCOMPARE(h, quint64(quintptr(mon)));
            QCOMPARE(from, 0);
            QCOMPARE(m->currentIndex, 0);
            QVERIFY(animation.isPlaying(mon));
            QVERIFY(!thumbs::screenSamplingEnabled());
            covered = true;
        });
        bool rebuiltBehindCover = false;
        connect(&sm, &SpaceManager::spacePreviewInvalidated, this, [&](quint64, int) {
            QVERIFY(covered);
            QVERIFY(animation.isPlaying(mon));
            QVERIFY(!thumbs::screenSamplingEnabled());
            rebuiltBehindCover = true;
        });
        bool prepared = false;
        connect(&sm, &SpaceManager::requestSwitchAnimation, this,
                [&](quint64, int from, int to, int direction) {
                    QCOMPARE(sm.currentSpaceIndex(mon), from);
                    QCOMPARE(to, 1);
                    QCOMPARE(direction, 1);
                    QVERIFY(covered);
                    QVERIFY(rebuiltBehindCover);
                    QVERIFY(animation.isPlaying(mon));
                    QVERIFY(!thumbs::screenSamplingEnabled());
                    for (const auto &space : m->spaces) {
                        QVERIFY(!space.screenshot.isNull());
                        QCOMPARE(space.screenshot.size(),
                                 QSize(m->physRect.right - m->physRect.left,
                                       m->physRect.bottom - m->physRect.top));
                    }
                    prepared = true;
                });
        QVERIFY(sm.switchSpace(mon, 1, true));
        QVERIFY(prepared);
        QCOMPARE(sm.currentSpaceIndex(mon), 1);
        QTRY_VERIFY_WITH_TIMEOUT(!animation.isPlaying(mon), 3000);
        QVERIFY(thumbs::screenSamplingEnabled());
        QSignalSpy requested(&sm, &SpaceManager::requestSwitchAnimation);
        QSignalSpy covers(&sm, &SpaceManager::requestSwitchCover);
        QVERIFY(!sm.switchSpace(mon, 1, true));
        QVERIFY(!sm.switchSpace(mon, 99, true));
        QCOMPARE(requested.count(), 0);
        QCOMPARE(covers.count(), 0);
        sm.setOverviewOpen(true);
        QVERIFY(sm.switchSpace(mon, 0, true));
        QCOMPARE(requested.count(), 0);
        QCOMPARE(covers.count(), 0);
        sm.setOverviewOpen(false);
    }
    void disabledAnimationDoesNotCreateOverlay()
    {
        SpaceManager sm;
        SpaceSwitchAnimator animation(&sm);
        auto *m = sm.monitors().first();
        QVERIFY(sm.addSpace(m->hmon));
        sm.setAnimationEnabled(false);
        QVERIFY(sm.switchSpace(m->hmon, 1));
        QVERIFY(!animation.isPlaying(m->hmon));
        QVERIFY(thumbs::screenSamplingEnabled());
    }
    void overlappingMonitorsAndLayoutChange()
    {
        SpaceManager sm;
        sm.applyMonitorEntries(
            {{reinterpret_cast<HMONITOR>(1), {0, 0, 400, 300}, QRect(0, 0, 400, 300), true, "a"},
             {reinterpret_cast<HMONITOR>(2),
              {400, 0, 800, 300},
              QRect(400, 0, 400, 300),
              false,
              "b"}});
        SpaceSwitchAnimator animation(&sm);
        const auto a = reinterpret_cast<HMONITOR>(1), b = reinterpret_cast<HMONITOR>(2);
        QVERIFY(sm.addSpace(a));
        QVERIFY(sm.addSpace(b));
        QVERIFY(sm.switchSpace(a, 1));
        QVERIFY(sm.switchSpace(b, 1));
        QVERIFY(animation.isPlaying(a));
        QVERIFY(animation.isPlaying(b));
        sm.setOverviewOpen(true);
        QVERIFY(sm.addSpace(a)); // layout invalidation cancels both overlays
        QVERIFY(!animation.isPlaying(a));
        QVERIFY(!animation.isPlaying(b));
        QVERIFY(!thumbs::screenSamplingEnabled());
        sm.setOverviewOpen(false);
        QVERIFY(thumbs::screenSamplingEnabled());
    }
};
QTEST_MAIN(TestSpaceSwitchAnimation)
#include "test_space_switch_animation.moc"
