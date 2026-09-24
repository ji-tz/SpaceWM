#pragma once

#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QVector>
#include <QtGlobal>

class QMimeData;

// One space tile in the top strip: drop target + click-to-switch.
// Compact mode shrinks the preview for the Mission Control space bar.
class SpaceCardWidget : public QFrame {
    Q_OBJECT
public:
    explicit SpaceCardWidget(QWidget *parent = nullptr);

    void setSpace(int index, const QString &name, bool current);
    void setMonitorAspect(int physWidth, int physHeight);
    void setScreenshot(const QImage &image);
    void setThumbnails(const QVector<QImage> &images);
    void setHighlighted(bool on);
    // Top strip uses smaller budget so a row of spaces fits.
    void setCompact(bool compact);

    int spaceIndex() const { return m_index; }
    double aspect() const { return m_aspect; }
    bool isCompact() const { return m_compact; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void activated(int spaceIndex);
    void hovered(int spaceIndex);
    // Window dropped onto this space card (Mission Control placement).
    void windowDropped(int spaceIndex, quint64 hwnd);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void applyAspectLayout();
    QSize previewSizeForAspect() const;
    void paintPixmap();
    static bool extractHwnd(const QMimeData *mime, quint64 *out);

    int m_index = -1;
    bool m_current = false;
    bool m_highlight = false;
    bool m_compact = false;
    bool m_dropHover = false;
    double m_aspect = 16.0 / 9.0;
    bool m_aspectFromMonitor = false;
    QImage m_image;
    QLabel *m_title = nullptr;
    QLabel *m_badge = nullptr;
    QLabel *m_preview = nullptr;
    QWidget *m_thumbRow = nullptr;
};
