// dead simple logging. writes to %LOCALAPPDATA%\creechr\creechr.log,
// rotates at ~1MB and keeps the last 3 files. thread-safe via a single
// mutex. don't @ me about lock contention, this isn't the hot path.
//
// usage: LOG_INFO("walked {} pixels", n); LOG_WARN("dwm lied again");
//
// (yes the format is qt-style %1 %2 %3, not python {} — we're inside
//  a qt app, just use QString::arg.)
#pragma once

#include <QString>

namespace cr {

enum class LogLevel : int {
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4,
};

// initialize the log file. safe to call multiple times. returns false
// if it couldn't open the log path (in which case logging silently
// becomes a no-op, which is fine — we are not making the app fail to
// start over a log file).
bool initLogging();

// minimum level to actually emit. defaults to Info.
void setLogLevel(LogLevel level);

void log(LogLevel level, const QString& message);

} // namespace cr

#define LOG_TRACE(msg) ::cr::log(::cr::LogLevel::Trace, (msg))
#define LOG_DEBUG(msg) ::cr::log(::cr::LogLevel::Debug, (msg))
#define LOG_INFO(msg)  ::cr::log(::cr::LogLevel::Info,  (msg))
#define LOG_WARN(msg)  ::cr::log(::cr::LogLevel::Warn,  (msg))
#define LOG_ERROR(msg) ::cr::log(::cr::LogLevel::Error, (msg))
