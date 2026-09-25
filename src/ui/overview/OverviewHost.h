#pragma once

#include "OverviewWindow.h"

#include <QObject>
#include <QVector>

// Mission Control style: one OverviewWindow per monitor, opened and closed together.
class OverviewHost : public QObject {
    Q_OBJECT
  public:
    explicit OverviewHost(SpaceManager *manager, QObject *parent = nullptr);
    ~OverviewHost() override;

    bool isOpen() const;
    void openAll();
    void closeAll(bool commit);

    OverviewWindow *panelFor(HMONITOR hmon);
    OverviewWindow *activePanel() const { return m_active; }
    int openPanelCount() const;
    // Synchronous hide of every panel (tests / emergency).
    void forceHideAll();

  signals:
    void spaceChosen(quint64 hmon, int spaceIndex);
    void allClosed();

  private:
    void ensurePanels();
    void onPanelClosed(int chosen);
    void startExitAllAndFinish();
    void finishIfAllQuiet();

    SpaceManager *m_manager = nullptr;
    QVector<OverviewWindow *> m_panels;
    OverviewWindow *m_active = nullptr;
    bool m_switching = false;
    int m_closeGeneration = 0;
};
