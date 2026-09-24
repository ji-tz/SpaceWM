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
    if (monitorGeometry.isEmpty())
        return;
    setGeometry(monitorGeometry);
    start(direction);
}

void SwitchFlashOverlay::start(int direction)
{
    // Kill any in-flight timer so rapid switches never stack handlers.
    if (m_timer) {
        m_timer->stop();
        m_timer->deleteLater();
        m_timer = nullptr;
    }

    m_direction = direction;
    m_progress = 0.0;
    m_playing = true;
    m_frame = 0;
    show();
    raise();

    m_timer = new QTimer(this);
    m_timer->setInterval(16);
    const int total = 18;
    connect(m_timer, &QTimer::timeout, this, [this, total]() {
        ++m_frame;
        m_progress = double(m_frame) / total;
        if (m_progress >= 1.0) {
            m_progress = 1.0;
            if (m_timer) {
                m_timer->stop();
                m_timer->deleteLater();
                m_timer = nullptr;
            }
            m_playing = false;
            hide();
        }
        update();
    });
    m_timer->start();
}

void SwitchFlashOverlay::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const qreal t = m_progress;
    const qreal ease = 1.0 - (1.0 - t) * (1.0 - t);

    if (m_direction == 0) {
        p.fillRect(rect(), QColor(10, 12, 20, int(70 * (1.0 - ease))));
        return;
    }

    const int w = width();
    if (w <= 0 || height() <= 0)
        return;

    const int band = qMax(48, w / 8);
    const int travel = w + band * 2;
    int x = 0;
    if (m_direction > 0)
        x = int(-band + ease * travel);
    else
        x = int(w + band - ease * travel);

    QColor c1(122, 162, 255, int(110 * (1.0 - ease)));
    QColor c2(122, 162, 255, 0);
    QLinearGradient g(x, 0, x + band, 0);
    g.setColorAt(0.0, c2);
    g.setColorAt(0.5, c1);
    g.setColorAt(1.0, c2);
    p.fillRect(QRect(x, 0, band, height()), g);
    p.fillRect(rect(), QColor(8, 10, 18, int(50 * (1.0 - ease))));
}
