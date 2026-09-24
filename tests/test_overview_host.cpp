#include <QtTest>

#include "core/space/SpaceManager.h"
#include "ui/overview/OverviewHost.h"
#include "ui/overview/OverviewWindow.h"

#include <Windows.h>

// Mission Control style multi-monitor overview host.
class TestOverviewHost : public QObject {
    Q_OBJECT
private slots:
    void nullManagerOpenIsNoop()
    {
        OverviewHost host(nullptr);
        host.openAll();
        QVERIFY(!host.isOpen());
        QCOMPARE(host.openPanelCount(), 0);
    }

    void openAllCoversEveryMonitor()
    {
        SpaceManager sm;
        OverviewHost host(&sm);
        const auto mons = sm.monitors();
        QVERIFY(!mons.isEmpty());

        host.openAll();
        QVERIFY(host.isOpen());
        QCOMPARE(host.openPanelCount(), mons.size());

        for (MonitorSpaces *m : mons) {
            OverviewWindow *panel = host.panelFor(m->hmon);
            QVERIFY(panel != nullptr);
            QVERIFY(panel->isOpen());
            QVERIFY(panel->isVisible());
            QCOMPARE(panel->targetMonitor(), m->hmon);
            QCOMPARE(panel->cardCount(), m->spaces.size());
        }

        QVERIFY(host.activePanel() != nullptr);
        QVERIFY(host.activePanel()->isOpen());

        host.closeAll(false);
        QTRY_VERIFY_WITH_TIMEOUT(!host.isOpen(), 3000);
    }

    void closeAllExitsEveryMonitorTogether()
    {
        SpaceManager sm;
        OverviewHost host(&sm);
        const auto mons = sm.monitors();
        QVERIFY(!mons.isEmpty());
        QSignalSpy closed(&host, &OverviewHost::allClosed);

        host.openAll();
        if (!host.isOpen())
            QSKIP("open failed in this environment");

        // Mark everyone closing on the same tick — must not leave stragglers.
        host.closeAll(false);

        // Immediately after cancel, no panel should still be isOpen().
        for (MonitorSpaces *m : mons) {
            OverviewWindow *panel = host.panelFor(m->hmon);
            QVERIFY(panel);
            QVERIFY(!panel->isOpen());
        }

        QTRY_VERIFY_WITH_TIMEOUT(closed.count() >= 1, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!host.isOpen(), 3000);
        QCOMPARE(host.openPanelCount(), 0);
        for (MonitorSpaces *m : mons) {
            OverviewWindow *panel = host.panelFor(m->hmon);
            QVERIFY(panel);
            QVERIFY(!panel->isVisible());
        }
        QVERIFY(!sm.overviewOpen());
    }

    void closeAllCancelEmitsAndClears()
    {
        SpaceManager sm;
        OverviewHost host(&sm);
        QSignalSpy closed(&host, &OverviewHost::allClosed);

        host.openAll();
        if (!host.isOpen())
            QSKIP("open failed in this environment");

        host.closeAll(false);
        QTRY_VERIFY_WITH_TIMEOUT(!host.isOpen(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(closed.count() >= 1, 3000);
        QCOMPARE(host.openPanelCount(), 0);
        QVERIFY(!sm.overviewOpen());
    }

    void doubleOpenIsIdempotent()
    {
        SpaceManager sm;
        OverviewHost host(&sm);
        host.openAll();
        const int n = host.openPanelCount();
        host.openAll(); // second must not double panels
        QCOMPARE(host.openPanelCount(), n);
        host.closeAll(false);
        QTRY_VERIFY_WITH_TIMEOUT(!host.isOpen(), 3000);
    }

    void spaceChosenSwitchesThatMonitorOnly()
    {
        SpaceManager sm;
        OverviewHost host(&sm);
        QObject::connect(&host, &OverviewHost::spaceChosen, &sm,
                         [&](quint64 hmon, int space) {
            sm.switchSpace(reinterpret_cast<HMONITOR>(hmon), space, false);
        });

        const auto mons = sm.monitors();
        QVERIFY(!mons.isEmpty());

        QSignalSpy chosen(&host, &OverviewHost::spaceChosen);
        QSignalSpy allDone(&host, &OverviewHost::allClosed);
        host.openAll();
        if (!host.isOpen())
            QSKIP("open failed");

        OverviewWindow *active = host.activePanel();
        QVERIFY(active);

        const int start = active->selectedIndex();
        QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
        QApplication::sendEvent(active, &right);
        const int selected = active->selectedIndex();
        QVERIFY(selected != start || active->cardCount() <= 1);

        active->closeOverview(true);
        QTRY_VERIFY_WITH_TIMEOUT(chosen.count() >= 1, 2000);
        const quint64 hmon = chosen.first().at(0).toULongLong();
        const int space = chosen.first().at(1).toInt();
        QCOMPARE(hmon, quint64(active->targetMonitor()));
        QCOMPARE(space, selected);

        auto *m = sm.monitorOf(active->targetMonitor());
        QVERIFY(m);
        QCOMPARE(m->currentIndex, selected);

        // ALL panels must leave together after commit.
        QTRY_VERIFY_WITH_TIMEOUT(allDone.count() >= 1, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!host.isOpen(), 3000);
        for (MonitorSpaces *mon : mons) {
            OverviewWindow *panel = host.panelFor(mon->hmon);
            QVERIFY(panel);
            QVERIFY(!panel->isVisible());
        }
    }

    void rapidToggleDoesNotCrash()
    {
        SpaceManager sm;
        OverviewHost host(&sm);
        for (int i = 0; i < 4; ++i) {
            host.openAll();
            for (int j = 0; j < 5; ++j)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            host.closeAll(false);
            for (int j = 0; j < 8; ++j)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        // Drain exit animations (force-hide safety is ~350ms).
        for (int i = 0; i < 100 && host.isOpen(); ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (host.isOpen())
            host.forceHideAll();
        for (int i = 0; i < 20; ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QVERIFY(!host.isOpen());
        QCOMPARE(host.openPanelCount(), 0);
    }
};

QTEST_MAIN(TestOverviewHost)
#include "test_overview_host.moc"
