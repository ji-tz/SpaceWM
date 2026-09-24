#pragma once

#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QVector>

// One space tile: title + full-monitor screenshot preview.
class SpaceCardWidget : public QFrame {
    Q_OBJECT
public:
    explicit SpaceCardWidget(QWidget *parent = nullptr);

    void setSpace(int index, const QString &name, bool current);
    // Primary: a single full-screen shot of that space.
    void setScreenshot(const QImage &image);
    // Fallback tiled window thumbs (if no screenshot yet).
    void setThumbnails(const QVector<QImage> &images);
    void setHighlighted(bool on);

    int spaceIndex() const { return m_index; }

signals:
    void activated(int spaceIndex);
    void hovered(int spaceIndex);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;

private:
    void clearPreview();
    void showPlaceholder();

    int m_index = -1;
    bool m_current = false;
    bool m_highlight = false;
    QLabel *m_title = nullptr;
    QLabel *m_badge = nullptr;
    QLabel *m_preview = nullptr;
    QWidget *m_thumbRow = nullptr;
};
