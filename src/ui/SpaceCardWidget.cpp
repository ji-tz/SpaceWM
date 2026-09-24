#include "SpaceCardWidget.h"

#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QStyle>
#include <QVBoxLayout>

SpaceCardWidget::SpaceCardWidget(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("SpaceCard"));
    setAttribute(Qt::WA_Hover);
    setCursor(Qt::PointingHandCursor);
    setMinimumSize(280, 200);
    setMaximumHeight(320);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(10);

    auto *head = new QHBoxLayout;
    m_title = new QLabel(QStringLiteral("Space"), this);
    m_title->setStyleSheet(QStringLiteral(
        "QLabel { color: #f0f0f0; font-size: 18px; font-weight: 600; border: none; background: transparent; }"));
    m_badge = new QLabel(this);
    m_badge->setStyleSheet(QStringLiteral(
        "QLabel { color: #0b0b0f; font-size: 12px; font-weight: 700; padding: 2px 8px; "
        "border-radius: 9px; background: #7aa2ff; border: none; }"));
    m_badge->setAlignment(Qt::AlignCenter);
    head->addWidget(m_title, 1);
    head->addWidget(m_badge, 0, Qt::AlignTop);
    root->addLayout(head);

    m_thumbRow = new QWidget(this);
    m_thumbLayout = new QHBoxLayout(m_thumbRow);
    m_thumbLayout->setContentsMargins(0, 0, 0, 0);
    m_thumbLayout->setSpacing(8);
    root->addWidget(m_thumbRow, 1);

    setStyleSheet(QStringLiteral(
        "#SpaceCard { background: rgba(28, 28, 34, 220); border: 2px solid rgba(255,255,255,40);"
        " border-radius: 14px; }"
        "#SpaceCard[current=\"true\"] { border: 2px solid #7aa2ff; }"
        "#SpaceCard[highlight=\"true\"] { background: rgba(40, 44, 56, 235); border: 2px solid #9ec1ff; }"));
}

void SpaceCardWidget::setSpace(int index, const QString &name, bool current)
{
    m_index = index;
    m_current = current;
    m_title->setText(name);
    m_badge->setText(current ? tr("CURRENT") : QString::number(index + 1));
    m_badge->setStyleSheet(current
        ? QStringLiteral("QLabel { color: #0b0b0f; font-size: 12px; font-weight: 700; padding: 2px 8px; border-radius: 9px; background: #7aa2ff; border: none; }")
        : QStringLiteral("QLabel { color: #c8c8d0; font-size: 12px; font-weight: 600; padding: 2px 8px; border-radius: 9px; background: rgba(255,255,255,30); border: none; }"));
    setProperty("current", current);
    style()->unpolish(this);
    style()->polish(this);
}

void SpaceCardWidget::setThumbnails(const QVector<QImage> &images)
{
    // Rebuild lightly — overview open is infrequent.
    while (QLayoutItem *item = m_thumbLayout->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    m_thumbs.clear();

    const int maxShow = 3;
    int shown = 0;
    for (const QImage &img : images) {
        if (shown >= maxShow)
            break;
        if (img.isNull())
            continue;
        auto *lab = new QLabel(m_thumbRow);
        lab->setPixmap(QPixmap::fromImage(img).scaled(
            QSize(120, 68), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
        lab->setFixedSize(120, 68);
        lab->setStyleSheet(QStringLiteral(
            "QLabel { border-radius: 6px; border: 1px solid rgba(255,255,255,50); background: #111; }"));
        m_thumbLayout->addWidget(lab);
        m_thumbs.push_back(lab);
        ++shown;
    }

    if (shown == 0) {
        auto *lab = new QLabel(tr("Empty"), m_thumbRow);
        lab->setStyleSheet(QStringLiteral(
            "QLabel { color: rgba(255,255,255,90); font-size: 13px; border: none; background: transparent; }"));
        m_thumbLayout->addWidget(lab);
        m_thumbs.push_back(lab);
    }
    m_thumbLayout->addStretch(1);
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

void SpaceCardWidget::paintEvent(QPaintEvent *event)
{
    QFrame::paintEvent(event);
}
