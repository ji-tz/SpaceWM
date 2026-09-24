#include "SwitchFlashOverlay.h"

#include <QLinearGradient>
#include <QPainter>
#include <QTimer>

SwitchFlashOverlay::SwitchFlashOverlay(QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::Tool | Qt::WindowTransparentForInput
              | Qt::WindowDoesNotAcceptFocus | Qt::WindowStaysOnTopHint)
{
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    hide();
}

void SwitchFlashOverlay::play(const QRect &monitorGeometry, int direction)
{
    setGeometry(monitorGeometry);
    start(direction);
}

void SwitchFlashOverlay::start(int direction)
{
    m_direction = direction;
    m_progress = 0.0;
    m_playing = true;
    show();
    raise();

    auto *timer = new QTimer(this);
    timer->setInterval(16); // ~60fps
    int frame = 0;
    const int total = 18; // ~300ms
    connect(timer, &QTimer::timeout, this, [this, timer, frame, total]() mutable {
        ++frame;
        m_progress = double(frame) / total;
        if (m_progress >= 1.0) {
            m_progress = 1.0;
            timer->stop();
            timer->deleteLater();
            m_playing = false;
            hide();
        }
        update();
    });
    timer->start();
}

void SwitchFlashOverlay::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const qreal t = m_progress;
    // Ease out
    const qreal ease = 1.0 - (1.0 - t) * (1.0 - t);

    if (m_direction == 0) {
        p.fillRect(rect(), QColor(10, 12, 20, int(70 * (1.0 - ease))));
        return;
    }

    // Soft edge band sweeping across the monitor.
    const int w = width();
    const int band = qMax(48, w / 8);
    const int travel = w + band * 2;
    int x = 0;
    if (m_direction > 0)
        x = int(-band + ease * travel);
    else
        x = int(w + band - ease * travel);

    // Vertical gradient band
    QColor c1(122, 162, 255, int(110 * (1.0 - ease)));
    QColor c2(122, 162, 255, 0);
    QLinearGradient g(x, 0, x + band, 0);
    if (m_direction > 0) {
        g.setColorAt(0.0, c2);
        g.setColorAt(0.5, c1);
        g.setColorAt(1.0, c2);
    } else {
        g.setColorAt(0.0, c2);
        g.setColorAt(0.5, c1);
        g.setColorAt(1.0, c2);
    }
    p.fillRect(QRect(x, 0, band, height()), g);

    // Overall dim that fades out
    p.fillRect(rect(), QColor(8, 10, 18, int(50 * (1.0 - ease))));
}
