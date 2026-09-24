#include "HotkeyManager.h"

#include "core/settings/AppSettings.h"

#include <QCoreApplication>
#include <QAbstractEventDispatcher>
#include <QHash>

#include <Windows.h>

namespace {
constexpr int kIdPrev = 1001;
constexpr int kIdNext = 1002;
constexpr int kIdOverview = 1003;
constexpr int kIdJump1 = 1011;
constexpr int kIdJump2 = 1012;
constexpr int kIdJump3 = 1013;
constexpr int kIdJump4 = 1014;

int actionToId(int action)
{
    switch (action) {
    case HotkeyManager::SwitchPrevSpace: return kIdPrev;
    case HotkeyManager::SwitchNextSpace: return kIdNext;
    case HotkeyManager::ToggleOverview: return kIdOverview;
    case HotkeyManager::JumpSpace1: return kIdJump1;
    case HotkeyManager::JumpSpace2: return kIdJump2;
    case HotkeyManager::JumpSpace3: return kIdJump3;
    case HotkeyManager::JumpSpace4: return kIdJump4;
    default: return 0;
    }
}

int idToAction(int id)
{
    switch (id) {
    case kIdPrev: return HotkeyManager::SwitchPrevSpace;
    case kIdNext: return HotkeyManager::SwitchNextSpace;
    case kIdOverview: return HotkeyManager::ToggleOverview;
    case kIdJump1: return HotkeyManager::JumpSpace1;
    case kIdJump2: return HotkeyManager::JumpSpace2;
    case kIdJump3: return HotkeyManager::JumpSpace3;
    case kIdJump4: return HotkeyManager::JumpSpace4;
    default: return 0;
    }
}

HHOOK g_llHookHandle = nullptr;
HotkeyManager *g_manager = nullptr;

// --- LL Win-key state machine (System preset occupies Win combos) ---
// The shell opens the Start/Windows menu when it sees Win down + Win up
// without a key it recognizes in between. Swallowing only Left/Tab still
// lets that Win tap through → menu. Defer Win down until we know the next key.
bool g_winArmed = false;       // any binding uses MOD_WIN
bool g_winDeferred = false;    // saw Win down, not yet forwarded to the shell
bool g_winForwarded = false;   // deferred Win down was flushed via SendInput
bool g_ateChordKey = false;    // a MOD_WIN binding swallowed its trigger key
UINT g_pendingWinVk = VK_LWIN;
int g_winDownCount = 0;        // physical LWIN/RWIN downs seen by the hook

// Tracked modifiers (updated even for swallowed keys). OR'd with async state.
bool g_tCtrl = false;
bool g_tAlt = false;
bool g_tShift = false;

void emitAction(int action)
{
    if (g_manager)
        QMetaObject::invokeMethod(g_manager, [action]() {
            emit g_manager->actionTriggered(action);
        }, Qt::QueuedConnection);
}

bool isModVk(UINT vk)
{
    switch (vk) {
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
    case VK_MENU: case VK_LMENU: case VK_RMENU:
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
    case VK_LWIN: case VK_RWIN:
        return true;
    default:
        return false;
    }
}

void trackModifier(UINT vk, bool down)
{
    switch (vk) {
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: g_tCtrl = down; break;
    case VK_MENU: case VK_LMENU: case VK_RMENU: g_tAlt = down; break;
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: g_tShift = down; break;
    default: break;
    }
}

void injectVk(UINT vk, bool down)
{
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = WORD(vk);
    in.ki.dwFlags = down ? 0u : KEYEVENTF_KEYUP;
    ::SendInput(1, &in, sizeof(INPUT));
}

void flushDeferredWinDown()
{
    if (!g_winDeferred || g_winForwarded)
        return;
    injectVk(g_pendingWinVk, true);
    g_winDeferred = false;
    g_winForwarded = true;
    g_ateChordKey = false;
}

void resetWinChordState()
{
    g_winDeferred = false;
    g_winForwarded = false;
    g_ateChordKey = false;
}

void readMods(bool *ctrl, bool *alt, bool *shift, bool *win)
{
    *ctrl = g_tCtrl || ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0);
    *alt = g_tAlt || ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0);
    *shift = g_tShift || ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);
    // Deferred Win was never forwarded — rely on the hook-local count.
    *win = g_winDownCount > 0
        || ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) != 0;
}

LRESULT CALLBACK llKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION
        && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN
            || wParam == WM_KEYUP || wParam == WM_SYSKEYUP)) {
        auto *kb = reinterpret_cast<KBDLLHOOKSTRUCT *>(lParam);
        if (!(kb->flags & LLKHF_INJECTED) && g_manager) {
            const UINT vk = kb->vkCode;
            const bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
            const bool isUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

            if (isModVk(vk)) {
                if (vk == VK_LWIN || vk == VK_RWIN) {
                    if (isDown) {
                        ++g_winDownCount;
                        g_pendingWinVk = vk;
                    } else if (g_winDownCount > 0) {
                        --g_winDownCount;
                    }
                } else if (isDown || isUp) {
                    trackModifier(vk, isDown);
                }
            }

            // Physical Win key: defer / swallow so the shell never sees a
            // bare or Ctrl+Win tap (those open the Windows/Start menu).
            if (g_winArmed && (vk == VK_LWIN || vk == VK_RWIN)) {
                if (isDown
                    && HotkeyManager::winKeyDownDecision(g_winArmed)
                           == HotkeyManager::WinDownDecision::Defer) {
                    g_winDeferred = true;
                    g_winForwarded = false;
                    g_ateChordKey = false;
                    g_pendingWinVk = vk;
                    return 1;
                }
                if (isUp) {
                    if (g_winDeferred && !g_winForwarded) {
                        const bool foreign = g_tCtrl || g_tAlt || g_tShift;
                        const auto upDecision = HotkeyManager::winKeyUpDecision(
                            /*deferredNotForwarded=*/true, g_ateChordKey, foreign);
                        resetWinChordState();
                        if (upDecision == HotkeyManager::WinUpDecision::InjectTap) {
                            injectVk(vk, true);
                            injectVk(vk, false);
                        }
                        // Pass / Swallow / InjectTap all end the deferred cycle.
                        return 1;
                    }
                    if (g_winForwarded) {
                        // Shell already got the deferred down — pass the up.
                        resetWinChordState();
                        return CallNextHookEx(g_llHookHandle, nCode, wParam, lParam);
                    }
                }
            }

            bool c = false, a = false, s = false, w = false;
            readMods(&c, &a, &s, &w);
            // While Win is deferred, GetAsyncKeyState may not report it; force on.
            if (g_winDeferred && !g_winForwarded)
                w = true;

            const auto bindings = g_manager->bindings();
            for (const auto &b : bindings) {
                if (b.vk == 0)
                    continue;
                if (b.vk != vk)
                    continue;
                if (!HotkeyManager::modifiersMatch(b.modifiers, c, a, s, w))
                    continue;
                if (isDown)
                    emitAction(b.action);
                // Swallow down AND up so Windows virtual-desktop / Task View
                // never sees Win+Tab or Ctrl+Win+←/→.
                if ((b.modifiers & MOD_WIN) != 0)
                    g_ateChordKey = true;
                return 1;
            }

            // Not a SpaceWM chord: release a deferred Win so Win+E etc. still work.
            if (g_winDeferred && !g_winForwarded && !isModVk(vk))
                flushDeferredWinDown();

            return CallNextHookEx(g_llHookHandle, nCode, wParam, lParam);
        }
    }
    return CallNextHookEx(g_llHookHandle, nCode, wParam, lParam);
}
} // namespace

HotkeyManager::HotkeyManager(QObject *parent)
    : QObject(parent)
{
    if (qApp)
        qApp->installNativeEventFilter(this);
    g_manager = this;
    g_llHookHandle = SetWindowsHookExW(WH_KEYBOARD_LL, llKeyboardProc,
                                       GetModuleHandleW(nullptr), 0);
}

HotkeyManager::~HotkeyManager()
{
    unregisterAll();
    if (g_llHookHandle) {
        UnhookWindowsHookEx(g_llHookHandle);
        g_llHookHandle = nullptr;
    }
    if (g_manager == this) {
        g_manager = nullptr;
        g_winArmed = false;
        resetWinChordState();
        g_winDownCount = 0;
        g_tCtrl = g_tAlt = g_tShift = false;
    }
    if (qApp)
        qApp->removeNativeEventFilter(this);
}

QString HotkeyManager::defaultSequence(int action)
{
    switch (action) {
    case SwitchPrevSpace: return QStringLiteral("Ctrl+Alt+Left");
    case SwitchNextSpace: return QStringLiteral("Ctrl+Alt+Right");
    case ToggleOverview: return QStringLiteral("Ctrl+Alt+Space");
    case JumpSpace1: return QStringLiteral("Ctrl+Alt+1");
    case JumpSpace2: return QStringLiteral("Ctrl+Alt+2");
    case JumpSpace3: return QStringLiteral("Ctrl+Alt+3");
    case JumpSpace4: return QStringLiteral("Ctrl+Alt+4");
    default: return {};
    }
}

QString HotkeyManager::systemSequence(int action)
{
    // Occupy well-known Windows shortcuts (swallowed by the LL hook).
    switch (action) {
    case SwitchPrevSpace: return QStringLiteral("Ctrl+Win+Left");
    case SwitchNextSpace: return QStringLiteral("Ctrl+Win+Right");
    case ToggleOverview: return QStringLiteral("Win+Tab");
    case JumpSpace1:
    case JumpSpace2:
    case JumpSpace3:
    case JumpSpace4:
        return defaultSequence(action); // jumps stay on Ctrl+Alt+N
    default: return {};
    }
}

UINT HotkeyManager::qtModsToWinMods(Qt::KeyboardModifiers mods)
{
    UINT m = 0;
    if (mods & Qt::CTRL) m |= MOD_CONTROL;
    if (mods & Qt::ALT) m |= MOD_ALT;
    if (mods & Qt::SHIFT) m |= MOD_SHIFT;
    if (mods & Qt::META) m |= MOD_WIN;
    return m;
}

int HotkeyManager::winModsToQtMods(UINT mods)
{
    int m = 0;
    if (mods & MOD_CONTROL) m |= int(Qt::CTRL);
    if (mods & MOD_ALT) m |= int(Qt::ALT);
    if (mods & MOD_SHIFT) m |= int(Qt::SHIFT);
    if (mods & MOD_WIN) m |= int(Qt::META);
    return m;
}

UINT HotkeyManager::qtKeyToVk(int qtKey)
{
    switch (qtKey) {
    case Qt::Key_Left: return VK_LEFT;
    case Qt::Key_Right: return VK_RIGHT;
    case Qt::Key_Up: return VK_UP;
    case Qt::Key_Down: return VK_DOWN;
    case Qt::Key_Space: return VK_SPACE;
    case Qt::Key_Tab: return VK_TAB;
    case Qt::Key_Escape: return VK_ESCAPE;
    case Qt::Key_Enter:
    case Qt::Key_Return: return VK_RETURN;
    case Qt::Key_PageUp: return VK_PRIOR;
    case Qt::Key_PageDown: return VK_NEXT;
    case Qt::Key_Home: return VK_HOME;
    case Qt::Key_End: return VK_END;
    default: break;
    }
    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z)
        return UINT('A' + (qtKey - Qt::Key_A));
    if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9)
        return UINT('0' + (qtKey - Qt::Key_0));
    if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F24)
        return UINT(VK_F1 + (qtKey - Qt::Key_F1));
    return 0;
}

int HotkeyManager::vkToQtKey(UINT vk)
{
    switch (vk) {
    case VK_LEFT: return Qt::Key_Left;
    case VK_RIGHT: return Qt::Key_Right;
    case VK_UP: return Qt::Key_Up;
    case VK_DOWN: return Qt::Key_Down;
    case VK_SPACE: return Qt::Key_Space;
    case VK_TAB: return Qt::Key_Tab;
    case VK_ESCAPE: return Qt::Key_Escape;
    case VK_RETURN: return Qt::Key_Return;
    case VK_PRIOR: return Qt::Key_PageUp;
    case VK_NEXT: return Qt::Key_PageDown;
    case VK_HOME: return Qt::Key_Home;
    case VK_END: return Qt::Key_End;
    default: break;
    }
    if (vk >= 'A' && vk <= 'Z')
        return int(Qt::Key_A) + (int(vk) - 'A');
    if (vk >= '0' && vk <= '9')
        return int(Qt::Key_0) + (int(vk) - '0');
    if (vk >= VK_F1 && vk <= VK_F24)
        return int(Qt::Key_F1) + (int(vk) - VK_F1);
    return 0;
}

bool HotkeyManager::sequenceToBinding(int action, const QString &sequence, Binding *out)
{
    if (sequence.isEmpty())
        return false;
    // Qt portable text uses Meta for the Windows key; we display/store Win+.
    QString portable = sequence;
    portable.replace(QStringLiteral("Win+"), QStringLiteral("Meta+"), Qt::CaseInsensitive);
    const QKeySequence seq(portable, QKeySequence::PortableText);
    if (seq.isEmpty())
        return false;
    const QKeyCombination comb = seq[0];
    const int key = int(comb.key());
    const UINT vk = qtKeyToVk(key);
    if (vk == 0)
        return false;
    Binding b;
    b.action = action;
    b.modifiers = qtModsToWinMods(comb.keyboardModifiers());
    b.vk = vk;
    b.sequence = sequence; // keep the original text (Win+Tab etc.)
    if (out)
        *out = b;
    return true;
}

QString HotkeyManager::bindingToSequence(const Binding &b)
{
    if (!b.sequence.isEmpty())
        return b.sequence;
    if (!b.vk)
        return {};
    const int combined = winModsToQtMods(b.modifiers) | vkToQtKey(b.vk);
    QString s = QKeySequence(combined).toString(QKeySequence::PortableText);
    s.replace(QStringLiteral("Meta+"), QStringLiteral("Win+"), Qt::CaseInsensitive);
    return s;
}

bool HotkeyManager::modifiersMatch(UINT required, bool ctrl, bool alt, bool shift, bool win)
{
    const bool rc = (required & MOD_CONTROL) != 0;
    const bool ra = (required & MOD_ALT) != 0;
    const bool rs = (required & MOD_SHIFT) != 0;
    const bool rw = (required & MOD_WIN) != 0;
    return rc == ctrl && ra == alt && rs == shift && rw == win;
}

bool HotkeyManager::bindingsUseWin(const QVector<Binding> &bindings)
{
    for (const auto &b : bindings) {
        if ((b.modifiers & MOD_WIN) != 0)
            return true;
    }
    return false;
}

HotkeyManager::WinDownDecision HotkeyManager::winKeyDownDecision(bool winBindingsArmed)
{
    return winBindingsArmed ? WinDownDecision::Defer : WinDownDecision::Pass;
}

HotkeyManager::WinUpDecision HotkeyManager::winKeyUpDecision(bool deferredNotForwarded,
                                                             bool chordKeyAte,
                                                             bool foreignModifierHeld)
{
    if (!deferredNotForwarded)
        return WinUpDecision::Pass;
    // Chord already consumed by us, or Ctrl/Win without a trigger key —
    // never let the shell complete a Win tap (Windows/Start menu).
    if (chordKeyAte || foreignModifierHeld)
        return WinUpDecision::Swallow;
    // Plain Win tap: replay down+up so Start still opens.
    return WinUpDecision::InjectTap;
}

bool HotkeyManager::usesWinBindings() const
{
    return bindingsUseWin(m_bindings);
}

void HotkeyManager::loadFromSettings()
{
    AppSettings settings;
    m_bindings.clear();
    const int actions[] = {
        SwitchPrevSpace, SwitchNextSpace, ToggleOverview,
        JumpSpace1, JumpSpace2, JumpSpace3, JumpSpace4,
    };
    for (int a : actions) {
        Binding b;
        const QString seq = settings.hotkeyOrDefault(a);
        if (sequenceToBinding(a, seq, &b))
            m_bindings.push_back(b);
    }
}

bool HotkeyManager::armFromBindings()
{
    unregisterAll();
    g_winArmed = bindingsUseWin(m_bindings);
    resetWinChordState();
    // Prefer LL hook (can swallow Win+Tab etc.). RegisterHotKey only if hook failed.
    const bool llOk = g_llHookHandle != nullptr;
    if (!llOk) {
        for (const auto &b : m_bindings) {
            const int id = actionToId(b.action);
            if (!id)
                continue;
            if (::RegisterHotKey(nullptr, id, b.modifiers | MOD_NOREPEAT, b.vk)) {
                m_registeredIds.push_back(id);
                m_registered.push_back({id, b.modifiers, b.vk});
            }
        }
    }
    return llOk || !m_registeredIds.isEmpty();
}

bool HotkeyManager::registerDefaults()
{
    loadFromSettings();
    if (m_bindings.isEmpty()) {
        // Hard fallback if settings are empty/corrupt.
        const int actions[] = {
            SwitchPrevSpace, SwitchNextSpace, ToggleOverview,
            JumpSpace1, JumpSpace2, JumpSpace3, JumpSpace4,
        };
        for (int a : actions) {
            Binding b;
            if (sequenceToBinding(a, defaultSequence(a), &b))
                m_bindings.push_back(b);
        }
    }
    return armFromBindings();
}

bool HotkeyManager::applySequences(const QVector<QString> &actionSequencePairs)
{
    QVector<Binding> next;
    for (int i = 0; i + 1 < actionSequencePairs.size(); i += 2) {
        const int action = actionSequencePairs[i].toInt();
        const QString seq = actionSequencePairs[i + 1];
        Binding b;
        if (sequenceToBinding(action, seq, &b))
            next.push_back(b);
    }
    if (next.isEmpty())
        return false;
    m_bindings = next;
    return armFromBindings();
}

HotkeyManager::Binding HotkeyManager::bindingFor(int action) const
{
    for (const auto &b : m_bindings)
        if (b.action == action)
            return b;
    return {};
}

void HotkeyManager::unregisterAll()
{
    for (int id : m_registeredIds)
        ::UnregisterHotKey(nullptr, id);
    m_registeredIds.clear();
    m_registered.clear();
}

bool HotkeyManager::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(eventType)
    Q_UNUSED(result)
    auto *msg = static_cast<MSG *>(message);
    if (!msg || msg->message != WM_HOTKEY || m_registeredIds.isEmpty())
        return false;

    const int action = idToAction(int(msg->wParam));
    if (action) {
        emit actionTriggered(action);
        return true;
    }
    return false;
}
