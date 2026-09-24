#include "OverviewWindow.h"

#include "SpaceCardWidget.h"
#include "../core/ThumbnailCapture.h"
#include "../core/WindowTracker.h"

#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPropertyAnimation>
#include <QTimer>

OverviewWindow::OverviewWindow(SpaceManager *manager, QWidget *parent)
    // No WindowDoesNotAcceptFocus — we need reliable focus for Esc/arrows
    // and so Ctrl+Alt+Space can toggle without a desktop click first.
    : QWidget(parent, Qt::FramelessWindowHint | Qt::Tool)
    , m_manager(manager)
{
    setAttribute(Qt::WA_DeleteOnClose, false);
    setObjectName(QStringLiteral("OverviewRoot"));
    setStyleSheet(QStringLiteral(
        "#OverviewRoot { background: rgba(8, 8, 12, 210); }"));

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

    auto *hint = new QLabel(
        tr("←/→ or 1–N select · Enter switch · Esc close · click card"), m_root);
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
    if (m_closePending)
        return; // still tearing down — ignore re-open (prevents animation re-entry crash)

    auto *m = m_manager ? m_manager->monitorOf(hmon) : nullptr;
    if (!m)
        return;

    cancelAnimations();
    m_hmon = hmon;
    setGeometry(m->geometry);

    rebuildCards();

    if (m_manager)
        m_manager->setOverviewOpen(true);
    m_open = true;
    m_pendingCommit = -1;
    m_selected = qBound(0, m->currentIndex, qMax(0, m_cards.size() - 1));

    show();
    raise();
    activateWindow();
    setFocus(Qt::ActiveWindowFocusReason);
    if (HWND h = reinterpret_cast<HWND>(winId())) {
        ::SetForegroundWindow(h);
        ::SetFocus(h);
    }

    playEnterAnimation();
}

void OverviewWindow::closeOverview(bool commit)
{
    if (!m_open || m_closePending)
        return;

    m_pendingCommit = commit ? m_selected : -1;
    m_open = false;
    m_closePending = true;
    cancelAnimations();
    playExitAnimation();
}

void OverviewWindow::finishClose()
{
    hide();
    if (m_manager)
        m_manager->setOverviewOpen(false);

    const int chosen = m_pendingCommit;
    m_pendingCommit = -1;
    m_closePending = false;
    m_animating = false;
    emit closed(chosen);
}

void OverviewWindow::cancelAnimations()
{
    if (m_fadeAnim) {
        auto *a = m_fadeAnim;
        m_fadeAnim = nullptr;
        a->stop();
        a->disconnect(this);
        a->deleteLater();
    }
    // Graphics effect is parented to this widget; clear without double-delete.
    if (graphicsEffect())
        setGraphicsEffect(nullptr);

    // Stop any pending card-move animations by clearing effects on cards.
    for (SpaceCardWidget *card : std::as_const(m_cards)) {
        if (card && card->graphicsEffect())
            card->setGraphicsEffect(nullptr);
    }
    m_animating = false;
}

void OverviewWindow::rebuildCards()
{
    auto *m = m_manager ? m_manager->monitorOf(m_hmon) : nullptr;
    if (!m)
        return;

    m_header->setText(tr("Monitor spaces — %1").arg(m->deviceName));

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

        QVector<QImage> imgs;
        int captured = 0;
        for (HWND h : m->spaces[i].windows) {
            if (captured >= 3)
                break;
            if (!::IsWindow(h) || ::IsIconic(h))
                continue;
            QImage img = thumbs::capture(h, QSize(240, 135));
            if (!img.isNull()) {
                imgs.push_back(img);
                ++captured;
            }
        }
        card->setThumbnails(imgs);

        connect(card, &SpaceCardWidget::activated, this, [this](int idx) {
            if (!m_open)
                return;
            if (idx >= 0 && idx < m_cards.size())
                m_selected = idx;
            closeOverview(true);
        });
        connect(card, &SpaceCardWidget::hovered, this, [this](int idx) {
            if (m_open)
                setSelected(idx);
        });

        m_cardRow->addWidget(card);
        m_cards.push_back(card);
    }
    m_cardRow->addStretch(1);
    setSelected(m_cards.isEmpty() ? -1 : current);
}

void OverviewWindow::setSelected(int index)
{
    if (m_cards.isEmpty())
        return;
    if (index < 0 || index >= m_cards.size())
        return;
    m_selected = index;
    for (int i = 0; i < m_cards.size(); ++i)
        m_cards[i]->setHighlighted(i == index);
}

void OverviewWindow::keyPressEvent(QKeyEvent *event)
{
    if (!m_open || m_closePending) {
        QWidget::keyPressEvent(event);
        return;
    }

    const int n = m_cards.size();
    // Guard: empty card row must never hit % 0 (was a hard crash).
    if (n <= 0) {
        if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Return
            || event->key() == Qt::Key_Enter) {
            closeOverview(event->key() != Qt::Key_Escape);
            event->accept();
        } else {
            QWidget::keyPressEvent(event);
        }
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
        setSelected((m_selected - 1 + n) % n);
        event->accept();
        return;
    case Qt::Key_Right:
        setSelected((m_selected + 1) % n);
        event->accept();
        return;
    case Qt::Key_Home:
        setSelected(0);
        event->accept();
        return;
    case Qt::Key_End:
        setSelected(n - 1);
        event->accept();
        return;
    default:
        break;
    }

    if (event->key() >= Qt::Key_1 && event->key() <= Qt::Key_9) {
        const int idx = event->key() - Qt::Key_1;
        if (idx < n) {
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
    if (m_open && !m_closePending) {
        closeOverview(true);
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void OverviewWindow::playEnterAnimation()
{
    m_animating = true;

    // Fade via a single owned animation. Do NOT double-delete the effect:
    // setGraphicsEffect(nullptr) already destroys the previous effect.
    auto *eff = new QGraphicsOpacityEffect(this);
    eff->setOpacity(1.0); // start fully visible; skip flashy fade if anims are flaky
    setGraphicsEffect(eff);

    auto *anim = new QPropertyAnimation(eff, "opacity", this);
    m_fadeAnim = anim;
    anim->setDuration(160);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(anim, &QPropertyAnimation::finished, this, [this]() {
        if (m_fadeAnim == sender()) {
            m_fadeAnim = nullptr;
            if (graphicsEffect())
                setGraphicsEffect(nullptr); // deletes effect once
        }
        if (!m_closePending)
            m_animating = false;
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);

    // Stagger card rise without graphics effects (effects on cards caused paint crashes).
    for (int i = 0; i < m_cards.size(); ++i) {
        auto *card = m_cards[i];
        if (!card)
            continue;
        const QPoint end = card->pos();
        const QPoint start = end + QPoint(0, 20);
        card->move(start);
        QTimer::singleShot(i * 30, this, [card, end]() {
            if (!card)
                return;
            auto *a = new QPropertyAnimation(card, "pos", card);
            a->setDuration(200);
            a->setStartValue(card->pos());
            a->setEndValue(end);
            a->setEasingCurve(QEasingCurve::OutCubic);
            a->start(QAbstractAnimation::DeleteWhenStopped);
        });
    }
}

void OverviewWindow::playExitAnimation()
{
    m_animating = true;

    auto *eff = new QGraphicsOpacityEffect(this);
    eff->setOpacity(1.0);
    setGraphicsEffect(eff);

    auto *anim = new QPropertyAnimation(eff, "opacity", this);
    m_fadeAnim = anim;
    anim->setDuration(120);
    anim->setStartValue(1.0);
    anim->setEndValue(0.0);
    anim->setEasingCurve(QEasingCurve::InCubic);
    connect(anim, &QPropertyAnimation::finished, this, [this]() {
        if (m_fadeAnim == sender())
            m_fadeAnim = nullptr;
        if (graphicsEffect())
            setGraphicsEffect(nullptr);
        finishClose();
    });
    // Safety: if animation is destroyed without finished (rare), still close.
    connect(anim, &QObject::destroyed, this, [this]() {
        if (m_closePending && isVisible())
            finishClose();
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}
