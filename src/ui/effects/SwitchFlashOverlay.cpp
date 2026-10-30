#include "SwitchFlashOverlay.h"
#include "core/space/SpaceManager.h"
#include "core/capture/ThumbnailCapture.h"
#include <QEasingCurve>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <dwmapi.h>
#include <QScopedValueRollback>
#include "core/log/Log.h"

namespace {
constexpr int durationMs = 520;
qreal ease(qreal t)
{
    return QEasingCurve(QEasingCurve::InOutCubic).valueForProgress(qBound(0.0, t, 1.0));
}
} // namespace

SwitchFlashOverlay::SwitchFlashOverlay(QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::Tool | Qt::WindowTransparentForInput |
                          Qt::WindowDoesNotAcceptFocus | Qt::WindowStaysOnTopHint)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_ShowWithoutActivating);
    m_timer.setInterval(16);
    connect(&m_timer, &QTimer::timeout, this, [this]() {
        if (m_clock.elapsed() >= durationMs)
            stop();
        else
            update();
    });
}

void SwitchFlashOverlay::present(const QRect &geometry, const RECT &physical)
{
    setGeometry(geometry);
    show();
    ::SetWindowPos(reinterpret_cast<HWND>(winId()), HWND_TOPMOST, physical.left, physical.top,
                   physical.right - physical.left, physical.bottom - physical.top, SWP_NOACTIVATE);
    repaint();
    ::DwmFlush(); // submit the opaque cover before synchronous capture can block
}

void SwitchFlashOverlay::cover(const QRect &geometry, const RECT &physical, const QImage &source)
{
    if (geometry.isEmpty())
        return;
    m_timer.stop();
    m_clock.invalidate();
    m_source = source;
    m_preparing = true;
    m_boundary = false;
    m_shots = {source}; // null source uses the opaque background, never screen capture
    m_from = m_to = 0;
    m_direction = 1;
    m_playing = true;
    present(geometry, physical);
}

void SwitchFlashOverlay::play(const QRect &geometry, const RECT &physical,
                              const QVector<QImage> &shots, int from, int to, int direction)
{
    if (geometry.isEmpty() || shots.isEmpty() || from < 0 || to < 0 || from >= shots.size() ||
        to >= shots.size() || from == to)
        return;
    m_shots = shots;
    // The outgoing tile stays the exact desktop captured before the cover.
    // Replacing it with a window composite here would visibly change the image
    // at the start of the shrink animation (taskbar, untracked windows, etc.).
    if (m_preparing && !m_source.isNull())
        m_shots[from] = m_source;
    else
        m_source = shots[from];
    m_preparing = false;
    m_boundary = false;
    m_from = from;
    m_to = to;
    m_direction = direction;
    m_playing = true;
    m_timer.stop();
    m_clock.invalidate(); // replay must not paint using the previous run's elapsed time
    present(geometry, physical);
    m_clock.start();
    m_timer.start();
}

void SwitchFlashOverlay::playBoundary(const QRect &geometry, const RECT &physical,
                                      const QImage &source, int direction)
{
    if (geometry.isEmpty() || source.isNull() || (direction != -1 && direction != 1))
        return;
    cover(geometry, physical, source);
    m_preparing = false;
    m_boundary = true;
    m_direction = direction;
    m_clock.start();
    m_timer.start();
}

QRectF SwitchFlashOverlay::boundaryRect(const QSize &viewport, int direction, qreal progress)
{
    const qreal t = qBound(0.0, progress, 1.0);
    const qreal scale = t < .25 ? 1.0 - .12 * ease(t / .25)
                               : (t <= .75 ? .88 : .88 + .12 * ease((t - .75) / .25));
    const qreal travel = t < .25 || t > .75 ? 0.0
                         : (t <= .5 ? ease((t - .25) / .25) : ease((.75 - t) / .25));
    const qreal w = viewport.width() * scale, h = viewport.height() * scale;
    return QRectF((viewport.width() - w) / 2 - direction * viewport.width() * .2 * travel,
                  (viewport.height() - h) / 2, w, h);
}

void SwitchFlashOverlay::stop()
{
    m_timer.stop();
    const bool wasPlaying = m_playing;
    m_playing = false;
    hide();
    m_shots.clear();
    m_source = {};
    m_preparing = false;
    if (wasPlaying)
        emit finished();
}

QRectF SwitchFlashOverlay::tileRect(const QSize &viewport, int tile, int from, int to, int count,
                                    int direction, qreal progress)
{
    const qreal t = qBound(0.0, progress, 1.0);
    const qreal scale =
        t < .25 ? 1.0 - .12 * ease(t / .25) : (t <= .75 ? .88 : .88 + .12 * ease((t - .75) / .25));
    // Wrap joins the two ends of a circular strip in the requested key direction.
    int target = to;
    int position = tile;
    if (direction > 0 && to < from) {
        target += count;
        if (tile <= to)
            position += count;
    } else if (direction < 0 && to > from) {
        target -= count;
        if (tile >= to)
            position -= count;
    }
    const qreal camera = from + (target - from) * ease((t - .25) / .5);
    const qreal w = viewport.width() * scale, h = viewport.height() * scale;
    const qreal stride = w + viewport.width() * .025 * (1.0 - scale) / .12;
    return QRectF((viewport.width() - w) / 2 + (position - camera) * stride,
                  (viewport.height() - h) / 2, w, h);
}

void SwitchFlashOverlay::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(18, 20, 28));
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const qreal progress = m_clock.isValid() ? qreal(m_clock.elapsed()) / durationMs : 0;
    if (m_boundary) {
        p.drawImage(boundaryRect(size(), m_direction, progress), m_source);
        return;
    }
    for (int i = 0; i < m_shots.size(); ++i) {
        const QRectF tile =
            tileRect(size(), i, m_from, m_to, int(m_shots.size()), m_direction, progress);
        if (tile.intersects(QRectF(rect())))
            p.drawImage(tile, m_shots[i]);
    }
}

SpaceSwitchAnimator::SpaceSwitchAnimator(SpaceManager *manager, QObject *parent,
                                         MonitorCapture capture)
    : QObject(parent),
      m_manager(manager),
      m_capture(capture ? std::move(capture) : MonitorCapture([](const RECT &rect) {
          return thumbs::captureMonitor(rect, QSize());
      }))
{
    m_taskbarTimer.setSingleShot(true);
    m_taskbarTimer.setInterval(600);
    connect(&m_taskbarTimer, &QTimer::timeout, this, &SpaceSwitchAnimator::releaseTaskbarCovers);
    connect(manager, &SpaceManager::requestSwitchCover, this, [this](quint64 handle, int from) {
        auto *m = m_manager->monitorOf(reinterpret_cast<HMONITOR>(handle));
        if (!m || from < 0 || from >= m->spaces.size())
            return;
        const quintptr key = quintptr(handle);
        if (m_taskbarCovers.value(key, -1) == from) {
            // Retain the clean PRE-click frame. Taking another screen copy now
            // could include the app Explorer has just exposed.
            m_taskbarCovers.remove(key);
            releaseTaskbarCovers();
            return;
        }
        // One full-resolution monitor copy is all that precedes the cover.
        // Do not rebuild a space or capture individual windows on this path.
        // A repeated switch can capture the still-visible previous animation;
        // overview itself never requests a switch cover.
        // Without a successful pre-click cover (Alt+Tab, timeout), foreground
        // notifications arrive too late to trust live monitor pixels.
        QImage source = m_manager->foregroundSwitchInProgress() ? m->spaces[from].screenshot
                                                                : m_capture(m->physRect);
        if (source.isNull())
            source = m->spaces[from].screenshot;
        thumbs::setScreenSamplingEnabled(false);
        overlayFor(handle)->cover(m->geometry, m->physRect, source);
    });
    connect(manager, &SpaceManager::requestSwitchAnimation, this,
            [this](quint64 handle, int from, int to, int direction) {
                auto *m = m_manager->monitorOf(reinterpret_cast<HMONITOR>(handle));
                if (!m)
                    return;
                auto *overlay = overlayFor(handle);
                QVector<QImage> shots;
                for (const Space &space : m->spaces)
                    shots.push_back(space.screenshot);
                thumbs::setScreenSamplingEnabled(false);
                overlay->play(m->geometry, m->physRect, shots, from, to, direction);
            });
    connect(manager, &SpaceManager::requestBoundaryAnimation, this,
            [this](quint64 handle, int direction) {
                auto *m = m_manager->monitorOf(reinterpret_cast<HMONITOR>(handle));
                if (!m || isPlaying(m->hmon))
                    return;
                QImage source = m_capture(m->physRect);
                if (source.isNull())
                    source = m->spaces[m->currentIndex].screenshot;
                if (source.isNull())
                    return;
                thumbs::setScreenSamplingEnabled(false);
                overlayFor(handle)->playBoundary(m->geometry, m->physRect, source, direction);
            });
    connect(manager, &SpaceManager::monitorLayoutChanged, this, &SpaceSwitchAnimator::stopAll);
}

bool SpaceSwitchAnimator::prepareTaskbarSwitch()
{
    if (m_preparingTaskbar || m_manager->overviewOpen())
        return false;
    if (!m_taskbarCovers.isEmpty()) {
        m_taskbarTimer.start(); // a long press is protected again on release
        return true;
    }
    for (auto *overlay : m_overlays)
        if (overlay->isPlaying())
            return false;
    QScopedValueRollback<bool> preparing(m_preparingTaskbar, true);
    QElapsedTimer timer;
    timer.start();
    QHash<quintptr, QImage> sources;
    for (auto *mon : m_manager->monitors()) {
        bool hasOtherWindows = false;
        for (int i = 0; i < mon->spaces.size(); ++i)
            if (i != mon->currentIndex && m_manager->spaceHasWindows(mon->hmon, i))
                hasOtherWindows = true;
        if (!hasOtherWindows)
            continue;
        QImage source = m_capture(mon->physRect);
        if (source.isNull())
            source = mon->spaces[mon->currentIndex].screenshot;
        sources.insert(quintptr(mon->hmon), source);
        m_taskbarCovers.insert(quintptr(mon->hmon), mon->currentIndex);
    }
    // Capture all candidates before any overlay becomes visible.
    for (auto it = sources.constBegin(); it != sources.constEnd(); ++it) {
        auto *mon = m_manager->monitorOf(reinterpret_cast<HMONITOR>(it.key()));
        if (mon)
            overlayFor(quint64(it.key()))->cover(mon->geometry, mon->physRect, it.value());
    }
    if (sources.isEmpty())
        return false;
    thumbs::setScreenSamplingEnabled(false);
    m_taskbarTimer.start();
    spacelog::info(QStringLiteral("taskbar pre-activation cover monitors=%1 ms=%2")
                       .arg(sources.size())
                       .arg(timer.elapsed()));
    return true;
}

void SpaceSwitchAnimator::releaseTaskbarCovers()
{
    m_taskbarTimer.stop();
    const auto pending = m_taskbarCovers;
    m_taskbarCovers.clear();
    for (auto it = pending.constBegin(); it != pending.constEnd(); ++it)
        if (auto *overlay = m_overlays.value(it.key()))
            overlay->stop();
    updateSampling();
}

SwitchFlashOverlay *SpaceSwitchAnimator::overlayFor(quint64 handle)
{
    auto *&overlay = m_overlays[quintptr(handle)];
    if (!overlay) {
        overlay = new SwitchFlashOverlay;
        connect(overlay, &SwitchFlashOverlay::finished, this, &SpaceSwitchAnimator::updateSampling);
    }
    return overlay;
}

SpaceSwitchAnimator::~SpaceSwitchAnimator()
{
    stopAll();
    qDeleteAll(m_overlays);
}

bool SpaceSwitchAnimator::isPlaying(HMONITOR monitor) const
{
    const auto *overlay = m_overlays.value(quintptr(monitor));
    return overlay && overlay->isPlaying();
}

void SpaceSwitchAnimator::updateSampling()
{
    bool playing = false;
    for (auto *overlay : m_overlays)
        playing |= overlay->isPlaying();
    thumbs::setScreenSamplingEnabled(!playing && !m_manager->overviewOpen());
}

void SpaceSwitchAnimator::stopAll()
{
    m_taskbarTimer.stop();
    m_taskbarCovers.clear();
    for (auto *overlay : m_overlays)
        overlay->stop();
    updateSampling();
}
