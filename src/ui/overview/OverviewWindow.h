#pragma once

#include "core/space/SpaceManager.h"

#include <QSize>
#include <QWidget>
#include <Windows.h>

class QHBoxLayout;
class QVBoxLayout;
class QLabel;
class QPropertyAnimation;
class SpaceCardWidget;
class WindowPreviewWidget;
class AddSpaceButton;
class QScrollArea;
class QEvent;
class QTimer;

// Mission Control style overview for ONE monitor:
//   top    — space strip (click to switch, drop windows to place)
//   bottom — window previews on this monitor (drag onto a space)
class OverviewWindow : public QWidget {
    Q_OBJECT
public:
    explicit OverviewWindow(SpaceManager *manager, QWidget *parent = nullptr);

    void assignMonitor(HMONITOR hmon) { m_hmon = hmon; }
    void openOnMonitor(HMONITOR hmon, bool takeFocus = true);

    void closeOverview(bool commit);
    void prepareClose();
    void startExit();
    void forceHide();
    void closeQuietly();
    void dismiss();
    void setHostManaged(bool on) { m_hostManaged = on; }

    bool isOpen() const { return m_open; }
    bool isDismissing() const { return m_closePending && !m_open; }
    bool isAnimating() const { return m_animating; }
    bool isExitStarted() const { return m_exitStarted; }
    HMONITOR targetMonitor() const { return m_hmon; }
    int selectedIndex() const { return m_selected; }
    int cardCount() const { return m_cards.size(); }
    int windowPreviewCount() const { return m_windowPreviews.size(); }
    // Image box / hwnd of the i-th bottom tile (tests: never exceed real size).
    QSize windowPreviewBoxSize(int i) const;
    HWND windowPreviewHandle(int i) const;

    // Mission Control: drop hwnd onto space index (public for tests).
    // Does NOT switch the live monitor space — stays on the current space.
    bool placeWindowInSpace(HWND hwnd, int spaceIndex);

    // UI-only preview (hover / arrows): highlight + bottom strip + card image.
    // Does not change the real desktop space; click/Enter commits via closed().
    bool previewSpace(int spaceIndex);
    // Space index captured when overview opened (restored on cancel if needed).
    int originSpaceIndex() const { return m_originSpace; }

    // Soft-preview hold: after leaving space/window strips, wait this long
    // before restoring the current space's window strip (default 1000 ms).
    void setSoftPreviewHoldMs(int ms);
    int softPreviewHoldMs() const { return m_holdMs; }
    // True while the hold timer is armed (pointer off keep-zones, not yet restored).
    bool isSoftPreviewHoldPending() const;

signals:
    void closed(int chosenSpace);
    // Emitted after a successful drag-place (hwnd may be null in tests via placeWindowInSpace).
    void windowPlaced(quint64 hwnd, int spaceIndex);
    // Live preview moved to another space (desktop already synced).
    void spacePreviewed(int spaceIndex);
    // Clicked a bottom tile: will switch to that window's space and focus it.
    void windowActivated(quint64 hwnd);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void rebuildCards();
    void rebuildWindowPreviews();
    void refreshCardBadges();
    void refreshCardScreenshot(int index);
    void setSelected(int index);
    void showContextMenu(int spaceIndex, const QPoint &globalPos);
    void showHint(const QString &text);
    void cancelAnimations();
    void playEnterAnimation();
    void playExitAnimation();
    void finishClose();
    void pinToMonitorPhysically();
    QString windowTitle(HWND hwnd) const;
    // True if pos is in the outer margin band of the panel (start hold timer).
    bool isOuterMarginPos(const QPoint &pos) const;
    bool isKeepZoneWidget(QObject *w) const;
    void restoreStripToCurrentSpace();
    void armSoftPreviewHold();
    void cancelSoftPreviewHold();
    bool addSpaceFromStrip();
    bool addSpaceAndPlaceWindow(quint64 hwnd);
    // Click tile → commit that window's space, then focus/raise the HWND.
    bool activateWindowPreview(HWND hwnd);

    SpaceManager *m_manager = nullptr;
    HMONITOR m_hmon = nullptr;
    bool m_open = false;
    bool m_animating = false;
    bool m_closePending = false;
    bool m_hostManaged = false;
    bool m_exitStarted = false;
    int m_selected = 0;
    int m_originSpace = 0;
    int m_pendingCommit = -1;
    int m_holdMs = 1000;
    HWND m_frontHwnd = nullptr; // focus after commit close
    QTimer *m_holdTimer = nullptr;

    QWidget *m_root = nullptr;
    QLabel *m_header = nullptr;
    QLabel *m_hint = nullptr;
    QString m_defaultHint;
    QHBoxLayout *m_cardRow = nullptr;
    QWidget *m_spaceStripHost = nullptr;
    AddSpaceButton *m_addSpaceBtn = nullptr;
    QWidget *m_windowHost = nullptr;
    QVBoxLayout *m_windowStack = nullptr; // rows from shelf packing
    QScrollArea *m_windowScroll = nullptr;
    QVector<SpaceCardWidget *> m_cards;
    QVector<WindowPreviewWidget *> m_windowPreviews;

    QPropertyAnimation *m_fadeAnim = nullptr;
};
