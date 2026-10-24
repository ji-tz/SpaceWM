#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QTimer>
#include <QWidget>
#include <Windows.h>

class SpaceManager;

// Opaque screenshot strip: shrink, translate, expand. Never takes app focus.
class SwitchFlashOverlay : public QWidget {
    Q_OBJECT
  public:
    explicit SwitchFlashOverlay(QWidget *parent = nullptr);
    void play(const QRect &geometry, const RECT &physical, const QVector<QImage> &shots, int from,
              int to, int direction);
    void stop();
    bool isPlaying() const { return m_playing; }
    // Shared layout calculation also allows deterministic frame regression tests.
    static QRectF tileRect(const QSize &viewport, int tile, int from, int to, int count,
                           int direction, qreal progress);
  signals:
    void finished();

  protected:
    void paintEvent(QPaintEvent *) override;

  private:
    QVector<QImage> m_shots;
    int m_from = 0, m_to = 0, m_direction = 1;
    bool m_playing = false;
    QElapsedTimer m_clock;
    QTimer m_timer;
};

// One overlay per monitor, shared wiring for the app and integration tests.
class SpaceSwitchAnimator : public QObject {
    Q_OBJECT
  public:
    explicit SpaceSwitchAnimator(SpaceManager *manager, QObject *parent = nullptr);
    ~SpaceSwitchAnimator() override;
    void stopAll();
    bool isPlaying(HMONITOR monitor) const;

  private:
    void updateSampling();
    SpaceManager *m_manager;
    QHash<quintptr, SwitchFlashOverlay *> m_overlays;
};
