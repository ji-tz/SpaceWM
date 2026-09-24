#pragma once

#include "MonitorInfo.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QVector>
#include <Windows.h>

// Per-monitor independent spaces.
//
// State model (kept entirely in this process — Windows has no native API for it):
//   monitor -> currentSpaceIndex
//   monitor -> spaces[i] -> set of HWND
//
// Display model: windows not in the current space of their monitor are cloaked.
// Switching a space on monitor M only cloaks/uncloaks windows on M.
struct Space {
    QString name;
    QSet<HWND> windows;
};

struct MonitorSpaces {
    HMONITOR hmon = nullptr;
    QRect geometry;
    QString deviceName;
    int currentIndex = 0;
    QVector<Space> spaces;
};

class SpaceManager : public QObject {
    Q_OBJECT
public:
    explicit SpaceManager(QObject *parent = nullptr);

    // Rebuild monitor list (display change). Keeps window assignments best-effort.
    void refreshMonitors();

    QVector<MonitorSpaces *> monitors();
    MonitorSpaces *monitorOf(HMONITOR hmon);
    MonitorSpaces *monitorAt(const QPoint &globalPos);
    MonitorSpaces *primaryMonitor();
    MonitorSpaces *monitorFromCursor();

    int spaceCount(HMONITOR hmon) const;
    int currentSpaceIndex(HMONITOR hmon) const;
    QString spaceName(HMONITOR hmon, int index) const;

    // Switch current space on one monitor and apply cloak deltas there only.
    bool switchSpace(HMONITOR hmon, int index, bool animateHint = true);

    // Move a window into a space (and cloak/uncloak as needed).
    bool assignWindow(HWND hwnd, HMONITOR hmon, int spaceIndex);

    // Which space currently owns this window (on its monitor), or -1.
    int spaceOfWindow(HWND hwnd) const;

    // Which monitor currently owns this window, or nullptr.
    HMONITOR ownerMonitorOf(HWND hwnd) const;

    // Initial adoption of existing windows onto space 0 of their monitor.
    void adoptExistingWindows();

    // Direct cloak application for the whole monitor (used after switches).
    void applyVisibility(HMONITOR hmon);

    // All windows the manager believes belong to a space (for overview).
    QVector<HWND> windowsOn(HMONITOR hmon, int spaceIndex) const;

    // Ensure this hwnd is tracked; returns false if not manageable.
    bool trackWindow(HWND hwnd);

    void untrackWindow(HWND hwnd);

    // When overview is open we suppress cloak writes to avoid fighting the UI.
    void setOverviewOpen(bool open);
    bool overviewOpen() const { return m_overviewOpen; }

    // Disable our flash overlay for a moment (used when overview handles UX).
    void setAnimationEnabled(bool on) { m_animationEnabled = on; }

signals:
    // Fired after cloak state has been updated for a monitor.
    void spaceChanged(quint64 hmon, int index);
    void monitorLayoutChanged();
    void windowTracked(quint64 hwnd);
    void windowUntracked(quint64 hwnd);
    // UI should play a short switch flash on this monitor.
    void requestSwitchAnimation(quint64 hmon, int fromIndex, int toIndex);

private:
    void ensureMonitor(HMONITOR hmon);
    void cloakWindow(HWND hwnd, bool hide);
    void placeNewWindow(HWND hwnd);

    QHash<quintptr, MonitorSpaces> m_monitors; // key: HMONITOR as pointer
    // Reverse map: window -> (monitor key, space index) for O(1) lookup.
    struct Owner { quintptr hmon = 0; int space = -1; };
    QHash<HWND, Owner> m_owner;

    bool m_overviewOpen = false;
    bool m_animationEnabled = true;
    int m_defaultSpaceCount = 4;
};
