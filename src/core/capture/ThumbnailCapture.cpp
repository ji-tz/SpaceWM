#include "core/capture/ThumbnailCapture.h"

#include <QPainter>
#include <QFileInfo>

#include <algorithm>

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

#ifndef SPI_GETDESKWALLPAPER
#define SPI_GETDESKWALLPAPER 0x0073
#endif

namespace {

QImage gdiToImage(HDC hdc, HBITMAP bmp, int w, int h)
{
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    QVector<uchar> pixels(size_t(w) * size_t(h) * 4);
    const int lines = ::GetDIBits(hdc, bmp, 0, UINT(h), pixels.data(), &bi, DIB_RGB_COLORS);
    if (lines <= 0)
        return {};

    QImage img(w, h, QImage::Format_ARGB32);
    memcpy(img.bits(), pixels.data(), size_t(w) * size_t(h) * 4);
    return img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

// Center-crop (or pass through) so wallpaper fills exactly w×h without stretching.
QImage fillExact(QImage img, int w, int h)
{
    if (img.isNull() || w <= 0 || h <= 0)
        return {};
    if (img.width() == w && img.height() == h)
        return img;
    QImage covered = img.scaled(w, h, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    if (covered.width() == w && covered.height() == h)
        return covered;
    const int x = std::max(0, (covered.width() - w) / 2);
    const int y = std::max(0, (covered.height() - h) / 2);
    return covered.copy(x, y, std::min(w, covered.width()), std::min(h, covered.height()));
}

QImage loadWallpaperImage()
{
    wchar_t path[MAX_PATH]{};
    if (::SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, path, 0) && path[0]) {
        QImage img(QString::fromWCharArray(path));
        if (!img.isNull())
            return img;
    }
    return {};
}

} // namespace

namespace thumbs {

QImage capture(HWND hwnd, const QSize &maxSize)
{
    if (!hwnd || !::IsWindow(hwnd))
        return {};
    if (::IsIconic(hwnd))
        return {};

    RECT rc{};
    if (!::GetWindowRect(hwnd, &rc))
        return {};
    // Always render into a FULL-window bitmap. PrintWindow draws 1:1 into the DC;
    // a pre-shrunk DC only captures the top-left corner (clipped / wrong content).
    const int fullW = rc.right - rc.left;
    const int fullH = rc.bottom - rc.top;
    if (fullW <= 0 || fullH <= 0)
        return {};

    HDC screen = ::GetDC(nullptr);
    if (!screen)
        return {};
    HDC mem = ::CreateCompatibleDC(screen);
    HBITMAP bmp = ::CreateCompatibleBitmap(screen, fullW, fullH);
    if (!mem || !bmp) {
        if (bmp) ::DeleteObject(bmp);
        if (mem) ::DeleteDC(mem);
        ::ReleaseDC(nullptr, screen);
        return {};
    }
    HGDIOBJ old = ::SelectObject(mem, bmp);

    const BOOL ok = ::PrintWindow(hwnd, mem, PW_RENDERFULLCONTENT);
    if (!ok)
        ::BitBlt(mem, 0, 0, fullW, fullH, screen, rc.left, rc.top, SRCCOPY);

    QImage img = gdiToImage(mem, bmp, fullW, fullH);

    ::SelectObject(mem, old);
    ::DeleteObject(bmp);
    ::DeleteDC(mem);
    ::ReleaseDC(nullptr, screen);

    if (img.isNull())
        return {};
    if (maxSize.isValid() && (img.width() > maxSize.width() || img.height() > maxSize.height()))
        img = img.scaled(maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return img;
}

QImage captureMonitor(const RECT &physRect, const QSize &maxSize)
{
    const int w = physRect.right - physRect.left;
    const int h = physRect.bottom - physRect.top;
    if (w <= 0 || h <= 0)
        return {};

    HDC screen = ::GetDC(nullptr);
    if (!screen)
        return {};
    HDC mem = ::CreateCompatibleDC(screen);
    HBITMAP bmp = ::CreateCompatibleBitmap(screen, w, h);
    if (!mem || !bmp) {
        if (bmp) ::DeleteObject(bmp);
        if (mem) ::DeleteDC(mem);
        ::ReleaseDC(nullptr, screen);
        return {};
    }
    HGDIOBJ old = ::SelectObject(mem, bmp);

    // CAPTUREBLT includes layered windows.
    const BOOL ok = ::BitBlt(mem, 0, 0, w, h, screen,
                             physRect.left, physRect.top, SRCCOPY | CAPTUREBLT);
    QImage img;
    if (ok)
        img = gdiToImage(mem, bmp, w, h);

    ::SelectObject(mem, old);
    ::DeleteObject(bmp);
    ::DeleteDC(mem);
    ::ReleaseDC(nullptr, screen);

    if (img.isNull())
        return {};
    if (maxSize.isValid() && (img.width() > maxSize.width() || img.height() > maxSize.height()))
        img = img.scaled(maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return img;
}

QImage desktopWallpaper(const RECT &physRect, const QSize &maxSize)
{
    QImage img = loadWallpaperImage();
    if (!img.isNull()) {
        const int w = physRect.right - physRect.left;
        const int h = physRect.bottom - physRect.top;
        if (w > 0 && h > 0)
            img = fillExact(img, w, h);
        if (maxSize.isValid() && (img.width() > maxSize.width() || img.height() > maxSize.height()))
            img = img.scaled(maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        return img;
    }
    // Fallback: solid desktop-like fill so cards never show "No preview".
    const int w = maxSize.width() > 0 ? maxSize.width() : 640;
    const int h = maxSize.height() > 0 ? maxSize.height() : 360;
    QImage flat(w, h, QImage::Format_ARGB32_Premultiplied);
    flat.fill(QColor(32, 36, 48));
    return flat;
}

// Wallpaper scaled to EXACTly canvas size. physRect reserved for future
// per-monitor wallpaper APIs; size comes from canvas (monitor-aspect).
QImage wallpaperFilled(const RECT &physRect, const QSize &canvas)
{
    Q_UNUSED(physRect);
    const int w = canvas.width() > 0 ? canvas.width() : 640;
    const int h = canvas.height() > 0 ? canvas.height() : 360;
    QImage img = loadWallpaperImage();
    if (!img.isNull())
        return fillExact(img, w, h);
    QImage flat(w, h, QImage::Format_ARGB32_Premultiplied);
    flat.fill(QColor(32, 36, 48));
    return flat;
}

QImage spacePreview(const RECT &physRect, const QSize &maxSize)
{
    return wallpaperFilled(physRect, maxSize.isValid() ? maxSize : QSize(640, 360));
}

} // namespace thumbs
