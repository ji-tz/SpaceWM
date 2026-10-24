#include "SwitchFlashOverlay.h"
#include "core/space/SpaceManager.h"
#include "core/capture/ThumbnailCapture.h"
#include <QEasingCurve>
#include <QPainter>
#include <algorithm>
#include <cmath>

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

void SwitchFlashOverlay::play(const QRect &geometry, const RECT &physical,
                              const QVector<QImage> &shots, int from, int to, int direction)
{
    if (geometry.isEmpty() || shots.isEmpty() || from < 0 || to < 0 || from >= shots.size() ||
        to >= shots.size() || from == to)
        return;
    m_shots = shots;
    m_from = from;
    m_to = to;
    m_direction = direction;
    m_playing = true;
    setGeometry(geometry);
    show();
    ::SetWindowPos(reinterpret_cast<HWND>(winId()), HWND_TOPMOST, physical.left, physical.top,
                   physical.right - physical.left, physical.bottom - physical.top, SWP_NOACTIVATE);
    m_clock.start();
    repaint(); // first full source frame before SpaceManager changes cloak
    m_timer.start();
}

void SwitchFlashOverlay::stop()
{
    m_timer.stop();
    const bool wasPlaying = m_playing;
    m_playing = false;
    hide();
    m_shots.clear();
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
    for (int i = 0; i < m_shots.size(); ++i) {
        const QRectF tile =
            tileRect(size(), i, m_from, m_to, int(m_shots.size()), m_direction, progress);
        if (tile.intersects(QRectF(rect())))
            p.drawImage(tile, m_shots[i]);
    }
}

SpaceSwitchAnimator::SpaceSwitchAnimator(SpaceManager *manager, QObject *parent)
    : QObject(parent),
      m_manager(manager)
{
    connect(manager, &SpaceManager::requestSwitchAnimation, this,
            [this](quint64 handle, int from, int to, int direction) {
                auto *m = m_manager->monitorOf(reinterpret_cast<HMONITOR>(handle));
                if (!m)
                    return;
                auto *&overlay = m_overlays[quintptr(handle)];
                if (!overlay) {
                    overlay = new SwitchFlashOverlay;
                    connect(overlay, &SwitchFlashOverlay::finished, this,
                            &SpaceSwitchAnimator::updateSampling);
                }
                QVector<QImage> shots;
                for (const Space &space : m->spaces)
                    shots.push_back(space.screenshot);
                thumbs::setScreenSamplingEnabled(false);
                overlay->play(m->geometry, m->physRect, shots, from, to, direction);
            });
    connect(manager, &SpaceManager::monitorLayoutChanged, this, &SpaceSwitchAnimator::stopAll);
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
    for (auto *overlay : m_overlays)
        overlay->stop();
    updateSampling();
}
