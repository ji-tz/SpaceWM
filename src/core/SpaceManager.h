#pragma once

#include "MonitorInfo.h"

#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QVector>
#include <Windows.h>

// Per-monitor independent spaces.
//
// State model (kept entirely in this process — Windows has no native API for it):
//   monitor -> currentSpaceIndex
//   monitor -> spaces[i] -> set of HWND + last screenshot
//
// Display model: windows not in the current space of their monitor are cloaked.
// Switching a space on monitor M only cloaks/uncloaks windows on M.
struct Space {
    QString name;
    QSet<HWND> windows;
    // Top → bottom HWND order captured when this space was last visible.
    QVector<HWND> zOrder;
    // Last full-monitor screenshot while this space was visible.
    QImage screenshot;
};

struct MonitorSpaces {
    HMONITOR hmon = nullptr;
    RECT physRect{};
    QRect geometry;       // Qt logical
    QString deviceName;
    int currentIndex = 0;
    QVector<Space> spaces;
};

class SpaceManager : public QObject {
    Q_OBJECT
public:
    explicit SpaceManager(QObject *parent = nullptr);

    void refreshMonitors();

    QVector<MonitorSpaces *> monitors();
    MonitorSpaces *monitorOf(HMONITOR hmon);
    MonitorSpaces *monitorAt(const QPoint &globalPos);
    MonitorSpaces *primaryMonitor();
    MonitorSpaces *monitorFromCursor();

    int spaceCount(HMONITOR hmon) const;
    int currentSpaceIndex(HMONITOR hmon) const;
    QString spaceName(HMONITOR hmon, int index) const;

    bool switchSpace(HMONITOR hmon, int index, bool animateHint = true);

    // Live-preview a space while the overview overlay is open:
    // updates currentIndex + cloak immediately (no flash). Same index still
    // re-applies visibility so callers can refresh UI after drops.
    bool previewSpace(HMONITOR hmon, int index);

    bool assignWindow(HWND hwnd, HMONITOR hmon, int spaceIndex);

    int spaceOfWindow(HWND hwnd) const;
    HMONITOR ownerMonitorOf(HWND hwnd) const;

    void adoptExistingWindows();
    void applyVisibility(HMONITOR hmon);

    QVector<HWND> windowsOn(HMONITOR hmon, int spaceIndex) const;

    bool trackWindow(HWND hwnd);
    void untrackWindow(HWND hwnd);

    void setOverviewOpen(bool open);
    bool overviewOpen() const { return m_overviewOpen; }

    void setAnimationEnabled(bool on) { m_animationEnabled = on; }

    // Snapshot the monitor into space[index].screenshot (call while space is visible).
    void captureSpaceScreenshot(HMONITOR hmon, int index);

    // Composite wallpaper + window PrintWindows into space[index].screenshot.
    // Safe while the overview overlay covers the screen (does not BitBlt desktop).
    void rebuildSpaceScreenshot(HMONITOR hmon, int index);

    // Fill empty space previews with desktop wallpaper / live shot (cold start).
    void seedScreenshots();

signals:
    void spaceChanged(quint64 hmon, int index);
    void monitorLayoutChanged();
    void windowTracked(quint64 hwnd);
    void windowUntracked(quint64 hwnd);
    void requestSwitchAnimation(quint64 hmon, int fromIndex, int toIndex);

private:
    void ensureMonitor(HMONITOR hmon);
    void cloakWindow(HWND hwnd, bool hide);
    void placeNewWindow(HWND hwnd);

    QHash<quintptr, MonitorSpaces> m_monitors;
    struct Owner { quintptr hmon = 0; int space = -1; };
    QHash<HWND, Owner> m_owner;

    bool m_overviewOpen = false;
    bool m_animationEnabled = true;
    int m_defaultSpaceCount = 4;
};
