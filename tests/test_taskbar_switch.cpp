#include <QtTest>
#include "core/capture/ThumbnailCapture.h"
#include "core/space/SpaceManager.h"
#include "core/window/CloakController.h"
#include "core/window/WindowTracker.h"
#include "hotkeys/HotkeyManager.h"
#include "ui/effects/SwitchFlashOverlay.h"
#include <future>
#include <QThread>
#include <QProcess>
#include <QScopeGuard>
#include <QUuid>
#include <objbase.h>
#include <oleauto.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <shobjidl.h>

namespace {
QColor coverColor()
{
    for (auto *widget : QApplication::topLevelWidgets()) {
        auto *overlay = qobject_cast<SwitchFlashOverlay *>(widget);
        if (overlay && overlay->isVisible()) {
            const QImage frame = overlay->grab().toImage();
            return frame.pixelColor(frame.width() / 2, frame.height() / 2);
        }
    }
    return {};
}
QImage solid(const RECT &rect, QColor color)
{
    QImage image(rect.right - rect.left, rect.bottom - rect.top, QImage::Format_RGB32);
    image.fill(color);
    return image;
}
bool inputDesktopIsUnlocked()
{
    HDESK desktop = ::OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (!desktop)
        return false;
    wchar_t name[128]{};
    DWORD needed = 0;
    const bool unlocked =
        ::GetUserObjectInformationW(desktop, UOI_NAME, name, sizeof(name), &needed) &&
        _wcsicmp(name, L"Default") == 0;
    ::CloseDesktop(desktop);
    return unlocked;
}

QRect taskbarButton(const QString &title, const QString &appId)
{
    // Taskbar providers may query our own top-level window. Keep every UIA
    // object on an MTA worker while the test's GUI thread pumps messages.
    const HRESULT apartment = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(apartment))
        return {};
    auto uninitialize = qScopeGuard([]() { ::CoUninitialize(); });
    using Microsoft::WRL::ComPtr;
    ComPtr<IUIAutomation2> automation;
    if (FAILED(::CoCreateInstance(__uuidof(CUIAutomation8), nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(automation.GetAddressOf()))))
        return {};
    automation->put_ConnectionTimeout(1500);
    automation->put_TransactionTimeout(1500);
    ComPtr<IUIAutomationElement> taskbar;
    if (FAILED(automation->ElementFromHandle(::FindWindowW(L"Shell_TrayWnd", nullptr),
                                             taskbar.GetAddressOf())))
        return {};
    ComPtr<IUIAutomationCondition> condition;
    // The Windows taskbar's app entries may be ListItems rather than Buttons.
    // Match the unique helper identity, independent of the provider's type.
    automation->CreateTrueCondition(condition.GetAddressOf());
    ComPtr<IUIAutomationElementArray> elements;
    if (!condition ||
        FAILED(taskbar->FindAll(TreeScope_Descendants, condition.Get(), elements.GetAddressOf())))
        return {};
    int count = 0;
    elements->get_Length(&count);
    for (int i = 0; i < count; ++i) {
        ComPtr<IUIAutomationElement> element;
        elements->GetElement(i, element.GetAddressOf());
        if (!element)
            continue;
        BSTR name = nullptr;
        element->get_CurrentName(&name);
        const QString label = name ? QString::fromWCharArray(name) : QString();
        ::SysFreeString(name);
        BSTR identifier = nullptr;
        element->get_CurrentAutomationId(&identifier);
        const QString id = identifier ? QString::fromWCharArray(identifier) : QString();
        ::SysFreeString(identifier);
        const QString taskbarId = id.startsWith(QStringLiteral("Appid:"))
                                      ? id.mid(6).trimmed()
                                      : id;
        // Windows 11 can name a grouped taskbar button after the application
        // instead of its window title. Only our helper's unique ID is accepted.
        const bool matches = label.contains(title) || taskbarId == appId;
        if (matches) {
            RECT bounds{};
            element->get_CurrentBoundingRectangle(&bounds);
            return QRect(bounds.left, bounds.top, bounds.right - bounds.left,
                         bounds.bottom - bounds.top);
        }
    }
    return {};
}
} // namespace

class TestTaskbarSwitch : public QObject {
    Q_OBJECT
  private slots:
    void actualTaskbarClickIsCoveredBeforeForeground()
    {
        if (!qEnvironmentVariableIsSet("SPACEWM_TASKBAR_IT"))
            QSKIP("native taskbar path is opt-in: SPACEWM_TASKBAR_IT=1");
        if (!inputDesktopIsUnlocked())
            QSKIP("unlock Windows first: taskbar input needs the Default input desktop");
        if (HANDLE event = ::OpenEventW(EVENT_MODIFY_STATE, FALSE, L"SpaceWM-quit")) {
            ::CloseHandle(event);
            QSKIP("quit the live SpaceWM instance before native taskbar testing");
        }
        const QString title =
            QStringLiteral("SpaceWM-taskbar-QA-") + QUuid::createUuid().toString(QUuid::Id128);
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(),
                    {QStringLiteral("--taskbar-target"), title});
        QVERIFY(child.waitForStarted(5000));
        HWND target = nullptr;
        auto findTarget = [&]() {
            target = ::FindWindowW(nullptr, reinterpret_cast<LPCWSTR>(title.utf16()));
            if (!target)
                return false;
            DWORD pid = 0;
            ::GetWindowThreadProcessId(target, &pid);
            return pid == DWORD(child.processId());
        };
        QTRY_VERIFY_WITH_TIMEOUT(findTarget(), 5000);
        SpaceManager sm;
        auto *mon = sm.primaryMonitor();
        QWidget peer;
        peer.setWindowTitle(QStringLiteral("SpaceWM taskbar regression source"));
        peer.setGeometry(mon->geometry.adjusted(60, 60, -60, -60));
        peer.show();
        POINT original{};
        ::GetCursorPos(&original);
        auto cleanup = qScopeGuard([&]() {
            sm.untrackWindow(target);
            if (::IsWindow(target))
                ::PostMessageW(target, WM_CLOSE, 0, 0);
            child.waitForFinished(3000);
            ::SetCursorPos(original.x, original.y);
        });
        QVERIFY(sm.addSpace(mon->hmon));
        ::SetWindowPos(target, nullptr, mon->physRect.left + 60, mon->physRect.top + 60, 480, 320,
                       SWP_NOZORDER | SWP_NOACTIVATE);
        ::SetForegroundWindow(reinterpret_cast<HWND>(peer.winId()));
        SpaceSwitchAnimator animation(&sm);
        HotkeyManager input;
        WindowTracker tracker;
        input.setTaskbarGuardEnabled(true);
        connect(&input, &HotkeyManager::taskbarPressed, &animation,
                [&]() { animation.prepareTaskbarSwitch(); });
        bool coveredBeforeForeground = false;
        connect(&tracker, &WindowTracker::windowForeground, &sm, [&](quint64 hwnd) {
            if (HWND(hwnd) != target)
                return;
            coveredBeforeForeground = animation.hasTaskbarCover(mon->hmon);
            sm.onWindowForeground(target);
        });
        QRect button;
        const QString appId =
            QStringLiteral("SpaceWM.TaskbarRegression.%1").arg(child.processId());
        QElapsedTimer search;
        search.start();
        do {
            auto query = std::async(std::launch::async,
                                    [title, appId]() { return taskbarButton(title, appId); });
            QTRY_VERIFY_WITH_TIMEOUT(query.wait_for(std::chrono::milliseconds(0)) ==
                                        std::future_status::ready,
                                    10000);
            button = query.get();
            if (button.isEmpty())
                QTest::qWait(100);
        } while (button.isEmpty() && search.elapsed() < 10000);
        QVERIFY2(!button.isEmpty(), "test helper's taskbar button was not found");
        // Wait for Shell to register the new app before hiding it. Otherwise
        // GetViewForHwnd can fail during startup and a DWM fallback removes
        // precisely the taskbar button this regression needs to click.
        QVERIFY(sm.assignWindow(target, mon->hmon, 1));
        QCOMPARE(cloak::backendOf(target), cloak::Backend::ImmersiveView);
        ::SetForegroundWindow(reinterpret_cast<HWND>(peer.winId()));
        const QPoint click = button.center();
        QVERIFY(::SetCursorPos(click.x(), click.y()));
        wchar_t rootClass[128]{};
        ::GetClassNameW(::GetAncestor(::WindowFromPoint({click.x(), click.y()}), GA_ROOT),
                        rootClass, 128);
        qInfo() << "native taskbar click" << button << QString::fromWCharArray(rootClass);
        auto sendClick = std::async(std::launch::async, []() {
            INPUT mouse[2]{};
            mouse[0].type = mouse[1].type = INPUT_MOUSE;
            mouse[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
            mouse[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
            return ::SendInput(2, mouse, sizeof(INPUT));
        });
        QTRY_VERIFY(sendClick.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready);
        QCOMPARE(sendClick.get(), UINT(2));
        QTRY_COMPARE_WITH_TIMEOUT(sm.currentSpaceIndex(mon->hmon), 1, 5000);
        QVERIFY(coveredBeforeForeground);
        QCOMPARE(sm.spaceOfWindow(target), 1);
        QTRY_VERIFY_WITH_TIMEOUT(!animation.isPlaying(mon->hmon), 3000);
    }

    void nativeBridgeFinishesOnGuiThreadBeforeInputContinues()
    {
        HotkeyManager hotkeys;
        QVERIFY(hotkeys.taskbarBridgeWindow());
        hotkeys.setTaskbarGuardEnabled(true);
        bool covered = false;
        connect(&hotkeys, &HotkeyManager::taskbarPressed, this, [&]() {
            QVERIFY(QThread::currentThread() == qApp->thread());
            covered = true;
        });
        const HWND bridge = hotkeys.taskbarBridgeWindow();
        auto input = std::async(std::launch::async, [bridge]() {
            DWORD_PTR result = 0;
            return ::SendMessageTimeoutW(bridge, HotkeyManager::TaskbarPressMessage,
                                         WPARAM(::GetTickCount64() + 500), 0,
                                         SMTO_ABORTIFHUNG | SMTO_BLOCK, 500, &result);
        });
        QTRY_VERIFY(input.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready);
        QVERIFY(input.get() != 0);
        QVERIFY(covered);
    }

    void busyGuiDoesNotApplyExpiredCover()
    {
        HotkeyManager hotkeys;
        hotkeys.setTaskbarGuardEnabled(true);
        QSignalSpy covered(&hotkeys, &HotkeyManager::taskbarPressed);
        const HWND bridge = hotkeys.taskbarBridgeWindow();
        std::promise<void> started;
        auto ready = started.get_future();
        auto input = std::async(std::launch::async, [bridge, &started]() {
            const WPARAM deadline = WPARAM(::GetTickCount64() + 40);
            started.set_value();
            DWORD_PTR result = 0;
            return ::SendMessageTimeoutW(bridge, HotkeyManager::TaskbarPressMessage, deadline, 0,
                                         SMTO_ABORTIFHUNG | SMTO_BLOCK, 50, &result);
        });
        ready.wait();
        ::Sleep(100); // deliberately do not pump GUI messages
        QVERIFY(input.get() == 0);
        QCoreApplication::processEvents();
        QCOMPARE(covered.count(), 0);
        hotkeys.setTaskbarGuardEnabled(false);
        hotkeys.notifyTaskbarPress(::GetTickCount64() + 1000);
        QCOMPARE(covered.count(), 0);
    }

    void foregroundReusesPreClickImageWithoutRecapture()
    {
        QWidget target;
        target.resize(300, 180);
        target.show();
        const HWND hwnd = reinterpret_cast<HWND>(target.winId());
        SpaceManager sm;
        auto *mon = sm.primaryMonitor();
        QVERIFY(sm.addSpace(mon->hmon));
        QVERIFY(sm.assignWindow(hwnd, mon->hmon, 1));
        int captures = 0;
        SpaceSwitchAnimator animation(&sm, nullptr, [&](const RECT &rect) {
            ++captures;
            // Any late capture would contain the already exposed target.
            return solid(rect, captures == 1 ? Qt::red : Qt::green);
        });
        QVERIFY(animation.prepareTaskbarSwitch());
        QVERIFY(animation.hasTaskbarCover(mon->hmon));
        QCOMPARE(captures, 1);
        QCOMPARE(coverColor(), QColor(Qt::red));
        QVERIFY(cloak::set(hwnd, false)); // Shell exposes the target before notification
        bool coverVerified = false;
        connect(&sm, &SpaceManager::requestSwitchCover, this, [&](quint64, int) {
            QCOMPARE(coverColor(), QColor(Qt::red));
            QCOMPARE(captures, 1);
            coverVerified = true;
        });
        QVERIFY(sm.onWindowForeground(hwnd));
        QVERIFY(coverVerified);
        QCOMPARE(captures, 1);
        QCOMPARE(sm.currentSpaceIndex(mon->hmon), 1);
        QCOMPARE(sm.spaceOfWindow(hwnd), 1);
        QVERIFY(!animation.hasTaskbarCover(mon->hmon));
        QVERIFY(animation.isPlaying(mon->hmon));
        animation.stopAll();
        sm.untrackWindow(hwnd);
        QVERIFY(thumbs::screenSamplingEnabled());
    }

    void lateForegroundFallbackUsesCleanSourceComposite()
    {
        QWidget target;
        target.show();
        const HWND hwnd = reinterpret_cast<HWND>(target.winId());
        SpaceManager sm;
        auto *mon = sm.primaryMonitor();
        QVERIFY(sm.addSpace(mon->hmon));
        QVERIFY(sm.assignWindow(hwnd, mon->hmon, 1));
        mon->spaces[0].screenshot = solid(mon->physRect, Qt::blue);
        int captures = 0;
        SpaceSwitchAnimator animation(&sm, nullptr, [&](const RECT &rect) {
            ++captures;
            return solid(rect, Qt::green);
        });
        connect(&sm, &SpaceManager::requestSwitchCover, this,
                [&](quint64, int) { QCOMPARE(coverColor(), QColor(Qt::blue)); });
        QVERIFY(sm.onWindowForeground(hwnd));
        QCOMPARE(captures, 0);
        QVERIFY(!sm.foregroundSwitchInProgress());
        animation.stopAll();
        sm.untrackWindow(hwnd);
    }

    void nonSwitchClickReleasesItsTemporaryCover()
    {
        QWidget target;
        target.show();
        SpaceManager sm;
        auto *mon = sm.primaryMonitor();
        QVERIFY(sm.addSpace(mon->hmon));
        const HWND hwnd = reinterpret_cast<HWND>(target.winId());
        QVERIFY(sm.assignWindow(hwnd, mon->hmon, 1));
        SpaceSwitchAnimator animation(&sm, nullptr,
                                      [](const RECT &rect) { return solid(rect, Qt::red); });
        QVERIFY(animation.prepareTaskbarSwitch());
        QTRY_VERIFY_WITH_TIMEOUT(!animation.isPlaying(mon->hmon), 1500);
        QVERIFY(!animation.hasTaskbarCover(mon->hmon));
        QVERIFY(thumbs::screenSamplingEnabled());
        sm.setOverviewOpen(true);
        QVERIFY(!animation.prepareTaskbarSwitch());
        sm.setOverviewOpen(false);
        sm.untrackWindow(hwnd);
    }
};

int main(int argc, char **argv)
{
    const bool helper = argc == 3 && qstrcmp(argv[1], "--taskbar-target") == 0;
    if (helper) {
        const QString id =
            QStringLiteral("SpaceWM.TaskbarRegression.") + QString::number(::GetCurrentProcessId());
        ::SetCurrentProcessExplicitAppUserModelID(reinterpret_cast<LPCWSTR>(id.utf16()));
    }
    QApplication app(argc, argv);
    if (helper) {
        QWidget target;
        target.setWindowTitle(app.arguments()[2]);
        target.resize(480, 320);
        target.show();
        return app.exec();
    }
    TestTaskbarSwitch test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_taskbar_switch.moc"
