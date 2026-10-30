#include <QtTest>
#include "ui/overview/WinPreviewEvents.h"
class TestWinPreviewEvents : public QObject {
    Q_OBJECT
private slots:
    void tabBeforeReleaseOpens() {
        WinPreviewEvents queue;
        QCOMPARE(queue.push(WinPreviewEvents::Event::WinDown), WinPreviewEvents::Result::Prepare);
        queue.push(WinPreviewEvents::Event::TabDown);
        queue.push(WinPreviewEvents::Event::WinUp);
        QCOMPARE(queue.complete(), WinPreviewEvents::Result::Open);
        QVERIFY(!queue.active());
        QCOMPARE(queue.complete(), WinPreviewEvents::Result::None);
        QCOMPARE(queue.push(WinPreviewEvents::Event::WinUp), WinPreviewEvents::Result::None);
    }
    void releaseBeforeTabDiscards() {
        WinPreviewEvents queue;
        queue.push(WinPreviewEvents::Event::WinDown);
        queue.push(WinPreviewEvents::Event::WinUp);
        queue.push(WinPreviewEvents::Event::TabDown);
        QCOMPARE(queue.complete(), WinPreviewEvents::Result::Discard);
        QVERIFY(!queue.active());
    }
    void preparedWaitsForNextEvent() {
        WinPreviewEvents queue;
        QCOMPARE(queue.push(WinPreviewEvents::Event::TabDown), WinPreviewEvents::Result::None);
        queue.push(WinPreviewEvents::Event::WinDown);
        QCOMPARE(queue.complete(), WinPreviewEvents::Result::None);
        QVERIFY(queue.active());
        queue.push(WinPreviewEvents::Event::TabDown);
        QCOMPARE(queue.complete(), WinPreviewEvents::Result::Open);
        QCOMPARE(queue.push(WinPreviewEvents::Event::WinDown), WinPreviewEvents::Result::Prepare);
        queue.push(WinPreviewEvents::Event::WinUp);
        QCOMPARE(queue.complete(), WinPreviewEvents::Result::Discard);
    }
};
QTEST_GUILESS_MAIN(TestWinPreviewEvents)
#include "test_win_preview_events.moc"
