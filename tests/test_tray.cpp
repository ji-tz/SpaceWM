#include <QtTest>

#include "ui/tray/TrayIcon.h"

class TestTray : public QObject {
    Q_OBJECT
private slots:
    void constructsAndShows()
    {
        TrayIcon tray;
        QVERIFY(true);
    }

    void setSpaceLabelDoesNotCrash()
    {
        TrayIcon tray;
        tray.setSpaceLabel(QStringLiteral("\\\\.\\DISPLAY1"), 1, 4);
        tray.showMessage(QStringLiteral("SpaceWM"), QStringLiteral("unit-tray-msg"));
        QVERIFY(true);
    }

    void signalsExistForWiring()
    {
        TrayIcon tray;
        QSignalSpy settings(&tray, &TrayIcon::settingsRequested);
        QSignalSpy overview(&tray, &TrayIcon::overviewRequested);
        QSignalSpy quit(&tray, &TrayIcon::quitRequested);
        // Signals are wired via menu; just ensure spies attach without error.
        QVERIFY(settings.isValid());
        QVERIFY(overview.isValid());
        QVERIFY(quit.isValid());
    }
};

QTEST_MAIN(TestTray)
#include "test_tray.moc"
