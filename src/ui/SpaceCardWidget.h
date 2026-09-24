#pragma once

#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QVector>

// One space tile: title + full-monitor screenshot preview.
// Preview box keeps the monitor's true aspect ratio (portrait or landscape).
class SpaceCardWidget : public QFrame {
    Q_OBJECT
public:
    explicit SpaceCardWidget(QWidget *parent = nullptr);

    void setSpace(int index, const QString &name, bool current);
    // physWidth/physHeight in pixels — forces preview box to that aspect.
    void setMonitorAspect(int physWidth, int physHeight);
    void setScreenshot(const QImage &image);
    void setThumbnails(const QVector<QImage> &images);
    void setHighlighted(bool on);

    int spaceIndex() const { return m_index; }
    double aspect() const { return m_aspect; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void activated(int spaceIndex);
    void hovered(int spaceIndex);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void applyAspectLayout();
    QSize previewSizeForAspect() const;
    void paintPixmap();

    int m_index = -1;
    bool m_current = false;
    bool m_highlight = false;
    double m_aspect = 16.0 / 9.0; // width / height of the real monitor
    bool m_aspectFromMonitor = false;
    QImage m_image;
    QLabel *m_title = nullptr;
    QLabel *m_badge = nullptr;
    QLabel *m_preview = nullptr;
    QWidget *m_thumbRow = nullptr;
};
