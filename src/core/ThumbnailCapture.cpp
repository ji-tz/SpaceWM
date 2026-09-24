#include "ThumbnailCapture.h"

#include <QPainter>

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

namespace thumbs {

QImage capture(HWND hwnd, const QSize &maxSize)
{
    if (!hwnd || !::IsWindow(hwnd))
        return {};

    RECT rc{};
    if (!::GetWindowRect(hwnd, &rc))
        return {};
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0)
        return {};

    // Cap source size to keep capture cheap.
    const int capW = 960;
    const int capH = 540;
    if (w > capW || h > capH) {
        const double s = std::min(double(capW) / w, double(capH) / h);
        w = int(w * s);
        h = int(h * s);
    }

    HDC screen = ::GetDC(nullptr);
    HDC mem = ::CreateCompatibleDC(screen);
    HBITMAP bmp = ::CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = ::SelectObject(mem, bmp);

    // PW_RENDERFULLCONTENT captures layered/DWM content better than plain PrintWindow.
    const BOOL ok = ::PrintWindow(hwnd, mem, PW_RENDERFULLCONTENT | PW_CLIENTONLY);
    // Fallback if PrintWindow fails (some protected windows).
    if (!ok) {
        ::BitBlt(mem, 0, 0, w, h, screen, rc.left, rc.top, SRCCOPY);
    }

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    QVector<uchar> pixels(size_t(w) * size_t(h) * 4);
    ::GetDIBits(mem, bmp, 0, UINT(h), pixels.data(), &bi, DIB_RGB_COLORS);

    QImage img(w, h, QImage::Format_ARGB32);
    memcpy(img.bits(), pixels.data(), size_t(w) * size_t(h) * 4);

    ::SelectObject(mem, old);
    ::DeleteObject(bmp);
    ::DeleteDC(mem);
    ::ReleaseDC(nullptr, screen);

    // GDI is BGRA premultiplied-ish; convert to RGBA-safe ARGB32.
    img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    if (maxSize.isValid() && (img.width() > maxSize.width() || img.height() > maxSize.height()))
        img = img.scaled(maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    return img;
}

} // namespace thumbs
