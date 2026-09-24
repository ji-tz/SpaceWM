#pragma once

#include <QString>

// Runtime logging via spdlog (third-party).
//
// TR (trace):  runtime flow → <logDir>/trace.log   (info and above)
// EH (error):  errors / crashes / critical         → <logDir>/error.log
//
// Log dir default: <AppData>/SpaceWM/logs  (override: SpaceWM::log::init(dir)).
// Logs are NOT committed to git.
namespace spacelog {

// Create sinks/threads. Safe to call once at startup; idempotent.
bool init(const QString &dirOverride = QString());
void shutdown();
// Force flush both sinks (tests / before process exit).
void flush();

// Directory that contains trace.log / error.log (empty if not initialized).
QString logDir();
QString tracePath();
QString errorPath();

// TR — normal runtime trace (also mirrors to error sink when level >= warn).
void trace(const QString &msg);
void info(const QString &msg);
void warn(const QString &msg);

// EH — failures, exceptions, crashes.
void error(const QString &msg);
void critical(const QString &msg);

// Install process-wide crash hooks: terminate / pure-virtual / unhandled
// SEH → write EH then flush. Call once after init().
void installCrashHandlers();

} // namespace spacelog
