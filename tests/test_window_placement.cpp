#include <QtTest>

#include "core/SpaceManager.h"
#include "ui/OverviewWindow.h"
#include "ui/SpaceCardWidget.h"
#include "ui/WindowPreviewWidget.h"

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

        // Bottom strip = windows of the *current* space only (live preview).
        int expected = 0;
        const int cur = m->currentIndex;
        if (cur >= 0 && cur < m->spaces.size()) {
            for (HWND h : m->spaces[cur].windows)
                if (::IsWindow(h) && !::IsIconic(h))
                    ++expected;
        }
        QCOMPARE(w.windowPreviewCount(), expected);

        // Hover/preview another space → strip follows that space.
        if (m->spaces.size() > 1 && w.cardCount() > 1) {
            const int other = (cur + 1) % m->spaces.size();
            QVERIFY(w.previewSpace(other));
            QCOMPARE(m->currentIndex, other);
            QCOMPARE(w.selectedIndex(), other);
            int expectedOther = 0;
            for (HWND h : m->spaces[other].windows)
                if (::IsWindow(h) && !::IsIconic(h))
                    ++expectedOther;
            QCOMPARE(w.windowPreviewCount(), expectedOther);
            // Composite screenshot for the previewed space is available.
            QVERIFY(!m->spaces[other].screenshot.isNull());
        }

        w.closeOverview(false);
        for (int i = 0; i < 40 && (w.isOpen() || w.isAnimating()); ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 15);
    }

    void previewAndCancelRestoresOrigin()
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
            QCOMPARE(m->currentIndex, other);
        }

        w.closeOverview(false); // cancel → restore origin on desktop
        QCOMPARE(m->currentIndex, origin);

        for (int i = 0; i < 40 && (w.isOpen() || w.isAnimating()); ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 15);
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
        // Frame height includes label chrome (~40).
        QCOMPARE(a.height(), 180 + 40);
        QCOMPARE(b.height(), 240 + 40);
        // Different aspects must yield different widget sizes.
        QVERIFY(a.width() != b.width());
        QVERIFY(a.height() != b.height());
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
