#include <QtTest>

#include "core/settings/AppSettings.h"
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

    void winKeyDownDefersOnlyWhenWinBindingsArmed()
    {
        QCOMPARE(HotkeyManager::winKeyDownDecision(false), HotkeyManager::WinDownDecision::Pass);
        QCOMPARE(HotkeyManager::winKeyDownDecision(true), HotkeyManager::WinDownDecision::Defer);
    }

    // System preset: Ctrl+Win without ←/→ must not complete a shell Win tap
    // (that pops the Windows/Start menu).
    void winKeyUpSwallowsCtrlWinWithoutTrigger()
    {
        QCOMPARE(HotkeyManager::winKeyUpDecision(true, /*chord*/ false,
                                                 /*ctrl*/ true),
                 HotkeyManager::WinUpDecision::Swallow);
    }

    // After Ctrl+Win+Left was swallowed, Win up must also be swallowed.
    void winKeyUpSwallowsAfterChord()
    {
        QCOMPARE(HotkeyManager::winKeyUpDecision(true, /*chord*/ true,
                                                 /*ctrl*/ true),
                 HotkeyManager::WinUpDecision::Swallow);
        QCOMPARE(HotkeyManager::winKeyUpDecision(true, /*chord*/ true,
                                                 /*ctrl*/ false),
                 HotkeyManager::WinUpDecision::Swallow);
    }

    // Plain Win tap still opens Start (replay down+up).
    void winKeyUpInjectsPlainTap()
    {
        QCOMPARE(HotkeyManager::winKeyUpDecision(true, false, false),
                 HotkeyManager::WinUpDecision::InjectTap);
    }

    void winKeyUpPassesWhenNotDeferred()
    {
        QCOMPARE(HotkeyManager::winKeyUpDecision(false, false, false),
                 HotkeyManager::WinUpDecision::Pass);
    }

    void bindingsUseWinDetectsSystemPreset()
    {
        QVector<HotkeyManager::Binding> defs;
        HotkeyManager::Binding b;
        QVERIFY(HotkeyManager::sequenceToBinding(
            HotkeyManager::SwitchPrevSpace,
            HotkeyManager::defaultSequence(HotkeyManager::SwitchPrevSpace), &b));
        defs.push_back(b);
        QVERIFY(!HotkeyManager::bindingsUseWin(defs));

        QVERIFY(HotkeyManager::sequenceToBinding(
            HotkeyManager::SwitchPrevSpace,
            HotkeyManager::systemSequence(HotkeyManager::SwitchPrevSpace), &b));
        defs.push_back(b);
        QVERIFY(HotkeyManager::bindingsUseWin(defs));
    }

    void systemPresetArmsUsesWinBindings()
    {
        AppSettings s;
        s.setHotkeyPreset(QStringLiteral("system"));
        HotkeyManager hm;
        QVERIFY(hm.registerDefaults());
        QVERIFY(hm.usesWinBindings());
        hm.unregisterAll();
        s.setHotkeyPreset(QStringLiteral("default"));
        HotkeyManager hm2;
        QVERIFY(hm2.registerDefaults());
        QVERIFY(!hm2.usesWinBindings());
        hm2.unregisterAll();
    }
};

QTEST_MAIN(TestHotkeys)
#include "test_hotkeys.moc"
