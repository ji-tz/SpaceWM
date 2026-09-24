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
    // Show top-right "×" on hover (only for spaces that may be deleted).
    void setRemovable(bool on);
    bool isRemovable() const { return m_removable; }

    int spaceIndex() const { return m_index; }
    double aspect() const { return m_aspect; }
    bool isCompact() const { return m_compact; }
    // Exposed for tests: badge rect in widget coords.
    QRect removeBadge() const { return removeBadgeRect(); }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void activated(int spaceIndex);
    void hovered(int spaceIndex);
    // Pointer left the card (paint only — does NOT reset bottom strip).
    void hoverLeft();
    // Window dropped onto this space card (Mission Control placement).
    void windowDropped(int spaceIndex, quint64 hwnd);
    // User clicked the top-right × (delete this space).
    void removeRequested(int spaceIndex);
    // Drag-to-reorder: user dragged this card onto target index.
    void reorderRequested(int from, int to);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void applyAspectLayout();
    QSize previewSizeForAspect() const;
    void paintPixmap();
    static bool extractHwnd(const QMimeData *mime, quint64 *out);
    // Hit-test top-right close badge (for tests / paint).
    QRect removeBadgeRect() const;

    int m_index = -1;
    bool m_current = false;
    bool m_highlight = false;
    bool m_compact = false;
    bool m_dropHover = false;
    bool m_removable = false;
    bool m_hovered = false;
    bool m_pressed = false;
    bool m_draggingReorder = false;
    QPoint m_pressPos;
    int m_lastReorderTarget = -1;
    double m_aspect = 16.0 / 9.0;
    bool m_aspectFromMonitor = false;
    QImage m_image;
    QLabel *m_title = nullptr;
    QLabel *m_badge = nullptr;
    QLabel *m_preview = nullptr;
    QWidget *m_thumbRow = nullptr;
};
