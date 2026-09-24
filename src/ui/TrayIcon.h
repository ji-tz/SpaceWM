#pragma once

#include <QObject>
#include <QSystemTrayIcon>

class QAction;
class QMenu;

class TrayIcon : public QObject {
    Q_OBJECT
public:
    explicit TrayIcon(QObject *parent = nullptr);

    void setSpaceLabel(const QString &monitorDevice, int index, int total);
    void showMessage(const QString &title, const QString &body);

signals:
    void overviewRequested();
    void nextSpaceRequested();
    void prevSpaceRequested();
    void quitRequested();
    void refreshMonitorsRequested();

private:
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_menu = nullptr;
    QAction *m_statusAction = nullptr;
};
