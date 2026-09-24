#include <QtTest>

#include "ui/SwitchFlashOverlay.h"

class TestFlashOverlay : public QObject {
    Q_OBJECT
private slots:
    void emptyGeometryIsIgnored()
    {
        SwitchFlashOverlay o;
        o.play(QRect(), 1);
        QVERIFY(!o.isPlaying());
    }

    void playsAndFinishes()
    {
        SwitchFlashOverlay o;
        o.play(QRect(0, 0, 800, 600), 1);
        QVERIFY(o.isPlaying());

        // ~18 frames * 16ms ≈ 300ms; allow generous CI slack.
        QTRY_VERIFY_WITH_TIMEOUT(!o.isPlaying(), 3000);
    }

    void rapidReplayRestartsWithoutStacking()
    {
        SwitchFlashOverlay o;
        o.play(QRect(0, 0, 640, 480), 1);
        o.play(QRect(0, 0, 640, 480), -1);
        o.play(QRect(0, 0, 640, 480), 0);
        QVERIFY(o.isPlaying());
        QTRY_VERIFY_WITH_TIMEOUT(!o.isPlaying(), 3000);
    }

    void negativeDirectionWorks()
    {
        SwitchFlashOverlay o;
        o.play(QRect(100, 100, 400, 300), -1);
        QVERIFY(o.isPlaying());
        QTRY_VERIFY_WITH_TIMEOUT(!o.isPlaying(), 3000);
    }
};

QTEST_MAIN(TestFlashOverlay)
#include "test_flash_overlay.moc"
