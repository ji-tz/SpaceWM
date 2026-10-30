#include <QtTest>

#include "core/space/SpaceManager.h"
#include "ui/overview/OverviewWindow.h"
#include "ui/preview/SpaceCardWidget.h"
#include "ui/preview/WindowPreviewWidget.h"

#include <QDragEnterEvent>
#include <QMimeData>
#include <QScrollArea>
#include <QVariantAnimation>

namespace {
void movePointer(QWidget *widget, const QPoint &position)
{
    // Exercise the widget's normal event path even on a locked host desktop.
    QMouseEvent move(QEvent::MouseMove, position, widget->mapToGlobal(position), Qt::NoButton,
                     Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(widget, &move);
}
} // namespace

class TestOverviewSpaces : public QObject {
    Q_OBJECT
  private slots:
    void currentWindowsFirstAndSpacesExpandOnDemand()
    {
        QWidget target;
        target.resize(800, 500);
        target.show();
        const HWND hwnd = reinterpret_cast<HWND>(target.winId());
        SpaceManager sm;
        const HMONITOR mon = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        QVERIFY(sm.assignWindow(hwnd, mon, 0));
        QVERIFY(sm.addSpace(mon));
        OverviewWindow panel(&sm);
        QCursor::setPos(target.mapToGlobal(QPoint(400, 400)));
        panel.openOnMonitor(mon);
        QTRY_VERIFY(!panel.isAnimating());
        movePointer(&panel, QPoint(panel.width() / 2, panel.height() - 50));
        QTRY_VERIFY(!panel.isSpaceStripExpanded());
        QCOMPARE(panel.selectedIndex(), 0);
        QCOMPARE(panel.windowPreviewCount(), 1);
        QCOMPARE(panel.windowPreviewHandle(0), hwnd);
        auto *strip = panel.findChild<QWidget *>(QStringLiteral("SpaceStripHost"));
        auto *scroll = panel.findChild<QScrollArea *>(QStringLiteral("WindowScroll"));
        QVERIFY(strip && scroll);
        const int compactHeight = strip->height();
        const int windowHeight = scroll->height();
        QVERIFY(compactHeight <= 60);
        auto *tile = panel.findChild<WindowPreviewWidget *>();
        QVERIFY(tile);
        QTRY_VERIFY(qAbs(tile->mapTo(scroll->viewport(), tile->rect().center()).x() -
                         scroll->viewport()->rect().center().x()) < 4);
        QTRY_VERIFY(qAbs(tile->mapTo(scroll->viewport(), tile->rect().center()).y() -
                         scroll->viewport()->rect().center().y()) < 4);
        for (auto *card : panel.findChildren<SpaceCardWidget *>())
            QVERIFY(!card->isPreviewVisible());

        // Enter via the actual top margin, including the gap above the cards.
        movePointer(&panel, QPoint(panel.width() / 2, 2));
        QTRY_VERIFY(panel.isSpaceStripExpanded());
        QTRY_VERIFY(strip->height() > compactHeight);
        QTRY_VERIFY(scroll->height() < windowHeight);
        QTRY_VERIFY([&]() {
            const auto cards = panel.findChildren<SpaceCardWidget *>();
            return cards.size() == 2 && cards[0]->y() == cards[1]->y() &&
                   cards[0]->geometry().bottom() < strip->height();
        }());
        for (auto *card : panel.findChildren<SpaceCardWidget *>())
            QTRY_VERIFY(card->isPreviewVisible());
        QCOMPARE(sm.currentSpaceIndex(mon), 0);

        // Once expanded, the strip stays expanded after the pointer leaves.
        movePointer(scroll->viewport(), QPoint(10, scroll->viewport()->height() - 20));
        QTest::qWait(500);
        QVERIFY(panel.isSpaceStripExpanded());
        QVERIFY(strip->height() > compactHeight);
        panel.forceHide();
        sm.untrackWindow(hwnd);
    }

    void keyboardExpandsAndReopenStartsCompact()
    {
        SpaceManager sm;
        const HMONITOR mon = sm.monitors().first()->hmon;
        QVERIFY(sm.addSpace(mon));
        OverviewWindow panel(&sm);
        panel.openOnMonitor(mon);
        QTRY_VERIFY(!panel.isAnimating());
        QTest::keyClick(&panel, Qt::Key_Right);
        QVERIFY(panel.isSpaceStripExpanded());
        QCOMPARE(panel.selectedIndex(), 1);
        QCOMPARE(sm.currentSpaceIndex(mon), 0);
        panel.forceHide();
        panel.beginPanelOpen(mon);
        panel.populateOpenContent();
        QVERIFY(!panel.isSpaceStripExpanded());
        QCOMPARE(panel.selectedIndex(), 0);
        panel.forceHide();
    }

    void stripAnimatesExpansionAndExitCollapse()
    {
        SpaceManager sm;
        const HMONITOR mon = sm.monitors().first()->hmon;
        OverviewWindow panel(&sm);
        panel.openOnMonitor(mon);
        QTRY_VERIFY(!panel.isAnimating());
        auto *card = panel.findChild<SpaceCardWidget *>();
        QVERIFY(card);
        const int compact = card->height();
        QTest::keyClick(&panel, Qt::Key_Right);
        auto *animation = panel.findChild<QVariantAnimation *>(QStringLiteral("SpaceStripAnimation"));
        QVERIFY(animation);
        animation->setCurrentTime(110);
        const int middle = card->height();
        animation->setCurrentTime(220);
        const int expanded = card->height();
        QVERIFY(middle > compact && middle < expanded);
        panel.prepareClose();
        panel.startExit();
        animation = panel.findChild<QVariantAnimation *>(QStringLiteral("SpaceStripAnimation"));
        QVERIFY(animation);
        animation->setCurrentTime(70);
        QVERIFY(card->height() < expanded && card->height() > compact);
        panel.forceHide();
    }

    void draggingIntoCollapsedStripExpandsTargets()
    {
        SpaceManager sm;
        const HMONITOR mon = sm.monitors().first()->hmon;
        OverviewWindow panel(&sm);
        panel.openOnMonitor(mon);
        QTRY_VERIFY(!panel.isAnimating());
        auto *strip = panel.findChild<QWidget *>(QStringLiteral("SpaceStripHost"));
        QVERIFY(strip);
        QMimeData unrelated;
        unrelated.setText(QStringLiteral("text"));
        QDragEnterEvent ignored(QPoint(2, 2), Qt::MoveAction, &unrelated, Qt::LeftButton,
                                Qt::NoModifier);
        QApplication::sendEvent(strip, &ignored);
        QVERIFY(!ignored.isAccepted());
        QVERIFY(!panel.isSpaceStripExpanded());
        QMimeData mime;
        QByteArray payload;
        QDataStream stream(&payload, QIODevice::WriteOnly);
        stream << quint64(0x1234);
        mime.setData(WindowPreviewWidget::kMimeType, payload);
        QDragEnterEvent enter(QPoint(2, 2), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(strip, &enter);
        QVERIFY(enter.isAccepted());
        QVERIFY(panel.isSpaceStripExpanded());
        panel.forceHide();
    }

    void pointerOverStripExpandsOnlyAfterEntry()
    {
        SpaceManager sm;
        const HMONITOR mon = sm.monitors().first()->hmon;
        OverviewWindow panel(&sm);
        const QPoint oldCursor = QCursor::pos();
        panel.beginPanelOpen(mon);
        panel.populateOpenContent();
        auto *strip = panel.findChild<QWidget *>(QStringLiteral("SpaceStripHost"));
        QVERIFY(strip);
        QCursor::setPos(strip->mapToGlobal(strip->rect().center()));
        QEvent enter(QEvent::Enter);
        QApplication::sendEvent(strip, &enter);
        QVERIFY(!panel.isSpaceStripExpanded());
        QVERIFY(panel.isAnimating());
        QVERIFY(!panel.isSpaceStripExpanded());
        if (!strip->rect().contains(strip->mapFromGlobal(QCursor::pos()))) {
            panel.forceHide();
            QCursor::setPos(oldCursor);
            QSKIP("The desktop does not allow positioning the cursor over the strip");
        }
        QTRY_VERIFY(!panel.isAnimating());
        QTRY_VERIFY(panel.isSpaceStripExpanded());
        panel.forceHide();
        QCursor::setPos(oldCursor);
    }
};

QTEST_MAIN(TestOverviewSpaces)
#include "test_overview_spaces.moc"
