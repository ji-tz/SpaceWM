#pragma once

#include "../core/SpaceManager.h"

#include <QWidget>
#include <Windows.h>

class QHBoxLayout;
class QLabel;
class QPropertyAnimation;
class SpaceCardWidget;

// Fullscreen overview for ONE monitor.
class OverviewWindow : public QWidget {
    Q_OBJECT
public:
    explicit OverviewWindow(SpaceManager *manager, QWidget *parent = nullptr);

    void assignMonitor(HMONITOR hmon) { m_hmon = hmon; }
    void openOnMonitor(HMONITOR hmon, bool takeFocus = true);

    // User commit/cancel on this panel: emits closed() once.
    void closeOverview(bool commit);

    // Host-sync lifecycle:
    // 1) prepareClose — mark closing (isOpen→false), no anim yet
    // 2) startExit    — begin fade-out together with sibling panels
    // 3) forceHide    — belt-and-suspenders hide()
    void prepareClose();
    void startExit();
    void forceHide();

    // Back-compat helpers used by tests / single-panel path.
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

signals:
    void closed(int chosenSpace);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void rebuildCards();
    void setSelected(int index);
    void cancelAnimations();
    void playEnterAnimation();
    void playExitAnimation();
    void finishClose();
    void pinToMonitorPhysically();

    SpaceManager *m_manager = nullptr;
    HMONITOR m_hmon = nullptr;
    bool m_open = false;
    bool m_animating = false;
    bool m_closePending = false;
    bool m_hostManaged = false;
    bool m_exitStarted = false;
    int m_selected = 0;
    int m_pendingCommit = -1;

    QWidget *m_root = nullptr;
    QLabel *m_header = nullptr;
    QHBoxLayout *m_cardRow = nullptr;
    QVector<SpaceCardWidget *> m_cards;

    QPropertyAnimation *m_fadeAnim = nullptr;
};
