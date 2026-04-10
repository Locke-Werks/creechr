#include "util/logging.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QTextStream>

namespace cr {
namespace {

constexpr qint64 kMaxLogBytes = 1024 * 1024; // 1MB. the spec says 1MB. shut up.
constexpr int kKeepFiles = 3;

QMutex g_mutex;
QFile* g_file = nullptr;          // owned, leaked at exit, who cares
QTextStream* g_stream = nullptr;  // ditto
LogLevel g_level = LogLevel::Info;
bool g_initialized = false;

QString logDirPath()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    // AppLocalDataLocation already includes the org/app name on windows,
    // so this resolves to something like:
    // C:\Users\vexam\AppData\Local\creechr\creechr
    return base;
}

QString logFilePath()
{
    return logDirPath() + QStringLiteral("/creechr.log");
}

// rotate creechr.log -> creechr.log.1 -> .2 -> .3, dropping the oldest.
// only called while holding g_mutex.
void rotateLocked()
{
    if (g_stream) {
        g_stream->flush();
        delete g_stream;
        g_stream = nullptr;
    }
    if (g_file) {
        g_file->close();
        delete g_file;
        g_file = nullptr;
    }

    const QString base = logFilePath();
    // delete the oldest, then shift each one up by 1
    QFile::remove(base + QStringLiteral(".") + QString::number(kKeepFiles));
    for (int i = kKeepFiles - 1; i >= 1; --i) {
        const QString from = base + QStringLiteral(".") + QString::number(i);
        const QString to   = base + QStringLiteral(".") + QString::number(i + 1);
        if (QFile::exists(from)) {
            QFile::rename(from, to);
        }
    }
    if (QFile::exists(base)) {
        QFile::rename(base, base + QStringLiteral(".1"));
    }

    g_file = new QFile(base);
    if (!g_file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        delete g_file;
        g_file = nullptr;
        return;
    }
    g_stream = new QTextStream(g_file);
}

const char* levelTag(LogLevel l)
{
    switch (l) {
    case LogLevel::Trace: return "trace";
    case LogLevel::Debug: return "debug";
    case LogLevel::Info:  return "info ";
    case LogLevel::Warn:  return "warn ";
    case LogLevel::Error: return "ERROR";
    }
    return "?    ";
}

} // namespace

bool initLogging()
{
    QMutexLocker lock(&g_mutex);
    if (g_initialized) {
        return g_file != nullptr;
    }
    g_initialized = true;

    QDir().mkpath(logDirPath());
    rotateLocked(); // open fresh stream against existing file (no actual rotation here)

    if (g_stream) {
        // marker so future me can grep for "creechr starting" when triaging
        const QString boot = QDateTime::currentDateTime().toString(Qt::ISODate)
            + QStringLiteral(" info  creechr starting up\n");
        *g_stream << boot;
        g_stream->flush();
    }
    return g_file != nullptr;
}

void setLogLevel(LogLevel level)
{
    QMutexLocker lock(&g_mutex);
    g_level = level;
}

void log(LogLevel level, const QString& message)
{
    QMutexLocker lock(&g_mutex);
    if (level < g_level || !g_stream) {
        return;
    }

    if (g_file && g_file->size() > kMaxLogBytes) {
        rotateLocked();
        if (!g_stream) {
            return;
        }
    }

    const QString line = QDateTime::currentDateTime().toString(Qt::ISODate)
        + QStringLiteral(" ") + QLatin1String(levelTag(level))
        + QStringLiteral(" ") + message + QStringLiteral("\n");
    *g_stream << line;
    g_stream->flush();
}

} // namespace cr
