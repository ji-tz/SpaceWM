#pragma once

#include "OverviewWindow.h"
#include "WinPreviewEvents.h"
#include <QTimer>

#include <QObject>
#include <QVector>
#include <memory>

struct OverviewWarmCapture;

// Mission Control style: one OverviewWindow per monitor, opened and closed together.
class OverviewHost : public QObject {
    Q_OBJECT
  public:
    explicit OverviewHost(SpaceManager *manager, QObject *parent = nullptr);
    ~OverviewHost() override;

    bool isOpen() const;
    void postWinPreviewEvent(WinPreviewEvents::Event event);
    void beginWinPreparation();
    void cancelWinPreparation();
    int preparedWindowCount() const;
    bool winPreparationReady() const;
    void openAll();
    void closeAll(bool commit);

    OverviewWindow *panelFor(HMONITOR hmon);
    OverviewWindow *activePanel() const { return m_active; }
    int openPanelCount() const;
    // Synchronous hide of every panel (tests / emergency).
    void forceHideAll();

  signals:
    void previewRequested();
    void spaceChosen(quint64 hmon, int spaceIndex);
    void allClosed();
    // Emitted only after every monitor has finished preparation.
    void panelReady(quint64 hmon, int spaceIndex);

  private:
    void processWinPreviewEvents();
    WinPreviewEvents m_winEvents;
    QTimer m_winEventTimer;
    void ensurePanels();
    void onPanelClosed(int chosen);
    void startExitAllAndFinish();
    void finishIfAllQuiet();
    bool m_opening = false;

    SpaceManager *m_manager = nullptr;
    QVector<OverviewWindow *> m_panels;
    OverviewWindow *m_active = nullptr;
    bool m_switching = false;
    int m_closeGeneration = 0;
    std::shared_ptr<OverviewWarmCapture> m_winPreparation;
};
