#pragma once

#include "../core/SpaceManager.h"

#include <QHash>
#include <QWidget>
#include <Windows.h>
#include <functional>

class QHBoxLayout;
class QLabel;
class SpaceCardWidget;

// Fullscreen, per-monitor overview of that monitor's spaces.
// Shows one row of SpaceCards; click / Enter / number keys to switch; Esc cancels.
class OverviewWindow : public QWidget {
    Q_OBJECT
public:
    explicit OverviewWindow(SpaceManager *manager, QWidget *parent = nullptr);

    // Show on the given monitor (geometry in virtual-desktop coords).
    void openOnMonitor(HMONITOR hmon);
    void closeOverview(bool commit);
    bool isOpen() const { return m_open; }
    HMONITOR targetMonitor() const { return m_hmon; }

    // Keyboard selection index for animation highlight.
    int selectedIndex() const { return m_selected; }

signals:
    void closed(int chosenSpace); // -1 if cancelled

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void rebuildCards();
    void setSelected(int index);
    void playEnterAnimation();
    void playExitAnimation(std::function<void()> after);

    SpaceManager *m_manager = nullptr;
    HMONITOR m_hmon = nullptr;
    bool m_open = false;
    int m_selected = 0;
    int m_pendingCommit = -1;

    QWidget *m_root = nullptr;
    QLabel *m_header = nullptr;
    QHBoxLayout *m_cardRow = nullptr;
    QVector<SpaceCardWidget *> m_cards;
};
