// Verifies scripts/package.ps1 end to end:
//   - portable zip is produced and passes the script's own entry checks
//   - a bad -BuildDir is rejected (non-zero exit)
//   - installer build with -Installer when Inno Setup 6 (ISCC.exe) is available
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

class TestPackage : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void zipPackage();
    void packageRejectsMissingBuild();
    void installerWhenIsccAvailable();

private:
    QString m_root; // repo root (tests/..)
    int runPowerShell(const QStringList &args, QString *output, int timeoutMs = 300000) const;
    static QString findIscc();
};

void TestPackage::initTestCase()
{
    const QString testsDir = QFileInfo(QString::fromLocal8Bit(__FILE__)).absolutePath();
    m_root = QDir::cleanPath(QFileInfo(testsDir + QStringLiteral("/..")).absoluteFilePath());
    QVERIFY2(QFile::exists(m_root + QStringLiteral("/scripts/package.ps1")),
             qPrintable(QStringLiteral("package.ps1 not found under %1").arg(m_root)));
}

int TestPackage::runPowerShell(const QStringList &args, QString *output, int timeoutMs) const
{
    QProcess p;
    p.start(QStringLiteral("powershell.exe"),
             QStringList{QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"),
                         QStringLiteral("Bypass"), QStringLiteral("-File")}
                 + args);
    if (!p.waitForStarted(15000))
        return -1;
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        return -1;
    }
    if (output)
        *output = QString::fromLocal8Bit(p.readAllStandardOutput()) +
                  QString::fromLocal8Bit(p.readAllStandardError());
    return p.exitCode();
}

QString TestPackage::findIscc()
{
    const QStringList candidates = {
        qEnvironmentVariable("ProgramFiles(x86)") + QStringLiteral("/Inno Setup 6/ISCC.exe"),
        qEnvironmentVariable("ProgramFiles") + QStringLiteral("/Inno Setup 6/ISCC.exe"),
        qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/Programs/Inno Setup 6/ISCC.exe"),
    };
    for (const QString &c : candidates) {
        if (QFile::exists(c))
            return c;
    }
    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("ISCC.exe"));
    return fromPath;
}

void TestPackage::zipPackage()
{
    if (!QFile::exists(m_root + QStringLiteral("/build/SpaceWM.exe")))
        QSKIP("build/SpaceWM.exe not found — build the app before running this test");

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    QString out;
    const int code = runPowerShell(
        {m_root + QStringLiteral("/scripts/package.ps1"), QStringLiteral("-OutDir"), tmp.path()},
        &out);
    QVERIFY2(code == 0, qPrintable(out));

    const QString zip = tmp.path() + QStringLiteral("/SpaceWM-win64.zip");
    QVERIFY2(QFile::exists(zip), qPrintable(zip));
    QVERIFY2(QFileInfo(zip).size() > 10 * 1024 * 1024,
             qPrintable(QStringLiteral("zip suspiciously small: %1 bytes")
                            .arg(QFileInfo(zip).size())));
    // The script itself verifies required entries (exe, Qt DLLs, platform plugin).
    QVERIFY2(out.contains(QStringLiteral("entries")), qPrintable(out));
}

void TestPackage::packageRejectsMissingBuild()
{
    QTemporaryDir empty;
    QVERIFY(empty.isValid());

    QString out;
    const int code = runPowerShell(
        {m_root + QStringLiteral("/scripts/package.ps1"),
         QStringLiteral("-BuildDir"),
         empty.path(),
         QStringLiteral("-OutDir"),
         empty.path()},
        &out);
    QVERIFY2(code != 0, qPrintable(out));
}

void TestPackage::installerWhenIsccAvailable()
{
    if (findIscc().isEmpty())
        QSKIP("Inno Setup 6 (ISCC.exe) not installed — installer build not verified here");
    if (!QFile::exists(m_root + QStringLiteral("/build/SpaceWM.exe")))
        QSKIP("build/SpaceWM.exe not found — build the app before running this test");

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    QString out;
    const int code = runPowerShell(
        {m_root + QStringLiteral("/scripts/package.ps1"), QStringLiteral("-Installer"),
         QStringLiteral("-OutDir"), tmp.path()},
        &out);
    QVERIFY2(code == 0, qPrintable(out));

    const QString setup = tmp.path() + QStringLiteral("/SpaceWM-Setup-x64.exe");
    QVERIFY2(QFile::exists(setup), qPrintable(setup));
    QVERIFY2(QFileInfo(setup).size() > 1024 * 1024,
             qPrintable(QStringLiteral("setup suspiciously small: %1 bytes")
                            .arg(QFileInfo(setup).size())));
}

QTEST_MAIN(TestPackage)
#include "test_package.moc"
