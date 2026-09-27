#include "core/capture/ThumbnailCapture.h"

#include "core/log/Log.h"

#include <QPainter>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <dwmapi.h>

#include <algorithm>

#pragma comment(lib, "dwmapi.lib")

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
    if (covered.isNull())
        return {};
    if (covered.width() == w && covered.height() == h)
        return covered;
    const int x = std::max(0, (covered.width() - w) / 2);
    const int y = std::max(0, (covered.height() - h) / 2);
    const int cw = std::min(w, covered.width());
    const int ch = std::min(h, covered.height());
    if (cw <= 0 || ch <= 0)
        return {};
    return covered.copy(x, y, cw, ch);
}

QString registryWallpaperPath()
{
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", 0, KEY_READ, &key) !=
        ERROR_SUCCESS)
        return {};
    wchar_t buf[MAX_PATH]{};
    DWORD size = DWORD(sizeof(buf) - sizeof(wchar_t));
    DWORD type = 0;
    const LSTATUS st =
        ::RegQueryValueExW(key, L"Wallpaper", nullptr, &type, reinterpret_cast<LPBYTE>(buf), &size);
    ::RegCloseKey(key);
    if (st != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || !buf[0])
        return {};
    return QString::fromWCharArray(buf);
}

QString transcodedWallpaperPath()
{
    // What Explorer actually paints: JPEG/PNG re-encode of HEIC/slideshow sources.
    const QString appData = QString::fromLocal8Bit(qgetenv("APPDATA"));
    if (appData.isEmpty())
        return {};
    return appData + QStringLiteral("/Microsoft/Windows/Themes/TranscodedWallpaper");
}

QImage tryLoadImagePath(const QString &path)
{
    if (path.isEmpty())
        return {};
    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile() || fi.size() <= 0)
        return {};
    QImage img(path);
    return img.isNull() ? QImage() : img;
}

QImage loadWallpaperImage()
{
    // 1) SPI — may be HEIC (often unloadable without a Qt HEIC plugin).
    wchar_t path[MAX_PATH]{};
    if (::SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, path, 0) && path[0]) {
        QImage img = tryLoadImagePath(QString::fromWCharArray(path));
        if (!img.isNull())
            return img;
    }
    // 2) Transcoded wallpaper — always a raster format Windows can display.
    QImage transcoded = tryLoadImagePath(transcodedWallpaperPath());
    if (!transcoded.isNull())
        return transcoded;
    // 3) Registry Wallpaper value (same as SPI in most cases, still try).
    return tryLoadImagePath(registryWallpaperPath());
}

} // namespace

namespace thumbs {

namespace {
// One shared full-window image per HWND. windowShot scales this on demand.
QHash<HWND, QImage> g_windowShots;
bool g_screenSampling = true; // false while the overview overlay is visible

QImage scaleToFit(const QImage &src, const QSize &maxSize)
{
    if (src.isNull())
        return {};
    if (!maxSize.isValid() || maxSize.isEmpty())
        return src;
    if (src.width() <= maxSize.width() && src.height() <= maxSize.height())
        return src;
    return src.scaled(maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

// PrintWindow often returns a solid black frame for cloaked / cross-adapter /
// some secondary-monitor windows. Only near-black uniform frames count as a
// failed shot — blank but light windows (white/gray STATIC) are valid captures.
bool looksLikeFailedShot(const QImage &img)
{
    if (img.isNull() || img.width() < 4 || img.height() < 4)
        return true;
    const QImage s = img.scaled(16, 16, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    // "Black frame" failure mode: every sampled pixel stays near zero.
    // Any real (even blank-white) content exceeds this ceiling immediately.
    for (int y = 0; y < s.height(); ++y) {
        for (int x = 0; x < s.width(); ++x) {
            const QRgb p = s.pixel(x, y);
            if (qRed(p) > 16 || qGreen(p) > 16 || qBlue(p) > 16)
                return false;
        }
    }
    return true;
}

// Screen BitBlt at the window's virtual-desktop rect (works across monitors).
QImage bitBltWindowFrame(const RECT &rc)
{
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0)
        return {};
    HDC screen = ::GetDC(nullptr);
    if (!screen)
        return {};
    HDC mem = ::CreateCompatibleDC(screen);
    void *bits = nullptr;
    HBITMAP bmp = nullptr;
    {
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        bmp = ::CreateDIBSection(screen, &bi, BI_RGB, &bits, nullptr, 0);
    }
    if (!mem || !bmp || !bits) {
        if (bmp)
            ::DeleteObject(bmp);
        if (mem)
            ::DeleteDC(mem);
        ::ReleaseDC(nullptr, screen);
        return {};
    }
    HGDIOBJ old = ::SelectObject(mem, bmp);
    const BOOL ok = ::BitBlt(mem, 0, 0, w, h, screen, rc.left, rc.top, SRCCOPY | CAPTUREBLT);
    QImage img;
    if (ok) {
        img = QImage(w, h, QImage::Format_ARGB32);
        memcpy(img.bits(), bits, size_t(w) * size_t(h) * 4);
        img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    ::SelectObject(mem, old);
    ::DeleteObject(bmp);
    ::DeleteDC(mem);
    ::ReleaseDC(nullptr, screen);
    return img;
}

// Single PrintWindow attempt into a FULL-window bitmap (PrintWindow draws
// 1:1 — a pre-shrunk DC only captures the top-left corner). The BOOL only
// says the call succeeded; composition/GPU windows often "succeed" with a
// black buffer, so the caller judges content via looksLikeFailedShot.
QImage printWindowImage(HWND hwnd, int fullW, int fullH, UINT flags)
{
    if (fullW <= 0 || fullH <= 0)
        return {};
    HDC screen = ::GetDC(nullptr);
    if (!screen)
        return {};
    HDC mem = ::CreateCompatibleDC(screen);
    void *bits = nullptr;
    HBITMAP bmp = nullptr;
    {
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = fullW;
        bi.bmiHeader.biHeight = -fullH; // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        bmp = ::CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    }
    if (!mem || !bmp || !bits) {
        if (bmp)
            ::DeleteObject(bmp);
        if (mem)
            ::DeleteDC(mem);
        ::ReleaseDC(nullptr, screen);
        return {};
    }
    HGDIOBJ old = ::SelectObject(mem, bmp);

    const BOOL ok = ::PrintWindow(hwnd, mem, flags);

    QImage img;
    if (ok && bits) {
        img = QImage(fullW, fullH, QImage::Format_ARGB32);
        memcpy(img.bits(), bits, size_t(fullW) * size_t(fullH) * 4);
        img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }

    ::SelectObject(mem, old);
    ::DeleteObject(bmp);
    ::DeleteDC(mem);
    ::ReleaseDC(nullptr, screen);
    return img;
}

// GetWindowDC of THIS hwnd (window surface, not the screen).
QImage windowDcImage(HWND hwnd, int fullW, int fullH)
{
    if (fullW <= 0 || fullH <= 0)
        return {};
    HDC wdc = ::GetWindowDC(hwnd);
    if (!wdc)
        return {};
    HDC mem = ::CreateCompatibleDC(wdc);
    void *bits = nullptr;
    HBITMAP bmp = nullptr;
    {
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = fullW;
        bi.bmiHeader.biHeight = -fullH;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        bmp = ::CreateDIBSection(wdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    }
    QImage img;
    if (mem && bmp && bits) {
        HGDIOBJ old = ::SelectObject(mem, bmp);
        if (::BitBlt(mem, 0, 0, fullW, fullH, wdc, 0, 0, SRCCOPY)) {
            img = QImage(fullW, fullH, QImage::Format_ARGB32);
            memcpy(img.bits(), bits, size_t(fullW) * size_t(fullH) * 4);
            img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        }
        ::SelectObject(mem, old);
    }
    if (bmp) ::DeleteObject(bmp);
    if (mem) ::DeleteDC(mem);
    ::ReleaseDC(hwnd, wdc);
    return img;
}

// Per-window cascade: PrintWindow (full content → plain) → GetWindowDC.
// Every step is rejected on a near-black frame — PrintWindow's BOOL stays
// TRUE for black buffers, so content decides, not the return value.
QImage perWindowImage(HWND hwnd, int fullW, int fullH)
{
    QImage img = printWindowImage(hwnd, fullW, fullH, PW_RENDERFULLCONTENT);
    if (!looksLikeFailedShot(img))
        return img;
    img = printWindowImage(hwnd, fullW, fullH, 0);
    if (!looksLikeFailedShot(img))
        return img;
    return windowDcImage(hwnd, fullW, fullH);
}

QString windowClassName(HWND hwnd)
{
    wchar_t buf[64]{};
    ::GetClassNameW(hwnd, buf, 64);
    return QString::fromWCharArray(buf);
}
} // namespace

QImage capture(HWND hwnd, const QSize &maxSize)
{
    if (!hwnd || !::IsWindow(hwnd))
        return {};
    if (::IsIconic(hwnd))
        return {};

    RECT rc{};
    if (!::GetWindowRect(hwnd, &rc))
        return {};
    const int fullW = rc.right - rc.left;
    const int fullH = rc.bottom - rc.top;
    if (fullW <= 0 || fullH <= 0)
        return {};

    QImage img = perWindowImage(hwnd, fullW, fullH);
    const bool perWindowBlack = looksLikeFailedShot(img);
    const bool sampleScreen = g_screenSampling && canSampleScreen(hwnd);
    if (perWindowBlack) {
        spacelog::warn(QStringLiteral(
            "per-window capture black (PrintWindow/GetWindowDC) hwnd=0x%1 class=%2 %3x%4 sampling=%5")
                            .arg(quintptr(hwnd), 0, 16)
                            .arg(windowClassName(hwnd))
                            .arg(fullW)
                            .arg(fullH)
                            .arg(sampleScreen ? 1 : 0));
    }

    // Screen BitBlt only when this rect really shows THIS window (visible,
    // not cloaked, on the virtual screen) and the overview mask is not up.
    if (perWindowBlack && sampleScreen) {
        const QImage alt = bitBltWindowFrame(rc);
        if (!alt.isNull() && !looksLikeFailedShot(alt))
            img = alt;
    }
    // Still no honest pixels — fail rather than paint overlay/other-space junk.
    if (img.isNull() || looksLikeFailedShot(img)) {
        spacelog::error(QStringLiteral(
            "per-window capture failed hwnd=0x%1 class=%2 %3x%4 cloaked=%5")
                            .arg(quintptr(hwnd), 0, 16)
                            .arg(windowClassName(hwnd))
                            .arg(fullW)
                            .arg(fullH)
                            .arg(canSampleScreen(hwnd) ? 0 : 1));
        return {};
    }

    // Crop to visible DWM frame — GetWindowRect includes invisible resize
    // borders that show up as empty margins in the preview tile.
    RECT vis{};
    if (SUCCEEDED(::DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &vis, sizeof(vis)))) {
        const int x = vis.left - rc.left;
        const int y = vis.top - rc.top;
        const int w = vis.right - vis.left;
        const int h = vis.bottom - vis.top;
        if (x >= 0 && y >= 0 && w > 0 && h > 0 && x + w <= img.width() && y + h <= img.height()) {
            img = img.copy(x, y, w, h);
        }
    }

    if (img.isNull())
        return {};
    if (maxSize.isValid() && (img.width() > maxSize.width() || img.height() > maxSize.height()))
        img = img.scaled(maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return img;
}

QImage windowShot(HWND hwnd, const QSize &maxSize)
{
    if (!hwnd || !::IsWindow(hwnd) || ::IsIconic(hwnd)) {
        if (hwnd)
            g_windowShots.remove(hwnd);
        return {};
    }
    auto it = g_windowShots.constFind(hwnd);
    if (it == g_windowShots.constEnd() || it->isNull()) {
        // Capture once at full window size (no maxSize) — shared by strip +
        // space composites for this open only (warm clears the cache first).
        QImage full = capture(hwnd, QSize());
        if (full.isNull())
            return {};
        g_windowShots.insert(hwnd, full);
        return scaleToFit(full, maxSize);
    }
    return scaleToFit(it.value(), maxSize);
}

void invalidateWindow(HWND hwnd)
{
    if (hwnd)
        g_windowShots.remove(hwnd);
}

void clearWindowCache()
{
    g_windowShots.clear();
}

int windowCacheCount()
{
    return int(g_windowShots.size());
}

bool canSampleScreen(HWND hwnd)
{
    if (!hwnd || !::IsWindow(hwnd) || !::IsWindowVisible(hwnd))
        return false;
    DWORD cloaked = 0;
    if (SUCCEEDED(::DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED,
                                          &cloaked, sizeof(cloaked)))
        && cloaked != 0) {
        return false;
    }
    // Off-screen rects (tray-restored at -32000, hidden across monitors)
    // have no honest pixels to sample.
    RECT wr{};
    if (!::GetWindowRect(hwnd, &wr))
        return false;
    const int l = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int t = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int r = l + ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int b = t + ::GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return wr.left < r && wr.right > l && wr.top < b && wr.bottom > t;
}

void setScreenSamplingEnabled(bool on)
{
    g_screenSampling = on;
}

bool screenSamplingEnabled()
{
    return g_screenSampling;
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
        if (bmp)
            ::DeleteObject(bmp);
        if (mem)
            ::DeleteDC(mem);
        ::ReleaseDC(nullptr, screen);
        return {};
    }
    HGDIOBJ old = ::SelectObject(mem, bmp);

    // CAPTUREBLT includes layered windows.
    const BOOL ok =
        ::BitBlt(mem, 0, 0, w, h, screen, physRect.left, physRect.top, SRCCOPY | CAPTUREBLT);
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
