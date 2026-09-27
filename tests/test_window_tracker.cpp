#include <QtTest>

#include "core/window/WindowTracker.h"

#include <Windows.h>

class TestWindowTracker : public QObject {
    Q_OBJECT
  private slots:
    void rejectsNullOrGarbage()
    {
        QVERIFY(!WindowTracker::isManageable(nullptr));
        QVERIFY(!WindowTracker::isManageable(reinterpret_cast<HWND>(1)));
    }

    // Same-process windows are intentionally NOT manageable: SpaceWM must never
    // cloak its own overview / flash overlay. A top-level window we create here
    // therefore must be rejected.
    void rejectsOwnProcessTopLevel()
    {
        HWND hwnd = ::CreateWindowExW(0, L"STATIC", L"own process window",
                                      WS_OVERLAPPEDWINDOW | WS_VISIBLE, 50, 50, 250, 150, nullptr,
                                      nullptr, ::GetModuleHandleW(nullptr), nullptr);
        QVERIFY(hwnd != nullptr);
        QVERIFY(::IsWindowVisible(hwnd));
        QVERIFY(!WindowTracker::isManageable(hwnd));
        ::DestroyWindow(hwnd);
    }

    void findsAtLeastOneForeignOrNone()
    {
        // On a real desktop there are usually Explorer/other-app windows.
        // We only assert: every reported HWND is a valid foreign top-level window.
        const auto list = WindowTracker::snapshotManageableWindows();
        for (HWND h : list) {
            QVERIFY(::IsWindow(h));
            DWORD pid = 0;
            ::GetWindowThreadProcessId(h, &pid);
            QVERIFY(pid != ::GetCurrentProcessId());
            QVERIFY(::GetAncestor(h, GA_ROOT) == h);
        }
    }

    void rejectsInvisible()
    {
        HWND hwnd = ::CreateWindowExW(0, L"STATIC", L"hidden", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100,
                                      nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
        QVERIFY(hwnd != nullptr);
        QVERIFY(!::IsWindowVisible(hwnd));
        QVERIFY(!WindowTracker::isManageable(hwnd));
        ::DestroyWindow(hwnd);
    }

    void rejectsShellClassNames()
    {
        HWND tray = ::FindWindowW(L"Shell_TrayWnd", nullptr);
        if (tray)
            QVERIFY(!WindowTracker::isManageable(tray));

        HWND progman = ::FindWindowW(L"Progman", nullptr);
        if (progman)
            QVERIFY(!WindowTracker::isManageable(progman));
    }

    void snapshotDoesNotCrash()
    {
        const auto list = WindowTracker::snapshotManageableWindows();
        for (HWND h : list)
            QVERIFY(::IsWindow(h));
    }

    void trackerConstructsAndHooks()
    {
        WindowTracker tracker;
        QSignalSpy created(&tracker, &WindowTracker::windowCreated);

        HWND hwnd =
            ::CreateWindowExW(0, L"STATIC", L"event window", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 10,
                              10, 120, 80, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
        QVERIFY(hwnd != nullptr);

        for (int i = 0; i < 20; ++i) {
            MSG msg;
            while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                ::TranslateMessage(&msg);
                ::DispatchMessageW(&msg);
            }
            ::Sleep(5);
        }

        // Own-process window must not be reported as created/manageable.
        for (int i = 0; i < created.count(); ++i) {
            const auto val = created.at(i).at(0).toULongLong();
            if (val == reinterpret_cast<quint64>(hwnd))
                QFAIL("own-process window must not be tracked");
        }

        ::DestroyWindow(hwnd);
    }

    void untrackUnknownIsSafe()
    {
        WindowTracker tracker;
        // Emitting destroy for unknown hwnd should be a no-op path via manager
        // — here we only ensure the tracker API itself doesn't crash.
        emit tracker.windowDestroyed(reinterpret_cast<quint64>(nullptr));
        QVERIFY(true);
    }

    void foregroundEventEmitsWindowForeground()
    {
        WindowTracker tracker;
        QSignalSpy fg(&tracker, &WindowTracker::windowForeground);

        HWND hwnd =
            ::CreateWindowExW(0, L"STATIC", L"fg-target", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 20, 20,
                              200, 120, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
        QVERIFY(hwnd != nullptr);

        tracker.handle(EVENT_SYSTEM_FOREGROUND, hwnd, OBJID_WINDOW);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 0);

        QCOMPARE(fg.count(), 1);
        QCOMPARE(fg.first().at(0).toULongLong(), quint64(reinterpret_cast<quintptr>(hwnd)));

        ::DestroyWindow(hwnd);
    }
};

QTEST_MAIN(TestWindowTracker)
#include "test_window_tracker.moc"
