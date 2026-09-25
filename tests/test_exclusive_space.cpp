#include <QtTest>

#include "core/space/SpaceManager.h"
#include "core/window/CloakController.h"

#include <Windows.h>

// Exclusive spaces (issue #1): opt-in one-window spaces, explicit gesture.
class TestExclusiveSpace : public QObject {
    Q_OBJECT
private slots:
    void featureOffAssignUnchanged()
    {
        SpaceManager sm;
        auto *m = sm.monitors().first();
        ensureSpaces(sm, m, 2);
        QVERIFY(!sm.exclusiveSpacesEnabled());

        HWND a = makeWindow(L"ex-off-a");
        HWND b = makeWindow(L"ex-off-b");
        QVERIFY(a && b);

        // Feature off: multiple windows share a space exactly as before.
        QVERIFY(sm.assignWindow(a, m->hmon, 0));
        QVERIFY(sm.assignWindow(b, m->hmon, 0));
        QCOMPARE(m->spaces[0].windows.size(), 2);
        QVERIFY(!sm.isSpaceExclusive(m->hmon, 0));
        QVERIFY(sm.spaceAcceptsWindow(m->hmon, 0, reinterpret_cast<HWND>(0x1234)));

        // Cannot enable exclusive while the global switch is off.
        QVERIFY(!sm.setSpaceExclusive(m->hmon, 0, true));
        QVERIFY(!sm.isSpaceExclusive(m->hmon, 0));

        sm.untrackWindow(a);
        sm.untrackWindow(b);
        destroy(a);
        destroy(b);
    }

    void exclusiveEmptyBindsFirstAssigned()
    {
        SpaceManager sm;
        auto *m = sm.monitors().first();
        ensureSpaces(sm, m, 2);
        sm.setExclusiveSpacesEnabled(true);

        // Zero members → enabling succeeds and binds the next assigned window.
        QVERIFY(sm.setSpaceExclusive(m->hmon, 0, true));
        QVERIFY(sm.isSpaceExclusive(m->hmon, 0));
        QVERIFY(sm.spaceAcceptsWindow(m->hmon, 0, reinterpret_cast<HWND>(0xABCD)));

        HWND a = makeWindow(L"ex-empty-a");
        QVERIFY(a);
        QVERIFY(sm.assignWindow(a, m->hmon, 0));
        QCOMPARE(m->spaces[0].windows.size(), 1);
        QVERIFY(sm.spaceAcceptsWindow(m->hmon, 0, a));

        // A second window is rejected, ownership unchanged + assignRejected fired.
        HWND b = makeWindow(L"ex-empty-b");
        QVERIFY(b);
        QSignalSpy rej(&sm, &SpaceManager::assignRejected);
        QVERIFY(!sm.assignWindow(b, m->hmon, 0));
        QCOMPARE(rej.count(), 1);
        QCOMPARE(rej.first().at(1).toInt(), 0);
        QCOMPARE(rej.first().at(2).toULongLong(), quint64(reinterpret_cast<quintptr>(b)));
        QVERIFY(!m->spaces[0].windows.contains(b));
        QCOMPARE(sm.spaceOfWindow(b), -1);
        QVERIFY(sm.isSpaceExclusive(m->hmon, 0));

        sm.untrackWindow(a);
        destroy(a);
        destroy(b);
    }

    void exclusiveWithOneWindowBinds()
    {
        SpaceManager sm;
        auto *m = sm.monitors().first();
        ensureSpaces(sm, m, 2);
        sm.setExclusiveSpacesEnabled(true);

        HWND a = makeWindow(L"ex-one-a");
        QVERIFY(a);
        QVERIFY(sm.assignWindow(a, m->hmon, 0));
        QVERIFY(sm.setSpaceExclusive(m->hmon, 0, true));
        QVERIFY(sm.isSpaceExclusive(m->hmon, 0));
        QVERIFY(sm.spaceAcceptsWindow(m->hmon, 0, a));

        HWND b = makeWindow(L"ex-one-b");
        QVERIFY(b);
        QSignalSpy rej(&sm, &SpaceManager::assignRejected);
        QVERIFY(!sm.assignWindow(b, m->hmon, 0));
        QCOMPARE(rej.count(), 1);
        QVERIFY(!m->spaces[0].windows.contains(b));
        QCOMPARE(m->spaces[0].windows.size(), 1);

        sm.untrackWindow(a);
        destroy(a);
        destroy(b);
    }

    void rejectEnableWhenMultipleMembers()
    {
        SpaceManager sm;
        auto *m = sm.monitors().first();
        ensureSpaces(sm, m, 2);
        sm.setExclusiveSpacesEnabled(true);

        HWND a = makeWindow(L"ex-multi-a");
        HWND b = makeWindow(L"ex-multi-b");
        QVERIFY(a && b);
        QVERIFY(sm.assignWindow(a, m->hmon, 0));
        QVERIFY(sm.assignWindow(b, m->hmon, 0));
        QCOMPARE(m->spaces[0].windows.size(), 2);

        // >1 member → refuse enabling, no state change.
        QVERIFY(!sm.setSpaceExclusive(m->hmon, 0, true));
        QVERIFY(!sm.isSpaceExclusive(m->hmon, 0));
        QCOMPARE(m->spaces[0].windows.size(), 2);

        sm.untrackWindow(a);
        sm.untrackWindow(b);
        destroy(a);
        destroy(b);
    }

    void boundWindowMovedAwayUnbindsSource()
    {
        SpaceManager sm;
        auto *m = sm.monitors().first();
        ensureSpaces(sm, m, 3);
        sm.setExclusiveSpacesEnabled(true);

        HWND a = makeWindow(L"ex-move-a");
        QVERIFY(a);
        QVERIFY(sm.assignWindow(a, m->hmon, 0));
        QVERIFY(sm.setSpaceExclusive(m->hmon, 0, true));
        QVERIFY(sm.isSpaceExclusive(m->hmon, 0));

        QSignalSpy changed(&sm, &SpaceManager::spaceExclusiveChanged);
        // Bound window moves elsewhere → source unbinds automatically.
        QVERIFY(sm.assignWindow(a, m->hmon, 2));
        QCOMPARE(sm.spaceOfWindow(a), 2);
        QVERIFY(!sm.isSpaceExclusive(m->hmon, 0));

        bool sawUnbind = false;
        for (const auto &args : changed) {
            if (args.at(1).toInt() == 0 && args.at(2).toBool() == false)
                sawUnbind = true;
        }
        QVERIFY(sawUnbind);

        sm.untrackWindow(a);
        destroy(a);
    }

    void untrackUnbindsExclusive()
    {
        SpaceManager sm;
        auto *m = sm.monitors().first();
        ensureSpaces(sm, m, 2);
        sm.setExclusiveSpacesEnabled(true);

        HWND a = makeWindow(L"ex-untrack-a");
        QVERIFY(a);
        QVERIFY(sm.assignWindow(a, m->hmon, 0));
        QVERIFY(sm.setSpaceExclusive(m->hmon, 0, true));
        QVERIFY(sm.isSpaceExclusive(m->hmon, 0));

        sm.untrackWindow(a);
        QVERIFY(!sm.isSpaceExclusive(m->hmon, 0));
        QCOMPARE(sm.spaceOfWindow(a), -1);

        destroy(a);
    }

    void removeSpaceMergeClearsTargetExclusive()
    {
        SpaceManager sm;
        auto *m = sm.monitors().first();
        ensureSpaces(sm, m, 2);
        sm.setExclusiveSpacesEnabled(true);

        HWND a = makeWindow(L"ex-merge-a");
        HWND b = makeWindow(L"ex-merge-b");
        QVERIFY(a && b);
        QVERIFY(sm.assignWindow(a, m->hmon, 0));
        QVERIFY(sm.setSpaceExclusive(m->hmon, 0, true));
        QVERIFY(sm.assignWindow(b, m->hmon, 1));
        QVERIFY(sm.isSpaceExclusive(m->hmon, 0));

        // Removing space 1 merges b into space 0 → exclusive cleared.
        QVERIFY(sm.removeSpace(m->hmon, 1));
        QVERIFY(m->spaces[0].windows.contains(a));
        QVERIFY(m->spaces[0].windows.contains(b));
        QVERIFY(!sm.isSpaceExclusive(m->hmon, 0));

        sm.untrackWindow(a);
        sm.untrackWindow(b);
        destroy(a);
        destroy(b);
    }

    void disableClearsAllExclusiveFlags()
    {
        SpaceManager sm;
        auto *m = sm.monitors().first();
        ensureSpaces(sm, m, 2);
        sm.setExclusiveSpacesEnabled(true);

        HWND a = makeWindow(L"ex-disable-a");
        QVERIFY(a);
        QVERIFY(sm.assignWindow(a, m->hmon, 0));
        QVERIFY(sm.setSpaceExclusive(m->hmon, 0, true));
        QVERIFY(sm.isSpaceExclusive(m->hmon, 0));

        sm.setExclusiveSpacesEnabled(false);
        QVERIFY(!sm.exclusiveSpacesEnabled());
        QVERIFY(!sm.isSpaceExclusive(m->hmon, 0));

        // Back to ordinary sharing after disable.
        HWND b = makeWindow(L"ex-disable-b");
        QVERIFY(b);
        QVERIFY(sm.assignWindow(b, m->hmon, 0));
        QCOMPARE(m->spaces[0].windows.size(), 2);

        sm.untrackWindow(a);
        sm.untrackWindow(b);
        destroy(a);
        destroy(b);
    }

    void invalidArgumentsAreNoOps()
    {
        SpaceManager sm;
        auto *m = sm.monitors().first();
        ensureSpaces(sm, m, 2);
        sm.setExclusiveSpacesEnabled(true);

        QVERIFY(!sm.setSpaceExclusive(m->hmon, -1, true));
        QVERIFY(!sm.setSpaceExclusive(m->hmon, 99, true));
        QVERIFY(!sm.setSpaceExclusive(nullptr, 0, true));
        QVERIFY(!sm.isSpaceExclusive(m->hmon, -1));
        QVERIFY(!sm.isSpaceExclusive(m->hmon, 99));
        QVERIFY(!sm.spaceAcceptsWindow(m->hmon, 99, reinterpret_cast<HWND>(0x1)));
    }

private:
    static HWND makeWindow(const wchar_t *title)
    {
        return ::CreateWindowExW(
            0, L"STATIC", title,
            WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, 320, 200,
            nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
    }

    static void destroy(HWND hwnd)
    {
        if (hwnd) {
            ::cloak::set(hwnd, false);
            ::DestroyWindow(hwnd);
        }
    }

    static void ensureSpaces(SpaceManager &sm, MonitorSpaces *m, int n)
    {
        while (m->spaces.size() < n)
            sm.addSpace(m->hmon);
    }
};

QTEST_MAIN(TestExclusiveSpace)
#include "test_exclusive_space.moc"
