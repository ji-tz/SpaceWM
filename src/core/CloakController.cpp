#include "CloakController.h"

#include <dwmapi.h>
#include <inspectable.h>
#include <objbase.h>
#include <servprov.h>

#include <atomic>
#include <unordered_map>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

namespace {

constexpr CLSID CLSID_ImmersiveShell =
    {0xC2F03A33, 0x21F5, 0x47FA, {0xB4, 0xBB, 0x15, 0x63, 0x62, 0xA2, 0xF2, 0x39}};

// {1841C6D7-4F9D-42C0-AF41-8747538F10E5}
static const GUID IID_IApplicationViewCollection =
    {0x1841C6D7, 0x4F9D, 0x42C0, {0xAF, 0x41, 0x87, 0x47, 0x53, 0x8F, 0x10, 0xE5}};

constexpr int AVCT_NONE = 0;
constexpr int AVCT_DEFAULT = 1;

std::atomic<int> g_lastBackend{0};

// How we hid a window — only reverse with the matching show path.
enum class How : unsigned char {
    NotHidden,
    ShowWindow,
    Dwm,
    Immersive,
};

struct HiddenInfo {
    How how = How::NotHidden;
    bool wasVisible = false;
    WINDOWPLACEMENT placement{};
    bool hasPlacement = false;
};

std::unordered_map<HWND, HiddenInfo> g_hidden;

struct ComMark {
    HRESULT hr;
    ComMark()
    {
        hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    }
    ~ComMark()
    {
        if (hr == S_OK)
            ::CoUninitialize();
    }
};

MIDL_INTERFACE("372E1D3B-38D3-42E4-A15B-8AB2B178F513")
IApplicationViewSlim : public IInspectable
{
public:
    virtual HRESULT STDMETHODCALLTYPE SetFocus() = 0;
    virtual HRESULT STDMETHODCALLTYPE SwitchTo() = 0;
    virtual HRESULT STDMETHODCALLTYPE TryInvokeBack(void *cb) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetThumbnailWindow(HWND *h) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMonitor(void **m) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetVisibility(int *v) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCloak(int cloakType, int unknown) = 0;
};

MIDL_INTERFACE("1841C6D7-4F9D-42C0-AF41-8747538F10E5")
IApplicationViewCollectionSlim : public IInspectable
{
public:
    virtual HRESULT STDMETHODCALLTYPE GetViews(void **v) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetViewsByZOrder(void **v) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetViewsByAppUserModelId(PCWSTR id, void **v) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetViewForHwnd(HWND h, IApplicationViewSlim **view) = 0;
};

IApplicationViewCollectionSlim *viewCollection()
{
    static IApplicationViewCollectionSlim *cached = nullptr;
    static bool tried = false;
    if (tried)
        return cached;
    tried = true;

    ComMark com;
    IUnknown *unk = nullptr;
    HRESULT hr = ::CoCreateInstance(CLSID_ImmersiveShell, nullptr,
                                    CLSCTX_INPROC_SERVER, IID_IUnknown,
                                    reinterpret_cast<void **>(&unk));
    if (FAILED(hr) || !unk)
        return nullptr;

    IServiceProvider *sp = nullptr;
    hr = unk->QueryInterface(IID_IServiceProvider, reinterpret_cast<void **>(&sp));
    unk->Release();
    if (FAILED(hr) || !sp)
        return nullptr;

    void *coll = nullptr;
    hr = sp->QueryService(IID_IApplicationViewCollection,
                          IID_IApplicationViewCollection, &coll);
    sp->Release();
    if (FAILED(hr) || !coll)
        return nullptr;

    cached = static_cast<IApplicationViewCollectionSlim *>(coll);
    return cached;
}

bool hideViaApplicationView(HWND hwnd)
{
    auto *coll = viewCollection();
    if (!coll)
        return false;
    ComMark com;
    IApplicationViewSlim *view = nullptr;
    if (FAILED(coll->GetViewForHwnd(hwnd, &view)) || !view)
        return false;
    const HRESULT hr = view->SetCloak(AVCT_DEFAULT, 1);
    view->Release();
    if (FAILED(hr))
        return false;
    g_lastBackend.store(static_cast<int>(cloak::Backend::ImmersiveView));
    return true;
}

bool showViaApplicationView(HWND hwnd)
{
    auto *coll = viewCollection();
    if (!coll)
        return false;
    ComMark com;
    IApplicationViewSlim *view = nullptr;
    if (FAILED(coll->GetViewForHwnd(hwnd, &view)) || !view)
        return false;
    const HRESULT hr = view->SetCloak(AVCT_NONE, 0);
    view->Release();
    return SUCCEEDED(hr);
}

bool hideViaDwm(HWND hwnd)
{
    BOOL value = TRUE;
    const HRESULT hr = ::DwmSetWindowAttribute(hwnd, 13, &value, sizeof(value));
    if (FAILED(hr))
        return false;
    g_lastBackend.store(static_cast<int>(cloak::Backend::DwmAttribute));
    return true;
}

bool showViaDwm(HWND hwnd)
{
    BOOL value = FALSE;
    const HRESULT hr = ::DwmSetWindowAttribute(hwnd, 13, &value, sizeof(value));
    return SUCCEEDED(hr);
}

bool hideViaShowWindow(HWND hwnd)
{
    HiddenInfo st;
    st.how = How::ShowWindow;
    st.wasVisible = ::IsWindowVisible(hwnd) != FALSE;
    st.hasPlacement = ::GetWindowPlacement(hwnd, &st.placement) != FALSE;
    g_hidden[hwnd] = st;

    ::ShowWindow(hwnd, SW_HIDE);
    g_lastBackend.store(static_cast<int>(cloak::Backend::ShowWindow));
    return true;
}

// ONLY call when g_hidden says we used ShowWindow to hide this HWND.
bool showViaShowWindow(HWND hwnd)
{
    auto it = g_hidden.find(hwnd);
    if (it == g_hidden.end() || it->second.how != How::ShowWindow)
        return false;

    const HiddenInfo st = it->second;
    g_hidden.erase(it);

    if (st.hasPlacement)
        ::SetWindowPlacement(hwnd, &st.placement);
    if (st.wasVisible)
        ::ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    else
        ::ShowWindow(hwnd, SW_SHOWNA);

    g_lastBackend.store(static_cast<int>(cloak::Backend::ShowWindow));
    return true;
}

void remember(HWND hwnd, How how)
{
    HiddenInfo &st = g_hidden[hwnd];
    // Keep original wasVisible/placement if we already recorded a hide.
    if (st.how == How::NotHidden) {
        st.wasVisible = ::IsWindowVisible(hwnd) != FALSE;
        st.hasPlacement = ::GetWindowPlacement(hwnd, &st.placement) != FALSE;
    }
    st.how = how;
}

void forget(HWND hwnd)
{
    g_hidden.erase(hwnd);
}

} // namespace

namespace cloak {

bool set(HWND hwnd, bool enable)
{
    if (!hwnd || !::IsWindow(hwnd))
        return false;

    if (enable) {
        // Already hidden by us — idempotent.
        auto it = g_hidden.find(hwnd);
        if (it != g_hidden.end() && it->second.how != How::NotHidden)
            return true;

        if (hideViaApplicationView(hwnd)) {
            remember(hwnd, How::Immersive);
            return true;
        }
        if (hideViaDwm(hwnd)) {
            remember(hwnd, How::Dwm);
            return true;
        }
        // Cross-process reliable path.
        return hideViaShowWindow(hwnd);
    }

    // ---- show: ONLY reverse what we did ----
    auto it = g_hidden.find(hwnd);
    if (it == g_hidden.end()) {
        // We never hid this window. Do NOT ShowWindow / uncloak —
        // it may be hidden by the shell, tray, system VD, etc.
        return false;
    }

    const How how = it->second.how;
    switch (how) {
    case How::ShowWindow:
        return showViaShowWindow(hwnd);
    case How::Dwm:
        forget(hwnd);
        return showViaDwm(hwnd);
    case How::Immersive:
        forget(hwnd);
        return showViaApplicationView(hwnd);
    case How::NotHidden:
    default:
        forget(hwnd);
        return false;
    }
}

bool isHiddenByUs(HWND hwnd)
{
    auto it = g_hidden.find(hwnd);
    return it != g_hidden.end() && it->second.how == How::ShowWindow;
}

bool isCloaked(HWND hwnd)
{
    if (!hwnd || !::IsWindow(hwnd))
        return false;
    if (isHiddenByUs(hwnd))
        return true;
    DWORD cloaked = 0;
    const HRESULT hr = ::DwmGetWindowAttribute(hwnd, 14, &cloaked, sizeof(cloaked));
    return SUCCEEDED(hr) && cloaked != 0;
}

Backend lastBackend()
{
    return static_cast<Backend>(g_lastBackend.load());
}

int hiddenCount()
{
    return static_cast<int>(g_hidden.size());
}

} // namespace cloak
