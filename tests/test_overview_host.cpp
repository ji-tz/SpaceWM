#include <QtTest>

#include "core/space/SpaceManager.h"
#include "core/window/CloakController.h"
#include "ui/overview/OverviewHost.h"
#include "ui/overview/OverviewWindow.h"
#include "ui/preview/SpaceCardWidget.h"

#include <QLabel>

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
        QObject::connect(&host, &OverviewHost::spaceChosen, &sm, [&](quint64 hmon, int space) {
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

    // Closing must recloak BEFORE the mask starts fading: warm uncloaks every
    // managed window behind the overview, and recloak used to wait for
    // setOverviewOpen(false) (after the panels were gone) — off-space windows
    // flashed on the desktop.
    void exitRecloaksWhileMaskStillVisible()
    {
        SpaceManager sm;
        OverviewHost host(&sm);
        auto *m = sm.monitors().first();
        while (sm.spaceCount(m->hmon) < 2)
            QVERIFY(sm.addSpace(m->hmon));

        HWND hwnd = ::CreateWindowExW(0, L"STATIC", L"exit recloak target",
                                      WS_OVERLAPPEDWINDOW | WS_VISIBLE, 90, 90, 280, 170, nullptr,
                                      nullptr, ::GetModuleHandleW(nullptr), nullptr);
        QVERIFY(hwnd != nullptr);
        QVERIFY(sm.assignWindow(hwnd, m->hmon, 1)); // other space → must hide on close
        ::cloak::set(hwnd, false);
        QTRY_VERIFY_WITH_TIMEOUT(!cloak::isCloaked(hwnd), 1500);

        host.openAll();
        if (!host.isOpen()) {
            sm.untrackWindow(hwnd);
            ::DestroyWindow(hwnd);
            QSKIP("open failed in this environment");
        }

        host.closeAll(false); // cancel → prepareClose → startExit → recloakNow
        QTRY_VERIFY_WITH_TIMEOUT(cloak::isCloaked(hwnd), 800);
        OverviewWindow *panel = host.panelFor(m->hmon);
        QVERIFY(panel);
        QVERIFY2(panel->isVisible(), "cloak must land while the mask still covers the desktop");

        QTRY_VERIFY_WITH_TIMEOUT(!host.isOpen(), 4000);
        // No switch happened (cancel) — the other-space window stays hidden.
        QVERIFY(cloak::isCloaked(hwnd));

        sm.untrackWindow(hwnd);
        ::cloak::showAllHidden();
        ::DestroyWindow(hwnd);
    }

    // CURRENT pill sync timing: clicking a space must (a) switch the model
    // and flip the pill immediately (spaceChanged → refreshCardBadges) while
    // the exit animation is still running, and (b) the NEXT open must build
    // the pill on the clicked card — not the previous current.
    void currentPillFollowsClickImmediately()
    {
        SpaceManager sm;
        OverviewHost host(&sm);
        // Multi-head: the panel opens on the CURSOR display — give every
        // monitor a second space and anchor assertions to the OPEN panel.
        for (MonitorSpaces *mon : sm.monitors())
            while (sm.spaceCount(mon->hmon) < 2)
                QVERIFY(sm.addSpace(mon->hmon));

        host.openAll();
        if (!host.isOpen())
            QSKIP("open failed in this environment");
        OverviewWindow *panel = host.activePanel();
        QVERIFY(panel);
        auto *m = sm.monitorOf(panel->targetMonitor());
        QVERIFY(m);
        QCOMPARE(m->spaces.size(), 2);

        // Same wiring as main.cpp (spaceChosen → switchSpace).
        connect(&host, &OverviewHost::spaceChosen, &sm, [&sm](quint64 hmon, int space) {
            sm.switchSpace(reinterpret_cast<HMONITOR>(hmon), space,
                           /*animateHint=*/false);
        });

        flushDeferredDeletes(panel);
        auto cards = panel->findChildren<SpaceCardWidget *>();
        QCOMPARE(cards.size(), 2);

        // Click the second card (press+release, within drag distance).
        const QPointF c(cards[1]->rect().center());
        const QPointF g = cards[1]->mapToGlobal(c);
        QMouseEvent press(QEvent::MouseButtonPress, c, g, g, Qt::LeftButton, Qt::LeftButton,
                          Qt::NoModifier);
        QApplication::sendEvent(cards[1], &press);
        QMouseEvent release(
            QEvent::MouseButtonRelease, c + QPointF(1, 0), cards[1]->mapToGlobal(c + QPointF(1, 0)),
            cards[1]->mapToGlobal(c + QPointF(1, 0)), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(cards[1], &release);

        // (a) model switched synchronously inside the click dispatch …
        QCOMPARE(m->currentIndex, 1);
        // … and the pill flipped while the panel is still on screen.
        QTRY_VERIFY_WITH_TIMEOUT(currentPillCard(cards) == cards[1], 1000);

        QTRY_VERIFY_WITH_TIMEOUT(!host.isOpen(), 4000);

        // (b) reopen: pill must be built directly on the clicked card.
        host.openAll();
        QVERIFY(host.isOpen());
        flushDeferredDeletes(panel);
        cards = panel->findChildren<SpaceCardWidget *>();
        QCOMPARE(cards.size(), 2);
        QVERIFY2(currentPillCard(cards) == cards[1],
                 "reopen must show CURRENT on the space clicked last time");

        host.closeAll(false);
        QTRY_VERIFY_WITH_TIMEOUT(!host.isOpen(), 4000);
    }

  private:
    static void flushDeferredDeletes(OverviewWindow *panel)
    {
        Q_UNUSED(panel)
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }

    // The card whose badge label shows "CURRENT" (nullptr if none).
    static SpaceCardWidget *currentPillCard(const QVector<SpaceCardWidget *> &cards)
    {
        for (SpaceCardWidget *card : cards) {
            for (QLabel *label : card->findChildren<QLabel *>()) {
                if (label->text() == QLatin1String("CURRENT"))
                    return card;
            }
        }
        return nullptr;
    }
};

QTEST_MAIN(TestOverviewHost)
#include "test_overview_host.moc"
