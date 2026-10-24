#include <QtTest>
#include "core/space/SpaceManager.h"
#include "core/capture/ThumbnailCapture.h"
#include "ui/effects/SwitchFlashOverlay.h"

class TestSpaceSwitchAnimation : public QObject {
    Q_OBJECT
  private slots:
    void preparesAllShotsBeforeSwitchAndCleansUp()
    {
        SpaceManager sm;
        SpaceSwitchAnimator animation(&sm);
        auto *m = sm.monitors().first();
        const HMONITOR mon = m->hmon;
        QVERIFY(sm.addSpace(mon));
        QVERIFY(sm.addSpace(mon));
        bool prepared = false;
        connect(&sm, &SpaceManager::requestSwitchAnimation, this,
                [&](quint64, int from, int to, int direction) {
                    QCOMPARE(sm.currentSpaceIndex(mon), from);
                    QCOMPARE(to, 1);
                    QCOMPARE(direction, 1);
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
        QVERIFY(!sm.switchSpace(mon, 1, true));
        QVERIFY(!sm.switchSpace(mon, 99, true));
        QCOMPARE(requested.count(), 0);
        sm.setOverviewOpen(true);
        QVERIFY(sm.switchSpace(mon, 0, true));
        QCOMPARE(requested.count(), 0);
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
