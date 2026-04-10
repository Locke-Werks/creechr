// Hoard — the registry of everything creechr has currently stolen.
//
// every entry knows how to UNDO itself (restore the original window,
// kill the occluder, whatever). on app exit, hoard.restoreAll() runs
// every entry's restore callback. that's our last line of defense
// against leaving a user's window hidden when we crash. priority one.
//
// also persisted to %LOCALAPPDATA%\creechr\creechr\hoard.json so that
// if creechr dies hard between launches, the next launch can find the
// orphan entries and try to restore them. spec §5.1.
#pragma once

#include <QJsonObject>
#include <QPixmap>
#include <QPoint>
#include <QString>
#include <QVector>
#include <functional>

#ifdef _WIN32
#  include <windows.h>
typedef HWND CrHwnd;
#else
typedef void* CrHwnd;
#endif

namespace cr {

enum class HoardKind {
    Window,
    Cursor,
    Uia,
    Dom,
};

struct HoardEntry {
    HoardKind kind;
    QString id;        // unique id for this entry
    QString label;     // human-readable for logging
    QPixmap pixmap;    // captured bitmap, drawn while carried/stashed
    QPoint stashPos;   // current position on screen (top-left)
    QPoint originPos; // where to put it back, top-left of original frame
    qint64 stolenAtMs = 0;
    qint64 returnAfterMs = 0;

    // for window heists
    CrHwnd hwnd = nullptr;

    // restore callback. invoked exactly once. set to whatever undoes
    // this particular theft (ShowWindow + SetWindowPos for windows,
    // SetCursorPos for cursor, etc.). nullable — restoreAll skips
    // entries with no callback (and yells in the log).
    std::function<void()> restore;
};

class Hoard
{
public:
    Hoard();

    // add a new entry. returns the id (echoes entry.id, or generates one
    // if entry.id is empty).
    QString add(HoardEntry entry);

    // restore one entry by id and remove it. returns true if found.
    bool restoreById(const QString& id);

    // restore everything in the hoard, in reverse insertion order, and
    // clear it. called on shutdown and on emergency cleanup.
    void restoreAll();

    int size() const { return m_entries.size(); }

    // search
    HoardEntry* find(const QString& id);
    const QVector<HoardEntry>& entries() const { return m_entries; }

    // serialize/deserialize. only the metadata fields make it into json
    // — the QPixmap and the restore lambda are runtime-only. on next
    // boot we read the json and try to restore by hwnd / class / title.
    QJsonObject toJson() const;
    void loadFromJson(const QJsonObject& obj);

    // path on disk to the hoard file
    static QString hoardPath();
    void persist();
    void loadFromDisk();

private:
    QVector<HoardEntry> m_entries;
    int m_nextId = 1;
};

} // namespace cr
