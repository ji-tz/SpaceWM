#include "HotkeyManager.h"

#include <QCoreApplication>

namespace {
constexpr int kIdPrev = 1001;
constexpr int kIdNext = 1002;
constexpr int kIdOverview = 1003;
constexpr int kIdJump1 = 1011;
constexpr int kIdJump2 = 1012;
constexpr int kIdJump3 = 1013;
constexpr int kIdJump4 = 1014;
} // namespace

HotkeyManager::HotkeyManager(QObject *parent)
    : QObject(parent)
{
    qApp->installNativeEventFilter(this);
}

HotkeyManager::~HotkeyManager()
{
    unregisterAll();
    qApp->removeNativeEventFilter(this);
}

bool HotkeyManager::registerDefaults()
{
    unregisterAll();

    // Ctrl+Alt+Left/Right : prev/next space on focused monitor
    // Ctrl+Alt+Space      : toggle overview
    // Ctrl+Alt+1..4       : jump to space N
    m_bindings = {
        {kIdPrev, MOD_CONTROL | MOD_ALT, VK_LEFT},
        {kIdNext, MOD_CONTROL | MOD_ALT, VK_RIGHT},
        {kIdOverview, MOD_CONTROL | MOD_ALT, VK_SPACE},
        {kIdJump1, MOD_CONTROL | MOD_ALT, '1'},
        {kIdJump2, MOD_CONTROL | MOD_ALT, '2'},
        {kIdJump3, MOD_CONTROL | MOD_ALT, '3'},
        {kIdJump4, MOD_CONTROL | MOD_ALT, '4'},
    };

    bool any = false;
    for (const auto &b : m_bindings) {
        if (::RegisterHotKey(nullptr, b.id, b.modifiers, b.vk)) {
            m_registeredIds.push_back(b.id);
            any = true;
        }
    }
    return any;
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
    auto *msg = static_cast<MSG *>(message);
    if (!msg || msg->message != WM_HOTKEY)
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
