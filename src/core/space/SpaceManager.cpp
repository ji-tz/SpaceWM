#include "SpaceManager.h"

#include "core/window/CloakController.h"
#include "core/capture/ThumbnailCapture.h"
#include "core/window/WindowTracker.h"

#include <QCoreApplication>
#include <QColor>
#include <QCursor>
#include <QPainter>
#include <QPoint>

#include <algorithm>
#include <cmath>

namespace {
QString defaultSpaceName(int index)
{
    return QCoreApplication::translate("SpaceManager", "Space %1").arg(index + 1);
}
} // namespace

SpaceManager::SpaceManager(QObject *parent)
    : QObject(parent)
{
    refreshMonitors();
}

void SpaceManager::refreshMonitors()
{
    const auto list = monitors::enumerate();
    QHash<quintptr, MonitorSpaces> next;

    for (const auto &m : list) {
        const auto key = reinterpret_cast<quintptr>(m.handle);
        MonitorSpaces ms;
        if (m_monitors.contains(key)) {
            ms = m_monitors.take(key);
        } else {
            ms.spaces.resize(m_defaultSpaceCount);
            for (int i = 0; i < ms.spaces.size(); ++i)
                ms.spaces[i].name = defaultSpaceName(i);
            ms.currentIndex = 0;
        }
        ms.hmon = m.handle;
        ms.physRect = m.physRect;
        ms.geometry = m.geometry;
        ms.deviceName = m.deviceName;
        if (ms.currentIndex < 0 || ms.currentIndex >= ms.spaces.size())
            ms.currentIndex = 0;
        next.insert(key, std::move(ms));
    }

    m_monitors = std::move(next);
    for (auto it = m_owner.begin(); it != m_owner.end();) {
        if (!m_monitors.contains(it.value().hmon))
            it = m_owner.erase(it);
        else
            ++it;
    }

    for (auto it = m_monitors.begin(); it != m_monitors.end(); ++it)
        applyVisibility(it.value().hmon);

    seedScreenshots();

    emit monitorLayoutChanged();
}

QVector<MonitorSpaces *> SpaceManager::monitors()
{
    QVector<MonitorSpaces *> out;
    out.reserve(int(m_monitors.size()));
    for (auto it = m_monitors.begin(); it != m_monitors.end(); ++it)
        out.push_back(&it.value());
    std::sort(out.begin(), out.end(), [](MonitorSpaces *a, MonitorSpaces *b) {
        if (a->geometry.x() != b->geometry.x())
            return a->geometry.x() < b->geometry.x();
        return a->geometry.y() < b->geometry.y();
    });
    return out;
}

MonitorSpaces *SpaceManager::monitorOf(HMONITOR hmon)
{
    const auto key = reinterpret_cast<quintptr>(hmon);
    auto it = m_monitors.find(key);
    return it == m_monitors.end() ? nullptr : &it.value();
}

MonitorSpaces *SpaceManager::monitorAt(const QPoint &globalPos)
{
    POINT pt{globalPos.x(), globalPos.y()};
    HMONITOR h = monitors::fromPoint(pt);
    return monitorOf(h);
}

MonitorSpaces *SpaceManager::primaryMonitor()
{
    for (auto *m : monitors())
        if (!m->geometry.isEmpty())
            return m;
    return m_monitors.isEmpty() ? nullptr : &*m_monitors.begin();
}

MonitorSpaces *SpaceManager::monitorFromCursor()
{
    return monitorOf(monitors::fromCursor());
}

int SpaceManager::spaceCount(HMONITOR hmon) const
{
    auto *self = const_cast<SpaceManager *>(this);
    if (auto *m = self->monitorOf(hmon))
        return int(m->spaces.size());
    return 0;
}

int SpaceManager::currentSpaceIndex(HMONITOR hmon) const
{
    auto *self = const_cast<SpaceManager *>(this);
    if (auto *m = self->monitorOf(hmon))
        return m->currentIndex;
    return 0;
}

QString SpaceManager::spaceName(HMONITOR hmon, int index) const
{
    auto *self = const_cast<SpaceManager *>(this);
    if (auto *m = self->monitorOf(hmon); m && index >= 0 && index < m->spaces.size())
        return m->spaces[index].name;
    return {};
}

void SpaceManager::captureSpaceScreenshot(HMONITOR hmon, int index)
{
    // Space previews are render-only (wallpaper + window composite). BitBlt is
    // never used for cards: overview would capture itself, and hidden spaces
    // have no on-screen pixels to sample.
    rebuildSpaceScreenshot(hmon, index);
}

void SpaceManager::seedScreenshots()
{
    // Cheap: only fill gaps. Full batch is buildAllSpacePreviews() on open.
    for (auto it = m_monitors.begin(); it != m_monitors.end(); ++it) {
        MonitorSpaces &m = it.value();
        const int w = m.physRect.right - m.physRect.left;
        const int h = m.physRect.bottom - m.physRect.top;
        if (w <= 0 || h <= 0)
            continue;

        for (int i = 0; i < m.spaces.size(); ++i) {
            if (m.spaces[i].screenshot.isNull())
                rebuildSpaceScreenshot(m.hmon, i);
        }
    }
}

void SpaceManager::buildAllSpacePreviews()
{
    for (auto it = m_monitors.begin(); it != m_monitors.end(); ++it) {
        MonitorSpaces &m = it.value();
        const int w = m.physRect.right - m.physRect.left;
        const int h = m.physRect.bottom - m.physRect.top;
        if (w <= 0 || h <= 0)
            continue;
        for (int i = 0; i < m.spaces.size(); ++i)
            rebuildSpaceScreenshot(m.hmon, i);
    }
}

void SpaceManager::warmWindowShots()
{
    for (auto it = m_monitors.begin(); it != m_monitors.end(); ++it) {
        const MonitorSpaces &m = it.value();
        for (const Space &sp : m.spaces) {
            for (HWND hwnd : sp.windows) {
                if (::IsWindow(hwnd) && !::IsIconic(hwnd))
                    thumbs::windowShot(hwnd); // capture once into cache
            }
        }
    }
}

bool SpaceManager::switchSpace(HMONITOR hmon, int index, bool animateHint)
{
    auto *m = monitorOf(hmon);
    if (!m || index < 0 || index >= m->spaces.size() || index == m->currentIndex)
        return false;

    const int from = m->currentIndex;
    if (!m_overviewOpen)
        captureSpaceScreenshot(hmon, from);

    m->currentIndex = index;
    applyVisibility(hmon);

    if (!m_overviewOpen)
        captureSpaceScreenshot(hmon, index);

    emit spaceChanged(reinterpret_cast<quint64>(hmon), index);
    if (animateHint && m_animationEnabled && !m_overviewOpen)
        emit requestSwitchAnimation(reinterpret_cast<quint64>(hmon), from, index);
    return true;
}

bool SpaceManager::previewSpace(HMONITOR hmon, int index)
{
    auto *m = monitorOf(hmon);
    if (!m || index < 0 || index >= m->spaces.size())
        return false;

    // Always set current + re-cloak so hover/drop stays in sync with the real desktop.
    const bool changed = (m->currentIndex != index);
    m->currentIndex = index;
    applyVisibility(hmon);

    // Rendered composite (wallpaper + windows in Z-order) — never BitBlt.
    rebuildSpaceScreenshot(hmon, index);

    if (changed)
        emit spaceChanged(reinterpret_cast<quint64>(hmon), index);
    return true;
}

void SpaceManager::rebuildSpaceScreenshot(HMONITOR hmon, int index)
{
    auto *m = monitorOf(hmon);
    if (!m || index < 0 || index >= m->spaces.size())
        return;
    const int monW = m->physRect.right - m->physRect.left;
    const int monH = m->physRect.bottom - m->physRect.top;
    if (monW <= 0 || monH <= 0)
        return;

    Space &sp = m->spaces[index];

    // Canvas matches monitor aspect, fitted into 640×360.
    const double monAspect = double(monW) / double(monH);
    int cw = 640;
    int ch = int(std::lround(cw / monAspect));
    if (ch > 360 || ch <= 0) {
        ch = 360;
        cw = int(std::lround(ch * monAspect));
        if (cw <= 0)
            cw = 640;
    }

    // Background: wallpaper (or solid) filling the ENTIRE canvas — no letterbox gaps.
    QImage canvas = thumbs::wallpaperFilled(m->physRect, QSize(cw, ch));
    if (canvas.isNull() || canvas.width() != cw || canvas.height() != ch) {
        canvas = QImage(cw, ch, QImage::Format_ARGB32_Premultiplied);
        canvas.fill(QColor(32, 36, 48));
    }

    const double sx = double(cw) / monW;
    const double sy = double(ch) / monH;

    // Z-order: EnumWindows is top → bottom; paint bottom first so top stays on top.
    QVector<HWND> ordered;
    ordered.reserve(int(sp.windows.size()));
    struct Ctx {
        const QSet<HWND> *set;
        QVector<HWND> *out;
    } ctx{&sp.windows, &ordered};
    ::EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        auto *c = reinterpret_cast<Ctx *>(lp);
        if (c->set->contains(hwnd))
            c->out->push_back(hwnd);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    for (HWND hwnd : sp.windows) {
        if (!ordered.contains(hwnd))
            ordered.push_back(hwnd);
    }
    sp.zOrder = ordered; // cache top → bottom

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    for (int i = ordered.size() - 1; i >= 0; --i) {
        const HWND hwnd = ordered[i];
        if (!::IsWindow(hwnd) || ::IsIconic(hwnd))
            continue;
        RECT wr{};
        if (!::GetWindowRect(hwnd, &wr))
            continue;
        const int ww = wr.right - wr.left;
        const int wh = wr.bottom - wr.top;
        if (ww <= 0 || wh <= 0)
            continue;
        const int dw = qMax(1, int(std::lround(ww * sx)));
        const int dh = qMax(1, int(std::lround(wh * sy)));
        // Shared per-window cache — same image as the bottom strip tiles.
        QImage shot = thumbs::windowShot(hwnd, QSize(dw, dh));
        if (shot.isNull())
            continue;
        if (shot.width() != dw || shot.height() != dh)
            shot = shot.scaled(dw, dh, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        const int x = int(std::lround((wr.left - m->physRect.left) * sx));
        const int y = int(std::lround((wr.top - m->physRect.top) * sy));
        painter.drawImage(QPoint(x, y), shot);
    }
    painter.end();
    sp.screenshot = canvas;
    emit spacePreviewInvalidated(reinterpret_cast<quint64>(hmon), index);
}

void SpaceManager::refreshWindowAfterUpdate(HWND hwnd)
{
    if (!hwnd || !m_owner.contains(hwnd))
        return;
    // Window may have been captured mid-create; drop stale shot.
    thumbs::invalidateWindow(hwnd);
    const Owner o = m_owner.value(hwnd);
    rebuildSpaceScreenshot(reinterpret_cast<HMONITOR>(o.hmon), o.space);
    emit windowTracked(reinterpret_cast<quint64>(hwnd));
}

bool SpaceManager::addSpace(HMONITOR hmon)
{
    auto *m = monitorOf(hmon);
    if (!m)
        return false;
    Space sp;
    sp.name = defaultSpaceName(m->spaces.size());
    m->spaces.push_back(std::move(sp));
    const int idx = m->spaces.size() - 1;
    rebuildSpaceScreenshot(hmon, idx);
    emit monitorLayoutChanged();
    return true;
}

bool SpaceManager::removeSpace(HMONITOR hmon, int index)
{
    auto *m = monitorOf(hmon);
    if (!m || index < 0 || index >= m->spaces.size())
        return false;
    if (m->spaces.size() <= 1)
        return false; // keep at least one space

    // Merge into previous space; index 0 has no previous → use next.
    const int dest = (index > 0) ? index - 1 : 1;
    if (dest == index || dest < 0 || dest >= m->spaces.size())
        return false;

    Space &removed = m->spaces[index];
    Space &keep = m->spaces[dest];

    // Move every window into the keep space (update owners).
    for (HWND hwnd : removed.windows) {
        m_owner.insert(hwnd, Owner{reinterpret_cast<quintptr>(hmon), dest});
        keep.windows.insert(hwnd);
    }
    removed.windows.clear();
    removed.zOrder.clear();

    m->spaces.remove(index);

    // Fix currentIndex after removal.
    if (m->currentIndex == index)
        m->currentIndex = dest;
    else if (m->currentIndex > index)
        --m->currentIndex;
    if (m->currentIndex < 0 || m->currentIndex >= m->spaces.size())
        m->currentIndex = 0;

    applyVisibility(hmon);
    rebuildSpaceScreenshot(hmon, dest);
    emit spaceChanged(reinterpret_cast<quint64>(hmon), m->currentIndex);
    emit monitorLayoutChanged();
    return true;
}

bool SpaceManager::spaceHasWindows(HMONITOR hmon, int spaceIndex) const
{
    auto *self = const_cast<SpaceManager *>(this);
    auto *m = self->monitorOf(hmon);
    if (!m || spaceIndex < 0 || spaceIndex >= m->spaces.size())
        return false;
    for (HWND hwnd : m->spaces[spaceIndex].windows) {
        if (::IsWindow(hwnd) && !::IsIconic(hwnd))
            return true;
    }
    return false;
}

bool SpaceManager::moveSpace(HMONITOR hmon, int from, int to)
{
    auto *m = monitorOf(hmon);
    if (!m)
        return false;
    const int n = m->spaces.size();
    if (from < 0 || from >= n || to < 0 || to >= n || from == to)
        return false;

    Space sp = m->spaces.takeAt(from);
    m->spaces.insert(to, std::move(sp));

    // Remap currentIndex for the shift.
    if (m->currentIndex == from)
        m->currentIndex = to;
    else if (from < m->currentIndex && to >= m->currentIndex)
        --m->currentIndex;
    else if (from > m->currentIndex && to <= m->currentIndex)
        ++m->currentIndex;
    m->currentIndex = std::clamp(m->currentIndex, 0, n - 1);

    // Owner indices are absolute — rebuild from the new order.
    for (int i = 0; i < m->spaces.size(); ++i) {
        for (HWND hwnd : m->spaces[i].windows)
            m_owner.insert(hwnd, Owner{reinterpret_cast<quintptr>(hmon), i});
    }

    applyVisibility(hmon);
    emit spaceChanged(reinterpret_cast<quint64>(hmon), m->currentIndex);
    emit monitorLayoutChanged();
    return true;
}

bool SpaceManager::assignWindow(HWND hwnd, HMONITOR hmon, int spaceIndex)
{
    auto *m = monitorOf(hmon);
    if (!m || spaceIndex < 0 || spaceIndex >= m->spaces.size() || !hwnd)
        return false;

    // Leave previous space if any (refresh vacated card later).
    int prevSpace = -1;
    HMONITOR prevMon = nullptr;
    if (m_owner.contains(hwnd)) {
        const auto prev = m_owner.value(hwnd);
        if (auto *pm = monitorOf(reinterpret_cast<HMONITOR>(prev.hmon));
            pm && prev.space >= 0 && prev.space < pm->spaces.size()) {
            prevSpace = prev.space;
            prevMon = pm->hmon;
            pm->spaces[prev.space].windows.remove(hwnd);
        }
    }

    m->spaces[spaceIndex].windows.insert(hwnd);
    m_owner.insert(hwnd, Owner{reinterpret_cast<quintptr>(hmon), spaceIndex});

    const bool shouldHide = (spaceIndex != m->currentIndex);
    cloakWindow(hwnd, shouldHide);

    // Content changed → repaint both spaces (source lost a window, dest gained one).
    if (prevSpace >= 0 && prevMon && (prevMon != hmon || prevSpace != spaceIndex))
        rebuildSpaceScreenshot(prevMon, prevSpace);
    rebuildSpaceScreenshot(hmon, spaceIndex);
    emit windowTracked(reinterpret_cast<quint64>(hwnd));

    return true;
}

int SpaceManager::spaceOfWindow(HWND hwnd) const
{
    auto it = m_owner.constFind(hwnd);
    return it == m_owner.constEnd() ? -1 : it.value().space;
}

HMONITOR SpaceManager::ownerMonitorOf(HWND hwnd) const
{
    auto it = m_owner.constFind(hwnd);
    if (it == m_owner.constEnd())
        return nullptr;
    return reinterpret_cast<HMONITOR>(it.value().hmon);
}

void SpaceManager::adoptExistingWindows()
{
    const auto windows = WindowTracker::snapshotManageableWindows();
    for (HWND hwnd : windows)
        trackWindow(hwnd);
    for (auto it = m_monitors.begin(); it != m_monitors.end(); ++it)
        applyVisibility(it.value().hmon);
    seedScreenshots();
}

void SpaceManager::applyVisibility(HMONITOR hmon)
{
    auto *m = monitorOf(hmon);
    if (!m)
        return;

    const int cur = m->currentIndex;
    if (cur < 0 || cur >= m->spaces.size())
        return;

    auto collectZ = [hmon](const QSet<HWND> &filter) {
        QVector<HWND> out;
        struct Ctx {
            HMONITOR mon;
            const QSet<HWND> *filter;
            QVector<HWND> *out;
        } ctx{hmon, &filter, &out};
        ::EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
            auto *c = reinterpret_cast<Ctx *>(lp);
            if (!c->filter->contains(hwnd) || ::IsIconic(hwnd))
                return TRUE;
            if (::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) != c->mon)
                return TRUE;
            if (!::IsWindowVisible(hwnd))
                return TRUE;
            c->out->push_back(hwnd);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&ctx));
        return out;
    };

    for (int i = 0; i < m->spaces.size(); ++i) {
        const auto z = collectZ(m->spaces[i].windows);
        if (!z.isEmpty())
            m->spaces[i].zOrder = z;
    }

    for (int i = 0; i < m->spaces.size(); ++i) {
        for (HWND hwnd : m->spaces[i].windows) {
            if (!::IsWindow(hwnd) || ::IsIconic(hwnd))
                continue;
            cloakWindow(hwnd, i != cur);
        }
    }

    const QVector<HWND> &z = m->spaces[cur].zOrder;
    for (int i = z.size() - 1; i >= 0; --i) {
        HWND hwnd = z[i];
        if (!::IsWindow(hwnd) || ::IsIconic(hwnd) || !::IsWindowVisible(hwnd))
            continue;
        ::SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    for (Space &sp : m->spaces) {
        for (auto it = sp.windows.begin(); it != sp.windows.end();) {
            if (!::IsWindow(*it)) {
                m_owner.remove(*it);
                it = sp.windows.erase(it);
            } else {
                ++it;
            }
        }
    }
}

QVector<HWND> SpaceManager::windowsOn(HMONITOR hmon, int spaceIndex) const
{
    auto *self = const_cast<SpaceManager *>(this);
    auto *m = self->monitorOf(hmon);
    if (!m || spaceIndex < 0 || spaceIndex >= m->spaces.size())
        return {};
    QVector<HWND> out;
    out.reserve(int(m->spaces[spaceIndex].windows.size()));
    for (HWND h : m->spaces[spaceIndex].windows)
        if (::IsWindow(h))
            out.push_back(h);
    QVector<HWND> zordered;
    zordered.reserve(out.size());
    for (HWND h : WindowTracker::snapshotManageableWindows()) {
        if (out.contains(h))
            zordered.push_back(h);
    }
    for (HWND h : out)
        if (!zordered.contains(h))
            zordered.push_back(h);
    return zordered;
}

bool SpaceManager::trackWindow(HWND hwnd)
{
    // Already ours: SHOW after CREATE / re-entry — content may be ready now.
    // Re-capture the shot and re-render the owner space (esp. secondary monitors).
    if (m_owner.contains(hwnd)) {
        refreshWindowAfterUpdate(hwnd);
        return true;
    }
    if (::IsIconic(hwnd))
        return false;
    if (!WindowTracker::isManageable(hwnd))
        return false;

    HMONITOR h = monitors::fromWindow(hwnd);
    ensureMonitor(h);
    auto *m = monitorOf(h);
    if (!m)
        return false;

    return assignWindow(hwnd, h, m->currentIndex);
}

void SpaceManager::untrackWindow(HWND hwnd)
{
    if (!m_owner.contains(hwnd))
        return;
    const auto owner = m_owner.take(hwnd);
    thumbs::invalidateWindow(hwnd);
    if (auto *m = monitorOf(reinterpret_cast<HMONITOR>(owner.hmon))) {
        if (owner.space >= 0 && owner.space < m->spaces.size()) {
            m->spaces[owner.space].windows.remove(hwnd);
            // Membership changed → repaint that space only.
            rebuildSpaceScreenshot(reinterpret_cast<HMONITOR>(owner.hmon), owner.space);
        }
    }
    emit windowUntracked(reinterpret_cast<quint64>(hwnd));
}

void SpaceManager::setOverviewOpen(bool open)
{
    const bool was = m_overviewOpen;
    m_overviewOpen = open;
    if (was && !open) {
        for (auto it = m_monitors.begin(); it != m_monitors.end(); ++it)
            applyVisibility(it.value().hmon);
        // Overview is gone — refresh current-space previews from the real desktop.
        seedScreenshots();
    }
}

void SpaceManager::ensureMonitor(HMONITOR hmon)
{
    const auto key = reinterpret_cast<quintptr>(hmon);
    if (m_monitors.contains(key))
        return;
    refreshMonitors();
}

void SpaceManager::cloakWindow(HWND hwnd, bool hide)
{
    if (!::IsWindow(hwnd) || ::IsIconic(hwnd))
        return;
    ::cloak::set(hwnd, hide);
}

void SpaceManager::placeNewWindow(HWND hwnd)
{
    trackWindow(hwnd);
}
