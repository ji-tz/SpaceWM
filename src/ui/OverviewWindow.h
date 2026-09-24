#pragma once

#include "../core/SpaceManager.h"

#include <QWidget>
#include <Windows.h>

class QHBoxLayout;
class QLabel;
class QPropertyAnimation;
class SpaceCardWidget;
class WindowPreviewWidget;
class QScrollArea;

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

    // Mission Control: drop hwnd onto space index (public for tests).
    bool placeWindowInSpace(HWND hwnd, int spaceIndex);

    // Hover/keyboard: live-switch the real desktop + refresh strip for that space.
    bool previewSpace(int spaceIndex);
    // Space index captured when overview opened (restored on cancel).
    int originSpaceIndex() const { return m_originSpace; }

signals:
    void closed(int chosenSpace);
    // Emitted after a successful drag-place (hwnd may be null in tests via placeWindowInSpace).
    void windowPlaced(quint64 hwnd, int spaceIndex);
    // Live preview moved to another space (desktop already synced).
    void spacePreviewed(int spaceIndex);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void rebuildCards();
    void rebuildWindowPreviews();
    void refreshCardBadges();
    void refreshCardScreenshot(int index);
    void setSelected(int index);
    void cancelAnimations();
    void playEnterAnimation();
    void playExitAnimation();
    void finishClose();
    void pinToMonitorPhysically();
    QString windowTitle(HWND hwnd) const;

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

    QWidget *m_root = nullptr;
    QLabel *m_header = nullptr;
    QLabel *m_hint = nullptr;
    QHBoxLayout *m_cardRow = nullptr;
    QWidget *m_spaceStripHost = nullptr;
    QWidget *m_windowHost = nullptr;
    QHBoxLayout *m_windowRow = nullptr;
    QScrollArea *m_windowScroll = nullptr;
    QVector<SpaceCardWidget *> m_cards;
    QVector<WindowPreviewWidget *> m_windowPreviews;

    QPropertyAnimation *m_fadeAnim = nullptr;
};
