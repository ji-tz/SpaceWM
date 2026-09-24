#include "OverviewHost.h"

#include "core/monitor/MonitorInfo.h"

#include <QTimer>

OverviewHost::OverviewHost(SpaceManager *manager, QObject *parent)
    : QObject(parent)
    , m_manager(manager)
{
}

OverviewHost::~OverviewHost()
{
    // Panels are raw QWidget* (not QObject-parented). Destroy them here so
    // pending timers/singleShots cannot outlive SpaceManager after the host dies.
    for (OverviewWindow *w : std::as_const(m_panels)) {
        if (!w)
            continue;
        w->forceHide();
        delete w;
    }
    m_panels.clear();
    m_active = nullptr;
}

bool OverviewHost::isOpen() const
{
    for (OverviewWindow *w : m_panels)
        if (w && (w->isOpen() || w->isVisible()))
            return true;
    return false;
}

int OverviewHost::openPanelCount() const
{
    int n = 0;
    for (OverviewWindow *w : m_panels)
        if (w && (w->isOpen() || w->isVisible()))
            ++n;
    return n;
}

void OverviewHost::forceHideAll()
{
    ++m_closeGeneration;
    for (OverviewWindow *w : std::as_const(m_panels)) {
        if (w)
            w->forceHide();
    }
    finishIfAllQuiet();
}

OverviewWindow *OverviewHost::panelFor(HMONITOR hmon)
{
    for (OverviewWindow *w : m_panels)
        if (w && w->targetMonitor() == hmon)
            return w;
    return nullptr;
}

void OverviewHost::ensurePanels()
{
    const auto mons = m_manager ? m_manager->monitors() : QVector<MonitorSpaces *>();
    if (m_panels.size() == mons.size())
        return;

    for (OverviewWindow *w : std::as_const(m_panels))
        delete w;
    m_panels.clear();

    for (MonitorSpaces *mon : mons) {
        auto *w = new OverviewWindow(m_manager);
        w->setHostManaged(true);
        w->assignMonitor(mon->hmon);
        connect(w, &OverviewWindow::closed, this, &OverviewHost::onPanelClosed);
        m_panels.push_back(w);
    }
}

void OverviewHost::openAll()
{
    if (!m_manager || isOpen())
        return;

    ensurePanels();
    if (m_panels.isEmpty())
        return;

    ++m_closeGeneration;
    m_switching = false;
    m_active = nullptr;

    // Refresh window shots FIRST (clear+recapture), then rebuild space
    // composites from those fresh shots — reopening must not reuse stale tiles.
    m_manager->warmWindowShots();
    m_manager->buildAllSpacePreviews();
    m_manager->setOverviewOpen(true);

    const HMONITOR cursor = monitors::fromCursor();

    for (OverviewWindow *w : std::as_const(m_panels)) {
        if (w->targetMonitor() == cursor)
            continue;
        w->openOnMonitor(w->targetMonitor(), /*takeFocus=*/false);
    }
    for (OverviewWindow *w : std::as_const(m_panels)) {
        if (w->targetMonitor() == cursor) {
            w->openOnMonitor(cursor, /*takeFocus=*/true);
            m_active = w;
            break;
        }
    }
    if (!m_active && !m_panels.isEmpty()) {
        auto *first = m_panels.first();
        first->openOnMonitor(first->targetMonitor(), true);
        m_active = first;
    }
}

void OverviewHost::closeAll(bool commit)
{
    if (!isOpen())
        return;

    if (commit && m_active && m_active->isOpen()) {
        m_active->closeOverview(true);
        return;
    }

    ++m_closeGeneration;
    // Cancel: prepare+exit ALL panels in lockstep (no per-panel closed storm).
    for (OverviewWindow *w : std::as_const(m_panels)) {
        if (w)
            w->prepareClose();
    }
    startExitAllAndFinish();
}

void OverviewHost::onPanelClosed(int chosen)
{
    if (m_switching)
        return;
    m_switching = true;

    if (chosen >= 0 && m_manager) {
        auto *w = qobject_cast<OverviewWindow *>(sender());
        if (w)
            emit spaceChosen(reinterpret_cast<quint64>(w->targetMonitor()), chosen);
        // Hold 200ms so cloak can settle, then ALL panels exit together.
        const int gen = m_closeGeneration;
        QTimer::singleShot(200, this, [this, gen]() {
            if (gen != m_closeGeneration)
                return;
            for (OverviewWindow *w : std::as_const(m_panels)) {
                if (w)
                    w->prepareClose();
            }
            startExitAllAndFinish();
        });
    } else {
        for (OverviewWindow *w : std::as_const(m_panels)) {
            if (w)
                w->prepareClose();
        }
        startExitAllAndFinish();
    }
}

void OverviewHost::startExitAllAndFinish()
{
    const int gen = m_closeGeneration;

    // Start every panel's exit animation on the same event-loop tick.
    for (OverviewWindow *w : std::as_const(m_panels)) {
        if (w)
            w->startExit();
    }

    // Safety: if any panel is still on screen, force-hide the whole set.
    QTimer::singleShot(350, this, [this, gen]() {
        if (gen != m_closeGeneration)
            return;
        for (OverviewWindow *w : std::as_const(m_panels)) {
            if (w && w->isVisible())
                w->forceHide();
        }
        finishIfAllQuiet();
    });
}

void OverviewHost::finishIfAllQuiet()
{
    for (OverviewWindow *w : std::as_const(m_panels)) {
        if (w && (w->isOpen() || w->isVisible() || w->isAnimating() || w->isDismissing())) {
            QTimer::singleShot(50, this, &OverviewHost::finishIfAllQuiet);
            return;
        }
    }

    if (m_manager)
        m_manager->setOverviewOpen(false);
    m_active = nullptr;
    m_switching = false;
    emit allClosed();
}
