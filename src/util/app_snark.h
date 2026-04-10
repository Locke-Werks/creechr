// app_snark — creechr's running commentary on the apps you have open.
//
// returns lines that real users of those apps would actually find
// funny. detection is by foreground process basename. if no specific
// lines exist for the foreground app, returns empty and the caller
// falls back to generic commentary.
//
// the snark table lives in the cpp file. add new entries there as
// you find apps creechr should have opinions about.
#pragma once

#include <QString>

namespace cr {

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
