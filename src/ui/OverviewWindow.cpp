#include "OverviewWindow.h"

#include "SpaceCardWidget.h"
#include "../core/ThumbnailCapture.h"

#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPropertyAnimation>
#include <QScreen>
#include <QTimer>

OverviewWindow::OverviewWindow(SpaceManager *manager, QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::Tool)
    , m_manager(manager)
{
    setAttribute(Qt::WA_DeleteOnClose, false);
    setObjectName(QStringLiteral("OverviewRoot"));
    setAttribute(Qt::WA_TranslucentBackground, false);

    m_root = new QWidget(this);
    m_root->setObjectName(QStringLiteral("OverviewPanel"));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(m_root);

    auto *v = new QVBoxLayout(m_root);
    v->setContentsMargins(40, 32, 40, 40);
    v->setSpacing(20);

    m_header = new QLabel(m_root);
    m_header->setStyleSheet(QStringLiteral(
        "QLabel { color: #ffffff; font-size: 24px; font-weight: 700; "
        "background: transparent; border: none; }"));
    v->addWidget(m_header);

    auto *hint = new QLabel(
        tr("←/→ select · Enter switch · Esc cancel · click card"), m_root);
    hint->setStyleSheet(QStringLiteral(
        "QLabel { color: rgba(255,255,255,140); font-size: 13px; "
        "background: transparent; border: none; }"));
    v->addWidget(hint);

    auto *scrollHost = new QWidget(m_root);
    m_cardRow = new QHBoxLayout(scrollHost);
    m_cardRow->setContentsMargins(0, 8, 0, 0);
    m_cardRow->setSpacing(16);
    m_cardRow->addStretch(1);
    v->addWidget(scrollHost, 1);

    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
    setStyleSheet(QStringLiteral(
        "#OverviewRoot { background: rgba(8, 8, 12, 215); }"
        "#OverviewPanel { background: transparent; }"));
}

void OverviewWindow::pinToMonitorPhysically()
{
    if (!m_hmon)
        return;
    RECT phys{};
    if (!monitors::physRectOf(m_hmon, &phys))
        return;
    if (HWND h = reinterpret_cast<HWND>(winId())) {
        // Physical pixels — exact clip even with mixed DPI (PMv2).
        ::SetWindowPos(h, HWND_TOP,
                       phys.left, phys.top,
                       phys.right - phys.left, phys.bottom - phys.top,
                       SWP_NOACTIVATE);
    }
}

void OverviewWindow::openOnMonitor(HMONITOR hmon, bool takeFocus)
{
    if (m_closePending)
        return;

    auto *m = m_manager ? m_manager->monitorOf(hmon) : nullptr;
    if (!m)
        return;

    cancelAnimations();
    m_hmon = hmon;
    m_exitStarted = false;

    if (m_manager) {
        if (!m_hostManaged) {
            m_manager->seedScreenshots();
            m_manager->captureSpaceScreenshot(hmon, m->currentIndex);
            m_manager->setOverviewOpen(true);
        } else {
            m_manager->captureSpaceScreenshot(hmon, m->currentIndex);
        }
    }

    setGeometry(m->geometry);
    rebuildCards();

    m_open = true;
    m_closePending = false;
    m_pendingCommit = -1;
    m_selected = qBound(0, m->currentIndex, qMax(0, m_cards.size() - 1));

    show();
    setWindowOpacity(1.0);
    pinToMonitorPhysically();
    raise();
    if (takeFocus) {
        activateWindow();
        setFocus(Qt::ActiveWindowFocusReason);
        if (HWND h = reinterpret_cast<HWND>(winId())) {
            ::SetForegroundWindow(h);
            ::SetFocus(h);
        }
    }

    playEnterAnimation();
}

void OverviewWindow::closeOverview(bool commit)
{
    if (!m_open || m_closePending)
        return;

    m_pendingCommit = commit ? m_selected : -1;
    // Mark closing on this panel; host will sync siblings.
    m_open = false;
    m_closePending = true;
    cancelAnimations();

    emit closed(m_pendingCommit);

    if (!m_hostManaged) {
        // Standalone panel (unit tests / single-monitor fallback).
        if (commit)
            QTimer::singleShot(200, this, &OverviewWindow::startExit);
        else
            startExit();
    }
    // Host-managed: OverviewHost::onPanelClosed drives prepare/start/force on ALL panels.
}

void OverviewWindow::prepareClose()
{
    m_open = false;
    m_closePending = true;
    m_exitStarted = false;
    m_pendingCommit = -1;
    cancelAnimations();
}

void OverviewWindow::startExit()
{
    if (!m_closePending)
        return;
    if (m_exitStarted)
        return;
    m_exitStarted = true;
    cancelAnimations();
    playExitAnimation();
}

void OverviewWindow::forceHide()
{
    cancelAnimations();
    m_open = false;
    m_closePending = false;
    m_exitStarted = false;
    m_animating = false;
    m_pendingCommit = -1;
    hide();
    if (m_manager && !m_hostManaged)
        m_manager->setOverviewOpen(false);
}

void OverviewWindow::closeQuietly()
{
    if (!m_open && !isVisible() && !m_closePending)
        return;
    prepareClose();
    startExit();
}

void OverviewWindow::dismiss()
{
    if (!m_closePending)
        return;
    startExit();
}

void OverviewWindow::finishClose()
{
    hide();
    m_exitStarted = false;
    if (m_manager && !m_hostManaged)
        m_manager->setOverviewOpen(false);

    m_pendingCommit = -1;
    m_closePending = false;
    m_animating = false;
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
    if (graphicsEffect())
        setGraphicsEffect(nullptr);

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

    m_header->setText(tr("Spaces — %1").arg(m->deviceName));

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
        // Strict real monitor aspect (portrait stays tall/narrow).
        card->setMonitorAspect(
            m->physRect.right - m->physRect.left,
            m->physRect.bottom - m->physRect.top);

        QImage shot = m->spaces[i].screenshot;
        if (shot.isNull())
            shot = thumbs::desktopWallpaper(m->physRect, QSize(640, 360));
        card->setScreenshot(shot);

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

        m_cardRow->addWidget(card, 0, Qt::AlignVCenter);
        m_cards.push_back(card);
    }
    m_cardRow->addStretch(1);
    setSelected(m_cards.isEmpty() ? -1 : current);

    for (int i = 0; i < m_cards.size() && i < m->spaces.size(); ++i) {
        m_cards[i]->setMonitorAspect(
            m->physRect.right - m->physRect.left,
            m->physRect.bottom - m->physRect.top);
        QImage shot = m->spaces[i].screenshot;
        if (shot.isNull())
            shot = thumbs::desktopWallpaper(m->physRect, QSize(640, 360));
        m_cards[i]->setScreenshot(shot);
    }
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

    auto *eff = new QGraphicsOpacityEffect(this);
    eff->setOpacity(1.0);
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
                setGraphicsEffect(nullptr);
        }
        if (!m_closePending)
            m_animating = false;
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);

    for (int i = 0; i < m_cards.size(); ++i) {
        auto *card = m_cards[i];
        if (!card)
            continue;
        const QPoint end = card->pos();
        const QPoint start = end + QPoint(0, 16);
        card->move(start);
        QTimer::singleShot(i * 25, this, [card, end]() {
            if (!card)
                return;
            auto *a = new QPropertyAnimation(card, "pos", card);
            a->setDuration(180);
            a->setStartValue(card->pos());
            a->setEndValue(end);
            a->setEasingCurve(QEasingCurve::OutCubic);
            a->start(QAbstractAnimation::DeleteWhenStopped);
        });
    }
}

void OverviewWindow::playExitAnimation()
{
    if (!m_closePending)
        return;
    if (m_exitStarted && m_fadeAnim)
        return; // already fading out

    m_animating = true;
    m_exitStarted = true;

    // Use window-level opacity: more reliable across monitors than QGraphicsEffect.
    cancelAnimations();
    setWindowOpacity(1.0);

    auto *anim = new QPropertyAnimation(this, "windowOpacity", this);
    m_fadeAnim = anim;
    anim->setDuration(140);
    anim->setStartValue(1.0);
    anim->setEndValue(0.0);
    anim->setEasingCurve(QEasingCurve::InCubic);
    connect(anim, &QPropertyAnimation::finished, this, [this]() {
        if (m_fadeAnim == sender())
            m_fadeAnim = nullptr;
        finishClose();
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);

    // Absolute fallback if finished never fires.
    QTimer::singleShot(250, this, [this]() {
        if (m_closePending && isVisible())
            forceHide();
    });
}
