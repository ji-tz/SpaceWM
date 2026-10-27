#include "OverviewHost.h"

#include "core/log/Log.h"
#include "core/capture/ThumbnailCapture.h"
#include "core/monitor/MonitorInfo.h"
#include "core/window/CloakController.h"

#include <QElapsedTimer>
#include <QTimer>
#include <QPointer>
#include <QHash>
#include <vector>
#include <QCoreApplication>
#include <QEventLoop>
#include <QPainter>
#include <QScopedValueRollback>
#include <QFutureWatcher>
#include <algorithm>
#include <QtConcurrent>
#include <QThreadPool>

namespace {
struct MonitorPreparation {
    MonitorSpaces monitor;
    QImage desktop;
    QImage wallpaper;
    QHash<HWND, QImage> shots;
    QHash<HWND, RECT> rectangles;
};

QImage filledWallpaper(const QImage &source, const QSize &size)
{
    if (!source.isNull()) {
        const QImage scaled =
            source.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        return scaled.copy((scaled.width() - size.width()) / 2,
                           (scaled.height() - size.height()) / 2, size.width(), size.height());
    }
    QImage result(size, QImage::Format_ARGB32_Premultiplied);
    result.fill(QColor(32, 36, 48));
    return result;
}

MonitorPreparation prepareMonitor(MonitorPreparation data, const QImage &wallpaperSource,
                                  bool sampleScreen, int generation)
{
    QElapsedTimer timer;
    timer.start();
    const auto log = [&](const QString &phase) {
        spacelog::info(
            QStringLiteral(
                "overview monitor worker generation=%1 mon=0x%2 thread=%3 phase=%4 total_ms=%5")
                .arg(generation)
                .arg(quintptr(data.monitor.hmon), 0, 16)
                .arg(::GetCurrentThreadId())
                .arg(phase)
                .arg(double(timer.nsecsElapsed()) / 1000000.0, 0, 'f', 3));
    };
    log(QStringLiteral("start"));
    const RECT rect = data.monitor.physRect;
    const QSize fullSize(rect.right - rect.left, rect.bottom - rect.top);
    if (sampleScreen)
        data.desktop = thumbs::captureMonitor(rect, QSize());
    log(QStringLiteral("desktopCaptured"));
    data.wallpaper = filledWallpaper(wallpaperSource, fullSize);
    if (data.desktop.isNull())
        data.desktop = data.wallpaper;
    log(QStringLiteral("wallpaperPrepared"));
    log(QStringLiteral("done"));
    return data;
}

struct WindowCapture {
    HMONITOR monitor;
    HWND window;
    QImage image;
    QElapsedTimer submitted;
};

void captureWindow(WindowCapture &job, int generation)
{
    const double queueMs = double(job.submitted.nsecsElapsed()) / 1000000.0;
    QElapsedTimer timer;
    timer.start();
    const auto log = [&](const QString &phase) {
        spacelog::info(
            QStringLiteral(
                "overview window worker generation=%1 mon=0x%2 hwnd=0x%3 thread=%4 phase=%5 ms=%6 ok=%7 queue_ms=%8")
                .arg(generation)
                .arg(quintptr(job.monitor), 0, 16)
                .arg(quintptr(job.window), 0, 16)
                .arg(::GetCurrentThreadId())
                .arg(phase)
                .arg(double(timer.nsecsElapsed()) / 1000000.0, 0, 'f', 3)
                .arg(!job.image.isNull())
                .arg(queueMs, 0, 'f', 3));
    };
    log(QStringLiteral("start"));
    job.image = thumbs::captureWindowOnly(job.window);
    log(QStringLiteral("done"));
}

void composeMonitor(MonitorPreparation &data, const QImage &wallpaperSource, int generation)
{
    QElapsedTimer timer;
    timer.start();
    const RECT rect = data.monitor.physRect;
    const QSize fullSize(rect.right - rect.left, rect.bottom - rect.top);
    for (auto &space : data.monitor.spaces) {
        const QSize canvasSize = fullSize.scaled(QSize(640, 360), Qt::KeepAspectRatio);
        QImage canvas = filledWallpaper(wallpaperSource, canvasSize);
        QPainter painter(&canvas);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const double sx = double(canvasSize.width()) / fullSize.width();
        const double sy = double(canvasSize.height()) / fullSize.height();
        for (auto it = space.zOrder.crbegin(); it != space.zOrder.crend(); ++it) {
            const QImage image = data.shots.value(*it);
            if (image.isNull() || !data.rectangles.contains(*it))
                continue;
            const RECT window = data.rectangles.value(*it);
            painter.drawImage(QRectF((window.left - rect.left) * sx, (window.top - rect.top) * sy,
                                     (window.right - window.left) * sx,
                                     (window.bottom - window.top) * sy),
                              image);
        }
        painter.end();
        space.screenshot = canvas;
    }
    spacelog::info(
        QStringLiteral("overview composite worker generation=%1 mon=0x%2 thread=%3 ms=%4")
            .arg(generation)
            .arg(quintptr(data.monitor.hmon), 0, 16)
            .arg(::GetCurrentThreadId())
            .arg(double(timer.nsecsElapsed()) / 1000000.0, 0, 'f', 3));
}

void waitForBatch(QFuture<void> batch)
{
    // PrintWindow may require this thread to service WM_PRINT. Waiting must
    // dispatch native messages, even for a Qt window owned by this process.
    QFutureWatcher<void> watcher;
    QEventLoop waitGroup;
    QObject::connect(&watcher, &QFutureWatcher<void>::finished, &waitGroup, &QEventLoop::quit);
    watcher.setFuture(batch);
    if (!batch.isFinished())
        waitGroup.exec(QEventLoop::ExcludeUserInputEvents);
    batch.waitForFinished();
}

} // namespace

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
    if (m_opening)
        return true;
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
    if (m_panels.size() == mons.size() &&
        std::all_of(mons.cbegin(), mons.cend(),
                    [this](const MonitorSpaces *mon) { return panelFor(mon->hmon) != nullptr; }))
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
    QScopedValueRollback<bool> opening(m_opening, true);
    QElapsedTimer timer;
    timer.start();
    ensurePanels();
    if (m_panels.isEmpty())
        return;
    const int generation = ++m_closeGeneration;
    m_switching = false;
    const auto checkpoint = [&](const QString &phase) {
        spacelog::info(QStringLiteral("overview open generation=%1 phase=%2 total_ms=%3")
                           .arg(generation)
                           .arg(phase)
                           .arg(double(timer.nsecsElapsed()) / 1000000.0, 0, 'f', 3));
    };
    m_active = panelFor(monitors::fromCursor());
    if (!m_active)
        m_active = m_panels.first();

    // Freeze model/cache/geometry on their owning thread. Workers own their
    // QImages and GDI resources; they never touch the live SpaceManager.
    const QImage source = thumbs::wallpaperSource();
    const bool sampleScreen = thumbs::screenSamplingEnabled();
    QVector<HWND> zOrder;
    ::EnumWindows(
        [](HWND hwnd, LPARAM value) -> BOOL {
            reinterpret_cast<QVector<HWND> *>(value)->push_back(hwnd);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&zOrder));
    std::vector<MonitorPreparation> prepared;
    std::vector<WindowCapture> windows;
    QSet<HWND> scheduled;
    for (auto *monitor : m_manager->monitors()) {
        MonitorPreparation data;
        data.monitor = *monitor;
        for (int index = 0; index < data.monitor.spaces.size(); ++index) {
            auto &space = data.monitor.spaces[index];
            space.zOrder.clear();
            for (HWND hwnd : zOrder)
                if (space.windows.contains(hwnd))
                    space.zOrder.push_back(hwnd);
            for (HWND hwnd : space.windows) {
                if (!space.zOrder.contains(hwnd))
                    space.zOrder.push_back(hwnd);
                const QImage cached = thumbs::cachedWindowShot(hwnd);
                data.shots.insert(hwnd, cached);
                if (!scheduled.contains(hwnd) && ::IsWindow(hwnd) && !::IsIconic(hwnd) &&
                    (index == data.monitor.currentIndex || cached.isNull())) {
                    windows.push_back({data.monitor.hmon, hwnd, {}});
                    scheduled.insert(hwnd);
                }
                RECT rect{};
                if (::GetWindowRect(hwnd, &rect))
                    data.rectangles.insert(hwnd, rect);
            }
        }
        prepared.push_back(std::move(data));
    }
    QThreadPool pool;
    // One independent task per monitor/window, with a ceiling on simultaneous
    // full-resolution GDI buffers when a desktop has many tracked windows.
    pool.setMaxThreadCount(qBound(1, int(prepared.size() + windows.size()), 32));
    struct EntryTask {
        MonitorPreparation *background = nullptr;
        WindowCapture *window = nullptr;
    };
    std::vector<EntryTask> tasks;
    for (auto &data : prepared)
        tasks.push_back({&data, nullptr});
    for (auto &job : windows)
        tasks.push_back({nullptr, &job});
    for (auto &job : windows)
        job.submitted.start();
    // A single parallel for-each lets both kinds of work share the pool fairly.
    // Separate concurrent map engines can reserve runners and delay each other.
    auto batch =
        QtConcurrent::map(&pool, tasks, [source, sampleScreen, generation](EntryTask &task) {
            if (task.background)
                *task.background =
                    prepareMonitor(std::move(*task.background), source, sampleScreen, generation);
            else
                captureWindow(*task.window, generation);
        });
    checkpoint(QStringLiteral("launchMonitorAndWindowPreparation"));
    waitForBatch(batch);
    checkpoint(QStringLiteral("backgroundsAndWindowsReady"));
    if (generation != m_closeGeneration)
        return;
    for (const auto &job : windows) {
        if (job.image.isNull())
            continue; // Preserve the previous image after a failed capture.
        for (auto &data : prepared)
            if (data.monitor.hmon == job.monitor)
                data.shots.insert(job.window, job.image);
    }
    // Composites only consume completed images, never trigger hidden capture.
    auto composites =
        QtConcurrent::map(&pool, prepared, [source, generation](MonitorPreparation &data) {
            composeMonitor(data, source, generation);
        });
    waitForBatch(composites);
    checkpoint(QStringLiteral("allMonitorsPrepared"));
    if (generation != m_closeGeneration)
        return;
    // Message dispatch may have changed ownership or monitor layout. Never
    // publish stale membership/composites from a previous desktop snapshot.
    bool valid = prepared.size() == size_t(m_manager->monitors().size());
    for (const auto &data : prepared) {
        const auto *monitor = m_manager->monitorOf(data.monitor.hmon);
        if (!monitor || monitor->currentIndex != data.monitor.currentIndex ||
            monitor->spaces.size() != data.monitor.spaces.size() ||
            memcmp(&monitor->physRect, &data.monitor.physRect, sizeof(RECT)) != 0) {
            valid = false;
            break;
        }
        for (int i = 0; i < monitor->spaces.size(); ++i)
            if (monitor->spaces[i].windows != data.monitor.spaces[i].windows)
                valid = false;
    }
    if (!valid) {
        spacelog::info(
            QStringLiteral("overview preparation discarded: desktop membership changed"));
        return;
    }
    for (const auto &data : prepared) {
        auto *monitor = m_manager->monitorOf(data.monitor.hmon);
        for (auto it = data.shots.cbegin(); it != data.shots.cend(); ++it)
            thumbs::storeWindowShot(it.key(), it.value());
        for (int i = 0; i < monitor->spaces.size(); ++i) {
            monitor->spaces[i].screenshot = data.monitor.spaces[i].screenshot;
            monitor->spaces[i].zOrder = data.monitor.spaces[i].zOrder;
        }
    }
    for (const auto &data : prepared)
        for (int i = 0; i < data.monitor.spaces.size(); ++i)
            emit m_manager->spacePreviewInvalidated(quint64(data.monitor.hmon), i);
    if (generation != m_closeGeneration)
        return;
    checkpoint(QStringLiteral("publishPreparedImages"));
    // Desktop copies from every worker predate EVERY cover. Only now is it
    // safe to show panels and uncloak non-current windows behind those covers.
    for (const auto &data : prepared) {
        auto *panel = panelFor(data.monitor.hmon);
        panel->beginPanelOpen(data.monitor.hmon, panel == m_active, data.desktop, data.wallpaper);
        if (generation != m_closeGeneration) {
            m_manager->recloakNow();
            forceHideAll();
            return;
        }
    }
    m_manager->setOverviewOpen(true);
    for (const auto &data : prepared)
        for (const auto &space : data.monitor.spaces)
            for (HWND hwnd : space.windows)
                if (::IsWindow(hwnd) && !::IsIconic(hwnd))
                    ::cloak::set(hwnd, false);
    checkpoint(QStringLiteral("coversAndVisibility"));
    // Finish ALL Qt layout/pixmap work before starting any animation.
    for (auto *panel : std::as_const(m_panels)) {
        panel->populateCards();
        panel->populateTiles();
        panel->preparePanelEnterAnimation();
    }
    checkpoint(QStringLiteral("allPanelLayoutsReady"));
    for (auto *panel : std::as_const(m_panels))
        panel->startPanelEnterAnimation();
    checkpoint(QStringLiteral("allAnimationsStarted"));
    m_opening = false;
    // Observers see every panel ready, never a mixture of prepared/deferred UI.
    for (auto *panel : std::as_const(m_panels))
        emit panelReady(quint64(panel->targetMonitor()),
                        m_manager->currentSpaceIndex(panel->targetMonitor()));
}

void OverviewHost::closeAll(bool commit)
{
    if (m_opening) {
        ++m_closeGeneration;
        if (openPanelCount() > 0) {
            m_manager->recloakNow();
            forceHideAll();
        }
        return;
    }
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
