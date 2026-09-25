#include "ui/preview/SpaceCardWidget.h"
#include "ui/preview/WindowPreviewWidget.h"

#include <QApplication>
#include <QDataStream>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QMimeData>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace {
constexpr int kPreviewMaxW = 340;
constexpr int kPreviewMaxH = 240;
constexpr int kPreviewMinEdge = 80;
constexpr int kCompactMaxW = 200;
constexpr int kCompactMaxH = 140;
} // namespace

SpaceCardWidget::SpaceCardWidget(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("SpaceCard"));
    setAttribute(Qt::WA_Hover);
    setCursor(Qt::PointingHandCursor);
    setAcceptDrops(true);

    m_revealTimer = new QTimer(this);
    m_revealTimer->setSingleShot(true);
    m_revealTimer->setInterval(m_revealDelayMs);
    connect(m_revealTimer, &QTimer::timeout, this, [this]() {
        if (!m_removable || !m_hovered)
            return;
        m_removeShown = true;
        update();
    });

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 10, 12, 12);
    root->setSpacing(8);

    auto *head = new QHBoxLayout;
    m_title = new QLabel(QStringLiteral("Space"), this);
    m_title->setStyleSheet(QStringLiteral(
        "QLabel { color: #f0f0f0; font-size: 16px; font-weight: 600; border: none; background: transparent; }"));
    m_badge = new QLabel(this);
    m_badge->setAlignment(Qt::AlignCenter);
    head->addWidget(m_title, 1);
    head->addWidget(m_badge, 0, Qt::AlignTop);
    root->addLayout(head);

    m_preview = new QLabel(this);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_preview->setStyleSheet(QStringLiteral(
        "QLabel { border-radius: 8px; border: 1px solid rgba(255,255,255,40); background: #0c0c10; }"));
    root->addWidget(m_preview, 0, Qt::AlignHCenter);

    m_thumbRow = new QWidget(this);
    m_thumbRow->hide();
    root->addWidget(m_thumbRow);
    root->addStretch(1);

    setStyleSheet(QStringLiteral(
        "#SpaceCard { background: rgba(28, 28, 34, 220); border: 2px solid rgba(255,255,255,40);"
        " border-radius: 14px; }"
        "#SpaceCard[current=\"true\"] { border: 2px solid #7aa2ff; }"
        "#SpaceCard[highlight=\"true\"] { background: rgba(40, 44, 56, 235); border: 2px solid #9ec1ff; }"
        "#SpaceCard[drophover=\"true\"] { background: rgba(50, 70, 110, 245); border: 3px solid #7affc8; }"));

    applyAspectLayout();
}

QSize SpaceCardWidget::previewSizeForAspect() const
{
    const double a = m_aspect > 0.001 ? m_aspect : (16.0 / 9.0);
    const int maxW = m_compact ? kCompactMaxW : kPreviewMaxW;
    const int maxH = m_compact ? kCompactMaxH : kPreviewMaxH;
    int w = maxW;
    int h = int(qRound(w / a));
    if (h > maxH) {
        h = maxH;
        w = int(qRound(h * a));
    }
    w = std::max(w, kPreviewMinEdge);
    h = std::max(h, kPreviewMinEdge);
    return {w, h};
}

void SpaceCardWidget::applyAspectLayout()
{
    const QSize s = previewSizeForAspect();
    m_preview->setFixedSize(s);
    setMinimumWidth(s.width() + 28);
    setMaximumWidth(s.width() + 40);
    setMinimumHeight(s.height() + (m_compact ? 56 : 72));
    setMaximumHeight(s.height() + (m_compact ? 64 : 88));
    updateGeometry();
}

QSize SpaceCardWidget::sizeHint() const
{
    const QSize s = previewSizeForAspect();
    return {s.width() + 28, s.height() + (m_compact ? 60 : 80)};
}

QSize SpaceCardWidget::minimumSizeHint() const
{
    return sizeHint();
}

void SpaceCardWidget::setCompact(bool compact)
{
    if (m_compact == compact)
        return;
    m_compact = compact;
    if (m_title)
        m_title->setStyleSheet(compact
            ? QStringLiteral("QLabel { color: #f0f0f0; font-size: 14px; font-weight: 600; border: none; background: transparent; }")
            : QStringLiteral("QLabel { color: #f0f0f0; font-size: 16px; font-weight: 600; border: none; background: transparent; }"));
    applyAspectLayout();
    paintPixmap();
}

void SpaceCardWidget::setMonitorAspect(int physWidth, int physHeight)
{
    if (physWidth <= 0 || physHeight <= 0)
        return;
    const double a = double(physWidth) / double(physHeight);
    m_aspectFromMonitor = true;
    if (qFuzzyCompare(m_aspect, a))
        return;
    m_aspect = a;
    applyAspectLayout();
    paintPixmap();
}

void SpaceCardWidget::setSpace(int index, const QString &name, bool current)
{
    m_index = index;
    m_current = current;
    m_title->setText(name);
    m_badge->setText(current ? tr("CURRENT") : QString::number(index + 1));
    m_badge->setStyleSheet(current
        ? QStringLiteral("QLabel { color: #0b0b0f; font-size: 11px; font-weight: 700; padding: 2px 8px; border-radius: 9px; background: #7aa2ff; border: none; }")
        : QStringLiteral("QLabel { color: #c8c8d0; font-size: 11px; font-weight: 600; padding: 2px 8px; border-radius: 9px; background: rgba(255,255,255,30); border: none; }"));
    setProperty("current", current);
    style()->unpolish(this);
    style()->polish(this);
}

void SpaceCardWidget::paintPixmap()
{
    if (m_image.isNull() || !m_preview)
        return;
    m_preview->setText({});
    const QSize target = m_preview->size();
    if (target.width() <= 0 || target.height() <= 0)
        return;
    const QPixmap pm = QPixmap::fromImage(m_image);
    const QPixmap fit = pm.scaled(target, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    m_preview->setPixmap(fit);
}

void SpaceCardWidget::setScreenshot(const QImage &image)
{
    if (image.isNull())
        return;
    m_image = image;
    if (!m_aspectFromMonitor && image.height() > 0) {
        m_aspect = double(image.width()) / double(image.height());
        applyAspectLayout();
    }
    paintPixmap();
}

void SpaceCardWidget::setThumbnails(const QVector<QImage> &images)
{
    for (const QImage &img : images) {
        if (!img.isNull()) {
            setScreenshot(img);
            return;
        }
    }
}

void SpaceCardWidget::setHighlighted(bool on)
{
    if (m_highlight == on)
        return;
    m_highlight = on;
    setProperty("highlight", on);
    style()->unpolish(this);
    style()->polish(this);
    update();
}

void SpaceCardWidget::setExclusive(bool on)
{
    if (m_exclusive == on)
        return;
    m_exclusive = on;
    update();
}

void SpaceCardWidget::setRemovable(bool on)
{
    if (m_removable == on)
        return;
    m_removable = on;
    if (!on)
        cancelRevealTimer();
    update();
}

void SpaceCardWidget::setRemoveRevealDelayMs(int ms)
{
    m_revealDelayMs = qMax(0, ms);
    if (m_revealTimer)
        m_revealTimer->setInterval(m_revealDelayMs);
}

void SpaceCardWidget::startRevealTimer()
{
    if (!m_removable || !m_hovered || !m_revealTimer)
        return;
    if (m_revealDelayMs <= 0) {
        m_removeShown = true;
        update();
        return;
    }
    m_removeShown = false;
    update();
    m_revealTimer->start();
}

void SpaceCardWidget::cancelRevealTimer()
{
    if (m_revealTimer)
        m_revealTimer->stop();
    m_removeShown = false;
}

QRect SpaceCardWidget::removeBadgeRect() const
{
    const int s = m_compact ? 18 : 22;
    return QRect(width() - s - 6, 6, s, s);
}

void SpaceCardWidget::paintEvent(QPaintEvent *event)
{
    QFrame::paintEvent(event);

    // Exclusive badge (issue #1): small amber lock in the top-left corner.
    if (m_exclusive) {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const int s = m_compact ? 18 : 22;
        const QRect r(6, 6, s, s);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 193, 7, 230));
        p.drawRoundedRect(r, s / 3.0, s / 3.0);
        // Lock glyph: shackle arc + solid body.
        p.setPen(QPen(QColor(40, 40, 40), 2));
        p.setBrush(Qt::NoBrush);
        const int cx = r.center().x();
        const int bodyTop = r.top() + s * 2 / 5;
        p.drawArc(QRect(cx - s / 4, r.top() + 2, s / 2, s / 2), 0, 180 * 16);
        p.setBrush(QColor(40, 40, 40));
        p.drawRect(QRect(cx - s / 4, bodyTop, s / 2, r.bottom() - bodyTop));
    }

    if (!m_removable || !m_removeShown)
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRect r = removeBadgeRect();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 160));
    p.drawEllipse(r);
    p.setPen(QPen(QColor(255, 90, 90), 2));
    const int m = r.width() / 4;
    p.drawLine(r.left() + m, r.top() + m, r.right() - m, r.bottom() - m);
    p.drawLine(r.right() - m, r.top() + m, r.left() + m, r.bottom() - m);
}

bool SpaceCardWidget::extractHwnd(const QMimeData *mime, quint64 *out)
{
    if (!mime || !mime->hasFormat(WindowPreviewWidget::kMimeType))
        return false;
    const QByteArray payload = mime->data(WindowPreviewWidget::kMimeType);
    if (payload.size() < int(sizeof(quint64)))
        return false;
    QDataStream ds(payload);
    quint64 h = 0;
    ds >> h;
    if (!h)
        return false;
    if (out)
        *out = h;
    return true;
}

void SpaceCardWidget::dragEnterEvent(QDragEnterEvent *event)
{
    quint64 h = 0;
    if (extractHwnd(event->mimeData(), &h)) {
        m_dropHover = true;
        setProperty("drophover", true);
        style()->unpolish(this);
        style()->polish(this);
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void SpaceCardWidget::dragMoveEvent(QDragMoveEvent *event)
{
    quint64 h = 0;
    if (extractHwnd(event->mimeData(), &h))
        event->acceptProposedAction();
    else
        event->ignore();
}

void SpaceCardWidget::dragLeaveEvent(QDragLeaveEvent *event)
{
    m_dropHover = false;
    setProperty("drophover", false);
    style()->unpolish(this);
    style()->polish(this);
    QFrame::dragLeaveEvent(event);
}

void SpaceCardWidget::dropEvent(QDropEvent *event)
{
    quint64 h = 0;
    m_dropHover = false;
    setProperty("drophover", false);
    style()->unpolish(this);
    style()->polish(this);

    if (extractHwnd(event->mimeData(), &h) && m_index >= 0) {
        emit windowDropped(m_index, h);
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void SpaceCardWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        emit contextMenuRequested(m_index, event->globalPosition().toPoint());
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_removable && m_removeShown
        && removeBadgeRect().contains(event->pos())) {
        // Badge press: do not arm card activation.
        m_pressed = false;
        m_draggingReorder = false;
        emit removeRequested(m_index);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        m_pressed = true;
        m_draggingReorder = false;
        m_pressPos = event->pos();
        m_lastReorderTarget = -1;
    }
    QFrame::mousePressEvent(event);
}

void SpaceCardWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_pressed && (event->buttons() & Qt::LeftButton)) {
        if (!m_draggingReorder
            && (event->pos() - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
            m_draggingReorder = true;
        }
        if (m_draggingReorder) {
            QWidget *w = QApplication::widgetAt(event->globalPosition().toPoint());
            while (w && w != this) {
                if (auto *card = qobject_cast<SpaceCardWidget *>(w)) {
                    const int target = card->spaceIndex();
                    if (target >= 0 && target != m_index && target != m_lastReorderTarget) {
                        m_lastReorderTarget = target;
                        emit reorderRequested(m_index, target);
                    }
                    break;
                }
                w = w->parentWidget();
            }
        }
    }
    QFrame::mouseMoveEvent(event);
}

void SpaceCardWidget::mouseReleaseEvent(QMouseEvent *event)
{
    const bool cardPress = m_pressed;
    const bool wasDrag = m_draggingReorder;
    m_pressed = false;
    m_draggingReorder = false;
    m_lastReorderTarget = -1;
    // Only a completed card-body press+release (not badge, not reorder drag) activates.
    if (event->button() == Qt::LeftButton && cardPress && !wasDrag)
        emit activated(m_index);
    QFrame::mouseReleaseEvent(event);
}

void SpaceCardWidget::enterEvent(QEnterEvent *event)
{
    m_hovered = true;
    startRevealTimer();
    update();
    emit hovered(m_index);
    QFrame::enterEvent(event);
}

void SpaceCardWidget::leaveEvent(QEvent *event)
{
    // Do NOT reset the bottom strip here — only outer overview margins should.
    m_hovered = false;
    cancelRevealTimer();
    update();
    emit hoverLeft();
    QFrame::leaveEvent(event);
}

void SpaceCardWidget::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    paintPixmap();
}
