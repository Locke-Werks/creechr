#include "heist/hoard.h"
#include "util/logging.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace cr {

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
        arr.append(o);
    }
    QJsonObject root;
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
        // restore lambda is not persisted — orphan entries from a previous
        // run can't be auto-restored after a crash because we no longer
        // hold the hwnd. logged for the user to know about.
        m_entries.push_back(std::move(e));
    }
}

void Hoard::persist()
{
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
            "hoard: found %1 orphan entries from previous run — these "
            "are dead, restore lambdas can't be reconstructed. clearing.")
            .arg(m_entries.size()));
        // we can't actually restore them because we don't have the live
        // HWNDs. log it and move on. user might have lost a window. sorry.
        m_entries.clear();
        persist();
    }
}

} // namespace cr
