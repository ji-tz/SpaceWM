#include <QtTest>

#include "core/CloakController.h"

#include <Windows.h>

// Cloak is the primitive behind space switching: hide/show without destroying.
class TestCloak : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        // A plain overlapped window we control.
        m_hwnd = ::CreateWindowExW(
            0, L"STATIC", L"SpaceWM cloak test",
            WS_OVERLAPPEDWINDOW, 0, 0, 200, 100,
            nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
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

private:
    HWND m_hwnd = nullptr;
};

QTEST_MAIN(TestCloak)
#include "test_cloak.moc"
