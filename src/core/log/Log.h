#pragma once

#include <QString>

// Runtime logging via spdlog (third-party).
//
// TR (trace):  runtime flow → <projectRoot>/TR/trace.log   (info and above)
// EH (error):  errors / crashes / critical → <projectRoot>/EH/error.log
//
// Default root: repo root detected from the exe path (build/ → parent with
// AGENTS.md + CMakeLists.txt + src/). Override: spacelog::init(dir) uses
// <dir>/TR/ and <dir>/EH/ (tests). Logs are NOT committed to git.
namespace spacelog {

// Create sinks/threads. Safe to call once at startup; idempotent.
bool init(const QString &dirOverride = QString());
void shutdown();
// Force flush both sinks (tests / before process exit).
void flush();

// Project (or override) root. Empty if not initialized.
QString logDir();
// <root>/TR/trace.log and <root>/EH/error.log
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
