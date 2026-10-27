#include "OverviewHost.h"

#include "core/log/Log.h"
#include "core/capture/ThumbnailCapture.h"
#include "core/monitor/MonitorInfo.h"

#include <QElapsedTimer>
#include <QTimer>
#include <QPointer>
#include <QHash>

OverviewHost::OverviewHost(SpaceManager *manager, QObject *parent)
    : QObject(parent),
      m_manager(manager)
{}

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
        connect(w, &OverviewWindow::spacePreparationRequested, this,
                [this, w](int index) { prioritizeSpace(w->targetMonitor(), index); });
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

    const int generation = ++m_closeGeneration;
    m_switching = false;
    m_preparations.clear();
    const HMONITOR cursor = monitors::fromCursor();
    m_active = panelFor(cursor);
    if (!m_active)
        m_active = m_panels.first();

    // Take every monitor copy BEFORE showing any panel or disabling sampling.
    // These are entry backgrounds only; space cards stay render-only.
    QHash<quintptr, QImage> backdrops;
    for (OverviewWindow *panel : std::as_const(m_panels)) {
        auto *mon = m_manager->monitorOf(panel->targetMonitor());
        if (!mon)
            continue;
        QImage backdrop;
        if (thumbs::screenSamplingEnabled())
            backdrop = thumbs::captureMonitor(mon->physRect, QSize());
        if (backdrop.isNull())
            backdrop = thumbs::wallpaperFilled(mon->physRect,
                                               QSize(mon->physRect.right - mon->physRect.left,
                                                     mon->physRect.bottom - mon->physRect.top));
        backdrops.insert(quintptr(mon->hmon), backdrop);
    }
    for (OverviewWindow *panel : std::as_const(m_panels))
        panel->beginPanelOpen(panel->targetMonitor(), panel == m_active,
                              backdrops.value(quintptr(panel->targetMonitor())));
    m_manager->setOverviewOpen(true);

    // Cheap cached cards exist on every panel for keyboard/drop targeting, but
    // their content stays invisible over the frozen desktop until prepared.
    for (OverviewWindow *panel : std::as_const(m_panels))
        panel->populateCards();

    QVector<OverviewWindow *> order{m_active};
    for (OverviewWindow *panel : std::as_const(m_panels))
        if (panel != m_active)
            order.push_back(panel);
    for (OverviewWindow *panel : std::as_const(order))
        m_preparations.push_back(
            {panel->targetMonitor(), m_manager->currentSpaceIndex(panel->targetMonitor()), true});
    // Inactive spaces must never delay the first usable current-space overview.
    for (OverviewWindow *panel : std::as_const(order)) {
        const HMONITOR mon = panel->targetMonitor();
        for (int i = 0; i < m_manager->spaceCount(mon); ++i)
            if (i != m_manager->currentSpaceIndex(mon))
                m_preparations.push_back({mon, i, false});
    }
    processNextPreparation(generation);
}

void OverviewHost::processNextPreparation(int generation)
{
    if (generation != m_closeGeneration || m_switching || !m_manager->overviewOpen() ||
        m_preparations.isEmpty())
        return;
    const Preparation task = m_preparations.takeFirst();
    QPointer<OverviewWindow> panel = panelFor(task.hmon);
    if (panel && panel->isOpen()) {
        QElapsedTimer timer;
        timer.start();
        m_manager->prepareOverviewSpace(task.hmon, task.space, task.reveal);
        if (generation != m_closeGeneration || m_switching || !panel || !panel->isOpen())
            return;
        if (task.reveal && !panel->isContentReady()) {
            panel->populateTiles();
            panel->startPanelEnterAnimation();
            emit panelReady(quint64(task.hmon), task.space);
        }
        spacelog::info(QStringLiteral("overview prepare mon=0x%1 space=%2 current=%3 ms=%4")
                           .arg(quintptr(task.hmon), 0, 16)
                           .arg(task.space)
                           .arg(task.reveal ? 1 : 0)
                           .arg(timer.elapsed()));
    }
    // Give the just-revealed panel a complete enter fade and paint before the
    // next monitor's capture can block the GUI thread. No worker touches widgets.
    QTimer::singleShot(task.reveal ? 180 : 16, this,
                       [this, generation]() { processNextPreparation(generation); });
}

void OverviewHost::prioritizeSpace(HMONITOR hmon, int index)
{
    if (m_switching || !m_manager || !m_manager->overviewOpen())
        return;
    for (int i = 0; i < m_preparations.size(); ++i) {
        if (m_preparations[i].hmon == hmon && m_preparations[i].space == index) {
            const Preparation task = m_preparations.takeAt(i);
            m_preparations.prepend(task);
            return;
        }
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
    ++m_closeGeneration;
    m_preparations.clear();

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
