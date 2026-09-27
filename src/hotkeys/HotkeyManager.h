#pragma once

#include <QObject>
#include <QAbstractNativeEventFilter>
#include <QKeySequence>
#include <QString>
#include <QVector>
#include <Windows.h>

class QThread;

// Global hotkeys via low-level keyboard hook (primary) + RegisterHotKey fallback.
// Bindings are configurable (including Win-combos that occupy system shortcuts).
//
// The LL hook runs on a DEDICATED thread with its own message pump: Windows
// silently removes hooks whose installer thread does not answer within
// LowLevelHooksTimeout, and the overview open path can block the main thread
// for seconds (capture batches) — on the main thread every hotkey would die
// after the first slow open.
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

    // Pure LL-hook policy for the physical Win key (unit-tested).
    enum class WinDownDecision { Pass, Defer };
    enum class WinUpDecision { Pass, Swallow, InjectTap };

    explicit HotkeyManager(QObject *parent = nullptr);
    ~HotkeyManager() override;

    // Load sequences from AppSettings (or defaults) and arm hooks/registration.
    bool registerDefaults();
    bool applySequences(const QVector<QString> &actionSequencePairs);
    void unregisterAll();

    QVector<Binding> bindings() const { return m_bindings; }
    Binding bindingFor(int action) const;
    bool usesWinBindings() const;

    // Esc handling while the overview is open: the hook swallows Esc and
    // emits escapeRequested() instead — works even when the panel lost
    // focus to the bounced foreground window (panel keyPressEvent still
    // covers the focused case / tests).
    void setConsumeEscape(bool on);
    bool consumeEscape() const;

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
    static bool bindingsUseWin(const QVector<Binding> &bindings);
    // Win key down: when any binding uses MOD_WIN, defer so the shell never
    // sees a bare Win tap until we know whether a SpaceWM chord completes.
    static WinDownDecision winKeyDownDecision(bool winBindingsArmed);
    // Win key up while down was deferred and not forwarded to the shell.
    //   chordKeyAte        — a MOD_WIN binding already swallowed its trigger
    //   foreignModifierHeld — Ctrl/Alt/Shift down (e.g. Ctrl+Win with no trigger)
    // Swallow both cases so Windows does not open the Start/Windows menu;
    // plain Win tap still injects down+up so Start keeps working.
    static WinUpDecision winKeyUpDecision(bool deferredNotForwarded,
                                          bool chordKeyAte,
                                          bool foreignModifierHeld);
    // LL-hook auto-repeat guard (unit-tested): Windows repeats keyDOWN while
    // a key is held and MOD_NOREPEAT does not apply to hooks — without this
    // one long Ctrl+Alt+Space press toggles the overview twice (open → close).
    // keyDownEmits: true only for the FIRST down of a press; keyUpSeen clears.
    static bool keyDownEmits(UINT vk);
    static void keyUpSeen(UINT vk);
    // Alt+Tab / Alt+Ctrl+Tab — the documented window-switcher chord. The hook
    // observes it directly (unit-tested) so main can treat the next
    // foreground change as a user switch instead of a system side-effect.
    static bool isWindowSwitchChord(UINT vk, bool altDown);

    // native filter
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

signals:
    void actionTriggered(int action);
    // User pressed Alt+Tab (window switcher) — queued like actionTriggered.
    void windowSwitchChord();
    // Esc pressed while consumeEscape() is set (overview open) — queued.
    void escapeRequested();

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
    QThread *m_hookThread = nullptr; // owns the LL hook + its message pump
};
