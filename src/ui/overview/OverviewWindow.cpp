#include "OverviewWindow.h"

#include "ui/preview/SpaceCardWidget.h"
#include "ui/preview/WindowPreviewWidget.h"
#include "core/capture/ThumbnailCapture.h"
#include "core/window/WindowTracker.h"

#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QColor>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

#include <dwmapi.h>

#include <algorithm>
#include <cmath>

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
    v->setContentsMargins(36, 28, 36, 36);
    v->setSpacing(14);

    m_header = new QLabel(m_root);
    m_header->setStyleSheet(QStringLiteral(
        "QLabel { color: #ffffff; font-size: 22px; font-weight: 700; "
        "background: transparent; border: none; }"));
    v->addWidget(m_header);

    m_hint = new QLabel(
        tr("Hover a space to sync the desktop · drag windows below onto a space · "
           "←/→ preview · Enter confirm (instant) · Esc cancel"),
        m_root);
    m_hint->setStyleSheet(QStringLiteral(
        "QLabel { color: rgba(255,255,255,140); font-size: 13px; "
        "background: transparent; border: none; }"));
    v->addWidget(m_hint);

    // --- Top: space strip ---
    auto *spaceLabel = new QLabel(tr("Spaces"), m_root);
    spaceLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: rgba(255,255,255,180); font-size: 13px; font-weight: 600; "
        "background: transparent; border: none; }"));
    v->addWidget(spaceLabel);

    m_spaceStripHost = new QWidget(m_root);
    auto *stripOuter = new QVBoxLayout(m_spaceStripHost);
    stripOuter->setContentsMargins(0, 0, 0, 0);
    m_cardRow = new QHBoxLayout;
    m_cardRow->setContentsMargins(0, 4, 0, 4);
    m_cardRow->setSpacing(14);
    m_cardRow->addStretch(1);
    stripOuter->addLayout(m_cardRow);
    v->addWidget(m_spaceStripHost);

    // --- Bottom: draggable windows ---
    auto *winLabel = new QLabel(tr("Windows in the previewed space — drag onto a space"), m_root);
    winLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: rgba(255,255,255,180); font-size: 13px; font-weight: 600; "
        "background: transparent; border: none; }"));
    v->addWidget(winLabel);

    m_windowScroll = new QScrollArea(m_root);
    m_windowScroll->setWidgetResizable(true);
    m_windowScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_windowScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_windowScroll->setFrameShape(QFrame::NoFrame);
    m_windowScroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));

    m_windowHost = new QWidget;
    m_windowHost->setStyleSheet(QStringLiteral("background: transparent;"));
    m_windowStack = new QVBoxLayout(m_windowHost);
    m_windowStack->setContentsMargins(0, 0, 0, 0);
    m_windowStack->setSpacing(12);
    m_windowStack->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_windowScroll->setWidget(m_windowHost);
    m_windowScroll->setWidgetResizable(true);
    v->addWidget(m_windowScroll, 1);

    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
    setStyleSheet(QStringLiteral(
        "#OverviewRoot { background: rgba(8, 8, 12, 215); }"
        "#OverviewPanel { background: transparent; }"));
}

QString OverviewWindow::windowTitle(HWND hwnd) const
{
    if (!hwnd || !::IsWindow(hwnd))
        return {};
    wchar_t buf[256]{};
    ::GetWindowTextW(hwnd, buf, 256);
    QString t = QString::fromWCharArray(buf).trimmed();
    return t;
}

bool OverviewWindow::placeWindowInSpace(HWND hwnd, int spaceIndex)
{
    if (!m_manager || !hwnd || !m_hmon)
        return false;
    auto *m = m_manager->monitorOf(m_hmon);
    if (!m || spaceIndex < 0 || spaceIndex >= m->spaces.size())
        return false;

    if (!m_manager->trackWindow(hwnd))
        return false;
    // Exclusive space rejects other windows (assignWindow enforces this).
    if (!m_manager->canAssignToSpace(m_hmon, spaceIndex, hwnd))
        return false;

    // Source space/monitor before the move so we can refresh the vacated card.
    const int srcSpace = m_manager->spaceOfWindow(hwnd);
    HMONITOR srcMon = m_manager->ownerMonitorOf(hwnd);

    if (!m_manager->assignWindow(hwnd, m_hmon, spaceIndex))
        return false;

    // 1) Sync the real desktop to the destination (windows show/hide).
    previewSpace(spaceIndex);

    // 2) Composite a fresh card image for the destination.
    refreshCardScreenshot(spaceIndex);

    // 3) Source space lost a window — assignWindow already rebuilt its screenshot;
    //    push that image onto the source card so the strip stays in sync.
    if (srcSpace >= 0 && (srcMon != m_hmon || srcSpace != spaceIndex)) {
        if (srcMon == m_hmon || !srcMon)
            refreshCardScreenshot(srcSpace);
        // Other monitors' panels pull the rebuilt shot on their next open/preview.
    }

    refreshCardBadges();

    // 4) Bottom strip = windows of the previewed space only.
    rebuildWindowPreviews();
    setSelected(spaceIndex);

    emit windowPlaced(reinterpret_cast<quint64>(hwnd), spaceIndex);
    return true;
}

bool OverviewWindow::previewSpace(int spaceIndex)
{
    if (!m_manager || !m_hmon)
        return false;
    auto *m = m_manager->monitorOf(m_hmon);
    if (!m || spaceIndex < 0 || spaceIndex >= m->spaces.size())
        return false;

    // Live cloak on the desktop under the overlay (no animation).
    m_manager->previewSpace(m_hmon, spaceIndex);
    m_selected = spaceIndex;
    refreshCardBadges();
    rebuildWindowPreviews();
    refreshCardScreenshot(spaceIndex);
    emit spacePreviewed(spaceIndex);
    return true;
}

void OverviewWindow::pinToMonitorPhysically()
{
    if (!m_hmon)
        return;
    RECT phys{};
    if (!monitors::physRectOf(m_hmon, &phys))
        return;
    if (HWND h = reinterpret_cast<HWND>(winId())) {
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
            // Host already set overviewOpen → BitBlt is a no-op; still re-seed empties.
            m_manager->seedScreenshots();
            m_manager->captureSpaceScreenshot(hmon, m->currentIndex);
        }
    }

    setGeometry(m->geometry);
    rebuildCards();
    // Bottom strip starts as the space already live on the desktop.
    m_originSpace = m->currentIndex;
    rebuildWindowPreviews();

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

    if (!commit && m_manager && m_hmon) {
        // Cancel: put the real desktop back so hover previews don't stick.
        auto *m = m_manager->monitorOf(m_hmon);
        if (m && m_originSpace >= 0 && m_originSpace < m->spaces.size()
            && m->currentIndex != m_originSpace) {
            m_manager->previewSpace(m_hmon, m_originSpace);
        }
    }

    m_pendingCommit = commit ? m_selected : -1;
    m_open = false;
    m_closePending = true;
    cancelAnimations();

    emit closed(m_pendingCommit);

    if (!m_hostManaged) {
        if (commit)
            QTimer::singleShot(200, this, &OverviewWindow::startExit);
        else
            startExit();
    }
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
        card->setCompact(true); // top strip
        card->setSpace(i, m->spaces[i].name, i == current);
        card->setMonitorAspect(
            m->physRect.right - m->physRect.left,
            m->physRect.bottom - m->physRect.top);

        QImage shot = m->spaces[i].screenshot;
        if (shot.isNull())
            shot = thumbs::desktopWallpaper(m->physRect, QSize(640, 360));
        if (shot.isNull()) {
            shot = QImage(640, 360, QImage::Format_ARGB32_Premultiplied);
            shot.fill(QColor(32, 36, 48));
        }
        card->setScreenshot(shot);

        connect(card, &SpaceCardWidget::activated, this, [this](int idx) {
            if (!m_open)
                return;
            if (idx >= 0 && idx < m_cards.size())
                m_selected = idx;
            // Desktop already matches if user hovered this card — commit is instant.
            closeOverview(true);
        });
        connect(card, &SpaceCardWidget::hovered, this, [this](int idx) {
            if (m_open)
                previewSpace(idx);
        });
        connect(card, &SpaceCardWidget::windowDropped, this,
                [this](int spaceIndex, quint64 hwnd) {
                    if (!m_open)
                        return;
                    placeWindowInSpace(reinterpret_cast<HWND>(hwnd), spaceIndex);
                });

        m_cardRow->addWidget(card, 0, Qt::AlignVCenter);
        m_cards.push_back(card);
    }
    m_cardRow->addStretch(1);
    setSelected(m_cards.isEmpty() ? -1 : current);

    // Cards may still have zero preview size before layout — re-apply once shown.
    // Context object (`this`) cancels the timer if the panel is destroyed first.
    QTimer::singleShot(0, this, [this]() {
        if (!m_manager || !m_hmon)
            return;
        auto *m = m_manager->monitorOf(m_hmon);
        if (!m)
            return;
        for (int i = 0; i < m_cards.size() && i < m->spaces.size(); ++i)
            refreshCardScreenshot(i);
    });
}

void OverviewWindow::rebuildWindowPreviews()
{
    auto *m = m_manager ? m_manager->monitorOf(m_hmon) : nullptr;
    if (!m || !m_windowStack)
        return;

    // Clear previous rows and tiles.
    while (QLayoutItem *item = m_windowStack->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    m_windowPreviews.clear();

    // --- Collect real window rects for the previewed space ---
    const int idx = m->currentIndex;
    struct Item {
        HWND hwnd = nullptr;
        int pw = 0;
        int ph = 0;
    };
    QVector<Item> items;
    if (idx >= 0 && idx < m->spaces.size()) {
        for (HWND hwnd : m->spaces[idx].windows) {
            if (!::IsWindow(hwnd) || ::IsIconic(hwnd))
                continue;
            RECT wr{};
            if (!::GetWindowRect(hwnd, &wr))
                continue;
            Item it;
            it.hwnd = hwnd;
            it.pw = wr.right - wr.left;
            it.ph = wr.bottom - wr.top;
            if (it.pw > 0 && it.ph > 0)
                items.push_back(it);
        }
    }

    if (items.isEmpty()) {
        auto *empty = new QLabel(
            idx >= 0 ? tr("No windows in this space") : tr("No windows on this display"),
            m_windowHost);
        empty->setStyleSheet(QStringLiteral(
            "QLabel { color: rgba(255,255,255,100); font-size: 13px; background: transparent; border: none; }"));
        m_windowStack->addWidget(empty, 0, Qt::AlignLeft);
        return;
    }

    // --- Available strip area ---
    int availW = m_windowScroll ? m_windowScroll->viewport()->width() : 0;
    int availH = m_windowScroll ? m_windowScroll->viewport()->height() : 0;
    if (availW < 200 || availH < 80) {
        availW = m->geometry.width() > 0 ? m->geometry.width() - 72 : 1200;
        availH = 240;
    }
    const int gap = 14;

    // Larger tiles first → less waste on the last row (排满).
    std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
        return qint64(a.pw) * a.ph > qint64(b.pw) * b.ph;
    });

    int maxPw = 1;
    for (const Item &it : items)
        maxPw = std::max(maxPw, it.pw);

    // Tile size at a given scale: keep real window aspect, then floor without stretching.
    auto tileSize = [&](const Item &it, double scale) -> QSize {
        double w = double(it.pw) * scale;
        double h = double(it.ph) * scale;
        if (w < 96.0) {
            h *= 96.0 / w;
            w = 96.0;
        }
        if (h < 64.0) {
            w *= 64.0 / h;
            h = 64.0;
        }
        return QSize(std::max(96, int(std::lround(w))),
                     std::max(64, int(std::lround(h))));
    };

    double lo = 0.02;
    double hi = std::min(4.0, double(availW) / double(maxPw));
    double best = lo;
    for (int iter = 0; iter < 24; ++iter) {
        const double mid = (lo + hi) * 0.5;
        int x = 0;
        int rowH = 0;
        int total = 0;
        for (const Item &it : items) {
            const QSize ts = tileSize(it, mid);
            const int cellW = ts.width() + gap;
            const int cellH = ts.height() + gap;
            if (x > 0 && x + cellW > availW) {
                total += rowH;
                x = 0;
                rowH = 0;
            }
            x += cellW;
            rowH = std::max(rowH, cellH);
        }
        total += rowH;
        if (total <= availH) {
            best = mid;
            lo = mid;
        } else {
            hi = mid;
        }
    }
    double scale = best;
    if (scale <= 0.02)
        scale = 0.02;

    // Build shelf rows: pack into QHBoxLayouts under m_windowStack.
    m_windowHost->setFixedWidth(availW);
    QHBoxLayout *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(gap);
    row->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    int x = 0;

    auto commitRow = [&](QHBoxLayout *r) {
        if (!r)
            return;
        auto *holder = new QWidget(m_windowHost);
        holder->setLayout(r);
        m_windowStack->addWidget(holder, 0, Qt::AlignLeft);
    };

    for (const Item &it : items) {
        const QSize ts = tileSize(it, scale);
        const int bw = ts.width();
        const int bh = ts.height();
        const int cellW = bw + gap;

        if (x > 0 && x + cellW > availW) {
            commitRow(row);
            row = new QHBoxLayout;
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(gap);
            row->setAlignment(Qt::AlignLeft | Qt::AlignTop);
            x = 0;
        }

        auto *tile = new WindowPreviewWidget(m_windowHost);
        tile->setImageBoxSize(QSize(bw, bh));
        // Capture already caps to (bw, bh) keeping aspect; box uses the same window aspect.
        QImage shot = thumbs::capture(it.hwnd, QSize(bw, bh));
        tile->setWindow(it.hwnd, windowTitle(it.hwnd), shot);
        row->addWidget(tile, 0, Qt::AlignTop);
        m_windowPreviews.push_back(tile);
        x += cellW;
    }
    commitRow(row);
}

void OverviewWindow::refreshCardBadges()
{
    auto *m = m_manager ? m_manager->monitorOf(m_hmon) : nullptr;
    if (!m)
        return;
    for (int i = 0; i < m_cards.size() && i < m->spaces.size(); ++i) {
        const bool isCurrent = (i == m->currentIndex);
        m_cards[i]->setSpace(i, m->spaces[i].name, isCurrent);
        m_cards[i]->setHighlighted(i == m_selected);
    }
}

void OverviewWindow::refreshCardScreenshot(int index)
{
    auto *m = m_manager ? m_manager->monitorOf(m_hmon) : nullptr;
    if (!m || index < 0 || index >= m->spaces.size() || index >= m_cards.size())
        return;
    QImage shot = m->spaces[index].screenshot;
    if (shot.isNull())
        shot = thumbs::desktopWallpaper(m->physRect, QSize(640, 360));
    if (shot.isNull()) {
        shot = QImage(640, 360, QImage::Format_ARGB32_Premultiplied);
        shot.fill(QColor(32, 36, 48));
    }
    m_cards[index]->setScreenshot(shot);
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
        previewSpace((m_selected - 1 + n) % n);
        event->accept();
        return;
    case Qt::Key_Right:
        previewSpace((m_selected + 1) % n);
        event->accept();
        return;
    case Qt::Key_Home:
        previewSpace(0);
        event->accept();
        return;
    case Qt::Key_End:
        previewSpace(n - 1);
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
        return;

    m_animating = true;
    m_exitStarted = true;
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

    QTimer::singleShot(250, this, [this]() {
        if (m_closePending && isVisible())
            forceHide();
    });
}
