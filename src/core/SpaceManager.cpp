#include "SpaceManager.h"

#include "CloakController.h"
#include "WindowTracker.h"

#include <QCoreApplication>
#include <QCursor>
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
        ms.geometry = m.geometry;
        ms.deviceName = m.deviceName;
        if (ms.currentIndex < 0 || ms.currentIndex >= ms.spaces.size())
            ms.currentIndex = 0;
        next.insert(key, std::move(ms));
    }

    // Drop owners that refer to vanished monitors.
    m_monitors = std::move(next);
    for (auto it = m_owner.begin(); it != m_owner.end();) {
        if (!m_monitors.contains(it.value().hmon))
            it = m_owner.erase(it);
        else
            ++it;
    }

    // Re-apply visibility everywhere after topology change.
    for (auto it = m_monitors.begin(); it != m_monitors.end(); ++it)
        applyVisibility(it.value().hmon);

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

bool SpaceManager::switchSpace(HMONITOR hmon, int index, bool animateHint)
{
    auto *m = monitorOf(hmon);
    if (!m || index < 0 || index >= m->spaces.size() || index == m->currentIndex)
        return false;

    const int from = m->currentIndex;
    m->currentIndex = index;
    applyVisibility(hmon);

    emit spaceChanged(reinterpret_cast<quint64>(hmon), index);
    if (animateHint && m_animationEnabled && !m_overviewOpen)
        emit requestSwitchAnimation(reinterpret_cast<quint64>(hmon), from, index);
    return true;
}

bool SpaceManager::assignWindow(HWND hwnd, HMONITOR hmon, int spaceIndex)
{
    auto *m = monitorOf(hmon);
    if (!m || spaceIndex < 0 || spaceIndex >= m->spaces.size())
        return false;

    // Remove from previous space on any monitor.
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
    // Apply visibility on all monitors after bulk adopt.
    for (auto it = m_monitors.begin(); it != m_monitors.end(); ++it)
        applyVisibility(it.value().hmon);
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

    // Save Z-order of currently visible windows per space before mutating.
    for (int i = 0; i < m->spaces.size(); ++i) {
        const auto z = collectZ(m->spaces[i].windows);
        if (!z.isEmpty())
            m->spaces[i].zOrder = z;
    }

    // Hide / show
    for (const Space &sp : m->spaces) {
        for (HWND hwnd : sp.windows) {
            if (!::IsWindow(hwnd) || ::IsIconic(hwnd))
                continue;
            const bool hide = (sp.windows.contains(hwnd) && !m->spaces[cur].windows.contains(hwnd));
            // equivalent: hide if not in current space set
            cloakWindow(hwnd, hide);
        }
    }

    // Explicitly hide non-current (in case of empty current set edge cases)
    for (int i = 0; i < m->spaces.size(); ++i) {
        if (i == cur)
            continue;
        for (HWND hwnd : m->spaces[i].windows) {
            if (::IsWindow(hwnd) && !::IsIconic(hwnd))
                cloakWindow(hwnd, true);
        }
    }
    for (HWND hwnd : m->spaces[cur].windows) {
        if (::IsWindow(hwnd) && !::IsIconic(hwnd))
            cloakWindow(hwnd, false);
    }

    // Restore saved Z-order for current space: bottom → top with HWND_TOP.
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
    // Include windows we hid ourselves (IsWindowVisible is false then).
    for (HWND h : m->spaces[spaceIndex].windows)
        if (::IsWindow(h))
            out.push_back(h);
    // Prefer Z-order top-to-bottom for overview aesthetics (include windows we hid).
    QVector<HWND> zordered;
    zordered.reserve(out.size());
    for (HWND h : WindowTracker::snapshotManageableWindows()) {
        if (out.contains(h))
            zordered.push_back(h);
    }
    // snapshot skips own-process and may skip hidden-untracked; append rest.
    for (HWND h : out)
        if (!zordered.contains(h))
            zordered.push_back(h);
    return zordered;
}

bool SpaceManager::trackWindow(HWND hwnd)
{
    if (::IsIconic(hwnd))
        return false; // minimized: not space-managed
    if (!WindowTracker::isManageable(hwnd))
        return false;
    if (m_owner.contains(hwnd))
        return true;

    HMONITOR h = monitors::fromWindow(hwnd);
    ensureMonitor(h);
    auto *m = monitorOf(h);
    if (!m)
        return false;

    // New windows go to the monitor's current space (like macOS Spaces).
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
    m_overviewOpen = open;
    if (!open) {
        // Re-apply after overview closes so state converges.
        for (auto it = m_monitors.begin(); it != m_monitors.end(); ++it)
            applyVisibility(it.value().hmon);
    }
}

void SpaceManager::ensureMonitor(HMONITOR hmon)
{
    const auto key = reinterpret_cast<quintptr>(hmon);
    if (m_monitors.contains(key))
        return;
    // Topology may have changed without a display-change message.
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
