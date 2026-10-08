#include <QtTest>

#include "core/window/CloakController.h"

#include <Windows.h>

// Cloak is the primitive behind space switching: hide/show without destroying.
class TestCloak : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase()
    {
        // A plain overlapped window we control.
        m_hwnd =
            ::CreateWindowExW(0, L"STATIC", L"SpaceWM cloak test", WS_OVERLAPPEDWINDOW, 0, 0, 200,
                              100, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
        QVERIFY(m_hwnd != nullptr);
        ::ShowWindow(m_hwnd, SW_SHOWNORMAL);
    }

    void cleanupTestCase()
    {
        if (m_hwnd) {
            ::DestroyWindow(m_hwnd);
            m_hwnd = nullptr;
        }
    }

    void nullWindowIsSafe()
    {
        QVERIFY(!cloak::set(nullptr, true));
        QVERIFY(!cloak::isCloaked(nullptr));
        QVERIFY(!cloak::set(reinterpret_cast<HWND>(0x1234), true));
    }

    void cloakAndUncloakRoundtrip()
    {
        QVERIFY(!cloak::isCloaked(m_hwnd));

        QVERIFY(cloak::set(m_hwnd, true));
        // DWM may need a paint cycle; poll briefly.
        bool cloaked = false;
        for (int i = 0; i < 50 && !cloaked; ++i) {
            cloaked = cloak::isCloaked(m_hwnd);
            if (!cloaked)
                ::Sleep(10);
        }
        QVERIFY(cloaked);

        QVERIFY(cloak::set(m_hwnd, false));
        bool visible = true;
        for (int i = 0; i < 50 && visible; ++i) {
            visible = !cloak::isCloaked(m_hwnd);
            if (!visible)
                ::Sleep(10);
        }
        QVERIFY(visible);
    }

    void doubleCloakIsIdempotent()
    {
        QVERIFY(cloak::set(m_hwnd, true));
        QVERIFY(cloak::set(m_hwnd, true));
        QVERIFY(cloak::isCloaked(m_hwnd));
        QVERIFY(cloak::set(m_hwnd, false));
        QVERIFY(!cloak::isCloaked(m_hwnd));
    }

    void showAllHiddenRestoresOnlyOurs()
    {
        QVERIFY(cloak::set(m_hwnd, true));
        QVERIFY(cloak::isCloaked(m_hwnd));
        const int restored = cloak::showAllHidden();
        QVERIFY(restored >= 1);
        // Our window must be out of the hide set after restore-all.
        QVERIFY(!cloak::isHiddenByUs(m_hwnd));
        bool visible = false;
        for (int i = 0; i < 50 && !visible; ++i) {
            visible = !cloak::isCloaked(m_hwnd);
            if (!visible)
                ::Sleep(10);
        }
        QVERIFY(visible);
        // Second call is a no-op (nothing left for us).
        QCOMPARE(cloak::showAllHidden(), 0);
        QCOMPARE(cloak::hiddenCount(), 0);
    }

    // Space switches must not play show/hide animations: cloak force-disables
    // the per-window DWM transition (DWMWA_TRANSITIONS_FORCEDISABLED) and a
    // successful show restores it so the app's normal animations come back.
    // The attribute is [set]-only in the SDK — assert via cloak bookkeeping,
    // which also fails if the underlying DwmSetWindowAttribute call errors.
    void cloakForcesTransitionsOffUntilShown()
    {
        QVERIFY(!cloak::transitionsForced(m_hwnd));

        QVERIFY(cloak::set(m_hwnd, true));
        QVERIFY(cloak::isCloaked(m_hwnd));
        QVERIFY2(cloak::transitionsForced(m_hwnd),
                 "cloak must force-disable DWM transitions while hidden");

        QVERIFY(cloak::set(m_hwnd, false));
        QVERIFY(!cloak::isCloaked(m_hwnd));
        QVERIFY2(!cloak::transitionsForced(m_hwnd),
                 "show must restore DWM transitions for the window");
    }

    // backendOf is how tests (and TR logs) see WHICH mechanism hid a window —
    // the shell SetCloak backend is the only one whose taskbar button
    // survives, so silently degrading to DWM/ShowWindow must be observable.
    void backendBookkeepingTracksHideState()
    {
        QCOMPARE(cloak::backendOf(m_hwnd), cloak::Backend::None);

        QVERIFY(cloak::set(m_hwnd, true));
        const auto b = cloak::backendOf(m_hwnd);
        QVERIFY2(b == cloak::Backend::ImmersiveView || b == cloak::Backend::DwmAttribute ||
                     b == cloak::Backend::ShowWindow,
                 "a hidden window must report the backend that hid it");

        QVERIFY(cloak::set(m_hwnd, false));
        QCOMPARE(cloak::backendOf(m_hwnd), cloak::Backend::None);
    }

    // Contract: whenever the ImmersiveShell proxy resolves, cloak MUST take
    // the IApplicationView::SetCloak path. The DWM/ShowWindow fallbacks strip
    // the taskbar button (regression this guards: CLSCTX_INPROC_SERVER made
    // activation fail with 0x80040154 and every window silently fell back).
    void shellBackendPreferredWhenAvailable()
    {
        if (!cloak::shellBackendAvailable())
            QSKIP("ImmersiveShell IApplicationViewCollection unavailable in this session");

        // A brand-new window may not have a shell view yet (probe: 0-150ms
        // registration lag) — retry cloak/show cycles before giving up.
        HWND w = ::CreateWindowExW(0, L"STATIC", L"shell backend probe", WS_OVERLAPPEDWINDOW, 0,
                                   0, 240, 120, nullptr, nullptr, ::GetModuleHandleW(nullptr),
                                   nullptr);
        QVERIFY(w != nullptr);
        ::ShowWindow(w, SW_SHOWNORMAL);

        bool viaShell = false;
        for (int i = 0; i < 50 && !viaShell; ++i) {
            QVERIFY(cloak::set(w, true));
            viaShell = cloak::backendOf(w) == cloak::Backend::ImmersiveView;
            if (!viaShell) {
                QVERIFY(cloak::set(w, false)); // undo the fallback, retry the race
                QTest::qWait(100);
            }
        }
        const bool shown = cloak::set(w, false);
        ::DestroyWindow(w);

        QVERIFY(shown);
        QVERIFY2(viaShell,
                 "shell backend available but cloak fell back to DWM/ShowWindow — "
                 "taskbar buttons would disappear");
        QCOMPARE(cloak::backendOf(w), cloak::Backend::None);
    }

  private:
    HWND m_hwnd = nullptr;
};

QTEST_MAIN(TestCloak)
#include "test_cloak.moc"
