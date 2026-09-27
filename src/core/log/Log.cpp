#include "core/log/Log.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <Windows.h>

#include <cstdio>
#include <exception>
#include <memory>
#include <mutex>
#include <vector>

namespace {

std::shared_ptr<spdlog::logger> g_trace;
std::shared_ptr<spdlog::logger> g_error;
QString g_root; // project root (or init override); TR/ and EH/ live under it
std::mutex g_initMu;
bool g_crashInstalled = false;

// Walk up from the exe (build/, build/tests/, …) to the repo root that has
// AGENTS.md + CMakeLists.txt + src/ — so runtime TR/EH land in <projectRoot>/.
QString detectProjectRoot()
{
    QString dir = QCoreApplication::applicationDirPath();
    for (int i = 0; i < 8 && !dir.isEmpty(); ++i) {
        if (QFile::exists(dir + QStringLiteral("/AGENTS.md")) &&
            QFile::exists(dir + QStringLiteral("/CMakeLists.txt")) &&
            QDir(dir + QStringLiteral("/src")).exists()) {
            return dir;
        }
        QDir d(dir);
        if (!d.cdUp())
            break;
        dir = d.absolutePath();
    }
    // Installed / portable copy without sources: still log beside the exe's parent.
    const QString appDir = QCoreApplication::applicationDirPath();
    QDir d(appDir);
    if (d.cdUp())
        return d.absolutePath();
    return appDir;
}

QString trDirOf(const QString &root)
{
    return root + QStringLiteral("/TR");
}

QString ehDirOf(const QString &root)
{
    return root + QStringLiteral("/EH");
}

void flushAll()
{
    if (g_trace)
        g_trace->flush();
    if (g_error)
        g_error->flush();
}

void writeCrashLine(const char *kind, const char *detail)
{
    try {
        if (g_error)
            g_error->critical("[crash] {} {}", kind, detail ? detail : "");
        else {
            // Logger not up — still best-effort append to <root>/EH/error.log.
            const QString root = g_root.isEmpty() ? detectProjectRoot() : g_root;
            const QString ehDir = ehDirOf(root);
            QDir().mkpath(ehDir);
            QFile f(ehDir + QStringLiteral("/error.log"));
            if (f.open(QIODevice::Append | QIODevice::Text))
                f.write(QByteArray("[crash] ") + kind + " " + (detail ? detail : "") + "\n");
        }
        flushAll();
    } catch (...) {
    }
}

LONG WINAPI sehHandler(EXCEPTION_POINTERS *info)
{
    char buf[128];
    const DWORD code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
    std::snprintf(buf, sizeof(buf), "SEH exception=0x%08lX", static_cast<unsigned long>(code));
    writeCrashLine("SEH", buf);
    return EXCEPTION_EXECUTE_HANDLER;
}

void terminateHandler()
{
    writeCrashLine("std::terminate", "uncaught exception or noexcept violation");
    std::abort();
}

} // namespace

namespace spacelog {

bool init(const QString &dirOverride)
{
    std::lock_guard lock(g_initMu);
    if (g_trace && g_error)
        return true;

    g_root = dirOverride.isEmpty() ? detectProjectRoot() : dirOverride;
    const QString trDir = trDirOf(g_root);
    const QString ehDir = ehDirOf(g_root);
    QDir().mkpath(trDir);
    QDir().mkpath(ehDir);

    try {
        // Drop previous instances so shutdown()+init() can recreate (tests).
        spdlog::drop("spacewm-trace");
        spdlog::drop("spacewm-error");

        // TR: info+ → <root>/TR/trace.log (append — never wipe on re-init)
        g_trace = spdlog::basic_logger_mt("spacewm-trace",
                                          (trDir + QStringLiteral("/trace.log")).toStdString(),
                                          /*truncate=*/false);
        g_trace->set_pattern("%Y-%m-%d %H:%M:%S.%e [%l] %v");
        g_trace->set_level(spdlog::level::info);
        g_trace->flush_on(spdlog::level::info);

        // EH: warn+ → <root>/EH/error.log
        g_error = spdlog::basic_logger_mt("spacewm-error",
                                          (ehDir + QStringLiteral("/error.log")).toStdString(),
                                          /*truncate=*/false);
        g_error->set_pattern("%Y-%m-%d %H:%M:%S.%e [%l] %v");
        g_error->set_level(spdlog::level::warn);
        g_error->flush_on(spdlog::level::warn);

        spdlog::set_default_logger(g_trace);
        g_trace->info("log init root={} TR={} EH={}", g_root.toStdString(), trDir.toStdString(),
                      ehDir.toStdString());
        return true;
    } catch (const std::exception &ex) {
        Q_UNUSED(ex)
        g_trace.reset();
        g_error.reset();
        spdlog::drop("spacewm-trace");
        spdlog::drop("spacewm-error");
        return false;
    }
}

void shutdown()
{
    std::lock_guard lock(g_initMu);
    if (g_trace)
        g_trace->info("log shutdown");
    flushAll();
    g_trace.reset();
    g_error.reset();
    spdlog::drop("spacewm-trace");
    spdlog::drop("spacewm-error");
}

void flush()
{
    std::lock_guard lock(g_initMu);
    flushAll();
}

QString logDir()
{
    return g_root;
}

QString tracePath()
{
    return g_root.isEmpty() ? QString() : g_root + QStringLiteral("/TR/trace.log");
}

QString errorPath()
{
    return g_root.isEmpty() ? QString() : g_root + QStringLiteral("/EH/error.log");
}

void trace(const QString &msg)
{
    if (g_trace)
        g_trace->info("{}", msg.toStdString());
}

void info(const QString &msg)
{
    if (g_trace)
        g_trace->info("{}", msg.toStdString());
}

void warn(const QString &msg)
{
    const std::string m = msg.toStdString();
    if (g_trace)
        g_trace->warn("{}", m);
    if (g_error)
        g_error->warn("{}", m);
}

void error(const QString &msg)
{
    const std::string m = msg.toStdString();
    if (g_trace)
        g_trace->error("{}", m);
    if (g_error)
        g_error->error("{}", m);
    flushAll();
}

void critical(const QString &msg)
{
    const std::string m = msg.toStdString();
    if (g_trace)
        g_trace->critical("{}", m);
    if (g_error)
        g_error->critical("{}", m);
    flushAll();
}

void installCrashHandlers()
{
    if (g_crashInstalled)
        return;
    g_crashInstalled = true;
    std::set_terminate(terminateHandler);
    ::SetUnhandledExceptionFilter([](EXCEPTION_POINTERS *info) -> LONG {
        sehHandler(info);
        return EXCEPTION_EXECUTE_HANDLER;
    });
}

} // namespace spacelog
