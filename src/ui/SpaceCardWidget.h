#pragma once

#include <QFrame>
#include <QHash>
#include <QImage>
#include <QLabel>
#include <QVector>
#include <Windows.h>

class QHBoxLayout;
class QVBoxLayout;

// One space tile inside the overview: title, window count, stacked thumbnails.
class SpaceCardWidget : public QFrame {
    Q_OBJECT
public:
    explicit SpaceCardWidget(QWidget *parent = nullptr);

    void setSpace(int index, const QString &name, bool current);
    void setThumbnails(const QVector<QImage> &images);
    void setHighlighted(bool on);

    int spaceIndex() const { return m_index; }

signals:
    void activated(int spaceIndex);
    void hovered(int spaceIndex);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    int m_index = -1;
    bool m_current = false;
    bool m_highlight = false;
    QLabel *m_title = nullptr;
    QLabel *m_badge = nullptr;
    QWidget *m_thumbRow = nullptr;
    QHBoxLayout *m_thumbLayout = nullptr;
    QVector<QLabel *> m_thumbs;
};
