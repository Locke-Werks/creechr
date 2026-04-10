#include "util/app_snark.h"

#include <QHash>
#include <QRandomGenerator>
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

// the table is built once at first access. all keys are lowercase
// process basenames. lines are picked uniformly at random.
struct SnarkTable {
    QHash<QString, QStringList> table;
    SnarkTable()
    {
        // === browsers ===
        table[QStringLiteral("chrome.exe")] = {
            QStringLiteral("20 tabs huh"),
            QStringLiteral("the ram is going great"),
            QStringLiteral("right click + inspect, you nerd"),
            QStringLiteral("ctrl+f5 is your friend"),
            QStringLiteral("what was that one tab"),
            QStringLiteral("ublock please"),
            QStringLiteral("close some tabs"),
            QStringLiteral("about:memory"),
            QStringLiteral("the cursor IS the cursor"),
            QStringLiteral("ctrl+shift+t"),
        };
        table[QStringLiteral("msedge.exe")] = {
            QStringLiteral("edge? really?"),
            QStringLiteral("yes microsoft, im sure"),
            QStringLiteral("right click + inspect anyway"),
            QStringLiteral("this isnt explorer fyi"),
            QStringLiteral("the bing button"),
            QStringLiteral("did you mean chrome"),
            QStringLiteral("import bookmarks no"),
            QStringLiteral("set as default no"),
        };
        table[QStringLiteral("firefox.exe")] = {
            QStringLiteral("about:config nerd"),
            QStringLiteral("containers and you"),
            QStringLiteral("the bookmarks toolbar"),
            QStringLiteral("ctrl+shift+i"),
            QStringLiteral("multi-account containers"),
        };

        // === editors ===
        table[QStringLiteral("code.exe")] = {
            QStringLiteral("ship it"),
            QStringLiteral("git blame yourself"),
            QStringLiteral("you missed a semicolon"),
            QStringLiteral("tabs vs spaces, again"),
            QStringLiteral("the indent is wrong"),
            QStringLiteral("rename: rename"),
            QStringLiteral("intellisense is wrong"),
            QStringLiteral("format on save"),
            QStringLiteral("ctrl+p, you weirdo"),
            QStringLiteral("the gitignore"),
            QStringLiteral("undo undo undo"),
        };
        table[QStringLiteral("devenv.exe")] = {
            QStringLiteral("visual studio is loading"),
            QStringLiteral("still loading"),
            QStringLiteral("actually loading this time"),
            QStringLiteral("rebuild solution"),
            QStringLiteral("dont press f5"),
            QStringLiteral("the find dialog is hidden again"),
            QStringLiteral("nuget restore"),
            QStringLiteral("clean rebuild it"),
        };
        table[QStringLiteral("notepad.exe")] = {
            QStringLiteral("you're using NOTEPAD"),
            QStringLiteral("ctrl+s. now."),
            QStringLiteral("save it somewhere weird"),
            QStringLiteral("no syntax highlighting era"),
            QStringLiteral("this isnt vim"),
            QStringLiteral("notepad++ exists you know"),
        };
        table[QStringLiteral("notepad++.exe")] = {
            QStringLiteral("ah, the npp diehards"),
            QStringLiteral("regex find/replace"),
            QStringLiteral("save the encoding"),
        };
        table[QStringLiteral("sublime_text.exe")] = {
            QStringLiteral("the unregistered banner"),
            QStringLiteral("ctrl+p"),
            QStringLiteral("multi cursor go brr"),
        };
        table[QStringLiteral("idea64.exe")] = {
            QStringLiteral("indexing"),
            QStringLiteral("still indexing"),
            QStringLiteral("the inspections are right"),
            QStringLiteral("invalidate caches and restart"),
        };

        // === chat / collab ===
        table[QStringLiteral("slack.exe")] = {
            QStringLiteral("two more meetings"),
            QStringLiteral("type something"),
            QStringLiteral("the unread count haunts me"),
            QStringLiteral("huddle no"),
            QStringLiteral("react with eyes"),
            QStringLiteral("this could have been an email"),
            QStringLiteral("set a status"),
            QStringLiteral("yikes"),
            QStringLiteral("the threads"),
        };
        table[QStringLiteral("discord.exe")] = {
            QStringLiteral("five servers, zero friends"),
            QStringLiteral("they're typing"),
            QStringLiteral("react with sob"),
            QStringLiteral("dont read the general"),
            QStringLiteral("the ping is unread"),
            QStringLiteral("voice chat awaits"),
            QStringLiteral("nitro?"),
        };
        table[QStringLiteral("teams.exe")] = {
            QStringLiteral("youre on mute"),
            QStringLiteral("no youre still on mute"),
            QStringLiteral("leave the call"),
            QStringLiteral("you can present now"),
            QStringLiteral("your camera is off thank god"),
            QStringLiteral("teams is loading"),
            QStringLiteral("ms teams update"),
        };
        table[QStringLiteral("ms-teams.exe")] = table[QStringLiteral("teams.exe")];

        // === office ===
        table[QStringLiteral("excel.exe")] = {
            QStringLiteral("VLOOKUP nightmare"),
            QStringLiteral("the merged cells"),
            QStringLiteral("save as .csv"),
            QStringLiteral("ctrl+shift+enter"),
            QStringLiteral("the sheet is named Sheet1"),
            QStringLiteral("autofill broke it"),
            QStringLiteral("=SUM(A:A)"),
        };
        table[QStringLiteral("winword.exe")] = {
            QStringLiteral("the styles are broken"),
            QStringLiteral("track changes is on. again."),
            QStringLiteral("stop using comic sans"),
            QStringLiteral("ctrl+z does nothing now"),
            QStringLiteral("the autocorrect"),
            QStringLiteral("section break"),
        };
        table[QStringLiteral("outlook.exe")] = {
            QStringLiteral("342 unread"),
            QStringLiteral("delete it"),
            QStringLiteral("decline this meeting"),
            QStringLiteral("out of office: lying"),
            QStringLiteral("the mailbox is full"),
            QStringLiteral("flag for follow up"),
            QStringLiteral("reply all? no."),
        };
        table[QStringLiteral("powerpnt.exe")] = {
            QStringLiteral("the font isnt installed on the projector"),
            QStringLiteral("smart art is not smart"),
            QStringLiteral("transition: dissolve"),
            QStringLiteral("speaker notes lol"),
        };

        // === terminals ===
        table[QStringLiteral("cmd.exe")] = {
            QStringLiteral("cmd? in 2026?"),
            QStringLiteral("did you mean powershell"),
            QStringLiteral("doskey isnt a fix"),
            QStringLiteral("alt-enter, you primitive"),
            QStringLiteral("dir /s"),
        };
        table[QStringLiteral("powershell.exe")] = {
            QStringLiteral("Get-Verb why"),
            QStringLiteral("set-executionpolicy bypass"),
            QStringLiteral("the verbs are too long"),
            QStringLiteral("ctrl+c is also Get-Clipboard"),
            QStringLiteral("powershell ise nostalgia"),
        };
        table[QStringLiteral("pwsh.exe")] = {
            QStringLiteral("core, fancy"),
            QStringLiteral("Get-Verb why"),
            QStringLiteral("a profile.ps1, eh"),
            QStringLiteral("the new ctrl+c"),
        };
        table[QStringLiteral("windowsterminal.exe")] = {
            QStringLiteral("settings.json again"),
            QStringLiteral("another tab? in the terminal?"),
            QStringLiteral("powerline too far"),
            QStringLiteral("alt+enter please"),
            QStringLiteral("the color scheme is wrong"),
        };
        table[QStringLiteral("wt.exe")] = table[QStringLiteral("windowsterminal.exe")];
        table[QStringLiteral("alacritty.exe")] = {
            QStringLiteral("yaml config nerd"),
            QStringLiteral("oh youre a TWM person"),
        };
        table[QStringLiteral("git-bash.exe")] = {
            QStringLiteral("git status. again."),
            QStringLiteral("git diff yourself"),
            QStringLiteral("git stash and pray"),
            QStringLiteral("rebase no"),
        };

        // === files / shell ===
        table[QStringLiteral("explorer.exe")] = {
            QStringLiteral("where did you put it"),
            QStringLiteral("search wont find it"),
            QStringLiteral("ah yes, downloads/temp/temp2/final"),
            QStringLiteral("untitled.txt"),
            QStringLiteral("the address bar is a path"),
            QStringLiteral("desktop is full"),
            QStringLiteral("recycle the recycle bin"),
        };

        // === media ===
        table[QStringLiteral("spotify.exe")] = {
            QStringLiteral("skip"),
            QStringLiteral("this song again?"),
            QStringLiteral("change the playlist"),
            QStringLiteral("shuffle is lying"),
            QStringLiteral("private session, eh?"),
            QStringLiteral("the wrapped is coming"),
        };
        table[QStringLiteral("vlc.exe")] = {
            QStringLiteral("traffic cone enthusiast"),
            QStringLiteral("subtitles offset by 0.4s"),
            QStringLiteral("press . to step frame"),
        };

        // === creative ===
        table[QStringLiteral("figma.exe")] = {
            QStringLiteral("another component variant"),
            QStringLiteral("auto-layout is fighting you"),
            QStringLiteral("frame this. then frame the frame."),
            QStringLiteral("the constraints are wrong"),
            QStringLiteral("export at 2x"),
        };
        table[QStringLiteral("photoshop.exe")] = {
            QStringLiteral("merge the layers"),
            QStringLiteral("undo. undo. UNDO."),
            QStringLiteral("the brush is wrong"),
            QStringLiteral("save as psd. always."),
        };
        table[QStringLiteral("blender.exe")] = {
            QStringLiteral("ctrl+s ctrl+s ctrl+s"),
            QStringLiteral("the donut tutorial again"),
            QStringLiteral("number pad shortcuts"),
        };
        table[QStringLiteral("obs64.exe")] = {
            QStringLiteral("youre still recording"),
            QStringLiteral("scenes and you"),
            QStringLiteral("audio levels are red"),
        };

        // === note-taking ===
        table[QStringLiteral("obsidian.exe")] = {
            QStringLiteral("another note"),
            QStringLiteral("the graph view scares me"),
            QStringLiteral("organizing your notes, not writing"),
            QStringLiteral("dataview yourself"),
            QStringLiteral("a daily note"),
        };
        table[QStringLiteral("notion.exe")] = {
            QStringLiteral("a database in a database"),
            QStringLiteral("its loading"),
            QStringLiteral("synced blocks"),
        };

        // === games ===
        table[QStringLiteral("steam.exe")] = {
            QStringLiteral("update available"),
            QStringLiteral("the wishlist"),
            QStringLiteral("verifying integrity of game files"),
        };
    }
};

const SnarkTable& getTable()
{
    static const SnarkTable t;
    return t;
}

} // namespace

QString snarkLineFor(const QString& appBasename)
{
    if (appBasename.isEmpty()) return {};
    const auto& table = getTable().table;
    auto it = table.constFind(appBasename);
    if (it == table.constEnd() || it.value().isEmpty()) return {};
    const QStringList& lines = it.value();
    return lines[QRandomGenerator::global()->bounded(lines.size())];
}

QString currentAppSnark()
{
    return snarkLineFor(currentForegroundApp());
}

} // namespace cr
