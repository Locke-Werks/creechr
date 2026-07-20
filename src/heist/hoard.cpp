#include "heist/hoard.h"
#include "util/crash_guard.h"
#include "util/logging.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <string>

namespace cr {

#ifdef _WIN32
namespace {
// EnumWindows hunt for a HIDDEN top-level window with an exact class
// and exact non-empty title. deliberately not reusing WindowEnumerator:
// that one filters to visible windows, and here the whole point is
// that the window we lost is invisible.
struct OrphanSearch {
    const wchar_t* cls = nullptr;
    const wchar_t* title = nullptr;
    HWND found = nullptr;
};

BOOL CALLBACK orphanEnumProc(HWND hwnd, LPARAM lp)
{
    auto* s = reinterpret_cast<OrphanSearch*>(lp);
    if (IsWindowVisible(hwnd)) return TRUE; // only hunting hidden ones
    wchar_t cls[256] = {};
    GetClassNameW(hwnd, cls, 256);
    if (wcscmp(cls, s->cls) != 0) return TRUE;
    wchar_t title[512] = {};
    GetWindowTextW(hwnd, title, 512);
    if (wcscmp(title, s->title) != 0) return TRUE;
    s->found = hwnd;
    return FALSE;
}
} // namespace
#endif

Hoard::Hoard() = default;

QString Hoard::hoardPath()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return base + QStringLiteral("/hoard.json");
}

QString Hoard::add(HoardEntry entry)
{
    if (entry.id.isEmpty()) {
        entry.id = QStringLiteral("h%1").arg(m_nextId++);
    }
    if (entry.stolenAtMs == 0) {
        entry.stolenAtMs = QDateTime::currentMSecsSinceEpoch();
    }
    LOG_INFO(QStringLiteral("hoard: add %1 (%2 entries total)").arg(entry.id).arg(m_entries.size() + 1));
    m_entries.push_back(std::move(entry));
    persist();
    return m_entries.last().id;
}

HoardEntry* Hoard::find(const QString& id)
{
    for (auto& e : m_entries) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

bool Hoard::restoreById(const QString& id)
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].id == id) {
            if (m_entries[i].restore) {
                LOG_INFO(QStringLiteral("hoard: restoring %1").arg(id));
                m_entries[i].restore();
            } else {
                LOG_WARN(QStringLiteral("hoard: %1 has no restore callback").arg(id));
            }
            m_entries.removeAt(i);
            persist();
            return true;
        }
    }
    return false;
}

void Hoard::restoreAll()
{
    LOG_INFO(QStringLiteral("hoard: restoreAll (%1 entries)").arg(m_entries.size()));
    // reverse order so the most-recent theft (probably visually layered
    // on top) gets restored first. matters less than i'm pretending.
    for (int i = m_entries.size() - 1; i >= 0; --i) {
        if (m_entries[i].restore) {
            try {
                m_entries[i].restore();
            } catch (...) {
                LOG_ERROR(QStringLiteral("hoard: restore threw, ignoring"));
            }
        }
    }
    m_entries.clear();
    persist();
}

QJsonObject Hoard::toJson() const
{
    QJsonArray arr;
    for (const auto& e : m_entries) {
        QJsonObject o;
        o["id"]    = e.id;
        o["kind"]  = static_cast<int>(e.kind);
        o["label"] = e.label;
        o["originX"] = e.originPos.x();
        o["originY"] = e.originPos.y();
        o["stashX"]  = e.stashPos.x();
        o["stashY"]  = e.stashPos.y();
        o["stolenAtMs"]    = QString::number(e.stolenAtMs);
        o["returnAfterMs"] = QString::number(e.returnAfterMs);
        // v2: enough identity to attempt a restore after a hard crash.
        // hwnd goes in as a stringified pointer value; next boot it
        // either still IsWindow()s (and class+pid confirm it) or the
        // class+title hunt takes over.
        o["hwnd"]   = QString::number(reinterpret_cast<quintptr>(e.hwnd));
        o["frameX"] = e.originFrame.x();
        o["frameY"] = e.originFrame.y();
        o["frameW"] = e.originFrame.width();
        o["frameH"] = e.originFrame.height();
        o["cls"]    = e.className;
        o["title"]  = e.title;
        o["pid"]    = static_cast<qint64>(e.pid);
        o["domId"]  = e.opaqueId;
        arr.append(o);
    }
    QJsonObject root;
    root["version"] = 2;
    root["entries"] = arr;
    return root;
}

void Hoard::loadFromJson(const QJsonObject& obj)
{
    m_entries.clear();
    const QJsonArray arr = obj.value("entries").toArray();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        HoardEntry e;
        e.id    = o.value("id").toString();
        e.kind  = static_cast<HoardKind>(o.value("kind").toInt());
        e.label = o.value("label").toString();
        e.originPos = QPoint(o.value("originX").toInt(), o.value("originY").toInt());
        e.stashPos  = QPoint(o.value("stashX").toInt(),  o.value("stashY").toInt());
        e.stolenAtMs    = o.value("stolenAtMs").toString().toLongLong();
        e.returnAfterMs = o.value("returnAfterMs").toString().toLongLong();
        // v2 identity fields. absent in v1 files, which leaves them
        // empty/zero and the orphan restore just logs a loss.
        e.hwnd = reinterpret_cast<CrHwnd>(
            static_cast<quintptr>(o.value("hwnd").toString().toULongLong()));
        e.originFrame = QRect(o.value("frameX").toInt(), o.value("frameY").toInt(),
                              o.value("frameW").toInt(), o.value("frameH").toInt());
        e.className = o.value("cls").toString();
        e.title     = o.value("title").toString();
        e.pid       = static_cast<quint32>(o.value("pid").toInteger());
        e.opaqueId  = o.value("domId").toString();
        m_entries.push_back(std::move(e));
    }
}

void Hoard::persist()
{
    // keep the crash guard's fixed table in lockstep with the entry
    // list. persist() already runs on every mutation, so this is the
    // one choke point and it cannot drift.
    {
        crashguard::GuardSlot guardTable[crashguard::kMaxSlots];
        int n = 0;
        for (const auto& e : m_entries) {
            if (e.kind != HoardKind::Window) continue;
            if (n >= crashguard::kMaxSlots) break;
            guardTable[n].hwnd = static_cast<void*>(e.hwnd);
            guardTable[n].frame = e.originFrame;
            ++n;
        }
        crashguard::syncSlots(guardTable, n);
    }
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
    QFile f(hoardPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        LOG_WARN(QStringLiteral("hoard: cant write %1").arg(hoardPath()));
        return;
    }
    f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Compact));
}

void Hoard::loadFromDisk()
{
    QFile f(hoardPath());
    if (!f.open(QIODevice::ReadOnly)) {
        return; // no hoard yet, that's fine
    }
    const auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return;
    loadFromJson(doc.object());
    if (!m_entries.isEmpty()) {
        LOG_WARN(QStringLiteral(
            "hoard: %1 orphan entries from a previous run. last time "
            "somebody died mid-heist. attempting restores.")
            .arg(m_entries.size()));
        attemptOrphanRestore();
    }
}

void Hoard::attemptOrphanRestore()
{
#ifdef _WIN32
    for (const auto& e : m_entries) {
        if (e.kind == HoardKind::Dom) {
            if (!e.opaqueId.isEmpty()) {
                m_pendingDomRestores.push_back(e.opaqueId);
                LOG_INFO(QStringLiteral("hoard: dom orphan %1 queued for the bridge")
                    .arg(e.opaqueId));
            }
            continue;
        }
        if (e.kind != HoardKind::Window) {
            continue; // cursor/uia thefts never modified anything
        }

        HWND h = e.hwnd;
        bool identityOk = false;
        if (h && IsWindow(h)) {
            // hwnd values recycle across sessions, so the handle alone
            // proves nothing. class + pid double check makes a false
            // positive effectively impossible.
            wchar_t cls[256] = {};
            GetClassNameW(h, cls, 256);
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            identityOk = (QString::fromWCharArray(cls) == e.className)
                      && (pid == e.pid) && e.pid != 0;
        }
        if (!identityOk && !e.className.isEmpty() && !e.title.isEmpty()) {
            // stale handle. hunt for an invisible top-level with the
            // exact class AND exact non-empty title. the non-empty
            // title requirement is what keeps this from SW_SHOWing one
            // of chrome's army of deliberately hidden windows.
            const std::wstring wcls = e.className.toStdWString();
            const std::wstring wtitle = e.title.toStdWString();
            OrphanSearch s;
            s.cls = wcls.c_str();
            s.title = wtitle.c_str();
            EnumWindows(orphanEnumProc, reinterpret_cast<LPARAM>(&s));
            if (s.found) {
                h = s.found;
                identityOk = true;
                LOG_INFO(QStringLiteral("hoard: orphan '%1' found by class+title hunt")
                    .arg(e.label));
            }
        }

        if (identityOk && h && !IsWindowVisible(h)) {
            if (e.originFrame.isValid()) {
                SetWindowPos(h, HWND_TOP,
                             e.originFrame.left(), e.originFrame.top(),
                             e.originFrame.width(), e.originFrame.height(),
                             SWP_NOACTIVATE | SWP_SHOWWINDOW);
            } else {
                // v1 entry: no stored frame, just un-hide it in place
                SetWindowPos(h, HWND_TOP, e.originPos.x(), e.originPos.y(), 0, 0,
                             SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            }
            LOG_INFO(QStringLiteral("hoard: restored orphan '%1'").arg(e.label));
        } else if (identityOk) {
            LOG_INFO(QStringLiteral("hoard: orphan '%1' is already visible, fine")
                .arg(e.label));
        } else {
            LOG_WARN(QStringLiteral("hoard: orphan '%1' is gone for good. sorry.")
                .arg(e.label));
        }
    }
#endif
    m_entries.clear();
    persist();
}

QStringList Hoard::takePendingDomRestores()
{
    QStringList out;
    out.swap(m_pendingDomRestores);
    return out;
}

} // namespace cr
