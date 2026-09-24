#include <QtTest>

#include "core/SpaceManager.h"
#include "ui/OverviewWindow.h"

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
};

QTEST_MAIN(TestOverview)
#include "test_overview.moc"
