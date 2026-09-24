#include "core/log/Log.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>

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
QString g_dir;
std::mutex g_initMu;
bool g_crashInstalled = false;

QString defaultLogDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QDir::tempPath() + QStringLiteral("/SpaceWM");
    return base + QStringLiteral("/logs");
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
            // Logger not up — still best-effort append to error.log.
            QDir().mkpath(g_dir.isEmpty() ? defaultLogDir() : g_dir);
            QFile f((g_dir.isEmpty() ? defaultLogDir() : g_dir)
                    + QStringLiteral("/error.log"));
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
    const DWORD code = info && info->ExceptionRecord
                           ? info->ExceptionRecord->ExceptionCode
                           : 0;
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

    g_dir = dirOverride.isEmpty() ? defaultLogDir() : dirOverride;
    QDir().mkpath(g_dir);

    try {
        // Drop previous instances so shutdown()+init() can recreate (tests).
        spdlog::drop("spacewm-trace");
        spdlog::drop("spacewm-error");

        // TR: info+ → trace.log (append — never wipe history on re-init)
        g_trace = spdlog::basic_logger_mt(
            "spacewm-trace", (g_dir + QStringLiteral("/trace.log")).toStdString(),
            /*truncate=*/false);
        g_trace->set_pattern("%Y-%m-%d %H:%M:%S.%e [%l] %v");
        g_trace->set_level(spdlog::level::info);
        g_trace->flush_on(spdlog::level::info);

        // EH: warn+ → error.log
        g_error = spdlog::basic_logger_mt(
            "spacewm-error", (g_dir + QStringLiteral("/error.log")).toStdString(),
            /*truncate=*/false);
        g_error->set_pattern("%Y-%m-%d %H:%M:%S.%e [%l] %v");
        g_error->set_level(spdlog::level::warn);
        g_error->flush_on(spdlog::level::warn);

        spdlog::set_default_logger(g_trace);
        g_trace->info("log init dir={}", g_dir.toStdString());
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
    return g_dir;
}

QString tracePath()
{
    return g_dir.isEmpty() ? QString() : g_dir + QStringLiteral("/trace.log");
}

QString errorPath()
{
    return g_dir.isEmpty() ? QString() : g_dir + QStringLiteral("/error.log");
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
    ::SetUnhandledExceptionFilter(
        [](EXCEPTION_POINTERS *info) -> LONG {
            sehHandler(info);
            return EXCEPTION_EXECUTE_HANDLER;
        });
}

} // namespace spacelog
