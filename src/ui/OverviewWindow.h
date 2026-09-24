#pragma once

#include "../core/SpaceManager.h"

#include <QHash>
#include <QWidget>
#include <Windows.h>
#include <functional>

class QHBoxLayout;
class QLabel;
class QPropertyAnimation;
class SpaceCardWidget;

// Fullscreen, per-monitor overview of that monitor's spaces.
// Shows one row of SpaceCards; click / Enter / number keys to switch; Esc cancels.
class OverviewWindow : public QWidget {
    Q_OBJECT
public:
    explicit OverviewWindow(SpaceManager *manager, QWidget *parent = nullptr);

    void openOnMonitor(HMONITOR hmon);
    void closeOverview(bool commit);
    bool isOpen() const { return m_open; }
    bool isAnimating() const { return m_animating; }
    HMONITOR targetMonitor() const { return m_hmon; }
    int selectedIndex() const { return m_selected; }
    int cardCount() const { return m_cards.size(); }

signals:
    void closed(int chosenSpace); // -1 if cancelled

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

    SpaceManager *m_manager = nullptr;
    HMONITOR m_hmon = nullptr;
    bool m_open = false;
    bool m_animating = false; // true while enter or exit animation runs
    bool m_closePending = false;
    int m_selected = 0;
    int m_pendingCommit = -1;

    QWidget *m_root = nullptr;
    QLabel *m_header = nullptr;
    QHBoxLayout *m_cardRow = nullptr;
    QVector<SpaceCardWidget *> m_cards;

    QPropertyAnimation *m_fadeAnim = nullptr;
};
