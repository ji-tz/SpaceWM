#include <QtTest>
#include "core/capture/ThumbnailCapture.h"
#include "core/log/Log.h"
#include "core/space/SpaceManager.h"
#include "core/window/CloakController.h"
#include "ui/overview/OverviewHost.h"
#include "ui/overview/OverviewWindow.h"
#include "ui/preview/SpaceCardWidget.h"
#include <QTimer>
#include <QFile>
#include <QTemporaryDir>

class TestOverviewStaging : public QObject {
    Q_OBJECT
    QTemporaryDir m_logDir;
  private slots:
    void initTestCase()
    {
        QVERIFY(m_logDir.isValid());
        QVERIFY(spacelog::init(m_logDir.path()));
    }

    void cleanupTestCase() { spacelog::shutdown(); }

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

    void allMonitorsReadyBeforeAnyAnimation()
    {
        SpaceManager sm;
        for (auto *mon : sm.monitors())
            QVERIFY(sm.addSpace(mon->hmon));
        OverviewHost host(&sm);
        QSignalSpy ready(&host, &OverviewHost::panelReady);
        QSignalSpy rendered(&sm, &SpaceManager::spacePreviewInvalidated);
        bool allReadyAtFirstSignal = false;
        connect(&host, &OverviewHost::panelReady, &host, [&]() {
            allReadyAtFirstSignal = true;
            for (auto *mon : sm.monitors())
                allReadyAtFirstSignal &= host.panelFor(mon->hmon)->isContentReady();
        });
        host.openAll();
        QVERIFY(allReadyAtFirstSignal);
        QCOMPARE(ready.count(), sm.monitors().size());
        QCOMPARE(rendered.count(), sm.monitors().size() * 2);
        for (auto *mon : sm.monitors()) {
            const auto *panel = host.panelFor(mon->hmon);
            QVERIFY(panel->isContentReady());
            QVERIFY(panel->isAnimating());
            QCOMPARE(panel->selectedIndex(), mon->currentIndex);
            for (const auto &space : mon->spaces)
                QVERIFY(!space.screenshot.isNull());
        }
        const int completed = rendered.count();
        QTest::qWait(400);
        QCOMPARE(rendered.count(), completed); // No capture/composite jobs during animation.
        host.forceHideAll();
        QVERIFY(thumbs::screenSamplingEnabled());
    }

    void closingCancelsPendingPreparation()
    {
        SpaceManager sm;
        OverviewHost host(&sm);
        QSignalSpy ready(&host, &OverviewHost::panelReady);
        QTimer::singleShot(0, &host, [&]() { host.closeAll(false); });
        host.openAll();
        QVERIFY(!host.isOpen());
        QVERIFY(!sm.overviewOpen());
        QCOMPARE(ready.count(), 0);
        QVERIFY(thumbs::screenSamplingEnabled());
        host.openAll();
        QCOMPARE(ready.count(), sm.monitors().size());
        host.forceHideAll();
        QTest::qWait(100);
        QVERIFY(!sm.overviewOpen());
    }

    void workerCanCaptureAnOwnedGuiWindow()
    {
        QWidget window;
        window.setStyleSheet(QStringLiteral("background: white;"));
        window.resize(300, 180);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        SpaceManager sm;
        const HWND hwnd = reinterpret_cast<HWND>(window.winId());
        const HMONITOR mon = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        QVERIFY(sm.assignWindow(hwnd, mon, 0));
        thumbs::invalidateWindow(hwnd);
        OverviewHost host(&sm);
        host.openAll();
        QVERIFY(host.isOpen());
        QVERIFY(!thumbs::cachedWindowShot(hwnd).isNull());
        host.forceHideAll();
        sm.untrackWindow(hwnd);
    }

    void monitorWorkersFinishBeforePanelsShow()
    {
        SpaceManager sm;
        OverviewHost host(&sm);
        spacelog::flush();
        QFile trace(spacelog::tracePath());
        QVERIFY(trace.open(QIODevice::ReadOnly | QIODevice::Text));
        trace.seek(trace.size());
        host.openAll();
        spacelog::flush();
        const QString log = QString::fromUtf8(trace.readAll());
        const int firstPanel = log.indexOf(QStringLiteral("overview panel open "));
        QVERIFY(firstPanel >= 0);
        int starts = 0, finishes = 0;
        QSet<QString> monitors;
        const QString guiThread = QStringLiteral("thread=%1 ").arg(::GetCurrentThreadId());
        for (const QString &line : log.split('\n')) {
            if (!line.contains(QStringLiteral("overview monitor worker ")))
                continue;
            QVERIFY(!line.contains(guiThread));
            QVERIFY(log.indexOf(line) < firstPanel);
            if (line.contains(QStringLiteral("phase=start")))
                ++starts;
            if (line.contains(QStringLiteral("phase=done"))) {
                ++finishes;
                const auto match =
                    QRegularExpression(QStringLiteral("mon=(0x[0-9a-f]+) ")).match(line);
                QVERIFY(match.hasMatch());
                monitors.insert(match.captured(1));
            }
        }
        QCOMPARE(starts, sm.monitors().size());
        QCOMPARE(finishes, starts);
        QCOMPARE(monitors.size(), starts);
        QCOMPARE(host.openPanelCount(), starts);
        host.forceHideAll();
        QVERIFY(thumbs::screenSamplingEnabled());
    }

    void windowsOnOneMonitorUseIndependentCaptureTasks()
    {
        QWidget first, second;
        first.resize(240, 160);
        second.resize(240, 160);
        first.setStyleSheet(QStringLiteral("background: white;"));
        second.setStyleSheet(QStringLiteral("background: yellow;"));
        first.show();
        second.show();
        QVERIFY(QTest::qWaitForWindowExposed(&first));
        QVERIFY(QTest::qWaitForWindowExposed(&second));
        SpaceManager sm;
        const HWND a = reinterpret_cast<HWND>(first.winId());
        const HWND b = reinterpret_cast<HWND>(second.winId());
        const HMONITOR mon = ::MonitorFromWindow(a, MONITOR_DEFAULTTONEAREST);
        QVERIFY(sm.assignWindow(a, mon, 0));
        QVERIFY(sm.assignWindow(b, mon, 0));
        thumbs::invalidateWindow(a);
        thumbs::invalidateWindow(b);
        spacelog::flush();
        QFile trace(spacelog::tracePath());
        QVERIFY(trace.open(QIODevice::ReadOnly | QIODevice::Text));
        trace.seek(trace.size());
        OverviewHost host(&sm);
        host.openAll();
        QVERIFY(host.isOpen());
        QVERIFY(!thumbs::cachedWindowShot(a).isNull());
        QVERIFY(!thumbs::cachedWindowShot(b).isNull());
        spacelog::flush();
        const QString log = QString::fromUtf8(trace.readAll());
        const int ready = log.indexOf(QStringLiteral("phase=allMonitorsPrepared "));
        QVERIFY(ready >= 0);
        for (const QString &field :
             {QStringLiteral("phase=pw.full.call.begin "),
              QStringLiteral("phase=pw.full.call.end "), QStringLiteral("phase=pw.pixelsCopied "),
              QStringLiteral("phase=pw.formatConverted "), QStringLiteral("target_pid="),
              QStringLiteral("target_tid="), QStringLiteral("tick_us="),
              QStringLiteral("queue_ms=")})
            QVERIFY2(log.contains(field), qPrintable(field));
        QSet<QString> completed;
        const QString guiThread = QStringLiteral("thread=%1 ").arg(::GetCurrentThreadId());
        for (const QString &line : log.split('\n')) {
            if (!line.contains(QStringLiteral("overview window worker ")))
                continue;
            QVERIFY(!line.contains(guiThread));
            QVERIFY(log.indexOf(line) < ready);
            if (line.contains(QStringLiteral("phase=done "))) {
                QVERIFY(line.contains(QStringLiteral("ok=1")));
                const auto match =
                    QRegularExpression(QStringLiteral("hwnd=(0x[0-9a-f]+) ")).match(line);
                QVERIFY(match.hasMatch());
                QVERIFY(!completed.contains(match.captured(1)));
                completed.insert(match.captured(1));
            }
        }
        QCOMPARE(completed.size(), 2);
        QVERIFY(completed.contains(QStringLiteral("0x%1").arg(quintptr(a), 0, 16)));
        QVERIFY(completed.contains(QStringLiteral("0x%1").arg(quintptr(b), 0, 16)));
        host.forceHideAll();
        sm.untrackWindow(a);
        sm.untrackWindow(b);
    }
};

QTEST_MAIN(TestOverviewStaging)
#include "test_overview_staging.moc"
