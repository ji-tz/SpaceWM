#include <QtTest>

#include "core/settings/AppSettings.h"
#include "hotkeys/HotkeyManager.h"
#include "ui/settings/SettingsDialog.h"

class TestSettings : public QObject {
    Q_OBJECT
private slots:
    void sequenceRoundTrip()
    {
        HotkeyManager::Binding b;
        QVERIFY(HotkeyManager::sequenceToBinding(HotkeyManager::SwitchPrevSpace,
                                                 QStringLiteral("Ctrl+Alt+Left"), &b));
        QCOMPARE(b.action, int(HotkeyManager::SwitchPrevSpace));
        QVERIFY(b.modifiers & MOD_CONTROL);
        QVERIFY(b.modifiers & MOD_ALT);
        QCOMPARE(b.vk, UINT(VK_LEFT));
        QCOMPARE(HotkeyManager::bindingToSequence(b), QStringLiteral("Ctrl+Alt+Left"));
    }

    void systemPresetUsesWinCombos()
    {
        QCOMPARE(HotkeyManager::systemSequence(HotkeyManager::ToggleOverview),
                 QStringLiteral("Win+Tab"));
        HotkeyManager::Binding b;
        QVERIFY(HotkeyManager::sequenceToBinding(HotkeyManager::ToggleOverview,
                                                 HotkeyManager::systemSequence(
                                                     HotkeyManager::ToggleOverview),
                                                 &b));
        QVERIFY(b.modifiers & MOD_WIN);
        QCOMPARE(b.vk, UINT(VK_TAB));

        QVERIFY(HotkeyManager::sequenceToBinding(
            HotkeyManager::SwitchPrevSpace,
            HotkeyManager::systemSequence(HotkeyManager::SwitchPrevSpace), &b));
        QVERIFY(b.modifiers & MOD_WIN);
        QVERIFY(b.modifiers & MOD_CONTROL);
        QCOMPARE(b.vk, UINT(VK_LEFT));
    }

    void modifiersMatchIsExact()
    {
        QVERIFY(HotkeyManager::modifiersMatch(MOD_CONTROL | MOD_ALT, true, true, false, false));
        QVERIFY(!HotkeyManager::modifiersMatch(MOD_CONTROL | MOD_ALT, true, true, false, true));
        QVERIFY(HotkeyManager::modifiersMatch(MOD_WIN, false, false, false, true));
        QVERIFY(!HotkeyManager::modifiersMatch(0, false, false, false, true));
    }

    void presetPersistsAndSyncsSequences()
    {
        AppSettings s;
        s.setHotkeyPreset(QStringLiteral("system"));
        QCOMPARE(s.hotkeyPreset(), QStringLiteral("system"));
        QCOMPARE(s.hotkeyOrDefault(HotkeyManager::ToggleOverview),
                 QStringLiteral("Win+Tab"));
        QCOMPARE(s.hotkeyOrDefault(HotkeyManager::SwitchPrevSpace),
                 QStringLiteral("Ctrl+Win+Left"));

        s.setHotkeyPreset(QStringLiteral("default"));
        QCOMPARE(s.hotkeyOrDefault(HotkeyManager::ToggleOverview),
                 QStringLiteral("Ctrl+Alt+Space"));

        s.setHotkey(HotkeyManager::ToggleOverview, QStringLiteral("Ctrl+Alt+O"));
        s.setHotkeyPreset(QStringLiteral("custom"));
        QCOMPARE(s.hotkeyOrDefault(HotkeyManager::ToggleOverview),
                 QStringLiteral("Ctrl+Alt+O"));
    }

    void hotkeyManagerLoadsSettings()
    {
        AppSettings s;
        s.setHotkeyPreset(QStringLiteral("system"));
        HotkeyManager hm;
        QVERIFY(hm.registerDefaults());
        const auto overview = hm.bindingFor(HotkeyManager::ToggleOverview);
        QVERIFY(overview.vk != 0);
        QCOMPARE(HotkeyManager::bindingToSequence(overview), QStringLiteral("Win+Tab"));
        hm.unregisterAll();
        s.setHotkeyPreset(QStringLiteral("default"));
    }

    void autostartRegistryToggle()
    {
        // Save/restore whatever is on the machine.
        const bool was = AppSettings::autoStartRegistryEnabled();
        QVERIFY(AppSettings::applyAutoStartRegistry(true));
        QVERIFY(AppSettings::autoStartRegistryEnabled());
        QVERIFY(AppSettings::applyAutoStartRegistry(false));
        QVERIFY(!AppSettings::autoStartRegistryEnabled());
        if (was)
            QVERIFY(AppSettings::applyAutoStartRegistry(true));
    }

    void settingsDialogConstructsAndReloads()
    {
        SettingsDialog dlg;
        dlg.reload();
        QSignalSpy applied(&dlg, &SettingsDialog::settingsApplied);
        QVERIFY(applied.isValid());
        // KeyPressEvent path exists; non-capture keys fall through safely.
        QKeyEvent ev(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(&dlg, &ev);
        QVERIFY(true);
    }
};

QTEST_MAIN(TestSettings)
#include "test_settings.moc"
