#include "WindowPreviewWidget.h"

#include <QApplication>
#include <QDataStream>
#include <QDrag>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>

const char *WindowPreviewWidget::kMimeType = "application/x-spacewm-hwnd";

WindowPreviewWidget::WindowPreviewWidget(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("WindowPreview"));
    setAttribute(Qt::WA_Hover);
    setMinimumSize(200, 140);
    setMaximumSize(280, 200);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setCursor(Qt::OpenHandCursor);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    m_imageLabel = new QLabel(this);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setMinimumSize(184, 104);
    m_imageLabel->setStyleSheet(QStringLiteral(
        "QLabel { border-radius: 6px; border: 1px solid rgba(255,255,255,35); background: #101018; }"));
    root->addWidget(m_imageLabel);

    m_label = new QLabel(this);
    m_label->setStyleSheet(QStringLiteral(
        "QLabel { color: #e8e8f0; font-size: 12px; border: none; background: transparent; }"));
    m_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    root->addWidget(m_label);

    setStyleSheet(QStringLiteral(
        "#WindowPreview { background: rgba(40, 44, 58, 230); border: 2px solid rgba(255,255,255,35);"
        " border-radius: 12px; }"
        "#WindowPreview:hover { border: 2px solid rgba(158,193,255,180); background: rgba(48,54,70,240); }"));
}

void WindowPreviewWidget::setWindow(HWND hwnd, const QString &title, const QImage &preview)
{
    m_hwnd = hwnd;
    m_title = title;
    m_image = preview;

    m_label->setText(title.isEmpty() ? tr("Untitled") : title);
    if (preview.isNull()) {
        m_imageLabel->setText(tr("No shot"));
        m_imageLabel->setPixmap(QPixmap());
    } else {
        m_imageLabel->setText({});
        const QSize target(m_imageLabel->width() > 0 ? m_imageLabel->width() : 184,
                           m_imageLabel->height() > 0 ? m_imageLabel->height() : 104);
        m_imageLabel->setPixmap(QPixmap::fromImage(preview).scaled(
            target, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    }
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
        if (!m_dragging
            && (event->pos() - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
            m_dragging = true;
            startDrag();
            m_dragging = false;
            setCursor(Qt::OpenHandCursor);
            return;
        }
    }
    QFrame::mouseMoveEvent(event);
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

    QPixmap pm;
    if (!m_image.isNull())
        pm = QPixmap::fromImage(m_image).scaled(160, 90, Qt::KeepAspectRatioByExpanding,
                                                Qt::SmoothTransformation);
    if (pm.isNull())
        pm = grab().scaled(160, 90, Qt::KeepAspectRatioByExpanding);

    auto *drag = new QDrag(this);
    drag->setMimeData(mime);
    if (!pm.isNull())
        drag->setPixmap(pm);
    emit dragStarted(reinterpret_cast<quint64>(m_hwnd));
    drag->exec(Qt::CopyAction);
}

void WindowPreviewWidget::paintEvent(QPaintEvent *event)
{
    QFrame::paintEvent(event);
}
