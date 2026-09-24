#include "ThumbnailCapture.h"

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
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0)
        return {};

    const int capW = 960;
    const int capH = 540;
    if (w > capW || h > capH) {
        const double s = std::min(double(capW) / w, double(capH) / h);
        w = int(w * s);
        h = int(h * s);
    }
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

    const BOOL ok = ::PrintWindow(hwnd, mem, PW_RENDERFULLCONTENT | PW_CLIENTONLY);
    if (!ok)
        ::BitBlt(mem, 0, 0, w, h, screen, rc.left, rc.top, SRCCOPY);

    QImage img = gdiToImage(mem, bmp, w, h);

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
    wchar_t path[MAX_PATH]{};
    if (::SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, path, 0) && path[0]) {
        QImage img(QString::fromWCharArray(path));
        if (!img.isNull()) {
            const int w = physRect.right - physRect.left;
            const int h = physRect.bottom - physRect.top;
            if (w > 0 && h > 0)
                img = img.scaled(w, h, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            if (maxSize.isValid() && (img.width() > maxSize.width() || img.height() > maxSize.height()))
                img = img.scaled(maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            return img;
        }
    }
    // Fallback: solid desktop-like fill so cards never show "No preview".
    const int w = maxSize.width() > 0 ? maxSize.width() : 640;
    const int h = maxSize.height() > 0 ? maxSize.height() : 360;
    QImage flat(w, h, QImage::Format_ARGB32_Premultiplied);
    flat.fill(QColor(32, 36, 48));
    return flat;
}

QImage spacePreview(const RECT &physRect, const QSize &maxSize)
{
    QImage shot = captureMonitor(physRect, maxSize);
    if (!shot.isNull())
        return shot;
    return desktopWallpaper(physRect, maxSize);
}

} // namespace thumbs
