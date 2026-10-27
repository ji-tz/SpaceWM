#include <QtTest>
#include "core/capture/ThumbnailCapture.h"
#include "core/space/SpaceManager.h"
#include "core/window/CloakController.h"
#include "ui/overview/OverviewHost.h"
#include "ui/overview/OverviewWindow.h"
#include "ui/preview/SpaceCardWidget.h"
#include <QTimer>

class TestOverviewStaging : public QObject {
    Q_OBJECT
  private slots:
    void preparationTouchesOnlyTheRequestedSpace()
    {
        QWidget first, second;
        first.setStyleSheet(QStringLiteral("background: white;"));
        second.setStyleSheet(QStringLiteral("background: white;"));
        first.resize(300, 180);
        second.resize(300, 180);
        first.show();
        second.show();
        SpaceManager sm;
        const HWND current = reinterpret_cast<HWND>(first.winId());
        const HWND inactive = reinterpret_cast<HWND>(second.winId());
        const HMONITOR mon = ::MonitorFromWindow(current, MONITOR_DEFAULTTONEAREST);
        QVERIFY(sm.addSpace(mon));
        QVERIFY(sm.assignWindow(current, mon, 0));
        QVERIFY(sm.assignWindow(inactive, mon, 1));
        thumbs::invalidateWindow(inactive);
        QVERIFY(thumbs::cachedWindowShot(inactive).isNull());
        sm.setOverviewOpen(true);
        sm.prepareOverviewSpace(mon, 0, true);
        QVERIFY(!thumbs::cachedWindowShot(current).isNull());
        QVERIFY(thumbs::cachedWindowShot(inactive).isNull());
        QVERIFY(cloak::isCloaked(inactive));
        // The staged renderer cannot silently perform deferred capture.
        sm.rebuildSpaceScreenshot(mon, 1, QSize(640, 360), true);
        QVERIFY(thumbs::cachedWindowShot(inactive).isNull());
        sm.prepareOverviewSpace(mon, 1, false);
        QVERIFY(!thumbs::cachedWindowShot(inactive).isNull());
        QVERIFY(!cloak::isCloaked(inactive));
        sm.setOverviewOpen(false);
        sm.untrackWindow(current);
        sm.untrackWindow(inactive);
    }

    void frozenDesktopIsPaintedBeforeContent()
    {
        SpaceManager sm;
        auto *mon = sm.monitors().first();
        OverviewWindow panel(&sm);
        QImage desktop(mon->physRect.right - mon->physRect.left,
                       mon->physRect.bottom - mon->physRect.top, QImage::Format_RGB32);
        desktop.fill(QColor(70, 150, 210));
        panel.beginPanelOpen(mon->hmon, false, desktop);
        QVERIFY(!panel.isContentReady());
        QCOMPARE(panel.windowOpacity(), 1.0);
        const QImage entry = panel.grab().toImage();
        QCOMPARE(entry.pixelColor(entry.width() / 2, entry.height() / 2), QColor(70, 150, 210));
        panel.populateOpenContent();
        QVERIFY(panel.isContentReady());
        panel.forceHide();
    }

    void currentSpaceIsReadyBeforeDeferredWork()
    {
        SpaceManager sm;
        for (auto *mon : sm.monitors())
            QVERIFY(sm.addSpace(mon->hmon));
        OverviewHost host(&sm);
        QSignalSpy ready(&host, &OverviewHost::panelReady);
        QSignalSpy rendered(&sm, &SpaceManager::spacePreviewInvalidated);
        host.openAll();
        QVERIFY(host.activePanel());
        const HMONITOR active = host.activePanel()->targetMonitor();
        QVERIFY(host.activePanel()->isContentReady());
        auto *inactiveCard = host.activePanel()->findChildren<SpaceCardWidget *>().last();
        QVERIFY(inactiveCard);
        // The old desktop pointer can enter a card as the layout appears.
        // Until the entry fade finishes, this must not select another space.
        inactiveCard->hovered(1);
        QCOMPARE(host.activePanel()->selectedIndex(), 0);
        QCOMPARE(ready.count(), 1);
        QCOMPARE(ready.first().at(0).toULongLong(), quint64(active));
        QCOMPARE(rendered.count(), 1);
        QCOMPARE(rendered.first().at(0).toULongLong(), quint64(active));
        QCOMPARE(rendered.first().at(1).toInt(), sm.currentSpaceIndex(active));
        QVERIFY(!thumbs::screenSamplingEnabled());
        for (auto *mon : sm.monitors())
            if (mon->hmon != active)
                QVERIFY(!host.panelFor(mon->hmon)->isContentReady());

        bool yielded = false;
        QTimer::singleShot(0, &host, [&]() { yielded = true; });
        QTRY_VERIFY(yielded);
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), sm.monitors().size(), 4000);
        QTRY_COMPARE_WITH_TIMEOUT(rendered.count(), sm.monitors().size() * 2, 5000);
        // All current-space panels precede every inactive-space render.
        for (int i = 0; i < sm.monitors().size(); ++i)
            QCOMPARE(rendered[i].at(1).toInt(), 0);
        host.forceHideAll();
        QVERIFY(thumbs::screenSamplingEnabled());
    }

    void closingCancelsPendingPreparation()
    {
        SpaceManager sm;
        for (auto *mon : sm.monitors())
            QVERIFY(sm.addSpace(mon->hmon));
        OverviewHost host(&sm);
        QSignalSpy rendered(&sm, &SpaceManager::spacePreviewInvalidated);
        QSignalSpy ready(&host, &OverviewHost::panelReady);
        host.openAll();
        QCOMPARE(ready.count(), 1);
        host.closeAll(false);
        QTRY_VERIFY(!host.isOpen());
        // Reopen before the old preparation timers would all have completed.
        host.openAll();
        const int afterReopen = ready.count();
        QCOMPARE(afterReopen, 2);
        host.forceHideAll();
        const int count = rendered.count();
        QTest::qWait(400);
        QCOMPARE(rendered.count(), count);
        QCOMPARE(ready.count(), afterReopen);
        QVERIFY(!sm.overviewOpen());
        QVERIFY(thumbs::screenSamplingEnabled());
    }
};

QTEST_MAIN(TestOverviewStaging)
#include "test_overview_staging.moc"
