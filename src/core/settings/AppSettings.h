#pragma once

#include <QObject>
#include <QString>

// QSettings-backed app preferences (hotkeys, general).
class AppSettings : public QObject {
    Q_OBJECT
  public:
    explicit AppSettings(QObject *parent = nullptr);

    // Hotkey sequences (QKeySequence portable text), e.g. "Ctrl+Alt+Left".
    QString hotkey(int action) const; // action = HotkeyManager::Action
    void setHotkey(int action, const QString &sequence);
    QString hotkeyOrDefault(int action) const; // never empty

    // "default" → Ctrl+Alt+…; "system" → Win+Tab / Ctrl+Win+←/→
    QString hotkeyPreset() const; // "default" | "system" | "custom"
    void setHotkeyPreset(const QString &preset);

    bool autoStart() const;
    void setAutoStart(bool on);

    // Write HKCU\...\Run entry. Returns false if registry write failed.
    static bool applyAutoStartRegistry(bool enable);
    static bool autoStartRegistryEnabled();

  signals:
    void changed();

  private:
    void syncHotkeysFromPreset(const QString &preset);
};
