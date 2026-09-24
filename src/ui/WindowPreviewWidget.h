#pragma once

#include <QFrame>
#include <QImage>
#include <Windows.h>

class QLabel;

// Draggable thumbnail of one top-level window (Mission Control bottom strip).
class WindowPreviewWidget : public QFrame {
    Q_OBJECT
public:
    static const char *kMimeType; // "application/x-spacewm-hwnd"

    explicit WindowPreviewWidget(QWidget *parent = nullptr);

    void setWindow(HWND hwnd, const QString &title, const QImage &preview);
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

    HWND m_hwnd = nullptr;
    QString m_title;
    QImage m_image;
    QPoint m_pressPos;
    bool m_dragging = false;
    QLabel *m_label = nullptr;
    QLabel *m_imageLabel = nullptr;
};
