#include "SpaceManager.h"

#include "CloakController.h"
#include "ThumbnailCapture.h"
#include "WindowTracker.h"

#include <QCoreApplication>
#include <QColor>
#include <QCursor>
#include <QPainter>
#include <QPoint>

#include <algorithm>

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
    auto *m = monitorOf(hmon);
    if (!m || index < 0 || index >= m->spaces.size())
        return;
    const int w = m->physRect.right - m->physRect.left;
    const int h = m->physRect.bottom - m->physRect.top;
    if (w <= 0 || h <= 0)
        return;
    // While overview is open, BitBlt would capture our own overlay — skip.
    if (m_overviewOpen)
        return;
    QImage shot = thumbs::captureMonitor(m->physRect, QSize(640, 360));
    if (shot.isNull())
        shot = thumbs::desktopWallpaper(m->physRect, QSize(640, 360));
    if (!shot.isNull())
        m->spaces[index].screenshot = shot;
}

void SpaceManager::seedScreenshots()
{
    for (auto it = m_monitors.begin(); it != m_monitors.end(); ++it) {
        MonitorSpaces &m = it.value();
        const int w = m.physRect.right - m.physRect.left;
        const int h = m.physRect.bottom - m.physRect.top;
        if (w <= 0 || h <= 0)
            continue;

        // One desktop-level shot for every empty space (cold boot).
        QImage seed;
        for (const Space &sp : m.spaces) {
            if (!sp.screenshot.isNull()) {
                seed = sp.screenshot;
                break;
            }
        }
        if (seed.isNull()) {
            seed = thumbs::desktopWallpaper(m.physRect, QSize(640, 360));
            if (seed.isNull() && !m_overviewOpen)
                seed = thumbs::captureMonitor(m.physRect, QSize(640, 360));
        }

        // Current space: live shot if possible (desktop / windows).
        if (!m_overviewOpen) {
            QImage live = thumbs::captureMonitor(m.physRect, QSize(640, 360));
            if (!live.isNull() && m.currentIndex >= 0 && m.currentIndex < m.spaces.size())
                m.spaces[m.currentIndex].screenshot = live;
        }

        for (Space &sp : m.spaces) {
            if (sp.screenshot.isNull() && !seed.isNull())
                sp.screenshot = seed;
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

    // Composite screenshot (overview overlay is up — never BitBlt the screen).
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

    QImage canvas = thumbs::desktopWallpaper(m->physRect, QSize(640, 360));
    if (canvas.isNull()) {
        canvas = QImage(640, 360, QImage::Format_ARGB32_Premultiplied);
        canvas.fill(QColor(32, 36, 48));
    }

    // Scale factor physical monitor → canvas.
    const double sx = double(canvas.width()) / monW;
    const double sy = double(canvas.height()) / monH;

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    for (HWND hwnd : m->spaces[index].windows) {
        if (!::IsWindow(hwnd) || ::IsIconic(hwnd))
            continue;
        RECT wr{};
        if (!::GetWindowRect(hwnd, &wr))
            continue;
        const int ww = wr.right - wr.left;
        const int wh = wr.bottom - wr.top;
        if (ww <= 0 || wh <= 0)
            continue;
        QImage shot = thumbs::capture(hwnd, QSize(qMax(1, int(ww * sx)), qMax(1, int(wh * sy))));
        if (shot.isNull())
            continue;
        const int x = int((wr.left - m->physRect.left) * sx);
        const int y = int((wr.top - m->physRect.top) * sy);
        painter.drawImage(QPoint(x, y), shot);
    }
    painter.end();
    m->spaces[index].screenshot = canvas;
}

bool SpaceManager::assignWindow(HWND hwnd, HMONITOR hmon, int spaceIndex)
{
    auto *m = monitorOf(hmon);
    if (!m || spaceIndex < 0 || spaceIndex >= m->spaces.size())
        return false;

    if (m_owner.contains(hwnd)) {
        const auto prev = m_owner.value(hwnd);
        if (auto *pm = monitorOf(reinterpret_cast<HMONITOR>(prev.hmon)); pm && prev.space >= 0 && prev.space < pm->spaces.size())
            pm->spaces[prev.space].windows.remove(hwnd);
    }

    m->spaces[spaceIndex].windows.insert(hwnd);
    m_owner.insert(hwnd, Owner{reinterpret_cast<quintptr>(hmon), spaceIndex});

    const bool shouldHide = (spaceIndex != m->currentIndex);
    cloakWindow(hwnd, shouldHide);
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
    if (::IsIconic(hwnd))
        return false;
    if (!WindowTracker::isManageable(hwnd))
        return false;
    if (m_owner.contains(hwnd))
        return true;

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
    if (auto *m = monitorOf(reinterpret_cast<HMONITOR>(owner.hmon))) {
        if (owner.space >= 0 && owner.space < m->spaces.size())
            m->spaces[owner.space].windows.remove(hwnd);
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
