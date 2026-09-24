#include "ui/preview/AddSpaceButton.h"
#include "ui/preview/WindowPreviewWidget.h"

#include <QDataStream>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QMimeData>
#include <QStyle>

AddSpaceButton::AddSpaceButton(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("AddSpaceButton"));
    setAcceptDrops(true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(72, 72);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setToolTip(tr("Add space (or drop a window here)"));
    setStyleSheet(QStringLiteral(
        "#AddSpaceButton { background: rgba(28, 28, 34, 180); border: 2px dashed rgba(255,255,255,70);"
        " border-radius: 14px; }"
        "#AddSpaceButton:hover, #AddSpaceButton[drophover=\"true\"] {"
        " background: rgba(40, 50, 70, 220); border: 2px solid #7aa2ff; }"));
}

void AddSpaceButton::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit addRequested();
        event->accept();
        return;
    }
    QFrame::mousePressEvent(event);
}

void AddSpaceButton::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData() && event->mimeData()->hasFormat(WindowPreviewWidget::kMimeType)) {
        m_dropHover = true;
        setProperty("drophover", true);
        style()->unpolish(this);
        style()->polish(this);
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void AddSpaceButton::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData() && event->mimeData()->hasFormat(WindowPreviewWidget::kMimeType))
        event->acceptProposedAction();
    else
        event->ignore();
}

void AddSpaceButton::dragLeaveEvent(QDragLeaveEvent *event)
{
    m_dropHover = false;
    setProperty("drophover", false);
    style()->unpolish(this);
    style()->polish(this);
    QFrame::dragLeaveEvent(event);
}

void AddSpaceButton::dropEvent(QDropEvent *event)
{
    m_dropHover = false;
    setProperty("drophover", false);
    style()->unpolish(this);
    style()->polish(this);

    const QMimeData *mime = event->mimeData();
    if (mime && mime->hasFormat(WindowPreviewWidget::kMimeType)) {
        QDataStream ds(mime->data(WindowPreviewWidget::kMimeType));
        quint64 h = 0;
        ds >> h;
        if (h && handleWindowDrop(h)) {
            event->acceptProposedAction();
            return;
        }
    }
    event->ignore();
}

bool AddSpaceButton::handleWindowDrop(quint64 hwnd)
{
    if (!hwnd)
        return false;
    emit windowDropped(hwnd);
    return true;
}

void AddSpaceButton::paintEvent(QPaintEvent *event)
{
    QFrame::paintEvent(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(200, 210, 230, 220), 3));
    const QRectF r = QRectF(rect()).adjusted(22, 22, -22, -22);
    p.drawLine(r.center().x(), r.top(), r.center().x(), r.bottom());
    p.drawLine(r.left(), r.center().y(), r.right(), r.center().y());
}
