#include "AppSettings.h"

#include "hotkeys/HotkeyManager.h"

#include <QCoreApplication>
#include <QSettings>

#include <Windows.h>

namespace {
constexpr auto kOrg = "SpaceWM";
constexpr auto kApp = "SpaceWM";

QString presetDefault(int action)
{
    return HotkeyManager::defaultSequence(action);
}

QString presetSystem(int action)
{
    return HotkeyManager::systemSequence(action);
}
} // namespace

AppSettings::AppSettings(QObject *parent)
    : QObject(parent)
{
}

QString AppSettings::hotkey(int action) const
{
    QSettings s(kOrg, kApp);
    return s.value(QStringLiteral("hotkeys/%1").arg(action)).toString();
}

void AppSettings::setHotkey(int action, const QString &sequence)
{
    QSettings s(kOrg, kApp);
    s.setValue(QStringLiteral("hotkeys/%1").arg(action), sequence);
    // Manual edit → custom preset unless it matches a known preset.
    const QString def = presetDefault(action);
    const QString sys = presetSystem(action);
    Q_UNUSED(def)
    Q_UNUSED(sys)
    emit changed();
}

QString AppSettings::hotkeyOrDefault(int action) const
{
    const QString raw = hotkey(action);
    if (!raw.isEmpty())
        return raw;
    return presetDefault(action);
}

QString AppSettings::hotkeyPreset() const
{
    QSettings s(kOrg, kApp);
    return s.value(QStringLiteral("hotkeys/preset"), QStringLiteral("default")).toString();
}

void AppSettings::setHotkeyPreset(const QString &preset)
{
    QSettings s(kOrg, kApp);
    const QString p = (preset == QStringLiteral("system") || preset == QStringLiteral("custom"))
                          ? preset
                          : QStringLiteral("default");
    s.setValue(QStringLiteral("hotkeys/preset"), p);
    if (p != QStringLiteral("custom"))
        syncHotkeysFromPreset(p);
    emit changed();
}

void AppSettings::syncHotkeysFromPreset(const QString &preset)
{
    QSettings s(kOrg, kApp);
    const int actions[] = {
        HotkeyManager::SwitchPrevSpace,
        HotkeyManager::SwitchNextSpace,
        HotkeyManager::ToggleOverview,
        HotkeyManager::JumpSpace1,
        HotkeyManager::JumpSpace2,
        HotkeyManager::JumpSpace3,
        HotkeyManager::JumpSpace4,
    };
    for (int a : actions) {
        const QString seq = (preset == QStringLiteral("system")) ? presetSystem(a)
                                                                 : presetDefault(a);
        s.setValue(QStringLiteral("hotkeys/%1").arg(a), seq);
    }
}

bool AppSettings::autoStart() const
{
    QSettings s(kOrg, kApp);
    if (s.contains(QStringLiteral("general/autoStart")))
        return s.value(QStringLiteral("general/autoStart")).toBool();
    return autoStartRegistryEnabled();
}

void AppSettings::setAutoStart(bool on)
{
    QSettings s(kOrg, kApp);
    s.setValue(QStringLiteral("general/autoStart"), on);
    applyAutoStartRegistry(on);
    emit changed();
}

bool AppSettings::exclusiveSpaces() const
{
    QSettings s(kOrg, kApp);
    return s.value(QStringLiteral("general/exclusiveSpaces"), false).toBool();
}

void AppSettings::setExclusiveSpaces(bool on)
{
    QSettings s(kOrg, kApp);
    s.setValue(QStringLiteral("general/exclusiveSpaces"), on);
    emit changed();
}

bool AppSettings::applyAutoStartRegistry(bool enable)
{
    HKEY key = nullptr;
    if (::RegCreateKeyExW(HKEY_CURRENT_USER,
                          L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                          0, nullptr, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, nullptr,
                          &key, nullptr)
        != ERROR_SUCCESS)
        return false;

    bool ok = true;
    if (enable) {
        const QString exe = QCoreApplication::applicationFilePath();
        if (exe.isEmpty()) {
            ok = false;
        } else {
            const QString cmd = QStringLiteral("\"%1\"").arg(exe);
            const QByteArray utf8 = cmd.toUtf8();
            // REG_SZ wide
            const std::wstring w = cmd.toStdWString();
            ok = ::RegSetValueExW(key, L"SpaceWM", 0, REG_SZ,
                                  reinterpret_cast<const BYTE *>(w.c_str()),
                                  DWORD((w.size() + 1) * sizeof(wchar_t)))
                == ERROR_SUCCESS;
            Q_UNUSED(utf8)
        }
    } else {
        const LSTATUS st = ::RegDeleteValueW(key, L"SpaceWM");
        ok = (st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND);
    }
    ::RegCloseKey(key);
    return ok;
}

bool AppSettings::autoStartRegistryEnabled()
{
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER,
                        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                        0, KEY_QUERY_VALUE, &key)
        != ERROR_SUCCESS)
        return false;
    DWORD type = 0;
    DWORD size = 0;
    const LSTATUS st = ::RegQueryValueExW(key, L"SpaceWM", nullptr, &type, nullptr, &size);
    ::RegCloseKey(key);
    return st == ERROR_SUCCESS && type == REG_SZ && size > 0;
}
