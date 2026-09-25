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

    int spaceOfWindow(HWND hwnd) const;
    HMONITOR ownerMonitorOf(HWND hwnd) const;

    // Exclusive spaces (issue #1) — opt-in, per-space, explicit gesture.
    void setExclusiveSpacesEnabled(bool on);
    bool exclusiveSpacesEnabled() const { return m_exclusiveSpacesEnabled; }
    bool setSpaceExclusive(HMONITOR hmon, int index, bool on);
    bool isSpaceExclusive(HMONITOR hmon, int index) const;
    bool spaceAcceptsWindow(HMONITOR hmon, int index, HWND hwnd) const;

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

    // Composite wallpaper (full-bleed) + cached window shots in Z-order.
    // Call only when that space's membership changes (or once on overview open).
    void rebuildSpaceScreenshot(HMONITOR hmon, int index);

    // Overview entry: rebuild EVERY space on EVERY monitor once, then cache.
    void buildAllSpacePreviews();

    // Fill null screenshots only (cheap). Prefer buildAllSpacePreviews on open.
    void seedScreenshots();

    // Overview entry: clear windowShot cache, then recapture every managed window.
    void warmWindowShots();

    // Drop cached shot for hwnd and re-render its owner space (SHOW/resize).
    void refreshWindowAfterUpdate(HWND hwnd);

    // Append a new empty space on this monitor (Mac-style "+").
    bool addSpace(HMONITOR hmon);

    // Delete space[index]; windows move to the previous space (index 0 → next).
    // Fails if only one space remains. Adjusts currentIndex and owners.
    bool removeSpace(HMONITOR hmon, int index);

    // True if space has at least one live managed window (for UI "can close").
    bool spaceHasWindows(HMONITOR hmon, int spaceIndex) const;

    // Reorder: move space[from] to index[to] (drag in the strip).
    bool moveSpace(HMONITOR hmon, int from, int to);

signals:
    void spaceChanged(quint64 hmon, int index);
    void monitorLayoutChanged();
    void windowTracked(quint64 hwnd);
    void windowUntracked(quint64 hwnd);
    void requestSwitchAnimation(quint64 hmon, int fromIndex, int toIndex);
    // A space's screenshot was re-rendered — overview cards should reload it.
    void spacePreviewInvalidated(quint64 hmon, int spaceIndex);
    // A space's exclusive flag toggled (or was auto-cleared).
    void spaceExclusiveChanged(quint64 hmon, int spaceIndex, bool exclusive);
    // assignWindow refused a window because the target space is exclusive.
    void assignRejected(quint64 hmon, int spaceIndex, quint64 hwnd);

private:
    void ensureMonitor(HMONITOR hmon);
    void cloakWindow(HWND hwnd, bool hide);
    void placeNewWindow(HWND hwnd);

    QHash<quintptr, MonitorSpaces> m_monitors;
    struct Owner { quintptr hmon = 0; int space = -1; };
    QHash<HWND, Owner> m_owner;

    bool m_overviewOpen = false;
    bool m_animationEnabled = true;
    bool m_exclusiveSpacesEnabled = false;
    // Cold start: every monitor begins with exactly one space (issue #8).
    // Space lists are never persisted — added spaces are session-only.
    int m_defaultSpaceCount = 1;
};
