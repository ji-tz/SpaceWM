#include "SpaceCardWidget.h"

#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QVBoxLayout>

SpaceCardWidget::SpaceCardWidget(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("SpaceCard"));
    setAttribute(Qt::WA_Hover);
    setCursor(Qt::PointingHandCursor);
    setMinimumSize(300, 220);
    setMaximumHeight(360);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 14);
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
    m_preview->setMinimumSize(260, 150);
    m_preview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_preview->setStyleSheet(QStringLiteral(
        "QLabel { border-radius: 8px; border: 1px solid rgba(255,255,255,40); background: #0c0c10; }"));
    root->addWidget(m_preview, 1);

    m_thumbRow = new QWidget(this);
    m_thumbRow->hide();
    root->addWidget(m_thumbRow);

    setStyleSheet(QStringLiteral(
        "#SpaceCard { background: rgba(28, 28, 34, 220); border: 2px solid rgba(255,255,255,40);"
        " border-radius: 14px; }"
        "#SpaceCard[current=\"true\"] { border: 2px solid #7aa2ff; }"
        "#SpaceCard[highlight=\"true\"] { background: rgba(40, 44, 56, 235); border: 2px solid #9ec1ff; }"));
    showPlaceholder();
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

void SpaceCardWidget::clearPreview()
{
    m_preview->setPixmap(QPixmap());
    m_preview->setText({});
}

void SpaceCardWidget::showPlaceholder()
{
    clearPreview();
    m_preview->setText(tr("No preview"));
    m_preview->setStyleSheet(QStringLiteral(
        "QLabel { color: rgba(255,255,255,80); font-size: 13px; border-radius: 8px;"
        " border: 1px solid rgba(255,255,255,40); background: #0c0c10; }"));
}

void SpaceCardWidget::setScreenshot(const QImage &image)
{
    if (image.isNull()) {
        showPlaceholder();
        return;
    }
    m_preview->setText({});
    m_preview->setStyleSheet(QStringLiteral(
        "QLabel { border-radius: 8px; border: 1px solid rgba(255,255,255,40); background: #0c0c10; }"));
    // Scale on paint via pixmap; use KeepAspectRatio so mixed DPI shots fit.
    const QPixmap pm = QPixmap::fromImage(image);
    const QSize target = (m_preview->width() > 0 && m_preview->height() > 0)
        ? m_preview->size()
        : QSize(320, 180);
    m_preview->setPixmap(pm.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void SpaceCardWidget::setThumbnails(const QVector<QImage> &images)
{
    // Prefer a full screenshot API; keep this as fallback only.
    for (const QImage &img : images) {
        if (!img.isNull()) {
            setScreenshot(img);
            return;
        }
    }
    showPlaceholder();
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

void SpaceCardWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        emit activated(m_index);
    QFrame::mousePressEvent(event);
}

void SpaceCardWidget::enterEvent(QEnterEvent *event)
{
    emit hovered(m_index);
    QFrame::enterEvent(event);
}
