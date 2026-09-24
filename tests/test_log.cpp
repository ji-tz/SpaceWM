#include <QtTest>

#include "core/log/Log.h"

#include <QFile>
#include <QTemporaryDir>

// TR = trace.log (runtime), EH = error.log (errors) — spdlog-backed.
class TestLog : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        m_dir = new QTemporaryDir();
        QVERIFY(m_dir->isValid());
        QVERIFY(spacelog::init(m_dir->path()));
        spacelog::installCrashHandlers();
    }

    void cleanupTestCase()
    {
        spacelog::shutdown();
        delete m_dir;
        m_dir = nullptr;
    }

    void pathsAreUnderLogDir()
    {
        QCOMPARE(spacelog::logDir(), m_dir->path());
        QVERIFY(spacelog::tracePath().endsWith(QStringLiteral("/trace.log")));
        QVERIFY(spacelog::errorPath().endsWith(QStringLiteral("/error.log")));
    }

    void traceWritesTraceLog()
    {
        spacelog::trace(QStringLiteral("unit-trace-hello"));
        spacelog::info(QStringLiteral("unit-info-line"));
        spacelog::flush();

        QFile t(spacelog::tracePath());
        QVERIFY(t.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString trace = QString::fromUtf8(t.readAll());
        t.close();
        QVERIFY(trace.contains(QStringLiteral("unit-trace-hello")));
        QVERIFY(trace.contains(QStringLiteral("unit-info-line")));
    }

    void errorWritesErrorLog()
    {
        spacelog::error(QStringLiteral("unit-error-boom"));
        spacelog::critical(QStringLiteral("unit-critical-boom"));
        spacelog::flush();

        QFile e(spacelog::errorPath());
        QVERIFY(e.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString err = QString::fromUtf8(e.readAll());
        e.close();
        QVERIFY(err.contains(QStringLiteral("unit-error-boom")));
        QVERIFY(err.contains(QStringLiteral("unit-critical-boom")));

        QFile t(spacelog::tracePath());
        QVERIFY(t.open(QIODevice::ReadOnly | QIODevice::Text));
        QVERIFY(QString::fromUtf8(t.readAll()).contains(QStringLiteral("unit-error-boom")));
    }

    void warnGoesToBoth()
    {
        spacelog::warn(QStringLiteral("unit-warn-both"));
        spacelog::flush();
        QFile t(spacelog::tracePath());
        QFile e(spacelog::errorPath());
        QVERIFY(t.open(QIODevice::ReadOnly | QIODevice::Text));
        QVERIFY(e.open(QIODevice::ReadOnly | QIODevice::Text));
        QVERIFY(QString::fromUtf8(t.readAll()).contains(QStringLiteral("unit-warn-both")));
        QVERIFY(QString::fromUtf8(e.readAll()).contains(QStringLiteral("unit-warn-both")));
    }

    void reinitAfterShutdownDoesNotWipe()
    {
        spacelog::trace(QStringLiteral("unit-before-restart"));
        spacelog::flush();
        spacelog::shutdown();
        QVERIFY(spacelog::init(m_dir->path()));
        spacelog::trace(QStringLiteral("unit-after-restart"));
        spacelog::flush();

        QFile t(spacelog::tracePath());
        QVERIFY(t.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString all = QString::fromUtf8(t.readAll());
        QVERIFY(all.contains(QStringLiteral("unit-before-restart")));
        QVERIFY(all.contains(QStringLiteral("unit-after-restart")));
    }

private:
    QTemporaryDir *m_dir = nullptr;
};

QTEST_MAIN(TestLog)
#include "test_log.moc"
