#include "ui/preview/WindowPreviewWidget.h"

#include <QApplication>
#include <QDataStream>
#include <QDrag>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

const char *WindowPreviewWidget::kMimeType = "application/x-spacewm-hwnd";

WindowPreviewWidget::WindowPreviewWidget(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("WindowPreview"));
    setAttribute(Qt::WA_Hover);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setCursor(Qt::OpenHandCursor);

    auto *root = new QVBoxLayout(this);
    // Budget must match setImageBoxSize: frame(4) + margins(16) + spacing(6) + header(16) = 42.
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    // Header ABOVE the image: title left, close button right. The button is a
    // child, so its clicks never reach this frame (no drag / no activation).
    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(4);

    m_label = new QLabel(this);
    m_label->setObjectName(QStringLiteral("TitleLabel"));
    m_label->setStyleSheet(QStringLiteral(
        "QLabel { color: #e8e8f0; font-size: 12px; border: none; background: transparent; }"));
    m_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_label->setFixedHeight(14);
    m_label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    header->addWidget(m_label, 1);

    m_closeBtn = new QPushButton(QStringLiteral("×"), this);
    m_closeBtn->setObjectName(QStringLiteral("CloseButton"));
    m_closeBtn->setFixedSize(16, 16);
    m_closeBtn->setFocusPolicy(Qt::NoFocus);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setToolTip(tr("Close window"));
    m_closeBtn->setEnabled(false); // until setWindow supplies an hwnd
    m_closeBtn->setStyleSheet(QStringLiteral(
        "QPushButton { color: rgba(255,255,255,130); background: transparent; border: none;"
        " border-radius: 8px; font-size: 11px; font-weight: 600; padding: 0; }"
        "QPushButton:hover { color: white; background: rgba(255,86,86,230); }"
        "QPushButton:pressed { background: rgba(255,86,86,255); }"));
    connect(m_closeBtn, &QPushButton::clicked, this, [this]() {
        if (m_hwnd)
            emit closeRequested(reinterpret_cast<quint64>(m_hwnd));
    });
    header->addWidget(m_closeBtn, 0, Qt::AlignVCenter);
    root->addLayout(header);

    m_imageLabel = new QLabel(this);
    m_imageLabel->setObjectName(QStringLiteral("ImageLabel"));
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setStyleSheet(QStringLiteral(
        "QLabel { border-radius: 6px; border: 1px solid rgba(255,255,255,35); background: #101018; }"));
    root->addWidget(m_imageLabel);

    setStyleSheet(QStringLiteral(
        "#WindowPreview { background: rgba(40, 44, 58, 230); border: 2px solid rgba(255,255,255,35);"
        " border-radius: 12px; }"
        "#WindowPreview:hover { border: 2px solid rgba(158,193,255,180); background: rgba(48,54,70,240); }"));

    setImageBoxSize(m_box);
}

QSize WindowPreviewWidget::imageBoxSize() const
{
    return m_box;
}

void WindowPreviewWidget::setImageBoxSize(const QSize &imageBox)
{
    const int w = std::max(imageBox.width(), 80);
    const int h = std::max(imageBox.height(), 50);
    m_box = QSize(w, h);
    if (m_imageLabel)
        m_imageLabel->setFixedSize(m_box);
    // QFrame stylesheet border is 2px each side — include it so the image box isn't clipped.
    // width: frame(4) + margins(16) = 20; height: frame(4) + margins(16) + spacing(6) + header(16) = 42.
    setFixedSize(m_box.width() + 20, m_box.height() + 42);
    applyPixmap();
}

void WindowPreviewWidget::setWindow(HWND hwnd, const QString &title, const QImage &preview)
{
    m_hwnd = hwnd;
    m_title = title;
    m_image = preview;
    m_label->setText(title.isEmpty() ? tr("Untitled") : title);
    if (m_closeBtn)
        m_closeBtn->setEnabled(m_hwnd != nullptr);
    applyPixmap();
}

void WindowPreviewWidget::applyPixmap()
{
    if (!m_imageLabel)
        return;
    if (m_image.isNull()) {
        m_imageLabel->setText(tr("No shot"));
        m_imageLabel->setPixmap(QPixmap());
        return;
    }
    m_imageLabel->setText({});

    // Scale ONCE to the label's physical pixel size. A logical-size pixmap is
    // upscaled by the compositor on 150%/200% DPI and looks soft.
    const qreal dpr = m_imageLabel->devicePixelRatioF();
    if (dpr <= 0.0)
        return;
    const QSize phys(qMax(1, int(std::lround(m_box.width() * dpr))),
                     qMax(1, int(std::lround(m_box.height() * dpr))));

    QImage src = m_image;
    // Always fill the physical box so the tile is never letterboxed/cropped short.
    src = src.scaled(phys, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    QPixmap pm = QPixmap::fromImage(src);
    pm.setDevicePixelRatio(dpr);
    m_imageLabel->setPixmap(pm);
}

void WindowPreviewWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressPos = event->pos();
        m_dragging = false;
        setCursor(Qt::ClosedHandCursor);
    }
    QFrame::mousePressEvent(event);
}

void WindowPreviewWidget::mouseMoveEvent(QMouseEvent *event)
{
    if ((event->buttons() & Qt::LeftButton) && m_hwnd) {
        if (!m_dragging &&
            (event->pos() - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
            m_dragging = true;
            startDrag();
            m_dragging = false;
            setCursor(Qt::OpenHandCursor);
            return;
        }
    }
    QFrame::mouseMoveEvent(event);
}

void WindowPreviewWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const bool wasDrag = m_dragging || (event->pos() - m_pressPos).manhattanLength() >=
                                               QApplication::startDragDistance();
        setCursor(Qt::OpenHandCursor);
        m_dragging = false;
        if (!wasDrag && m_hwnd)
            emit activated(reinterpret_cast<quint64>(m_hwnd));
    }
    QFrame::mouseReleaseEvent(event);
}

void WindowPreviewWidget::startDrag()
{
    if (!m_hwnd)
        return;

    auto *mime = new QMimeData;
    QByteArray payload;
    QDataStream ds(&payload, QIODevice::WriteOnly);
    ds << quint64(m_hwnd);
    mime->setData(kMimeType, payload);

    // Ghost: compact “screen thumbnail” (not the full tile), 80% opacity.
    const QSize kDragThumbMax(240, 150);
    QPixmap pm;
    if (!m_image.isNull())
        pm = QPixmap::fromImage(m_image);
    if (pm.isNull())
        pm = grab();

    QSize thumbSize = pm.size();
    thumbSize.scale(kDragThumbMax, Qt::KeepAspectRatio);
    if (thumbSize.width() > 0 && thumbSize.height() > 0)
        pm = pm.scaled(thumbSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    if (!pm.isNull()) {
        QImage img = pm.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QImage faded(img.size(), QImage::Format_ARGB32_Premultiplied);
        faded.fill(Qt::transparent);
        {
            QPainter p(&faded);
            p.setOpacity(0.8);
            p.drawImage(0, 0, img);
        }
        pm = QPixmap::fromImage(faded);
    }

    auto *drag = new QDrag(this);
    drag->setMimeData(mime);
    if (!pm.isNull()) {
        drag->setPixmap(pm);
        // Ghost sticks to the press point, not the pixmap top-left corner.
        const QPoint imageTopLeft = m_imageLabel ? m_imageLabel->pos() : QPoint(0, 0);
        drag->setHotSpot(mapPressToHotSpot(m_pressPos, imageTopLeft, m_box, pm.size()));
    }
    emit dragStarted(reinterpret_cast<quint64>(m_hwnd));
    drag->exec(Qt::CopyAction);
}

QPoint WindowPreviewWidget::mapPressToHotSpot(const QPoint &pressInWidget,
                                              const QPoint &imageTopLeftInWidget, const QSize &box,
                                              const QSize &pixmapSize)
{
    if (pixmapSize.width() <= 0 || pixmapSize.height() <= 0)
        return QPoint(0, 0);

    const double bx = double(pressInWidget.x() - imageTopLeftInWidget.x());
    const double by = double(pressInWidget.y() - imageTopLeftInWidget.y());

    // Image box → actual pixmap (KeepAspectRatio may letterbox inside the box).
    double hx = bx;
    double hy = by;
    if (box.width() > 0 && box.height() > 0) {
        hx = bx * double(pixmapSize.width()) / double(box.width());
        hy = by * double(pixmapSize.height()) / double(box.height());
    }

    return QPoint(std::clamp(int(std::lround(hx)), 0, pixmapSize.width() - 1),
                  std::clamp(int(std::lround(hy)), 0, pixmapSize.height() - 1));
}

void WindowPreviewWidget::paintEvent(QPaintEvent *event)
{
    QFrame::paintEvent(event);
}
