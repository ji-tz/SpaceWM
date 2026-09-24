#pragma once

#include <QFrame>

// Trailing "+" tile on the space strip: click to add a space; drop a window
// to add a space and place that window into it.
class AddSpaceButton : public QFrame {
    Q_OBJECT
public:
    explicit AddSpaceButton(QWidget *parent = nullptr);

    // Drop a window HWND onto + (used by dropEvent and unit tests).
    bool handleWindowDrop(quint64 hwnd);

signals:
    void addRequested();
    void windowDropped(quint64 hwnd);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    bool m_dropHover = false;
};
