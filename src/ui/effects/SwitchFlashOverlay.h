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
    void cover(const QRect &geometry, const RECT &physical, const QImage &source);
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
    void present(const QRect &geometry, const RECT &physical);
    QVector<QImage> m_shots;
    QImage m_source;
    bool m_preparing = false;
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
    SwitchFlashOverlay *overlayFor(quint64 handle);
    void updateSampling();
    SpaceManager *m_manager;
    QHash<quintptr, SwitchFlashOverlay *> m_overlays;
};
