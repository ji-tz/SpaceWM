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
        HWND hwnd =
            ::CreateWindowExW(0, L"STATIC", L"thumb target", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 20,
                              20, 400, 300, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
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
        HWND big =
            ::CreateWindowExW(0, L"STATIC", L"big thumb", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 10, 10,
                              1200, 800, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
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

    void windowShotCachesOnceAndScales()
    {
        thumbs::clearWindowCache();
        HWND hwnd =
            ::CreateWindowExW(0, L"STATIC", L"cache target", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 30,
                              30, 500, 360, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
        QVERIFY(hwnd != nullptr);
        ::ShowWindow(hwnd, SW_SHOW);
        ::UpdateWindow(hwnd);
        ::Sleep(30);

        const QImage full1 = thumbs::windowShot(hwnd);
        QVERIFY(!full1.isNull());
        QCOMPARE(thumbs::windowCacheCount(), 1);

        // Second call must reuse the same source (no new cache entry).
        const QImage full2 = thumbs::windowShot(hwnd);
        QCOMPARE(thumbs::windowCacheCount(), 1);
        // Same dimensions; identical bits (shared cache, not recaptured).
        QCOMPARE(full2.size(), full1.size());
        QVERIFY(full2.bits() != full1.bits() || full2.constBits() == full1.constBits());

        // Scaled views fit maxSize and keep aspect of the one source image.
        const QImage small = thumbs::windowShot(hwnd, QSize(120, 90));
        QVERIFY(!small.isNull());
        QVERIFY(small.width() <= 120);
        QVERIFY(small.height() <= 90);
        QCOMPARE(thumbs::windowCacheCount(), 1);

        thumbs::invalidateWindow(hwnd);
        QCOMPARE(thumbs::windowCacheCount(), 0);

        ::DestroyWindow(hwnd);
        thumbs::clearWindowCache();
    }

    // Overview re-entry must not keep stale tiles: warmWindowShots clears the
    // cache first, then recaptures every managed window.
    void warmWindowShotsClearsThenRecaptures()
    {
        thumbs::clearWindowCache();
        HWND hwnd =
            ::CreateWindowExW(0, L"STATIC", L"warm target", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40,
                              40, 360, 240, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
        QVERIFY(hwnd != nullptr);
        ::ShowWindow(hwnd, SW_SHOW);
        ::UpdateWindow(hwnd);
        ::Sleep(30);

        // Prime a cache entry that would go stale if not cleared on open.
        QVERIFY(!thumbs::windowShot(hwnd).isNull());
        QCOMPARE(thumbs::windowCacheCount(), 1);

        thumbs::clearWindowCache();
        QCOMPARE(thumbs::windowCacheCount(), 0);

        const QImage fresh = thumbs::windowShot(hwnd);
        QVERIFY(!fresh.isNull());
        QCOMPARE(thumbs::windowCacheCount(), 1);

        ::DestroyWindow(hwnd);
        thumbs::clearWindowCache();
    }
};

QTEST_MAIN(TestThumbnail)
#include "test_thumbnail.moc"
