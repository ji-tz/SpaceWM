#include "OverviewWindow.h"

#include "SpaceCardWidget.h"
#include "../core/ThumbnailCapture.h"
#include "../core/WindowTracker.h"

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPropertyAnimation>
#include <QScreen>
#include <QParallelAnimationGroup>
#include <QSequentialAnimationGroup>
#include <QGraphicsOpacityEffect>
#include <QTimer>

#include <dwmapi.h>

OverviewWindow::OverviewWindow(SpaceManager *manager, QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::Tool | Qt::WindowDoesNotAcceptFocus)
    , m_manager(manager)
{
    setAttribute(Qt::WA_DeleteOnClose, false);
    setObjectName(QStringLiteral("OverviewRoot"));
    setStyleSheet(QStringLiteral(
        "#OverviewRoot { background: rgba(8, 8, 12, 210); }"));

    // Ensure we don't get tracked by SpaceWM itself (skipped via process id).

    m_root = new QWidget(this);
    m_root->setObjectName(QStringLiteral("OverviewPanel"));
    m_root->setStyleSheet(QStringLiteral(
        "#OverviewPanel { background: transparent; }"));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(m_root);

    auto *v = new QVBoxLayout(m_root);
    v->setContentsMargins(48, 40, 48, 48);
    v->setSpacing(24);

    m_header = new QLabel(m_root);
    m_header->setStyleSheet(QStringLiteral(
        "QLabel { color: #ffffff; font-size: 28px; font-weight: 700; "
        "background: transparent; border: none; }"));
    v->addWidget(m_header);

    auto *hint = new QLabel(tr("←/→ or 1–N select · Enter switch · Esc close · click card"), m_root);
    hint->setStyleSheet(QStringLiteral(
        "QLabel { color: rgba(255,255,255,140); font-size: 14px; "
        "background: transparent; border: none; }"));
    v->addWidget(hint);

    auto *scrollHost = new QWidget(m_root);
    m_cardRow = new QHBoxLayout(scrollHost);
    m_cardRow->setContentsMargins(0, 8, 0, 0);
    m_cardRow->setSpacing(20);
    m_cardRow->addStretch(1);
    v->addWidget(scrollHost, 1);

    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
}

void OverviewWindow::openOnMonitor(HMONITOR hmon)
{
    m_hmon = hmon;
    auto *m = m_manager->monitorOf(hmon);
    if (!m)
        return;

    // Position fullscreen on that monitor (virtual desktop coordinates).
    const QRect g = m->geometry;
    setGeometry(g);

    // Per-screen translucent background works better with DWM blur on Win11,
    // but solid alpha is reliable across GPUs.
    rebuildCards();

    m_manager->setOverviewOpen(true);
    m_open = true;
    m_pendingCommit = -1;

    show();
    raise();
    activateWindow();
    setFocus(Qt::OtherFocusReason);
    // Tool windows don't take focus automatically on some setups:
    ::SetFocus((HWND)winId());

    playEnterAnimation();
}

void OverviewWindow::closeOverview(bool commit)
{
    if (!m_open)
        return;
    m_pendingCommit = commit ? m_selected : -1;
    m_open = false;

    playExitAnimation([this]() {
        hide();
        m_manager->setOverviewOpen(false);
        emit closed(m_pendingCommit);
        m_pendingCommit = -1;
    });
}

void OverviewWindow::rebuildCards()
{
    auto *m = m_manager->monitorOf(m_hmon);
    if (!m)
        return;

    m_header->setText(tr("Monitor spaces — %1").arg(m->deviceName));

    // Remove old cards
    while (QLayoutItem *item = m_cardRow->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    m_cards.clear();

    const int current = m->currentIndex;
    m_selected = current;

    for (int i = 0; i < m->spaces.size(); ++i) {
        auto *card = new SpaceCardWidget(m_root);
        card->setSpace(i, m->spaces[i].name, i == current);

        // Thumbnails for windows on this space (even if currently cloaked).
        QVector<QImage> imgs;
        QVector<HWND> hwnds;
        const auto &set = m->spaces[i].windows;
        hwnds.reserve(int(set.size()));
        for (HWND h : set)
            if (::IsWindow(h))
                hwnds.push_back(h);

        // Capture up to 3
        int captured = 0;
        for (HWND h : hwnds) {
            if (captured >= 3)
                break;
            QImage img = thumbs::capture(h, QSize(240, 135));
            if (!img.isNull()) {
                imgs.push_back(img);
                ++captured;
            }
        }
        card->setThumbnails(imgs);

        connect(card, &SpaceCardWidget::activated, this, [this](int idx) {
            m_selected = idx;
            closeOverview(true);
        });
        connect(card, &SpaceCardWidget::hovered, this, [this](int idx) {
            setSelected(idx);
        });

        m_cardRow->addWidget(card);
        m_cards.push_back(card);
    }
    m_cardRow->addStretch(1);

    // Select current
    setSelected(current);

    // Stagger cards slightly for enter animation targets.
    for (int i = 0; i < m_cards.size(); ++i) {
        m_cards[i]->setGraphicsEffect(nullptr);
    }
}

void OverviewWindow::setSelected(int index)
{
    if (index < 0 || index >= m_cards.size())
        return;
    m_selected = index;
    for (int i = 0; i < m_cards.size(); ++i)
        m_cards[i]->setHighlighted(i == index);
}

void OverviewWindow::keyPressEvent(QKeyEvent *event)
{
    if (!m_open) {
        QWidget::keyPressEvent(event);
        return;
    }

    switch (event->key()) {
    case Qt::Key_Escape:
        closeOverview(false);
        event->accept();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        closeOverview(true);
        event->accept();
        return;
    case Qt::Key_Left:
        setSelected((m_selected - 1 + m_cards.size()) % m_cards.size());
        event->accept();
        return;
    case Qt::Key_Right:
        setSelected((m_selected + 1) % m_cards.size());
        event->accept();
        return;
    case Qt::Key_Home:
        setSelected(0);
        event->accept();
        return;
    case Qt::Key_End:
        setSelected(m_cards.size() - 1);
        event->accept();
        return;
    default:
        break;
    }

    if (event->key() >= Qt::Key_1 && event->key() <= Qt::Key_9) {
        const int idx = event->key() - Qt::Key_1;
        if (idx < m_cards.size()) {
            m_selected = idx;
            closeOverview(true);
        }
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void OverviewWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (m_open) {
        closeOverview(true);
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void OverviewWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
}

void OverviewWindow::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
}

void OverviewWindow::playEnterAnimation()
{
    // Fade in the whole window.
    auto *eff = new QGraphicsOpacityEffect(this);
    setGraphicsEffect(eff);
    eff->setOpacity(0.0);
    auto *anim = new QPropertyAnimation(eff, "opacity", this);
    anim->setDuration(180);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(anim, &QPropertyAnimation::finished, this, [this, eff]() {
        setGraphicsEffect(nullptr);
        delete eff;
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);

    // Stagger cards: QPropertyAnimation has no setDelay — use a single-shot timer per card.
    for (int i = 0; i < m_cards.size(); ++i) {
        auto *card = m_cards[i];
        const QPoint end = card->pos();
        const QPoint start = end + QPoint(0, 24);
        card->move(start);
        QTimer::singleShot(i * 35, this, [card, end]() {
            auto *a = new QPropertyAnimation(card, "pos", card);
            a->setDuration(220);
            a->setStartValue(card->pos());
            a->setEndValue(end);
            a->setEasingCurve(QEasingCurve::OutCubic);
            a->start(QAbstractAnimation::DeleteWhenStopped);
        });
    }
}

void OverviewWindow::playExitAnimation(std::function<void()> after)
{
    auto *eff = new QGraphicsOpacityEffect(this);
    setGraphicsEffect(eff);
    eff->setOpacity(1.0);
    auto *anim = new QPropertyAnimation(eff, "opacity", this);
    anim->setDuration(140);
    anim->setStartValue(1.0);
    anim->setEndValue(0.0);
    anim->setEasingCurve(QEasingCurve::InCubic);
    connect(anim, &QPropertyAnimation::finished, this, [this, eff, after]() {
        setGraphicsEffect(nullptr);
        delete eff;
        if (after)
            after();
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}
