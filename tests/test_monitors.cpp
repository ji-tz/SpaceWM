#include <QtTest>

#include "core/MonitorInfo.h"

class TestMonitors : public QObject {
    Q_OBJECT
private slots:
    void enumerateFindsAtLeastOne()
    {
        const auto list = monitors::enumerate();
        QVERIFY(!list.isEmpty());
        QVERIFY(list.first().primary || list.size() > 0);
        for (const auto &m : list) {
            QVERIFY(m.handle != nullptr);
            QVERIFY(m.geometry.width() > 0);
            QVERIFY(m.geometry.height() > 0);
        }
    }

    void primaryIsFirst()
    {
        const auto list = monitors::enumerate();
        QVERIFY(!list.isEmpty());
        // Sorting puts primary first when present.
        int primaryCount = 0;
        for (const auto &m : list)
            if (m.primary)
                ++primaryCount;
        QVERIFY(primaryCount <= 1);
        if (primaryCount == 1)
            QVERIFY(list.first().primary);
    }

    void fromCursorIsNonNull()
    {
        HMONITOR h = monitors::fromCursor();
        QVERIFY(h != nullptr);
    }

    void containsIsConsistent()
    {
        const auto list = monitors::enumerate();
        QVERIFY(!list.isEmpty());
        POINT pt{ list.first().geometry.center().x(), list.first().geometry.center().y() };
        QVERIFY(monitors::contains(list.first(), pt));

        // A point far outside every monitor should not be contained in the first
        // (unless virtual desktop is huge — use a corner just outside its rect).
        POINT outside{ list.first().geometry.x() - 10000, list.first().geometry.y() - 10000 };
        QVERIFY(!monitors::contains(list.first(), outside));
    }
};

QTEST_MAIN(TestMonitors)
#include "test_monitors.moc"
