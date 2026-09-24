#pragma once

#include <QFrame>
#include <QImage>
#include <QSize>
#include <Windows.h>

class QLabel;

// Draggable preview of one top-level window.
// Tile size follows the real window aspect ratio (not a fixed thumbnail box).
class WindowPreviewWidget : public QFrame {
    Q_OBJECT
public:
    static const char *kMimeType; // "application/x-spacewm-hwnd"

    explicit WindowPreviewWidget(QWidget *parent = nullptr);

    void setWindow(HWND hwnd, const QString &title, const QImage &preview);
    // imageBox = pixel size of the picture area (real aspect preserved by caller).
    void setImageBoxSize(const QSize &imageBox);
    QSize imageBoxSize() const;

    HWND windowHandle() const { return m_hwnd; }
    QString windowTitle() const { return m_title; }

signals:
    void dragStarted(quint64 hwnd);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void startDrag();
    void applyPixmap();

    HWND m_hwnd = nullptr;
    QString m_title;
    QImage m_image;
    QSize m_box{184, 104};
    QPoint m_pressPos;
    bool m_dragging = false;
    QLabel *m_label = nullptr;
    QLabel *m_imageLabel = nullptr;
};
