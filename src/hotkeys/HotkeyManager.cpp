#include "HotkeyManager.h"

#include <QCoreApplication>
#include <QAbstractEventDispatcher>

#include <Windows.h>

namespace {
constexpr int kIdPrev = 1001;
constexpr int kIdNext = 1002;
constexpr int kIdOverview = 1003;
constexpr int kIdJump1 = 1011;
constexpr int kIdJump2 = 1012;
constexpr int kIdJump3 = 1013;
constexpr int kIdJump4 = 1014;

// WH_KEYBOARD_LL path — RegisterHotKey can stop delivering after focus storms.
HHOOK g_llHook = nullptr;
HotkeyManager *g_manager = nullptr;

bool ctrlAltDown()
{
    const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    return ctrl && alt;
}

void emitAction(int action)
{
    if (g_manager)
        QMetaObject::invokeMethod(g_manager, [action]() {
            emit g_manager->actionTriggered(action);
        }, Qt::QueuedConnection);
}

LRESULT CALLBACK llKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        auto *kb = reinterpret_cast<KBDLLHOOKSTRUCT *>(lParam);
        // Ignore injected / legacy VK
        if (!(kb->flags & LLKHF_INJECTED) && ctrlAltDown()) {
            switch (kb->vkCode) {
            case VK_LEFT:  emitAction(HotkeyManager::SwitchPrevSpace); break;
            case VK_RIGHT: emitAction(HotkeyManager::SwitchNextSpace); break;
            case VK_SPACE: emitAction(HotkeyManager::ToggleOverview); break;
            case '1': emitAction(HotkeyManager::JumpSpace1); break;
            case '2': emitAction(HotkeyManager::JumpSpace2); break;
            case '3': emitAction(HotkeyManager::JumpSpace3); break;
            case '4': emitAction(HotkeyManager::JumpSpace4); break;
            default: break;
            }
        }
    }
    return CallNextHookEx(g_llHook, nCode, wParam, lParam);
}
} // namespace

HotkeyManager::HotkeyManager(QObject *parent)
    : QObject(parent)
{
    if (qApp)
        qApp->installNativeEventFilter(this);
    g_manager = this;
    g_llHook = SetWindowsHookExW(WH_KEYBOARD_LL, llKeyboardProc,
                                 GetModuleHandleW(nullptr), 0);
}

HotkeyManager::~HotkeyManager()
{
    unregisterAll();
    if (g_llHook) {
        UnhookWindowsHookEx(g_llHook);
        g_llHook = nullptr;
    }
    if (g_manager == this)
        g_manager = nullptr;
    if (qApp)
        qApp->removeNativeEventFilter(this);
}

bool HotkeyManager::registerDefaults()
{
    unregisterAll();

    // RegisterHotKey as secondary path (WM_HOTKEY still handled).
    m_bindings = {
        {kIdPrev, MOD_CONTROL | MOD_ALT, VK_LEFT},
        {kIdNext, MOD_CONTROL | MOD_ALT, VK_RIGHT},
        {kIdOverview, MOD_CONTROL | MOD_ALT, VK_SPACE},
        {kIdJump1, MOD_CONTROL | MOD_ALT, '1'},
        {kIdJump2, MOD_CONTROL | MOD_ALT, '2'},
        {kIdJump3, MOD_CONTROL | MOD_ALT, '3'},
        {kIdJump4, MOD_CONTROL | MOD_ALT, '4'},
    };

    // Dedup: LL hook already covers these combos — do NOT also RegisterHotKey
    // (double-fire). Return true if LL hook is alive.
    const bool llOk = g_llHook != nullptr;
    if (!llOk) {
        for (const auto &b : m_bindings) {
            if (::RegisterHotKey(nullptr, b.id, b.modifiers, b.vk))
                m_registeredIds.push_back(b.id);
        }
    }
    return llOk || !m_registeredIds.isEmpty();
}

void HotkeyManager::unregisterAll()
{
    for (int id : m_registeredIds)
        ::UnregisterHotKey(nullptr, id);
    m_registeredIds.clear();
}

bool HotkeyManager::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(eventType)
    Q_UNUSED(result)
    // Only used when LL hook failed and RegisterHotKey is active.
    auto *msg = static_cast<MSG *>(message);
    if (!msg || msg->message != WM_HOTKEY || m_registeredIds.isEmpty())
        return false;

    const int id = int(msg->wParam);
    switch (id) {
    case kIdPrev: emit actionTriggered(SwitchPrevSpace); return true;
    case kIdNext: emit actionTriggered(SwitchNextSpace); return true;
    case kIdOverview: emit actionTriggered(ToggleOverview); return true;
    case kIdJump1: emit actionTriggered(JumpSpace1); return true;
    case kIdJump2: emit actionTriggered(JumpSpace2); return true;
    case kIdJump3: emit actionTriggered(JumpSpace3); return true;
    case kIdJump4: emit actionTriggered(JumpSpace4); return true;
    default: break;
    }
    return false;
}
