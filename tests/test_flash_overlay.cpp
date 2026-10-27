#include <QtTest>
#include "ui/effects/SwitchFlashOverlay.h"
#include "core/space/SpaceManager.h"
#include "core/capture/ThumbnailCapture.h"

class TestFlashOverlay : public QObject {
    Q_OBJECT
  private slots:
    void coverHoldsSourceUntilShotsAreReady()
    {
        SwitchFlashOverlay overlay;
        const QRect geometry(0, 0, 400, 300);
        const RECT physical{0, 0, 400, 300};
        QImage source(400, 300, QImage::Format_RGB32);
        source.fill(Qt::red);
        QImage destination = source;
        destination.fill(Qt::blue);
        overlay.play(geometry, physical, {source, destination}, 0, 1, 1);
        QTest::qWait(300); // the previous clock must not affect the next first frame
        overlay.cover(geometry, physical, source);
        QTest::qWait(600); // slow capture must neither move nor expire the cover
        QVERIFY(overlay.isPlaying());
        QVERIFY(overlay.isVisible());
        QCOMPARE(overlay.grab().toImage().pixelColor(200, 150), QColor(Qt::red));
        QImage rebuiltSource = source;
        rebuiltSource.fill(Qt::green);
        overlay.play(geometry, physical, {rebuiltSource, destination}, 0, 1, 1);
        // Window composites prepared behind the cover cannot replace the
        // original screen copy used for the outgoing animation tile.
        QCOMPARE(overlay.grab().toImage().pixelColor(200, 150), QColor(Qt::red));
        QTRY_VERIFY_WITH_TIMEOUT(!overlay.isPlaying(), 3000);

        overlay.cover(geometry, physical, {}); // cold cache is still opaque
        QCOMPARE(overlay.grab().toImage().pixelColor(200, 150), QColor(18, 20, 28));
        overlay.stop();
        QVERIFY(!overlay.isVisible());
    }
    void stripShrinksSlidesAndExpands_data()
    {
        QTest::addColumn<int>("from");
        QTest::addColumn<int>("to");
        QTest::addColumn<int>("direction");
        QTest::newRow("right") << 0 << 1 << 1;
        QTest::newRow("left") << 2 << 1 << -1;
        QTest::newRow("wrap-right") << 2 << 0 << 1;
        QTest::newRow("wrap-left") << 0 << 2 << -1;
    }
    void stripShrinksSlidesAndExpands()
    {
        QFETCH(int, from);
        QFETCH(int, to);
        QFETCH(int, direction);
        const QSize view(1000, 600);
        auto tile = [&](int i, qreal t) {
            return SwitchFlashOverlay::tileRect(view, i, from, to, 3, direction, t);
        };
        QCOMPARE(tile(from, 0), QRectF(0, 0, 1000, 600));
        QCOMPARE(tile(from, .25).width(), 880.0);
        QCOMPARE(tile(from, .25).center(), QPointF(500, 300));
        const qreal sourceMove = tile(from, .5).x() - tile(from, .25).x();
        QVERIFY(direction > 0 ? sourceMove < 0 : sourceMove > 0);
        QCOMPARE(tile(to, .75).center(), QPointF(500, 300));
        QCOMPARE(tile(to, 1), QRectF(0, 0, 1000, 600));
    }
    void invalidAndReplay()
    {
        SwitchFlashOverlay overlay;
        QImage red(400, 300, QImage::Format_RGB32);
        red.fill(Qt::red);
        QImage blue = red;
        blue.fill(Qt::blue);
        const QVector<QImage> shots{red, blue};
        const RECT physical{0, 0, 400, 300};
        overlay.play({}, physical, shots, 0, 1, 1);
        QVERIFY(!overlay.isPlaying());
        overlay.play(QRect(0, 0, 400, 300), physical, shots, 0, 0, 1);
        QVERIFY(!overlay.isPlaying());
        overlay.play(QRect(0, 0, 400, 300), physical, shots, 0, 1, 1);
        QVERIFY(overlay.isPlaying());
        QCOMPARE(overlay.grab().toImage().pixelColor(200, 150), QColor(Qt::red));
        overlay.play(QRect(0, 0, 400, 300), physical, shots, 1, 0, -1);
        QSignalSpy finished(&overlay, &SwitchFlashOverlay::finished);
        QTRY_VERIFY_WITH_TIMEOUT(!overlay.isPlaying(), 3000);
        QCOMPARE(finished.count(), 1);
        QVERIFY(!overlay.isVisible());
    }
};
QTEST_MAIN(TestFlashOverlay)
#include "test_flash_overlay.moc"
