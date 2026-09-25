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
        POINT pt{(pr.left + pr.right) / 2, (pr.top + pr.bottom) / 2};
        QVERIFY(monitors::contains(list.first(), pt));

        POINT outside{pr.left - 10000, pr.top - 10000};
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

    void toLogicalSizeUsesPerMonitorDpi()
    {
        // 100% DPI: identity.
        QCOMPARE(monitors::toLogicalSize(1920, 1080, 96, 96), QSize(1920, 1080));
        // 150% DPI: physical 1920 → logical 1280.
        QCOMPARE(monitors::toLogicalSize(1920, 1080, 144, 144), QSize(1280, 720));
        // 200% DPI.
        QCOMPARE(monitors::toLogicalSize(1920, 1080, 192, 192), QSize(960, 540));
        // Degenerate.
        QCOMPARE(monitors::toLogicalSize(0, 100, 96, 96), QSize());
    }

    void scaleFactorMatchesDpi()
    {
        const auto list = monitors::enumerate();
        QVERIFY(!list.isEmpty());
        for (const auto &m : list) {
            const double s = monitors::scaleFactor(m.handle);
            QVERIFY(s > 0.0);
            // Physical monitor size / logical geometry ≈ scale factor.
            const double physW = m.physRect.right - m.physRect.left;
            const double logW = m.geometry.width();
            if (logW > 0)
                QVERIFY(qAbs(physW / logW - s) < 0.05);
        }
    }

    void logicalWindowSizeNotLargerThanPhysical()
    {
        HWND hwnd =
            ::CreateWindowExW(0, L"STATIC", L"dpi size", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 10, 10,
                              400, 300, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
        QVERIFY(hwnd != nullptr);
        ::ShowWindow(hwnd, SW_SHOW);
        ::Sleep(20);

        const QSize logical = monitors::logicalWindowSize(hwnd);
        QVERIFY(logical.width() > 0);
        RECT wr{};
        QVERIFY(::GetWindowRect(hwnd, &wr));
        // At any DPI ≥ 96, logical ≤ physical.
        QVERIFY(logical.width() <= wr.right - wr.left);
        QVERIFY(logical.height() <= wr.bottom - wr.top);

        ::DestroyWindow(hwnd);
    }

    void workAreaFitsInsideFullGeometry()
    {
        const auto list = monitors::enumerate();
        QVERIFY(!list.isEmpty());
        for (const auto &m : list) {
            const QRect work = monitors::logicalWorkArea(m.handle);
            QVERIFY(!work.isEmpty());
            QVERIFY(work.width() > 0);
            QVERIFY(work.height() > 0);
            // Work area never larger than the full monitor.
            QVERIFY(work.width() <= m.geometry.width() + 1);
            QVERIFY(work.height() <= m.geometry.height() + 1);
            QVERIFY(m.geometry.contains(work.topLeft()));
        }
    }
};

QTEST_MAIN(TestMonitors)
#include "test_monitors.moc"
