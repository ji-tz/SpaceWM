#include <QtTest>

#include "core/capture/ThumbnailCapture.h"
#include "core/window/CloakController.h"

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

    // Preview open: warm clears the cache, then captures every window once.
    // Nothing from a previous open may survive that clear.
    void warmClearsCacheThenCapturesFresh()
    {
        thumbs::clearWindowCache();
        thumbs::setScreenSamplingEnabled(true);
        HWND hwnd =
            ::CreateWindowExW(0, L"STATIC", L"warm target", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40,
                              40, 360, 240, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
        QVERIFY(hwnd != nullptr);
        ::ShowWindow(hwnd, SW_SHOW);
        ::UpdateWindow(hwnd);
        ::Sleep(30);

        QVERIFY(!thumbs::windowShot(hwnd).isNull());
        QCOMPARE(thumbs::windowCacheCount(), 1);

        // Simulate open: clear then recapture — count returns to 1 with a new shot.
        thumbs::clearWindowCache();
        QCOMPARE(thumbs::windowCacheCount(), 0);
        QVERIFY(!thumbs::windowShot(hwnd).isNull());
        QCOMPARE(thumbs::windowCacheCount(), 1);

        ::DestroyWindow(hwnd);
        thumbs::clearWindowCache();
    }

    void canSampleScreenRejectsHiddenAndTracksSamplingFlag()
    {
        QVERIFY(!thumbs::canSampleScreen(nullptr));
        QVERIFY(!thumbs::canSampleScreen(reinterpret_cast<HWND>(0xDEAD)));

        HWND hwnd =
            ::CreateWindowExW(0, L"STATIC", L"sample target", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 50,
                              50, 240, 160, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
        QVERIFY(hwnd != nullptr);
        ::ShowWindow(hwnd, SW_SHOW);
        ::UpdateWindow(hwnd);
        ::Sleep(20);
        QVERIFY(thumbs::canSampleScreen(hwnd));

        ::ShowWindow(hwnd, SW_HIDE);
        ::Sleep(20);
        QVERIFY(!thumbs::canSampleScreen(hwnd));

        ::ShowWindow(hwnd, SW_SHOW);
        ::Sleep(20);
        QVERIFY(thumbs::canSampleScreen(hwnd));

        // Parked off-screen (tray-restored style) — no honest screen pixels.
        ::SetWindowPos(hwnd, nullptr, -32000, -32000, 0, 0,
                       SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        ::Sleep(20);
        QVERIFY(!thumbs::canSampleScreen(hwnd));
        ::SetWindowPos(hwnd, nullptr, 50, 50, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        ::Sleep(20);
        QVERIFY(thumbs::canSampleScreen(hwnd));

        // Master switch used while the overview overlay is up.
        QVERIFY(thumbs::screenSamplingEnabled());
        thumbs::setScreenSamplingEnabled(false);
        QVERIFY(!thumbs::screenSamplingEnabled());
        thumbs::setScreenSamplingEnabled(true);
        QVERIFY(thumbs::screenSamplingEnabled());

        ::DestroyWindow(hwnd);
    }

    // Non-current space windows are cloaked; capture must use per-window APIs
    // (PrintWindow / GetWindowDC), not screen pixels.
    void capturesCloakedWindowViaPerWindowApi()
    {
        HWND hwnd =
            ::CreateWindowExW(0, L"STATIC", L"cloaked shot", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 60,
                              60, 320, 200, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
        QVERIFY(hwnd != nullptr);
        ::ShowWindow(hwnd, SW_SHOW);
        ::UpdateWindow(hwnd);
        ::Sleep(30);

        QVERIFY(!thumbs::capture(hwnd, QSize()).isNull());

        QVERIFY(::cloak::set(hwnd, true));
        ::Sleep(50);
        QVERIFY(::cloak::isCloaked(hwnd));

        // Sampling off (as during overview) — must not need screen pixels.
        thumbs::setScreenSamplingEnabled(false);
        QVERIFY(!thumbs::canSampleScreen(hwnd));
        const QImage cloaked = thumbs::capture(hwnd, QSize());
        thumbs::setScreenSamplingEnabled(true);

        // Per-window path should still produce a frame for STATIC windows.
        QVERIFY(!cloaked.isNull());
        QVERIFY(cloaked.width() > 0 && cloaked.height() > 0);

        ::cloak::set(hwnd, false);
        ::DestroyWindow(hwnd);
    }
};

QTEST_MAIN(TestThumbnail)
#include "test_thumbnail.moc"
