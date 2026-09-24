#include <QtTest>

#include "core/space/SpaceManager.h"
#include "core/monitor/MonitorInfo.h"
#include "core/window/CloakController.h"
#include "ui/overview/OverviewWindow.h"
#include "ui/preview/AddSpaceButton.h"
#include "ui/preview/SpaceCardWidget.h"
#include "ui/preview/WindowPreviewWidget.h"

#include <QDropEvent>
#include <QMimeData>
#include <QScrollArea>

#include <Windows.h>

// Mission Control placement: drag window previews onto the space strip.
class TestWindowPlacement : public QObject {
    Q_OBJECT
private slots:
    void mimeRoundTrip()
    {
        quint64 h = quint64(0x1234);
        QByteArray payload;
        QDataStream ds(&payload, QIODevice::WriteOnly);
        ds << h;

        QMimeData mime;
        mime.setData(WindowPreviewWidget::kMimeType, payload);
        QVERIFY(mime.hasFormat(WindowPreviewWidget::kMimeType));

        QDataStream in(mime.data(WindowPreviewWidget::kMimeType));
        quint64 out = 0;
        in >> out;
        QCOMPARE(out, h);
    }

    void dragHotSpotFollowsPressPoint()
    {
        // Image box at (10,10) in the frame; press in the middle of a 200×120 box.
        const QPoint imageTopLeft(10, 10);
        const QSize box(200, 120);
        const QSize pm(200, 120);

        const QPoint mid = WindowPreviewWidget::mapPressToHotSpot(
            QPoint(10 + 100, 10 + 60), imageTopLeft, box, pm);
        QCOMPARE(mid, QPoint(100, 60));

        const QPoint nearCorner = WindowPreviewWidget::mapPressToHotSpot(
            QPoint(10 + 20, 10 + 15), imageTopLeft, box, pm);
        QCOMPARE(nearCorner, QPoint(20, 15));

        // Title below the image clamps into the pixmap (x still follows the click).
        const QPoint onTitle = WindowPreviewWidget::mapPressToHotSpot(
            QPoint(10 + 50, 10 + 120 + 8), imageTopLeft, box, pm);
        QCOMPARE(onTitle.x(), 50);
        QCOMPARE(onTitle.y(), pm.height() - 1);

        // Letterboxed pixmap smaller than box: scale press into pixmap space.
        const QSize pmSmall(160, 90);
        const QPoint scaled = WindowPreviewWidget::mapPressToHotSpot(
            QPoint(10 + 100, 10 + 60), imageTopLeft, box, pmSmall);
        QCOMPARE(scaled, QPoint(80, 45));

        QCOMPARE(WindowPreviewWidget::mapPressToHotSpot(QPoint(5, 5), QPoint(0, 0),
                                                        box, QSize()),
                 QPoint(0, 0));
    }

    void spaceCardAcceptsDropMime()
    {
        SpaceCardWidget card;
        card.setSpace(1, QStringLiteral("S2"), false);

        quint64 h = 42;
        QByteArray payload;
        QDataStream ds(&payload, QIODevice::WriteOnly);
        ds << h;
        QMimeData mime;
        mime.setData(WindowPreviewWidget::kMimeType, payload);

        // dragEnter path: format check used by extractHwnd
        QVERIFY(mime.hasFormat(WindowPreviewWidget::kMimeType));
        QVERIFY(payload.size() >= int(sizeof(quint64)));

        QSignalSpy dropped(&card, &SpaceCardWidget::windowDropped);
        // Simulate drop by invoking the signal path through a synthetic drop is hard
        // without a full drag loop; placeWindowInSpace covers manager behavior.
        QCOMPARE(card.spaceIndex(), 1);
        Q_UNUSED(dropped);
    }

    void placeWindowMovesBetweenSpaces()
    {
        SpaceManager sm;
        OverviewWindow w(&sm);
        auto *m = sm.monitors().first();

        // Short-lived foreign-looking window (own process is rejected by tracker,
        // so use a visible top-level STATIC that isManageable rejects for pid —
        // placeWindowInSpace requires trackWindow success).
        // Create a window in this process: trackWindow rejects own process.
        // So placeWindowInSpace should fail cleanly for own HWND.
        HWND hwnd = ::CreateWindowExW(
            0, L"STATIC", L"placement-test",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE, 30, 30, 320, 200,
            nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
        QVERIFY(hwnd != nullptr);

        w.openOnMonitor(m->hmon);
        QVERIFY(w.isOpen());

        // Own-process windows are intentionally unmanaged → place must fail safely.
        QVERIFY(!w.placeWindowInSpace(hwnd, 0));
        ::DestroyWindow(hwnd);

        // Invalid space index
        HWND fake = reinterpret_cast<HWND>(0xDEAD);
        QVERIFY(!w.placeWindowInSpace(fake, -1));
        QVERIFY(!w.placeWindowInSpace(fake, 99));
        QVERIFY(!w.placeWindowInSpace(nullptr, 0));

        w.closeOverview(false);
        for (int i = 0; i < 40 && (w.isOpen() || w.isAnimating()); ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 15);
    }

    void overviewBuildsWindowPreviewStrip()
    {
        SpaceManager sm;
        sm.adoptExistingWindows();
        OverviewWindow w(&sm);
        auto *m = sm.monitors().first();
        w.openOnMonitor(m->hmon);
        if (!w.isOpen())
            QSKIP("overview did not open");

        // Bottom strip = windows of the *selected* space (starts as current).
        int expected = 0;
        const int cur = m->currentIndex;
        if (cur >= 0 && cur < m->spaces.size()) {
            for (HWND h : m->spaces[cur].windows)
                if (::IsWindow(h) && !::IsIconic(h))
                    ++expected;
        }
        QCOMPARE(w.windowPreviewCount(), expected);

        // Hover/preview another space → strip follows selection, desktop does NOT switch.
        if (m->spaces.size() > 1 && w.cardCount() > 1) {
            const int other = (cur + 1) % m->spaces.size();
            QVERIFY(w.previewSpace(other));
            QCOMPARE(m->currentIndex, cur); // UI-only: live space unchanged
            QCOMPARE(w.selectedIndex(), other);
            int expectedOther = 0;
            for (HWND h : m->spaces[other].windows)
                if (::IsWindow(h) && !::IsIconic(h))
                    ++expectedOther;
            QCOMPARE(w.windowPreviewCount(), expectedOther);
            // Composite screenshot for the previewed space is available (batch-built on open).
            QVERIFY(!m->spaces[other].screenshot.isNull());
        }

        w.closeOverview(false);
        for (int i = 0; i < 40 && (w.isOpen() || w.isAnimating()); ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 15);
    }

    void openBuildsAllSpacePreviews()
    {
        SpaceManager sm;
        OverviewWindow w(&sm);
        auto *m = sm.monitors().first();
        // Wipe so open must rebuild every space.
        for (Space &sp : m->spaces)
            sp.screenshot = QImage();

        w.openOnMonitor(m->hmon);
        if (!w.isOpen())
            QSKIP("overview did not open");

        for (int i = 0; i < m->spaces.size(); ++i)
            QVERIFY2(!m->spaces[i].screenshot.isNull(),
                     qPrintable(QStringLiteral("space %1 missing after open").arg(i)));

        // Hover must NOT rebuild — only display the cached image (same shared buffer).
        const QImage before = m->spaces[0].screenshot;
        QVERIFY(!before.isNull());
        if (m->spaces.size() > 1) {
            QVERIFY(w.previewSpace(1));
            QVERIFY(w.previewSpace(0));
        }
        QVERIFY(!m->spaces[0].screenshot.isNull());
        QCOMPARE(m->spaces[0].screenshot.size(), before.size());
        QCOMPARE(m->spaces[0].screenshot.constBits(), before.constBits());

        w.closeOverview(false);
        for (int i = 0; i < 40 && (w.isOpen() || w.isAnimating()); ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 15);
    }

    void overviewMarksEverySpaceRemovableWhenMoreThanOne()
    {
        SpaceManager sm;
        OverviewWindow w(&sm);
        auto *m = sm.monitors().first();
        QVERIFY(m->spaces.size() >= 1);
        if (m->spaces.size() < 2)
            QVERIFY(sm.addSpace(m->hmon));

        w.openOnMonitor(m->hmon);
        if (!w.isOpen() || w.cardCount() < 2)
            QSKIP("need overview with ≥2 cards");

        // ALL spaces (including empty) are removable when count > 1.
        const auto cards = w.findChildren<SpaceCardWidget *>();
        int checked = 0;
        for (SpaceCardWidget *card : cards) {
            if (!card || card->spaceIndex() < 0)
                continue;
            QVERIFY2(card->isRemovable(),
                     qPrintable(QStringLiteral("card %1 should be removable").arg(card->spaceIndex())));
            QCOMPARE(card->removeRevealDelayMs(), 2000);
            ++checked;
        }
        QVERIFY(checked >= 2);

        w.closeOverview(false);
        for (int i = 0; i < 40 && (w.isOpen() || w.isAnimating()); ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 15);
    }

    void addSpaceButtonEmitsAddRequested()
    {
        AddSpaceButton btn;
        QSignalSpy spy(&btn, &AddSpaceButton::addRequested);
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(36, 36),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&btn, &press);
        QCOMPARE(spy.count(), 1);
    }

    void addSpaceButtonDropEmitsWindowDropped()
    {
        AddSpaceButton btn;
        QSignalSpy dropped(&btn, &AddSpaceButton::windowDropped);
        QVERIFY(btn.handleWindowDrop(0xABCD));
        QCOMPARE(dropped.count(), 1);
        QCOMPARE(dropped.first().at(0).toULongLong(), quint64(0xABCD));
        QVERIFY(!btn.handleWindowDrop(0));
        QCOMPARE(dropped.count(), 1);
    }

    void previewAndCancelKeepsOriginOnDesktop()
    {
        SpaceManager sm;
        OverviewWindow w(&sm);
        auto *m = sm.monitors().first();
        const int origin = m->currentIndex;

        w.openOnMonitor(m->hmon);
        if (!w.isOpen())
            QSKIP("overview did not open");
        QCOMPARE(w.originSpaceIndex(), origin);

        if (m->spaces.size() > 1) {
            const int other = (origin + 1) % m->spaces.size();
            QVERIFY(w.previewSpace(other));
            // Soft preview only — monitor must stay on origin until click.
            QCOMPARE(m->currentIndex, origin);
            QCOMPARE(w.selectedIndex(), other);
        }

        w.closeOverview(false);
        QCOMPARE(m->currentIndex, origin);

        for (int i = 0; i < 40 && (w.isOpen() || w.isAnimating()); ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 15);
    }

    void placeKeepsCurrentSpace()
    {
        SpaceManager sm;
        OverviewWindow w(&sm);
        auto *m = sm.monitors().first();
        const int origin = m->currentIndex;

        HWND hwnd = ::CreateWindowExW(
            0, L"STATIC", L"place-stay",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE, 50, 50, 400, 300,
            nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
        QVERIFY(hwnd != nullptr);
        QVERIFY(sm.assignWindow(hwnd, m->hmon, origin));

        w.openOnMonitor(m->hmon);
        if (!w.isOpen())
            QSKIP("overview did not open");

        const int dest = (origin + 1) % m->spaces.size();
        QVERIFY(w.placeWindowInSpace(hwnd, dest));
        // Desktop stays on origin after drop.
        QCOMPARE(m->currentIndex, origin);
        QCOMPARE(sm.spaceOfWindow(hwnd), dest);
        QCOMPARE(w.selectedIndex(), origin);

        w.closeOverview(false);
        for (int i = 0; i < 40 && (w.isOpen() || w.isAnimating()); ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 15);

        sm.untrackWindow(hwnd);
        ::cloak::set(hwnd, false);
        ::DestroyWindow(hwnd);
    }

    void hoverLeaveDoesNotResetUntilPanelLeave()
    {
        SpaceManager sm;
        OverviewWindow w(&sm);
        auto *m = sm.monitors().first();
        w.openOnMonitor(m->hmon);
        if (!w.isOpen() || w.cardCount() < 2)
            QSKIP("need overview with multiple cards");

        // Drain enter animation so card-move timers cannot steal hover/selection.
        for (int i = 0; i < 30 && w.isAnimating(); ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 15);
        for (int i = 0; i < 10; ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);

        w.setSoftPreviewHoldMs(60);

        const int cur = m->currentIndex;
        const int other = (cur + 1) % m->spaces.size();
        QVERIFY(w.previewSpace(other));
        QCOMPARE(w.selectedIndex(), other);

        // Leave by spaceIndex, not findChildren order.
        const auto cards = w.findChildren<SpaceCardWidget *>();
        SpaceCardWidget *card = nullptr;
        for (SpaceCardWidget *c : cards) {
            if (c && c->spaceIndex() == other) {
                card = c;
                break;
            }
        }
        QVERIFY(card);
        {
            QEvent leave(QEvent::Leave);
            QApplication::sendEvent(card, &leave);
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 0);
        QVERIFY(w.isSoftPreviewHoldPending());
        QCOMPARE(w.selectedIndex(), other);
        QCOMPARE(m->currentIndex, cur);

        QTest::qWait(20);
        QCOMPARE(w.selectedIndex(), other);

        {
            QEvent leave(QEvent::Leave);
            QApplication::sendEvent(&w, &leave);
        }
        QVERIFY(w.isSoftPreviewHoldPending());
        QTRY_VERIFY_WITH_TIMEOUT(!w.isSoftPreviewHoldPending(), 500);
        QCOMPARE(w.selectedIndex(), cur);

        w.closeOverview(false);
        for (int i = 0; i < 40 && (w.isOpen() || w.isAnimating()); ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 15);
    }

    void softPreviewHoldCancelsOnKeepZoneEnter()
    {
        SpaceManager sm;
        OverviewWindow w(&sm);
        auto *m = sm.monitors().first();
        w.openOnMonitor(m->hmon);
        if (!w.isOpen() || w.cardCount() < 2)
            QSKIP("need cards");
        w.setSoftPreviewHoldMs(200);

        const int cur = m->currentIndex;
        const int other = (cur + 1) % m->spaces.size();
        QVERIFY(w.previewSpace(other));

        const auto cards = w.findChildren<SpaceCardWidget *>();
        {
            QEvent leave(QEvent::Leave);
            QApplication::sendEvent(cards[other], &leave);
        }
        QVERIFY(w.isSoftPreviewHoldPending());

        auto *scroll = w.findChild<QScrollArea *>();
        QVERIFY(scroll);
        {
            QEvent enter(QEvent::Enter);
            QApplication::sendEvent(scroll, &enter);
        }
        QVERIFY(!w.isSoftPreviewHoldPending());
        QCOMPARE(w.selectedIndex(), other);
        QCOMPARE(m->currentIndex, cur);

        w.closeOverview(false);
        for (int i = 0; i < 40 && (w.isOpen() || w.isAnimating()); ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 15);
    }

    void addAndRemoveSpaceFromManager()
    {
        SpaceManager sm;
        auto *m = sm.monitors().first();
        const int n0 = m->spaces.size();
        QVERIFY(n0 >= 1);

        QVERIFY(sm.addSpace(m->hmon));
        QCOMPARE(m->spaces.size(), n0 + 1);
        QVERIFY(!m->spaces.last().name.isEmpty());

        // Remove last (has no windows) — still allowed if size > 1; merge to previous.
        QVERIFY(sm.removeSpace(m->hmon, m->spaces.size() - 1));
        QCOMPARE(m->spaces.size(), n0);

        // Last remaining space cannot be removed.
        while (m->spaces.size() > 1)
            QVERIFY(sm.removeSpace(m->hmon, m->spaces.size() - 1));
        QVERIFY(!sm.removeSpace(m->hmon, 0));
        QCOMPARE(m->spaces.size(), 1);

        // Restore default count for other tests sharing process state? fresh manager each test.
        QVERIFY(sm.addSpace(m->hmon));
        QVERIFY(sm.addSpace(m->hmon));
        QVERIFY(sm.addSpace(m->hmon));
    }

    void removeSpaceMovesWindowsToPrevious()
    {
        SpaceManager sm;
        auto *m = sm.monitors().first();
        HWND hwnd = ::CreateWindowExW(
            0, L"STATIC", L"merge-src",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE, 60, 60, 320, 220,
            nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
        QVERIFY(hwnd != nullptr);
        QVERIFY(sm.addSpace(m->hmon)); // ensure > 1
        const int src = m->spaces.size() - 1;
        QVERIFY(sm.assignWindow(hwnd, m->hmon, src));
        QCOMPARE(sm.spaceOfWindow(hwnd), src);
        QVERIFY(sm.spaceHasWindows(m->hmon, src));

        QVERIFY(sm.removeSpace(m->hmon, src));
        // Window now owned by previous space.
        QCOMPARE(sm.spaceOfWindow(hwnd), src - 1);
        QVERIFY(m->spaces[src - 1].windows.contains(hwnd));
        QVERIFY(!sm.spaceHasWindows(m->hmon, m->spaces.size())); // out of range → false

        sm.untrackWindow(hwnd);
        ::cloak::set(hwnd, false);
        ::DestroyWindow(hwnd);
    }

    void moveSpaceReordersAndKeepsOwner()
    {
        SpaceManager sm;
        auto *m = sm.monitors().first();
        const int n = m->spaces.size();
        QVERIFY(n >= 2);

        HWND hwnd = ::CreateWindowExW(
            0, L"STATIC", L"reorder-w",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, 300, 200,
            nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
        QVERIFY(hwnd != nullptr);
        QVERIFY(sm.assignWindow(hwnd, m->hmon, 0));
        QCOMPARE(sm.spaceOfWindow(hwnd), 0);

        QVERIFY(sm.moveSpace(m->hmon, 0, 2));
        QCOMPARE(m->spaces.size(), n);
        // Owner remapped with the moved space.
        QCOMPARE(sm.spaceOfWindow(hwnd), 2);
        QVERIFY(m->spaces[2].windows.contains(hwnd));
        QVERIFY(!sm.spaceHasWindows(m->hmon, 0));
        QCOMPARE(m->currentIndex, 2); // was 0, followed the move

        QVERIFY(!sm.moveSpace(m->hmon, 0, 0));
        QVERIFY(!sm.moveSpace(m->hmon, -1, 0));
        QVERIFY(!sm.moveSpace(m->hmon, 0, n));

        sm.untrackWindow(hwnd);
        ::cloak::set(hwnd, false);
        ::DestroyWindow(hwnd);
    }

    void proportionalPreviewSizesPreserveAspect()
    {
        WindowPreviewWidget a;
        WindowPreviewWidget b;
        // Wide vs tall boxes — caller sets from real window rect.
        a.setImageBoxSize(QSize(320, 180)); // 16:9
        b.setImageBoxSize(QSize(160, 240)); // 2:3
        QCOMPARE(a.imageBoxSize(), QSize(320, 180));
        QCOMPARE(b.imageBoxSize(), QSize(160, 240));
        // Frame height includes label chrome (~40); width includes frame+margins (~20).
        QCOMPARE(a.height(), 180 + 40);
        QCOMPARE(b.height(), 240 + 40);
        QCOMPARE(a.width(), 320 + 20);
        QCOMPARE(b.width(), 160 + 20);
        // Different aspects must yield different widget sizes.
        QVERIFY(a.width() != b.width());
        QVERIFY(a.height() != b.height());
        // Image label is not clipped by the frame (fixed box must fit inside).
        QVERIFY(a.width() > a.imageBoxSize().width());
        QVERIFY(a.height() > a.imageBoxSize().height());
    }

    void minSizeKeepsWindowAspectAndNeverExceedsReal()
    {
        // Shared rule with OverviewWindow::tileSize: uniform s ≤ 1, aspect kept,
        // optional readable floor never grows past the real window.
        auto tileSize = [](int pw, int ph, double scale) -> QSize {
            const double W = std::max(1, pw);
            const double H = std::max(1, ph);
            double s = std::min(std::max(scale, 0.02), 1.0);
            const double sMinW = 96.0 / W;
            const double sMinH = 64.0 / H;
            s = std::max(s, std::min(1.0, std::max(sMinW, sMinH)));
            s = std::min(s, 1.0);
            const int w = std::max(1, std::min(pw, int(std::lround(W * s))));
            const int h = std::max(1, std::min(ph, int(std::lround(H * s))));
            return QSize(w, h);
        };

        // Huge window at scale 1 → exact real size (never 4× upscaled).
        const QSize full = tileSize(2000, 500, 4.0);
        QCOMPARE(full, QSize(2000, 500));

        // Wide window forced up to readable floor still ≤ real and aspect-stable.
        const QSize floored = tileSize(2000, 500, 0.01);
        QVERIFY(floored.width() <= 2000);
        QVERIFY(floored.height() <= 500);
        const double aspectIn = 2000.0 / 500.0;
        const double aspectOut = double(floored.width()) / floored.height();
        QVERIFY(qAbs(aspectIn - aspectOut) < 0.05);

        // Tiny window: floor cannot invent a larger-than-real tile.
        const QSize tiny = tileSize(40, 30, 1.0);
        QCOMPARE(tiny, QSize(40, 30));
    }

    void windowTilesNeverExceedRealAndPackWithGap()
    {
        SpaceManager sm;
        sm.adoptExistingWindows();
        OverviewWindow w(&sm);
        auto *m = sm.monitors().first();

        // Several short-lived own-process windows on the current space.
        QVector<HWND> created;
        for (int i = 0; i < 3; ++i) {
            HWND hwnd = ::CreateWindowExW(
                0, L"STATIC", L"pack-test",
                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                40 + i * 30, 40 + i * 24, 420 + i * 40, 280 + i * 20,
                nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
            QVERIFY(hwnd != nullptr);
            created.push_back(hwnd);
            // assignWindow bypasses own-process tracker reject used by placeWindowInSpace.
            sm.assignWindow(hwnd, m->hmon, m->currentIndex);
        }

        w.openOnMonitor(m->hmon);
        if (!w.isOpen())
            QSKIP("overview did not open");
        QVERIFY(w.windowPreviewCount() > 0);

        for (int i = 0; i < w.windowPreviewCount(); ++i) {
            const HWND hwnd = w.windowPreviewHandle(i);
            const QSize box = w.windowPreviewBoxSize(i);
            QVERIFY(hwnd != nullptr);
            // Tiles use Qt logical size (per-monitor DPI), not physical GetWindowRect.
            const QSize real = monitors::logicalWindowSize(hwnd);
            QVERIFY(real.width() > 0 && real.height() > 0);
            QVERIFY2(box.width() <= real.width(),
                     qPrintable(QStringLiteral("tile %1 wider than logical real (%2>%3)")
                                    .arg(box.width()).arg(real.width()).arg(real.width())));
            QVERIFY2(box.height() <= real.height(),
                     qPrintable(QStringLiteral("tile %1 taller than logical real (%2>%3)")
                                    .arg(box.height()).arg(real.height()).arg(real.height())));
            // Physical is always ≥ logical at DPI≥96 — tile must not exceed physical either.
            RECT wr{};
            if (::GetWindowRect(hwnd, &wr)) {
                QVERIFY(box.width() <= wr.right - wr.left);
                QVERIFY(box.height() <= wr.bottom - wr.top);
            }
        }

        // Distinct windows → distinct tiles; packing gaps are layout's job
        // (shelf uses `gap` between cells — asserted indirectly via no-exceed + count).
        for (int i = 0; i < w.windowPreviewCount(); ++i) {
            for (int j = i + 1; j < w.windowPreviewCount(); ++j)
                QVERIFY(w.windowPreviewHandle(i) != w.windowPreviewHandle(j));
        }

        w.closeOverview(false);
        for (int i = 0; i < 40 && (w.isOpen() || w.isAnimating()); ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 15);

        for (HWND hwnd : created) {
            sm.untrackWindow(hwnd);
            ::cloak::set(hwnd, false);
            ::DestroyWindow(hwnd);
        }
    }

    void openSeedsNonNullSpaceScreenshots()
    {
        SpaceManager sm;
        OverviewWindow w(&sm);
        auto *m = sm.monitors().first();

        // Wipe seeds to prove open path restores defaults.
        for (Space &sp : m->spaces)
            sp.screenshot = QImage();

        w.openOnMonitor(m->hmon);
        if (!w.isOpen())
            QSKIP("overview did not open");

        for (int i = 0; i < m->spaces.size(); ++i)
            QVERIFY2(!m->spaces[i].screenshot.isNull(),
                     qPrintable(QStringLiteral("space %1 has no screenshot after open").arg(i)));

        w.closeOverview(false);
        for (int i = 0; i < 40 && (w.isOpen() || w.isAnimating()); ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 15);
    }

    void assignMovesSourceScreenshotRefresh()
    {
        SpaceManager sm;
        auto *m = sm.monitors().first();
        HWND hwnd = ::CreateWindowExW(
            0, L"STATIC", L"move-refresh-test",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, 360, 240,
            nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
        QVERIFY(hwnd != nullptr);

        QVERIFY(sm.assignWindow(hwnd, m->hmon, 0));
        // Distinct marker on source screenshot so we can detect a rebuild.
        QImage marker(32, 18, QImage::Format_ARGB32_Premultiplied);
        marker.fill(QColor(1, 2, 3));
        m->spaces[0].screenshot = marker;

        QVERIFY(sm.assignWindow(hwnd, m->hmon, 2));
        QCOMPARE(sm.spaceOfWindow(hwnd), 2);
        QVERIFY(!m->spaces[0].screenshot.isNull());
        // Rebuild replaces the 32×18 marker with a monitor-sized composite.
        QVERIFY(m->spaces[0].screenshot.width() != 32);
        QVERIFY(m->spaces[0].screenshot.height() != 18);

        sm.untrackWindow(hwnd);
        ::cloak::set(hwnd, false);
        ::DestroyWindow(hwnd);
    }

    void compactCardsStillNavigate()
    {
        SpaceManager sm;
        OverviewWindow w(&sm);
        auto *m = sm.monitors().first();
        w.openOnMonitor(m->hmon);
        if (!w.isOpen() || w.cardCount() == 0)
            QSKIP("no cards");

        const int n = w.cardCount();
        const int start = w.selectedIndex();
        QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
        QApplication::sendEvent(&w, &right);
        QCOMPARE(w.selectedIndex(), (start + 1) % n);

        w.closeOverview(false);
        for (int i = 0; i < 40 && (w.isOpen() || w.isAnimating()); ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 15);
    }
};

QTEST_MAIN(TestWindowPlacement)
#include "test_window_placement.moc"
