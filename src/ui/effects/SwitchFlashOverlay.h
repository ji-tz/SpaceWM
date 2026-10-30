#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QTimer>
#include <QWidget>
#include <Windows.h>
#include <functional>

class SpaceManager;

// Opaque screenshot strip: shrink, translate, expand. Never takes app focus.
class SwitchFlashOverlay : public QWidget {
    Q_OBJECT
  public:
    explicit SwitchFlashOverlay(QWidget *parent = nullptr);
    void cover(const QRect &geometry, const RECT &physical, const QImage &source);
    void play(const QRect &geometry, const RECT &physical, const QVector<QImage> &shots, int from,
              int to, int direction);
    void playBoundary(const QRect &geometry, const RECT &physical, const QImage &source, int direction);
    static QRectF boundaryRect(const QSize &viewport, int direction, qreal progress);
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
    bool m_boundary = false;
    int m_from = 0, m_to = 0, m_direction = 1;
    bool m_playing = false;
    QElapsedTimer m_clock;
    QTimer m_timer;
};

// One overlay per monitor, shared wiring for the app and integration tests.
class SpaceSwitchAnimator : public QObject {
    Q_OBJECT
  public:
    using MonitorCapture = std::function<QImage(const RECT &)>;
    explicit SpaceSwitchAnimator(SpaceManager *manager, QObject *parent = nullptr,
                                 MonitorCapture capture = {});
    ~SpaceSwitchAnimator() override;
    void stopAll();
    bool isPlaying(HMONITOR monitor) const;
    // Runs synchronously on the GUI thread before Explorer receives the click.
    // False when overview/another animation already covers the desktop.
    bool prepareTaskbarSwitch();
    void finishTaskbarPress() { releaseTaskbarCovers(); }
    bool hasTaskbarCover(HMONITOR monitor) const
    {
        return m_taskbarCovers.contains(quintptr(monitor));
    }

  private:
    SwitchFlashOverlay *overlayFor(quint64 handle);
    void updateSampling();
    void releaseTaskbarCovers();
    SpaceManager *m_manager;
    QHash<quintptr, SwitchFlashOverlay *> m_overlays;
    QHash<quintptr, int> m_taskbarCovers;
    MonitorCapture m_capture;
    QTimer m_taskbarTimer;
    bool m_preparingTaskbar = false;
};
