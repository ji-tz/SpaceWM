#pragma once

#include <QUuid>
#include <Windows.h>

// AppSettings explicitly uses native QSettings and the Windows Run key.
// Redirect HKCU for this test process only, including all direct Win32 calls.
class SettingsIsolation {
  public:
    SettingsIsolation()
    {
        m_path = (QStringLiteral("Software\\SpaceWM-test-") +
                  QUuid::createUuid().toString(QUuid::WithoutBraces))
                     .toStdWString();
        // QSettings creates non-volatile children, which cannot live below a
        // volatile parent. The unique test subtree is removed on teardown.
        if (::RegCreateKeyExW(HKEY_CURRENT_USER, m_path.c_str(), 0, nullptr,
                              REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &m_root,
                              nullptr) == ERROR_SUCCESS)
            m_valid = ::RegOverridePredefKey(HKEY_CURRENT_USER, m_root) == ERROR_SUCCESS;
    }

    ~SettingsIsolation()
    {
        if (m_valid)
            ::RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
        if (m_root) {
            ::RegCloseKey(m_root);
            ::RegDeleteTreeW(HKEY_CURRENT_USER, m_path.c_str());
        }
    }

    SettingsIsolation(const SettingsIsolation &) = delete;
    SettingsIsolation &operator=(const SettingsIsolation &) = delete;
    bool isValid() const { return m_valid; }
    HKEY root() const { return m_root; }

  private:
    std::wstring m_path;
    HKEY m_root = nullptr;
    bool m_valid = false;
};
