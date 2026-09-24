#include <QtTest>

#include "hotkeys/HotkeyManager.h"

class TestHotkeys : public QObject {
    Q_OBJECT
private slots:
    void constructsAndDestructsCleanly()
    {
        // Must not crash without / with app instance (QTEST_MAIN provides one).
        auto *hm = new HotkeyManager(this);
        delete hm;
        QVERIFY(true);
    }

    void registerThenUnregister()
    {
        HotkeyManager hm;
        // Registration may partially fail if OS already owns the combo —
        // we only require no crash and that unregister is safe.
        const bool any = hm.registerDefaults();
        Q_UNUSED(any)
        hm.unregisterAll();
        hm.unregisterAll(); // double-unregister safe
        QVERIFY(true);
    }

    void reRegisterIsSafe()
    {
        HotkeyManager hm;
        hm.registerDefaults();
        hm.registerDefaults(); // clears previous first
        hm.unregisterAll();
        QVERIFY(true);
    }

    void nonHotkeyNativeEventIgnored()
    {
        HotkeyManager hm;
        MSG msg{};
        msg.message = WM_NULL;
        qintptr result = 0;
        QVERIFY(!hm.nativeEventFilter("windows_generic_MSG", &msg, &result));
        QVERIFY(!hm.nativeEventFilter("windows_dispatcher_MSG", nullptr, &result));
    }
};

QTEST_MAIN(TestHotkeys)
#include "test_hotkeys.moc"
