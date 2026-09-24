#pragma once

#include <QObject>
#include <QAbstractNativeEventFilter>
#include <QKeySequence>
#include <QVector>
#include <Windows.h>

// Global hotkeys via RegisterHotKey + Qt native event filter for WM_HOTKEY.
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

    explicit HotkeyManager(QObject *parent = nullptr);
    ~HotkeyManager() override;

    bool registerDefaults();
    void unregisterAll();

    // native filter
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

signals:
    void actionTriggered(int action);

private:
    struct Binding {
        int id = 0;
        UINT modifiers = 0;
        UINT vk = 0;
    };
    QVector<Binding> m_bindings;
    QVector<int> m_registeredIds;
};
