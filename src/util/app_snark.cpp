#include "util/app_snark.h"
#include "util/logging.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

#ifdef _WIN32
#  include <windows.h>
#  include <psapi.h>
#endif

namespace cr {

QString currentForegroundApp()
{
#ifdef _WIN32
    HWND fg = GetForegroundWindow();
    if (!fg) return {};
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (pid == 0) return {};
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return {};
    wchar_t path[MAX_PATH] = {};
    DWORD sz = MAX_PATH;
    QString result;
    if (QueryFullProcessImageNameW(h, 0, path, &sz)) {
        QString p = QString::fromWCharArray(path).toLower();
        const int slash = p.lastIndexOf('\\');
        result = (slash >= 0) ? p.mid(slash + 1) : p;
    }
    CloseHandle(h);
    return result;
#else
    return {};
#endif
}

namespace {

QHash<QString, QStringList> g_table;
bool g_loaded = false;

// try to load the table from the given json file. returns the number
// of apps loaded (0 on failure).
int tryLoadFromFile(const QString& path)
{
    QFile f(path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
        return 0;
    }
    const QByteArray bytes = f.readAll();
    f.close();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        LOG_WARN(QStringLiteral("app_snark: failed to parse %1: %2")
            .arg(path).arg(err.errorString()));
        return 0;
    }
    const QJsonObject root = doc.object();
    int loaded = 0;
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        const QString key = it.key();
        if (key.startsWith('_')) continue; // skip _comment etc
        if (!it.value().isArray()) continue;
        const QJsonArray arr = it.value().toArray();
        QStringList lines;
        lines.reserve(arr.size());
        for (const auto& v : arr) {
            if (v.isString()) lines.push_back(v.toString());
        }
        if (!lines.isEmpty()) {
            g_table.insert(key.toLower(), lines);
            ++loaded;
        }
    }
    LOG_INFO(QStringLiteral("app_snark: loaded %1 apps from %2").arg(loaded).arg(path));
    return loaded;
}

// copy the shipped default snark.json to the user-local location on
// first run so the user has a starting point to edit. doesnt overwrite
// an existing user copy.
void ensureUserCopy(const QString& userPath, const QString& shippedPath)
{
    if (QFile::exists(userPath)) return;
    if (!QFile::exists(shippedPath)) return;
    QDir().mkpath(QFileInfo(userPath).absolutePath());
    if (QFile::copy(shippedPath, userPath)) {
        LOG_INFO(QStringLiteral("app_snark: seeded %1 from %2").arg(userPath).arg(shippedPath));
    }
}

} // namespace

void initSnarkTable()
{
    if (g_loaded) return;
    g_loaded = true;

    // resolve the shipped default path (next to the exe, or in the
    // assets dir one level up for dev builds)
    const QString exeDir = QCoreApplication::applicationDirPath();
    QString shippedPath = exeDir + QStringLiteral("/snark.json");
    if (!QFile::exists(shippedPath)) {
        // dev build layout: build/creechr.exe next to build/, assets/ is
        // at repo root one directory up. try that.
        shippedPath = exeDir + QStringLiteral("/../assets/snark.json");
    }

    // user-editable copy in AppLocalData
    const QString userDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QString userPath = userDir + QStringLiteral("/snark.json");
    ensureUserCopy(userPath, shippedPath);

    // lookup order:
    //   1. CREECHR_SNARK_FILE env var
    //   2. %LOCALAPPDATA%\creechr\creechr\snark.json
    //   3. shipped default next to exe
    // first one that loads anything wins.
    const QByteArray envPath = qgetenv("CREECHR_SNARK_FILE");
    if (!envPath.isEmpty()) {
        if (tryLoadFromFile(QString::fromLocal8Bit(envPath)) > 0) return;
    }
    if (tryLoadFromFile(userPath) > 0) return;
    if (tryLoadFromFile(shippedPath) > 0) return;

    LOG_WARN(QStringLiteral("app_snark: no snark file found, table is empty. "
                            "creechr will only use generic idle lines."));
}

QString snarkLineFor(const QString& appBasename)
{
    if (appBasename.isEmpty()) return {};
    auto it = g_table.constFind(appBasename);
    if (it == g_table.constEnd() || it.value().isEmpty()) return {};
    const QStringList& lines = it.value();
    return lines[QRandomGenerator::global()->bounded(lines.size())];
}

QString currentAppSnark()
{
    return snarkLineFor(currentForegroundApp());
}

} // namespace cr
