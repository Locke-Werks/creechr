// app_snark — creechr's running commentary on the apps you have open.
//
// returns lines that real users of those apps would actually find
// funny. detection is by foreground process basename. if no specific
// lines exist for the foreground app, returns empty and the caller
// falls back to generic commentary.
//
// the snark table is loaded at startup from snark.json. lookup order:
//   1. $CREECHR_SNARK_FILE env var (explicit override)
//   2. %LOCALAPPDATA%\creechr\creechr\snark.json (user-editable copy)
//   3. <exe dir>\snark.json (the shipped default)
// if none exists or all fail to parse, the loader falls back to an
// empty table and everyone uses generic lines.
//
// the shipped default in assets/snark.json is copied to %LOCALAPPDATA%
// on first run (if the user copy doesnt already exist), so you can
// edit the local one without losing your changes on the next rebuild.
#pragma once

#include <QString>

namespace cr {

// one-time load at startup. safe to call multiple times — only the
// first has an effect.
void initSnarkTable();

// returns the current foreground process's basename in lowercase
// ("chrome.exe", "code.exe", etc), or empty if it can't be determined.
QString currentForegroundApp();

// returns a random snark line for the named app (lowercase basename),
// or empty if no lines are defined for that app.
QString snarkLineFor(const QString& appBasename);

// convenience: snark for the current foreground. empty when no
// foreground or when the foreground app has no entry in the table.
QString currentAppSnark();

} // namespace cr
