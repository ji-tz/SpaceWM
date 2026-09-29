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

    // Applies an explicit monitor list (refreshMonitors() = enumerate + this).
    // Handles whose monitor vanished lose their space lists with it — survivors
    // are re-homed onto a live monitor's current space instead of being purged.
    // Public as the test seam for that recovery (tests inject fake handles).
    void applyMonitorEntries(const QVector<MonitorEntry> &list);

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

    // WindowTracker::windowMoved entry (main + integration flow share it):
    // a cross-monitor move re-homes an OWNED window into the target
    // monitor's current space; unowned ones go through normal discovery.
    void onWindowMoved(HWND hwnd);

    void setOverviewOpen(bool open);
    bool overviewOpen() const { return m_overviewOpen; }

    // Re-apply cloak for every monitor right now (warm uncloaked everything
    // behind the overview mask — callers invoke this BEFORE the mask fades
    // so off-space windows never flash on the desktop).
    void recloakNow();

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

    // Preview open step 1 — desktop intact, BEFORE the overview masks are up:
    // drop + recapture every on-screen managed window. The screen fallback is
    // only honest at this point (after masks it would bake the overlay in).
    void refreshVisibleShots();

    // Preview open step 2 — behind the masks: uncloak every managed window,
    // then capture only windows still missing a shot (keeps switch-time shots
    // for off-space windows — recapturing them has no screen fallback).
    // Close re-cloaks via setOverviewOpen(false).
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

  private:
    void ensureMonitor(HMONITOR hmon);
    void cloakWindow(HWND hwnd, bool hide);
    void placeNewWindow(HWND hwnd);
    // Put an owned-or-orphaned window into a live monitor's current space.
    // Deliberately skips ensureMonitor/isManageable so it can never re-enter
    // refreshMonitors() mid-iteration and never skips a still-cloaked survivor.
    void rehomeWindow(HWND hwnd);

    QHash<quintptr, MonitorSpaces> m_monitors;
    struct Owner {
        quintptr hmon = 0;
        int space = -1;
    };
    QHash<HWND, Owner> m_owner;

    bool m_overviewOpen = false;
    bool m_animationEnabled = true;
    // Cold start: every monitor begins with exactly one space (issue #8).
    // Space lists are never persisted — added spaces are session-only.
    int m_defaultSpaceCount = 1;
};
