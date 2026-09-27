#include "OverviewWindow.h"

#include "ui/preview/AddSpaceButton.h"
#include "ui/preview/SpaceCardWidget.h"
#include "ui/preview/WindowPreviewWidget.h"
#include "core/capture/ThumbnailCapture.h"
#include "core/monitor/MonitorInfo.h"
#include "core/window/WindowTracker.h"

#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QColor>
#include <QLayout>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

#include <dwmapi.h>

#include <algorithm>
#include <cmath>

OverviewWindow::OverviewWindow(SpaceManager *manager, QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::Tool),
      m_manager(manager)
{
    setAttribute(Qt::WA_DeleteOnClose, false);
    setObjectName(QStringLiteral("OverviewRoot"));
    setAttribute(Qt::WA_TranslucentBackground, false);

    // Model re-rendered a space (new window, move, untrack) → reload cards.
    if (m_manager) {
        connect(m_manager, &SpaceManager::spacePreviewInvalidated, this,
                [this](quint64 hmon, int spaceIndex) {
                    if (!m_open || !m_hmon || hmon != quint64(m_hmon))
                        return;
                    if (spaceIndex >= 0 && spaceIndex < m_cards.size() &&
                        spaceIndex < m_manager->spaceCount(m_hmon))
                        refreshCardScreenshot(spaceIndex);
                    if (spaceIndex == m_selected)
                        rebuildWindowPreviews();
                });
        connect(m_manager, &SpaceManager::windowUntracked, this, [this](quint64) {
            if (!m_open)
                return;
            for (int i = 0; i < m_cards.size(); ++i)
                refreshCardScreenshot(i);
            rebuildWindowPreviews();
        });
        // New/moved window on this monitor → refresh bottom strip (taskbar launches etc.).
        connect(m_manager, &SpaceManager::windowTracked, this, [this](quint64 hwnd) {
            if (!m_open || !m_hmon || !m_manager)
                return;
            const HWND h = reinterpret_cast<HWND>(hwnd);
            if (m_manager->ownerMonitorOf(h) != m_hmon)
                return;
            const int sp = m_manager->spaceOfWindow(h);
            if (sp == m_selected || sp == m_manager->currentSpaceIndex(m_hmon))
                rebuildWindowPreviews();
        });
    }

    m_root = new QWidget(this);
    m_root->setObjectName(QStringLiteral("OverviewPanel"));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(m_root);

    auto *v = new QVBoxLayout(m_root);
    v->setContentsMargins(36, 28, 36, 36);
    v->setSpacing(14);

    m_header = new QLabel(m_root);
    m_header->setStyleSheet(
        QStringLiteral("QLabel { color: #ffffff; font-size: 22px; font-weight: 700; "
                       "background: transparent; border: none; }"));
    v->addWidget(m_header);

    m_hint =
        new QLabel(tr("Hover a space to preview it here · click to switch · "
                      "drag windows below onto a space · ←/→ select · Enter confirm · Esc cancel"),
                   m_root);
    m_hint->setStyleSheet(QStringLiteral("QLabel { color: rgba(255,255,255,140); font-size: 13px; "
                                         "background: transparent; border: none; }"));
    v->addWidget(m_hint);

    // --- Top: space strip ---
    auto *spaceLabel = new QLabel(tr("Spaces"), m_root);
    spaceLabel->setStyleSheet(
        QStringLiteral("QLabel { color: rgba(255,255,255,180); font-size: 13px; font-weight: 600; "
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

    m_addSpaceBtn = new AddSpaceButton(m_spaceStripHost);
    connect(m_addSpaceBtn, &AddSpaceButton::addRequested, this, [this]() { addSpaceFromStrip(); });
    connect(m_addSpaceBtn, &AddSpaceButton::windowDropped, this,
            [this](quint64 hwnd) { addSpaceAndPlaceWindow(hwnd); });

    // Soft-preview hold: leaving space/window strips starts a 1s timer;
    // re-entering those zones cancels it. Outer margins / panel leave also arm it.
    m_holdTimer = new QTimer(this);
    m_holdTimer->setSingleShot(true);
    m_holdTimer->setInterval(m_holdMs);
    connect(m_holdTimer, &QTimer::timeout, this, [this]() { restoreStripToCurrentSpace(); });
    installEventFilter(this);
    m_spaceStripHost->installEventFilter(this);
    // window scroll installed after creation below

    // --- Bottom: draggable windows ---
    auto *winLabel = new QLabel(tr("Windows in the previewed space — drag onto a space"), m_root);
    winLabel->setStyleSheet(
        QStringLiteral("QLabel { color: rgba(255,255,255,180); font-size: 13px; font-weight: 600; "
                       "background: transparent; border: none; }"));
    v->addWidget(winLabel);

    m_windowScroll = new QScrollArea(m_root);
    m_windowScroll->setWidgetResizable(true);
    m_windowScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_windowScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_windowScroll->setFrameShape(QFrame::NoFrame);
    m_windowScroll->setStyleSheet(
        QStringLiteral("QScrollArea { background: transparent; border: none; }"));

    m_windowHost = new QWidget;
    m_windowHost->setStyleSheet(QStringLiteral("background: transparent;"));
    m_windowStack = new QVBoxLayout(m_windowHost);
    m_windowStack->setContentsMargins(0, 0, 0, 0);
    m_windowStack->setSpacing(12);
    m_windowStack->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_windowScroll->setWidget(m_windowHost);
    m_windowScroll->setWidgetResizable(true);
    m_windowScroll->installEventFilter(this);
    v->addWidget(m_windowScroll, 1);

    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
    setStyleSheet(QStringLiteral("#OverviewRoot { background: rgba(8, 8, 12, 215); }"
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

QSize OverviewWindow::windowPreviewBoxSize(int i) const
{
    if (i < 0 || i >= m_windowPreviews.size() || !m_windowPreviews[i])
        return {};
    return m_windowPreviews[i]->imageBoxSize();
}

HWND OverviewWindow::windowPreviewHandle(int i) const
{
    if (i < 0 || i >= m_windowPreviews.size() || !m_windowPreviews[i])
        return nullptr;
    return m_windowPreviews[i]->windowHandle();
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

    // Source space/monitor before the move so we can refresh the vacated card.
    const int srcSpace = m_manager->spaceOfWindow(hwnd);
    HMONITOR srcMon = m_manager->ownerMonitorOf(hwnd);
    // Stay on the space we are viewing — do NOT jump to the drop target.
    const int stay = m->currentIndex;

    if (!m_manager->assignWindow(hwnd, m_hmon, spaceIndex))
        return false;

    // Destination card (window now lives there — may be hidden).
    refreshCardScreenshot(spaceIndex);
    // Source card lost a window.
    if (srcSpace >= 0 && (srcMon != m_hmon || srcSpace != spaceIndex)) {
        if (srcMon == m_hmon || !srcMon)
            refreshCardScreenshot(srcSpace);
    }

    refreshCardBadges();

    // Keep UI + desktop on the original space; strip drops the moved window.
    m_selected = stay;
    rebuildWindowPreviews();
    setSelected(stay);

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

    // UI-only preview: highlight + bottom strip + card image.
    // Does NOT change the real monitor space (currentIndex / cloak).
    // Formal switch happens on click/Enter via closed() → switchSpace.
    if (m_selected == spaceIndex && m_open)
        return true;

    m_selected = spaceIndex;
    refreshCardBadges();
    rebuildWindowPreviews();
    refreshCardScreenshot(spaceIndex);
    emit spacePreviewed(spaceIndex);
    return true;
}

bool OverviewWindow::activateWindowPreview(HWND hwnd)
{
    if (!m_manager || !hwnd || !::IsWindow(hwnd) || !m_open)
        return false;
    const int sp = m_manager->spaceOfWindow(hwnd);
    if (sp < 0)
        return false;
    // Only act for windows owned by this monitor's overview.
    if (m_manager->ownerMonitorOf(hwnd) && m_manager->ownerMonitorOf(hwnd) != m_hmon)
        return false;

    m_selected = qBound(0, sp, qMax(0, m_cards.size() - 1));
    m_frontHwnd = hwnd;
    emit windowActivated(reinterpret_cast<quint64>(hwnd));
    closeOverview(true);
    return true;
}

void OverviewWindow::pinToMonitorPhysically()
{
    if (!m_hmon)
        return;
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!::GetMonitorInfoW(m_hmon, &mi))
        return;
    // Pin to work area so the taskbar stays visible/clickable under the overlay.
    const RECT &r = mi.rcWork;
    if (HWND h = reinterpret_cast<HWND>(winId())) {
        ::SetWindowPos(h, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top,
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
    cancelSoftPreviewHold();
    m_hmon = hmon;
    m_exitStarted = false;

    if (m_manager) {
        if (!m_hostManaged) {
            // Fresh window shots before composites so reopen never shows stale tiles.
            m_manager->warmWindowShots();
            m_manager->buildAllSpacePreviews();
            m_manager->setOverviewOpen(true);
        }
        // Host path: batch caches already built in OverviewHost::openAll.
    }

    // Cover the work area only — taskbar remains on top / visible.
    const QRect work = monitors::logicalWorkArea(hmon);
    setGeometry(work.isValid() && !work.isEmpty() ? work : m->geometry);
    rebuildCards();
    m_originSpace = m->currentIndex;
    m_selected = qBound(0, m->currentIndex, qMax(0, m_cards.size() - 1));
    m_open = true;
    m_closePending = false;
    m_pendingCommit = -1;

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

    // Build the bottom strip AFTER the widget is shown so viewport width/height
    // match later rebuilds (drag/hover) — fixes size mismatch on first open.
    rebuildWindowPreviews();

    playEnterAnimation();
}

void OverviewWindow::closeOverview(bool commit)
{
    if (!m_open || m_closePending)
        return;

    if (!commit && m_manager && m_hmon) {
        // Cancel: put the real desktop back so hover previews don't stick.
        auto *m = m_manager->monitorOf(m_hmon);
        if (m && m_originSpace >= 0 && m_originSpace < m->spaces.size() &&
            m->currentIndex != m_originSpace) {
            m_manager->previewSpace(m_hmon, m_originSpace);
        }
    }

    m_pendingCommit = commit ? m_selected : -1;
    m_open = false;
    m_closePending = true;
    cancelAnimations();
    cancelSoftPreviewHold();

    emit closed(m_pendingCommit);

    // After commit + host switchSpace (200ms hold), raise/focus the clicked window.
    if (commit && m_frontHwnd) {
        HWND front = m_frontHwnd;
        m_frontHwnd = nullptr;
        QTimer::singleShot(250, this, [front]() {
            if (!::IsWindow(front))
                return;
            // Only raise — never ShowWindow(SW_SHOW) for windows we did not hide.
            ::SetWindowPos(front, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            ::SetForegroundWindow(front);
        });
    } else {
        m_frontHwnd = nullptr;
    }

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
        // Keep the shared + button alive across rebuilds.
        if (item->widget() && item->widget() != m_addSpaceBtn)
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
        card->setMonitorAspect(m->physRect.right - m->physRect.left,
                               m->physRect.bottom - m->physRect.top);

        QImage shot = m->spaces[i].screenshot;
        if (shot.isNull())
            shot = thumbs::desktopWallpaper(m->physRect, QSize(640, 360));
        if (shot.isNull()) {
            shot = QImage(640, 360, QImage::Format_ARGB32_Premultiplied);
            shot.fill(QColor(32, 36, 48));
        }
        card->setScreenshot(shot);
        // All spaces with >1 total: × after 2s hover (empty spaces merge windows too).
        card->setRemovable(m->spaces.size() > 1);

        connect(card, &SpaceCardWidget::activated, this, [this](int idx) {
            if (!m_open)
                return;
            if (idx >= 0 && idx < m_cards.size())
                m_selected = idx;
            // Desktop already matches if user hovered this card — commit is instant.
            closeOverview(true);
        });
        connect(card, &SpaceCardWidget::hovered, this, [this](int idx) {
            if (m_open) {
                cancelSoftPreviewHold();
                previewSpace(idx);
            }
        });
        connect(card, &SpaceCardWidget::hoverLeft, this, [this]() {
            // Left a card → start hold (restore after m_holdMs if not re-entered).
            if (m_open)
                armSoftPreviewHold();
        });
        connect(card, &SpaceCardWidget::windowDropped, this, [this](int spaceIndex, quint64 hwnd) {
            if (!m_open)
                return;
            placeWindowInSpace(reinterpret_cast<HWND>(hwnd), spaceIndex);
        });
        connect(card, &SpaceCardWidget::removeRequested, this, [this](int idx) {
            if (!m_open || !m_manager || !m_hmon)
                return;
            if (!m_manager->removeSpace(m_hmon, idx))
                return;
            // Clamp selection after structural change.
            const int n = m_manager->spaceCount(m_hmon);
            if (m_selected >= n)
                m_selected = qMax(0, n - 1);
            m_originSpace = qBound(0, m_originSpace, qMax(0, n - 1));
            rebuildCards();
            rebuildWindowPreviews();
            refreshCardBadges();
            setSelected(m_selected);
        });
        connect(card, &SpaceCardWidget::reorderRequested, this, [this](int from, int to) {
            if (!m_open || !m_manager || !m_hmon)
                return;
            if (!m_manager->moveSpace(m_hmon, from, to))
                return;
            // Keep following the same logical space (index shifted).
            if (m_selected == from)
                m_selected = to;
            else if (from < m_selected && to >= m_selected)
                --m_selected;
            else if (from > m_selected && to <= m_selected)
                ++m_selected;
            m_selected = qBound(0, m_selected, qMax(0, m_manager->spaceCount(m_hmon) - 1));
            m_originSpace = qBound(0, m_originSpace, qMax(0, m_manager->spaceCount(m_hmon) - 1));
            rebuildCards();
            rebuildWindowPreviews();
            refreshCardBadges();
            setSelected(m_selected);
        });

        m_cardRow->addWidget(card, 0, Qt::AlignVCenter);
        m_cards.push_back(card);
    }
    m_cardRow->addStretch(1);
    // Trailing + after stretch so it sits at the far right of the strip.
    m_cardRow->addWidget(m_addSpaceBtn, 0, Qt::AlignVCenter);
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

    // Bottom strip follows the UI selection (hover/arrows), not only the live space.
    int idx = m_selected;
    if (idx < 0 || idx >= m->spaces.size())
        idx = m->currentIndex;
    if (idx < 0 || idx >= m->spaces.size())
        idx = 0;

    struct Item {
        HWND hwnd = nullptr;
        int pw = 0; // logical (Qt) size — matches widget / strip coordinates
        int ph = 0;
    };
    QVector<Item> items;
    if (idx >= 0 && idx < m->spaces.size()) {
        for (HWND hwnd : m->spaces[idx].windows) {
            if (!::IsWindow(hwnd) || ::IsIconic(hwnd))
                continue;
            // GetWindowRect is physical; convert with THIS window's monitor DPI
            // so scale≤1 means "not larger than the on-screen window".
            const QSize logical = monitors::logicalWindowSize(hwnd);
            if (logical.width() > 0 && logical.height() > 0) {
                Item it;
                it.hwnd = hwnd;
                it.pw = logical.width();
                it.ph = logical.height();
                items.push_back(it);
            }
        }
    }

    if (items.isEmpty()) {
        auto *empty =
            new QLabel(idx >= 0 ? tr("No windows in this space") : tr("No windows on this display"),
                       m_windowHost);
        empty->setStyleSheet(QStringLiteral(
            "QLabel { color: rgba(255,255,255,100); font-size: 13px; background: transparent; border: none; }"));
        m_windowStack->addWidget(empty, 0, Qt::AlignLeft);
        return;
    }

    // --- Available strip area: prefer live viewport; force layout if not shown yet ---
    if (m_windowScroll) {
        m_windowScroll->ensurePolished();
        if (m_windowScroll->layout())
            m_windowScroll->layout()->activate();
    }
    int availW = m_windowScroll ? m_windowScroll->viewport()->width() : 0;
    int availH = m_windowScroll ? m_windowScroll->viewport()->height() : 0;
    if (availW < 200 || availH < 80) {
        // Not laid out yet — derive from the scroll area itself, not monitor/3,
        // so pre-show and post-show paths stay closer.
        if (m_windowScroll && m_windowScroll->width() >= 200)
            availW = m_windowScroll->width() - m_windowScroll->frameWidth() * 2;
        else
            availW = m->geometry.width() > 0 ? m->geometry.width() - 72 : 1200;
        if (m_windowScroll && m_windowScroll->height() >= 80)
            availH = m_windowScroll->height() - m_windowScroll->frameWidth() * 2;
        else
            availH = m->geometry.height() > 0 ? std::max(240, m->geometry.height() / 3) : 320;
    }
    const int gap = 14;
    // Tile chrome beyond the image box (must match WindowPreviewWidget::setImageBoxSize).
    constexpr int kChromeW = 20;
    constexpr int kChromeH = 40;

    // Larger tiles first → less waste on the last row (排满).
    std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
        return qint64(a.pw) * a.ph > qint64(b.pw) * b.ph;
    });

    // Uniform scale ≤ 1.0 so a tile is never larger than the real window
    // (pw/ph are already Qt logical — same units as availW/H and QWidget).
    auto tileSize = [&](const Item &it, double scale) -> QSize {
        const double pw = std::max(1, it.pw);
        const double ph = std::max(1, it.ph);
        double s = std::min(std::max(scale, 0.02), 1.0);
        // Readable floor in logical px — still never past 1.0 (real size).
        const double sMinW = 96.0 / pw;
        const double sMinH = 64.0 / ph;
        s = std::max(s, std::min(1.0, std::max(sMinW, sMinH)));
        s = std::min(s, 1.0);
        const int w = std::max(1, std::min(it.pw, int(std::lround(pw * s))));
        const int h = std::max(1, std::min(it.ph, int(std::lround(ph * s))));
        return QSize(w, h);
    };

    // Pack by FULL widget size (image box + chrome), not the bare image box —
    // otherwise tiles overflow into each other and look borderless/cramped.
    auto shelfFits = [&](double scale) -> bool {
        int x = 0;
        int rowH = 0;
        int total = 0;
        for (const Item &it : items) {
            const QSize ts = tileSize(it, scale);
            const int cellW = ts.width() + kChromeW + gap;
            const int cellH = ts.height() + kChromeH + gap;
            if (x > 0 && x + cellW > availW) {
                total += rowH;
                x = 0;
                rowH = 0;
            }
            x += cellW;
            rowH = std::max(rowH, cellH);
        }
        total += rowH;
        return total <= availH;
    };

    // Never upscale: upper bound is 1.0. Shrink until multi-row shelf fits
    // (including vertical gaps between rows).
    double lo = 0.02;
    double hi = 1.0;
    double best = lo;
    if (shelfFits(hi)) {
        best = hi;
    } else {
        for (int iter = 0; iter < 24; ++iter) {
            const double mid = (lo + hi) * 0.5;
            if (shelfFits(mid)) {
                best = mid;
                lo = mid;
            } else {
                hi = mid;
            }
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
        const int cellW = bw + kChromeW + gap;

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
        // Full-resolution cached shot — tile scales once to physical pixels (DPR).
        QImage shot = thumbs::windowShot(it.hwnd);
        tile->setWindow(it.hwnd, windowTitle(it.hwnd), shot);
        connect(tile, &WindowPreviewWidget::activated, this,
                [this](quint64 h) { activateWindowPreview(reinterpret_cast<HWND>(h)); });
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
        if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Return ||
            event->key() == Qt::Key_Enter) {
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

bool OverviewWindow::isOuterMarginPos(const QPoint &pos) const
{
    if (!m_root || !m_root->layout())
        return false;
    // Layout contentsRect is in m_root coordinates; m_root fills this widget.
    const QRect inner = m_root->layout()->contentsRect();
    return !inner.contains(pos);
}

void OverviewWindow::restoreStripToCurrentSpace()
{
    if (!m_open || !m_manager || !m_hmon)
        return;
    auto *m = m_manager->monitorOf(m_hmon);
    if (!m)
        return;
    if (m_selected != m->currentIndex)
        previewSpace(m->currentIndex);
}

bool OverviewWindow::addSpaceFromStrip()
{
    if (!m_open || !m_manager || !m_hmon)
        return false;
    if (!m_manager->addSpace(m_hmon))
        return false;
    const int last = m_manager->spaceCount(m_hmon) - 1;
    m_selected = last;
    rebuildCards();
    rebuildWindowPreviews();
    refreshCardBadges();
    setSelected(last);
    return true;
}

bool OverviewWindow::addSpaceAndPlaceWindow(quint64 hwnd)
{
    if (!m_open || !m_manager || !m_hmon)
        return false;
    if (!m_manager->addSpace(m_hmon))
        return false;
    const int last = m_manager->spaceCount(m_hmon) - 1;
    const bool placed = placeWindowInSpace(reinterpret_cast<HWND>(hwnd), last);
    // Stay on origin/current after place (placeWindowInSpace already keeps origin).
    m_selected = m_manager->monitorOf(m_hmon) ? m_manager->monitorOf(m_hmon)->currentIndex : last;
    rebuildCards();
    rebuildWindowPreviews();
    refreshCardBadges();
    setSelected(m_selected);
    return placed;
}

void OverviewWindow::setSoftPreviewHoldMs(int ms)
{
    m_holdMs = qMax(0, ms);
    if (m_holdTimer)
        m_holdTimer->setInterval(m_holdMs);
}

bool OverviewWindow::isSoftPreviewHoldPending() const
{
    return m_holdTimer && m_holdTimer->isActive();
}

void OverviewWindow::armSoftPreviewHold()
{
    if (!m_open || !m_holdTimer)
        return;
    // Restart so each leave event gets a full hold window.
    m_holdTimer->start();
}

void OverviewWindow::cancelSoftPreviewHold()
{
    if (m_holdTimer)
        m_holdTimer->stop();
}

bool OverviewWindow::isKeepZoneWidget(QObject *w) const
{
    while (w) {
        if (w == m_spaceStripHost || w == m_windowScroll || w == m_windowHost)
            return true;
        // Cards / tiles inside the strip or window scroll.
        if (qobject_cast<SpaceCardWidget *>(w) || qobject_cast<WindowPreviewWidget *>(w) ||
            w == m_addSpaceBtn)
            return true;
        w = w->parent();
    }
    return false;
}

bool OverviewWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (!m_open)
        return QWidget::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::Enter:
        if (isKeepZoneWidget(watched))
            cancelSoftPreviewHold();
        break;
    case QEvent::Leave:
        if (isKeepZoneWidget(watched))
            armSoftPreviewHold();
        break;
    case QEvent::MouseMove: {
        if (watched != this)
            break;
        const auto *me = static_cast<QMouseEvent *>(event);
        if (isOuterMarginPos(me->position().toPoint()))
            armSoftPreviewHold();
        break;
    }
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

void OverviewWindow::leaveEvent(QEvent *event)
{
    // Left the panel entirely — still allow hold (e.g. onto tray) before restore.
    armSoftPreviewHold();
    QWidget::leaveEvent(event);
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
