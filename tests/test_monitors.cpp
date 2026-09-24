#include <QtTest>

#include "core/monitor/MonitorInfo.h"

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
        // contains() works in physical coordinates (physRect).
        const RECT &pr = list.first().physRect;
        POINT pt{ (pr.left + pr.right) / 2, (pr.top + pr.bottom) / 2 };
        QVERIFY(monitors::contains(list.first(), pt));

        POINT outside{ pr.left - 10000, pr.top - 10000 };
        QVERIFY(!monitors::contains(list.first(), outside));
    }

    void logicalGeometryPositive()
    {
        const auto list = monitors::enumerate();
        QVERIFY(!list.isEmpty());
        for (const auto &m : list) {
            QVERIFY(m.geometry.width() > 0);
            QVERIFY(m.geometry.height() > 0);
            QVERIFY(m.physRect.right > m.physRect.left);
        }
    }
};

QTEST_MAIN(TestMonitors)
#include "test_monitors.moc"
