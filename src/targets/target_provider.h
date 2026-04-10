// abstract base for anything that can give creechr a list of stealable
// things. window target provider, cursor target provider, eventually
// uia target provider, eventually browser target provider.
//
// each derived class scans on its own schedule and answers
// pickRandom() with a snapshot in its own native target type. the
// orchestrator (in CreechrApp) decides which provider to ask.
#pragma once

#include <QRect>
#include <QString>

#ifdef _WIN32
#  include <windows.h>
typedef HWND CrHwnd;
#else
typedef void* CrHwnd;
#endif

namespace cr {

enum class TargetKind {
    Window,
    Cursor,
    UiaElement,
    DomElement,
};

// for non-window targets that need an opaque string id (dom targets
// use this — the extension assigns "creechr-N" identifiers per scan).
// window heists ignore it; uia/cursor heists ignore it.

// abstract description of any heist target. concrete providers populate
// the relevant subset of fields. the heist state machine reads these and
// figures out what to do — for kind=Window it'll capture the hwnd; for
// kind=Cursor it'll just need the rect.
struct HeistTarget {
    TargetKind kind = TargetKind::Window;
    QRect screenRect;          // where the target lives, in logical px
    CrHwnd hwnd = nullptr;     // for Window targets
    QString opaqueId;          // for DomElement targets ("creechr-N")
    QString label;             // human readable, for logging
};

} // namespace cr
