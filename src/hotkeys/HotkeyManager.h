#pragma once

#include <QObject>
#include <QAbstractNativeEventFilter>
#include <QKeySequence>
#include <QString>
#include <QVector>
#include <Windows.h>

// Global hotkeys via low-level keyboard hook (primary) + RegisterHotKey fallback.
// Bindings are configurable (including Win-combos that occupy system shortcuts).
class HotkeyManager : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    enum Action {
        SwitchPrevSpace = 1,
        SwitchNextSpace = 2,
        ToggleOverview = 3,
        JumpSpace1 = 11,
        JumpSpace2 = 12,
        JumpSpace3 = 13,
        JumpSpace4 = 14,
    };

    struct Binding {
        int action = 0;
        UINT modifiers = 0; // MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN
        UINT vk = 0;
        QString sequence;   // portable QKeySequence text (UI / settings)
    };

    explicit HotkeyManager(QObject *parent = nullptr);
    ~HotkeyManager() override;

    // Load sequences from AppSettings (or defaults) and arm hooks/registration.
    bool registerDefaults();
    bool applySequences(const QVector<QString> &actionSequencePairs);
    void unregisterAll();

    QVector<Binding> bindings() const { return m_bindings; }
    Binding bindingFor(int action) const;

    // Portable sequence helpers (pure — unit-tested).
    static QString defaultSequence(int action);
    static QString systemSequence(int action); // Win+Tab / Ctrl+Win+←/→
    static bool sequenceToBinding(int action, const QString &sequence, Binding *out);
    static QString bindingToSequence(const Binding &b);
    static bool modifiersMatch(UINT required, bool ctrl, bool alt, bool shift, bool win);
    static UINT qtModsToWinMods(Qt::KeyboardModifiers mods);
    static int winModsToQtMods(UINT mods);
    static UINT qtKeyToVk(int qtKey);
    static int vkToQtKey(UINT vk);

    // native filter
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

signals:
    void actionTriggered(int action);

private:
    struct BindingInternal {
        int id = 0;
        UINT modifiers = 0;
        UINT vk = 0;
    };

    void loadFromSettings();
    bool armFromBindings();

    QVector<Binding> m_bindings;
    QVector<BindingInternal> m_registered; // RegisterHotKey fallback only
    QVector<int> m_registeredIds;
};
