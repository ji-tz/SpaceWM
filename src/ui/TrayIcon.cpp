#include "TrayIcon.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QMenu>
#include <QStyle>

TrayIcon::TrayIcon(QObject *parent)
    : QObject(parent)
{
    m_tray = new QSystemTrayIcon(this);
    // Use a standard icon so we don't need a .rc yet.
    m_tray->setIcon(QIcon::fromTheme(QStringLiteral("preferences-system-windows"),
                                     qApp->style()->standardIcon(QStyle::SP_ComputerIcon)));
    m_tray->setToolTip(tr("SpaceWM — per-monitor spaces"));

    m_menu = new QMenu;
    m_statusAction = m_menu->addAction(tr("Space 1"));
    m_statusAction->setEnabled(false);
    m_menu->addSeparator();
    m_menu->addAction(tr("Show overview"), this, &TrayIcon::overviewRequested);
    m_menu->addAction(tr("Next space"), this, &TrayIcon::nextSpaceRequested);
    m_menu->addAction(tr("Previous space"), this, &TrayIcon::prevSpaceRequested);
    m_menu->addAction(tr("Refresh monitors"), this, &TrayIcon::refreshMonitorsRequested);
    m_menu->addSeparator();
    m_menu->addAction(tr("Quit"), this, &TrayIcon::quitRequested);

    m_tray->setContextMenu(m_menu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick)
            emit overviewRequested();
    });

    m_tray->show();
}

void TrayIcon::setSpaceLabel(const QString &monitorDevice, int index, int total)
{
    const QString text = tr("%1 — Space %2/%3").arg(monitorDevice).arg(index + 1).arg(total);
    m_statusAction->setText(text);
    m_tray->setToolTip(text);
}

void TrayIcon::showMessage(const QString &title, const QString &body)
{
    m_tray->showMessage(title, body, QSystemTrayIcon::Information, 2000);
}
