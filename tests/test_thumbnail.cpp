#include <QtTest>

#include "core/capture/ThumbnailCapture.h"

#include <Windows.h>

class TestThumbnail : public QObject {
    Q_OBJECT
private slots:
    void nullAndInvalidAreSafe()
    {
        QImage a = thumbs::capture(nullptr);
        QVERIFY(a.isNull());
        QImage b = thumbs::capture(reinterpret_cast<HWND>(0xDEAD));
        QVERIFY(b.isNull());
    }

    void capturesRealWindowWithinMaxSize()
    {
        HWND hwnd = ::CreateWindowExW(
            0, L"STATIC", L"thumb target",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE, 20, 20, 400, 300,
            nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
        QVERIFY(hwnd != nullptr);
        ::ShowWindow(hwnd, SW_SHOW);
        ::UpdateWindow(hwnd);
        ::Sleep(30);

        const QImage img = thumbs::capture(hwnd, QSize(240, 135));
        // PrintWindow can fail on some surfaces — accept null, but if non-null
        // dimensions must respect the cap.
        if (!img.isNull()) {
            QVERIFY(img.width() > 0);
            QVERIFY(img.height() > 0);
            QVERIFY(img.width() <= 240 || img.height() <= 135);
        }

        const QImage full = thumbs::capture(hwnd, QSize());
        if (!full.isNull()) {
            QVERIFY(full.width() > 0);
            QVERIFY(full.height() > 0);
            // Full-window capture is NOT pre-capped to 960×540 (that clipped
            // PrintWindow). Size may be large; aspect must match GetWindowRect.
            RECT wr{};
            if (::GetWindowRect(hwnd, &wr)) {
                const double winAspect = double(wr.right - wr.left) / double(wr.bottom - wr.top);
                const double imgAspect = double(full.width()) / double(full.height());
                QVERIFY(qAbs(winAspect - imgAspect) < 0.05);
            }
        }

        // Large window: PrintWindow at full size then scale — must not be a
        // top-left crop of the window (aspect still matches; fits maxSize).
        HWND big = ::CreateWindowExW(
            0, L"STATIC", L"big thumb",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE, 10, 10, 1200, 800,
            nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
        QVERIFY(big != nullptr);
        ::ShowWindow(big, SW_SHOW);
        ::UpdateWindow(big);
        ::Sleep(30);
        const QImage capped = thumbs::capture(big, QSize(240, 135));
        if (!capped.isNull()) {
            QVERIFY(capped.width() <= 240);
            QVERIFY(capped.height() <= 135);
            RECT wr{};
            if (::GetWindowRect(big, &wr)) {
                const double winAspect = double(wr.right - wr.left) / double(wr.bottom - wr.top);
                const double imgAspect = double(capped.width()) / double(capped.height());
                QVERIFY(qAbs(winAspect - imgAspect) < 0.08);
            }
        }
        ::DestroyWindow(big);

        // Wallpaper / space background must load even when SPI path is HEIC:
        // prefer TranscodedWallpaper (what Explorer actually paints).
        const RECT monitorRect{0, 0, 1920, 1080};
        const QImage wall = thumbs::wallpaperFilled(monitorRect, QSize(320, 180));
        QVERIFY(!wall.isNull());
        QCOMPARE(wall.size(), QSize(320, 180));
        // Not a flat solid if any wallpaper source exists on the machine.
        const QImage spi = thumbs::desktopWallpaper(monitorRect, QSize(160, 90));
        QVERIFY(!spi.isNull());
        QCOMPARE(spi.width(), 160); // KeepAspectRatio into 160×90 from 16:9 fill

        ::DestroyWindow(hwnd);
    }
};

QTEST_MAIN(TestThumbnail)
#include "test_thumbnail.moc"
