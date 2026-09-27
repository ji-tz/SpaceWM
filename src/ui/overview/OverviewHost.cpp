#include "OverviewHost.h"

#include "core/log/Log.h"
#include "core/monitor/MonitorInfo.h"

#include <QElapsedTimer>
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

    const HMONITOR cursor = monitors::fromCursor();
    QElapsedTimer t;
    t.start();

    // 1) Desktop intact: refresh shots for on-screen windows. The screen
    //    fallback is honest only now — once the mask is up it would bake
    //    the overlay into other spaces' cards.
    m_manager->refreshVisibleShots();
    const qint64 msRefresh = t.restart();

    // 2) Dark masks up first — uncloak/capture must not flash on the desktop.
    for (OverviewWindow *w : std::as_const(m_panels)) {
        if (w->targetMonitor() == cursor)
            continue;
        QElapsedTimer mt;
        mt.start();
        w->beginPanelOpen(w->targetMonitor(), /*takeFocus=*/false);
        spacelog::info(QStringLiteral("overview mask mon=0x%1 ms=%2 focus=0")
                           .arg(quintptr(w->targetMonitor()), 0, 16)
                           .arg(mt.elapsed()));
    }
    for (OverviewWindow *w : std::as_const(m_panels)) {
        if (w->targetMonitor() == cursor) {
            QElapsedTimer mt;
            mt.start();
            w->beginPanelOpen(cursor, /*takeFocus=*/true);
            spacelog::info(QStringLiteral("overview mask mon=0x%1 ms=%2 focus=1")
                               .arg(quintptr(cursor), 0, 16)
                               .arg(mt.elapsed()));
            m_active = w;
            break;
        }
    }
    if (!m_active && !m_panels.isEmpty()) {
        auto *first = m_panels.first();
        first->beginPanelOpen(first->targetMonitor(), true);
        m_active = first;
    }
    const qint64 msMasks = t.restart();

    // 3) Behind the mask: mark overview open, unhide, capture whatever is
    //    still missing (off-space windows), then composite space cards.
    m_manager->setOverviewOpen(true);
    m_manager->warmWindowShots();
    const qint64 msWarm = t.restart();
    m_manager->buildAllSpacePreviews();
    const qint64 msBuild = t.restart();

    // 4) Lockstep populate: ALL panels reveal cards and start their enter
    //    fade together FIRST — one monitor's heavy tile build must never
    //    delay the other monitor's reveal (was: sequential full populate per
    //    panel, which showed the secondary much later). Tiles fill in while
    //    the panels are already fading in.
    for (OverviewWindow *w : std::as_const(m_panels)) {
        QElapsedTimer pt;
        pt.start();
        w->populateCards();
        spacelog::info(QStringLiteral("overview cards mon=0x%1 ms=%2")
                           .arg(quintptr(w->targetMonitor()), 0, 16)
                           .arg(pt.elapsed()));
    }
    const qint64 msCards = t.restart();
    for (OverviewWindow *w : std::as_const(m_panels))
        w->startPanelEnterAnimation();
    const qint64 msAnims = t.restart();
    for (OverviewWindow *w : std::as_const(m_panels)) {
        QElapsedTimer pt;
        pt.start();
        w->populateTiles();
        spacelog::info(QStringLiteral("overview tiles mon=0x%1 ms=%2")
                           .arg(quintptr(w->targetMonitor()), 0, 16)
                           .arg(pt.elapsed()));
    }
    const qint64 msTiles = t.restart();

    spacelog::info(QStringLiteral(
        "overview open timings: refresh=%1ms masks=%2ms warm=%3ms build=%4ms "
        "cards=%5ms anims=%6ms tiles=%7ms panels=%8")
                       .arg(msRefresh)
                       .arg(msMasks)
                       .arg(msWarm)
                       .arg(msBuild)
                       .arg(msCards)
                       .arg(msAnims)
                       .arg(msTiles)
                       .arg(m_panels.size()));
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
