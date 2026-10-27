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

    // directionHint preserves left/right travel across the circular strip seam.
    bool switchSpace(HMONITOR hmon, int index, bool animateHint = true, int directionHint = 0);

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

    // Foreground activation follows the existing owner; never moves the window.
    // Returns true for a live, non-minimized managed window.
    bool onWindowForeground(HWND hwnd);
    bool foregroundSwitchInProgress() const { return m_foregroundSwitch; }

    // Pure proportional map of a physical window RECT from src monitor rect
    // to dst monitor rect: position and size keep their screen-relative
    // fractions (identity when src == dst, input on degenerate rects).
    static RECT mapRectToMonitor(const RECT &win, const RECT &src, const RECT &dst);

    // Physically move hwnd onto target monitor, size/position scaled by the
    // screen ratio (overview cross-monitor drop — ownership moves first via
    // assignWindow, this makes the window actually LAND on the new display).
    // Re-asserts the rect once if the app resizes on WM_DPICHANGED (mixed
    // DPI). No-op when the mapped rect equals the current one; skips iconic.
    bool moveWindowToMonitor(HWND hwnd, HMONITOR target);

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
    void rebuildSpaceScreenshot(HMONITOR hmon, int index, const QSize &maxSize = QSize(640, 360),
                                bool cachedOnly = false);

    // Overview entry: rebuild EVERY space on EVERY monitor once, then cache.
    void buildAllSpacePreviews();

    // Fill null screenshots only (cheap). Prefer buildAllSpacePreviews on open.
    void seedScreenshots();

    // Synchronous refresh for diagnostics/non-animation callers; honors the
    // screen-sampling switch and preserves previous shots on capture failure.
    void refreshVisibleShots();

    // Behind the masks: uncloak every managed window, then fill missing shots.
    // Close re-cloaks via setOverviewOpen(false).
    void warmWindowShots();
    // Prepare only this space behind its overview cover. Current spaces refresh;
    // off-space work fills missing shots while retaining known-good caches.
    void prepareOverviewSpace(HMONITOR hmon, int index, bool refresh);

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
    // Synchronous: capture the original monitor and cover it BEFORE any
    // per-window capture. Never emitted while overview is open.
    void requestSwitchCover(quint64 hmon, int fromIndex);
    // Synchronous: shots are ready, currentIndex/cloak still describe the source.
    // Listeners must paint an opaque first frame before returning.
    void requestSwitchAnimation(quint64 hmon, int fromIndex, int toIndex, int direction);
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
    bool m_foregroundSwitch = false;
    bool m_animationEnabled = true;
    // Cold start: every monitor begins with exactly one space (issue #8).
    // Space lists are never persisted — added spaces are session-only.
    int m_defaultSpaceCount = 1;
};
