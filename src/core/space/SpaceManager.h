#pragma once

#include "Space.h"
#include "core/monitor/MonitorInfo.h"

#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QVector>
#include <Windows.h>

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

    // Exclusive (maximized-window) space helpers.
    static bool isMaximizedWindow(HWND hwnd);
    bool isExclusiveSpace(HMONITOR hmon, int spaceIndex) const;
    HWND exclusiveWindowOn(HMONITOR hmon, int spaceIndex) const;
    // True if space rejects additional windows (bound to a maximized window).
    bool canAssignToSpace(HMONITOR hmon, int spaceIndex, HWND hwnd) const;

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

    // Snapshot the monitor into space[index].screenshot.
    // Delegates to rebuildSpaceScreenshot — space previews are render-only.
    void captureSpaceScreenshot(HMONITOR hmon, int index);

    // Composite wallpaper (full-bleed) + window PrintWindows in Z-order.
    // Always safe while the overview overlay is up (does not BitBlt desktop).
    void rebuildSpaceScreenshot(HMONITOR hmon, int index);

    // Ensure every space has a rendered preview (current re-rendered; empties filled).
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
