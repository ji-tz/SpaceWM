#pragma once

#include <QWidget>
#include <Windows.h>

class QTimer;

// Brief translucent flash on a monitor when a space switches —
// cheap "slide/fade" cue without capturing the framebuffer every frame.
class SwitchFlashOverlay : public QWidget {
    Q_OBJECT
public:
    explicit SwitchFlashOverlay(QWidget *parent = nullptr);

    // Play a short directional flash on the given monitor geometry.
    // direction: -1 = going left (prev), +1 = right (next), 0 = fade only.
    void play(const QRect &monitorGeometry, int direction);

    bool isPlaying() const { return m_playing; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void start(int direction);

    bool m_playing = false;
    int m_direction = 0;
    int m_frame = 0;
    qreal m_progress = 0.0; // 0..1
    QTimer *m_timer = nullptr;
};
