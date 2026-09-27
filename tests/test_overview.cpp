#include <QtTest>

#include "core/space/SpaceManager.h"
#include "ui/overview/OverviewWindow.h"
#include "ui/preview/SpaceCardWidget.h"

#include <Windows.h>

// Regression tests for the crash-on-click class of bugs:
// - empty card row + arrow keys (% 0)
// - re-open while closing (animation re-entry)
// - double close
class TestOverview : public QObject {
    Q_OBJECT
private slots:
    void nullManagerIsSafe()
    {
        OverviewWindow w(nullptr);
        w.openOnMonitor(nullptr);
        QVERIFY(!w.isOpen());
        w.closeOverview(true); // no-op
        QVERIFY(!w.isOpen());
    }

    void openCloseCycleOnPrimaryMonitor()
    {
        SpaceManager sm;
        OverviewWindow w(&sm);
        auto *m = sm.monitors().first();

        QSignalSpy closedSpy(&w, &OverviewWindow::closed);

        w.openOnMonitor(m->hmon);
        // Monitor lookup should succeed; open sets isOpen.
        if (w.cardCount() > 0 || w.isOpen()) {
            QVERIFY(w.isOpen());
            QCOMPARE(w.cardCount(), m->spaces.size());
        }

        if (w.isOpen()) {
            w.closeOverview(false);
            // closed() is emitted synchronously inside closeOverview();
            // wait() would block for a *second* signal. Poll count instead.
            QTRY_VERIFY_WITH_TIMEOUT(closedSpy.count() >= 1, 2000);
            QCOMPARE(closedSpy.first().at(0).toInt(), -1);
            QVERIFY(!w.isOpen());
            // dismiss animation should finish
            QTRY_VERIFY_WITH_TIMEOUT(!w.isVisible() || !w.isDismissing(), 3000);
        }
    }

    void doubleCloseIsSafe()
    {
        SpaceManager sm;
        OverviewWindow w(&sm);
        auto *m = sm.monitors().first();
        w.openOnMonitor(m->hmon);
        if (!w.isOpen())
            QSKIP("open failed in this environment");

        w.closeOverview(true);
        w.closeOverview(true); // second must be ignored
        w.closeOverview(false);
        QTRY_VERIFY_WITH_TIMEOUT(!w.isOpen() || w.isAnimating(), 500);
        // Drain animation.
        for (int i = 0; i < 50 && (w.isOpen() || w.isAnimating()); ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }

    void emptyArrowKeysDoNotCrash()
    {
        SpaceManager sm;
        OverviewWindow w(&sm);
        // Force empty-card keyboard path by not opening (m_open false is safe),
        // and also simulate open with zero spaces if we can clear them.
        auto *m = sm.monitors().first();
        const int savedCount = m->spaces.size();
        // Temporarily empty spaces to hit the n<=0 guard after rebuild.
        const auto saved = m->spaces;
        m->spaces.clear();

        w.openOnMonitor(m->hmon);
        // Even if open partially failed, send keys — must not crash.
        for (int key : {Qt::Key_Left, Qt::Key_Right, Qt::Key_Home, Qt::Key_End,
                        Qt::Key_1, Qt::Key_9, Qt::Key_Return, Qt::Key_Escape}) {
            QKeyEvent ev(QEvent::KeyPress, key, Qt::NoModifier);
            QApplication::sendEvent(&w, &ev);
        }
        for (int i = 0; i < 40; ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 15);

        m->spaces = saved;
        QCOMPARE(m->spaces.size(), savedCount);
        // Final close attempt.
        if (w.isOpen())
            w.closeOverview(false);
        for (int i = 0; i < 40; ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 15);
    }

    void keyboardNavigationWraps()
    {
        SpaceManager sm;
        OverviewWindow w(&sm);
        auto *m = sm.monitors().first();
        w.openOnMonitor(m->hmon);
        if (!w.isOpen() || w.cardCount() == 0)
            QSKIP("overview did not open with cards");

        const int n = w.cardCount();
        const int start = w.selectedIndex();

        QKeyEvent left(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
        QApplication::sendEvent(&w, &left);
        QCOMPARE(w.selectedIndex(), (start - 1 + n) % n);

        QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
        QApplication::sendEvent(&w, &right);
        QCOMPARE(w.selectedIndex(), start);

        w.closeOverview(false);
        for (int i = 0; i < 40 && w.isAnimating(); ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 15);
    }

    void rapidToggleDoesNotCrash()
    {
        SpaceManager sm;
        OverviewWindow w(&sm);
        auto *m = sm.monitors().first();

        for (int i = 0; i < 5; ++i) {
            w.openOnMonitor(m->hmon);
            for (int j = 0; j < 5; ++j)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            if (w.isOpen())
                w.closeOverview(i % 2 == 0);
            for (int j = 0; j < 8; ++j)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        // Drain
        for (int i = 0; i < 80 && (w.isOpen() || w.isAnimating()); ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QVERIFY(!w.isOpen());
    }
    // Regression: the enter animation captures card->pos() as its end point.
    // If the strip layout has not run yet, every card is still at (0,0) and
    // the stagger drives them all into one pile at the top-left corner.
    void cardsLayOutInARowAfterEnterAnimation()
    {
        SpaceManager sm;
        OverviewWindow w(&sm);
        auto *m = sm.monitors().first();
        if (m->spaces.size() < 2)
            QVERIFY(sm.addSpace(m->hmon));

        w.openOnMonitor(m->hmon);
        if (!w.isOpen() || w.cardCount() < 2)
            QSKIP("overview did not open with >= 2 cards");

        // Real wait so pending LayoutRequests + any animation settle for sure
        // (processEvents returns immediately when the queue is idle).
        QTest::qWait(600);
        for (int i = 0; i < 10; ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

        const auto cards = w.findChildren<SpaceCardWidget *>();
        QCOMPARE(int(cards.size()), w.cardCount());
        QVERIFY(cards.size() >= 2);

        QStringList overlaps;
        for (int i = 0; i < cards.size(); ++i) {
            if (cards[i]->pos() == QPoint(0, 0))
                overlaps << QStringLiteral("card %1 at (0,0)").arg(i);
            for (int j = i + 1; j < cards.size(); ++j) {
                if (cards[i]->geometry().intersects(cards[j]->geometry())) {
                    const QRect a = cards[i]->geometry();
                    const QRect b = cards[j]->geometry();
                    overlaps << QStringLiteral("cards %1,%2 overlap (%3,%4 %5x%6 vs %7,%8 %9x%10)")
                                    .arg(i)
                                    .arg(j)
                                    .arg(a.x())
                                    .arg(a.y())
                                    .arg(a.width())
                                    .arg(a.height())
                                    .arg(b.x())
                                    .arg(b.y())
                                    .arg(b.width())
                                    .arg(b.height());
                }
            }
        }
        QVERIFY2(overlaps.isEmpty(), qPrintable(overlaps.join(QLatin1Char('\n'))));

        w.closeOverview(false);
        for (int i = 0; i < 40 && (w.isOpen() || w.isAnimating()); ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 15);
    }

    // The mask is up for seconds while warm/build captures behind it — the
    // STALE card strip (old CURRENT pill) and window tiles must stay hidden
    // until populateOpenContent has rebuilt them.
    void staleStripsHiddenUntilPopulate()
    {
        SpaceManager sm;
        OverviewWindow w(&sm);
        auto *m = sm.monitors().first();
        if (m->spaces.size() < 2)
            QVERIFY(sm.addSpace(m->hmon));

        // First full open → cards exist (so the second open has something stale).
        w.openOnMonitor(m->hmon);
        if (!w.isOpen())
            QSKIP("overview did not open");
        QVERIFY(w.cardCount() >= 2);
        w.closeOverview(false);
        // Real waiting — a bare processEvents spin returns before the 140ms
        // exit fade finishes, leaving m_closePending set (begin would no-op).
        QTRY_VERIFY_WITH_TIMEOUT(!w.isOpen() && !w.isAnimating(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!w.isDismissing(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!w.isVisible(), 3000);

        // Second open, stepwise: begin = mask only, strips hidden …
        w.beginPanelOpen(m->hmon);
        QVERIFY(w.isOpen());
        auto *strip = w.findChild<QWidget *>(QStringLiteral("SpaceStripHost"));
        auto *scroll = w.findChild<QWidget *>(QStringLiteral("WindowScroll"));
        QVERIFY(strip && scroll);
        QVERIFY2(strip->isHidden(),
                 "stale card strip (old CURRENT pill) must be hidden during warm");
        QVERIFY2(scroll->isHidden(), "stale window tiles must be hidden during warm");

        // … populate = fresh cards, strips revealed.
        w.populateOpenContent();
        QVERIFY(w.cardCount() >= 2);
        QVERIFY(strip->isVisible());
        QVERIFY(scroll->isVisible());

        w.closeOverview(false);
        QTRY_VERIFY_WITH_TIMEOUT(!w.isOpen() && !w.isAnimating(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!w.isDismissing(), 3000);
    }
};

QTEST_MAIN(TestOverview)
#include "test_overview.moc"
