# creechr

a small monster that lives on your desktop and steals things.

## what

creechr is a desktop pet for windows. he walks along the bottom of your screen,
climbs up the edges of your windows, naps when you go idle, and on rare
occasions wanders over to something he likes the look of and **takes it.**

depending on which version you're running, "it" might be:

- a small floating window (calc, notepad, sticky notes — gone for a minute,
  then back where you left it)
- your mouse cursor (briefly, you'll get it back, probably)
- a button or a menu in some app (just the picture of it — the real one is
  still there, you just can't see it for a bit)
- an image or a link off a webpage (browser extension required, see below)

he always puts everything back. that's the whole bit. if he doesn't put
something back it's a bug and i want to hear about it.

## warning

- this is windows-only and probably will be forever. sorry.
- it is **experimental**. like, "compiled it last week" experimental.
- it does things that look like malware. transparent always-on-top window,
  enumerates other processes' windows, BitBlts other apps, walks the
  accessibility tree. your antivirus is going to have feelings about it.
- **do not run this on a work laptop.** seriously. your IT department will
  have a worse day than you.
- it pokes at the desktop, not at the secure desktop. it will not touch UAC
  prompts, login screens, password fields, or anything from system processes.
  i checked. multiple times. it still might trip an EDR though.

## building

assumes you already do windows c++ for fun (or punishment).

prereqs:
- visual studio 2022 or newer with the c++ workload
- qt 6.6+ (msvc 64-bit kit)
- cmake 3.25+
- a heart full of regret

```
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build
build/creechr.exe
```

if you don't have ninja, drop the `-G` and let cmake pick. it'll be slower
but it'll work.

## running him

double click `creechr.exe`. that's it. there's no installer. there's no
splash screen. there's a tray icon shaped like a small angry square — that's
him. right click for pause / quit.

if he gets stuck on the taskbar or starts climbing something he shouldn't,
right click the tray icon and pick quit. or kill him from task manager if
he's being a little shit. either way works.

## what does he actually do right now

(this section gets updated each release. things below the line aren't built
yet but are planned.)

### v0.1 — he exists and he walks ← you are here
- transparent click-through overlay covers the whole virtual desktop
- per-monitor v2 dpi awareness on the process so he doesn't get scaled
  out from under us by windows
- walks along the bottom of the desktop, bouncing off the screen edges
- picks a random nearby window every few seconds and walks over to it,
  climbs up the side, walks across the top, climbs back down
- minimized / cloaked / off-screen / shell windows are filtered out so
  he doesn't try to climb something that doesn't visually exist
- naps after 30 seconds of you not touching the mouse
- one twitch of the cursor wakes him up
- detects fullscreen games and presentations (via SHQueryUserNotificationState)
  and hides the overlay so he doesn't crash your movie night
- tray icon: right click for pause/resume and quit. pause genuinely
  stops the tick timers, not just no-ops them, before you ask.
- logs to %LOCALAPPDATA%\creechr\creechr\creechr.log (rotated at 1MB,
  3 files kept). set CREECHR_LOG_LEVEL=debug if you want the chatty stuff.

### v0.2 — he steals small windows and the cursor ← you are here
- HeistExecutor wired into creechr's main state machine as a sequence
  of states (heist_approach, heist_grab, heist_carry, heist_stash,
  heist_wait, heist_return). all 60s-bounded so a stuck heist can't
  zombie out forever.
- WindowTargetProvider scans the world enumerator's snapshot for small
  floating windows (80-600 px each axis, not maximized, not the shell,
  not consent.exe / logon ui). picks one at random.
- window heist flow: PrintWindow with PW_RENDERFULLCONTENT to capture
  the bitmap, ShowWindow SW_HIDE to make it disappear, walk to a random
  screen corner with the captured pixmap drawn next to creechr, drop it,
  wait 30-90s, walk back, SetWindowPos to put it back exactly where it
  was. registered in the hoard the entire time.
- cursor heists: SetCursorPos to drag the mouse along with creechr's
  mouth offset for ~1.5s, then put it back. clamped to virtual desktop
  bounds. aborts immediately if any mouse button is pressed mid-carry.
- input gating: heists only START if GetLastInputInfo says you've been
  idle for >5s, and ABORT mid-carry if you start clicking again.
- Hoard persists to %LOCALAPPDATA%\creechr\creechr\hoard.json. orphan
  entries from a hard crash get logged + cleared on next boot (we cant
  reconstruct restore lambdas across processes — sorry).
- quitGracefully always calls Hoard::restoreAll() before exit. if you
  ever lose a window to creechr permanently, that's a bug, file it.

### v0.3 — he steals individual ui controls (best effort) ← you are here
- UiaTargetProvider does CoInitializeEx + CoCreateInstance(CUIAutomation)
  on the main thread (NOT on a worker thread per spec — see below).
  walks the foreground window's UIA descendants every time the
  orchestrator picks a uia heist, filters to ControlType in
  {button, hyperlink, image, menuitem, listitem} AND IsOffscreen=false.
- the heist orchestrator now picks: 50% window heist, 30% uia heist,
  20% cursor heist (cursor is the always-available fallback if uia/window
  picks come back empty).
- uia heist flow piggybacks on the existing heist state machine.
  HeistGrab handles TargetKind::UiaElement by BitBlt-capturing the
  element's screen rect and stashing the pixmap on the heist context.
  the rest of the flow (carry, stash, return) is the same as window
  heists. nothing in the source app gets touched at any point.
- known to find buttons in notepad and chrome dev mode pages. the chrome
  case is a coin flip per the usual UIA-on-chromium reliability story.

#### v0.3 spec deviations (read these)
- **UIA on the main thread, not a worker thread.** spec §6.1 says all
  UIA work has to happen on a dedicated COM-init worker thread because
  UIA is slow and would stutter the render loop. this version cuts that
  corner because the demo target sizes (notepad, simple pages) are small
  enough that the scan completes in single-digit milliseconds. if you
  see a creechr stutter when the orchestrator picks a uia target, this
  is the first thing to refactor.
- **no occluder window class.** spec §6.2 wants a separate transparent
  always-on-top occluder per heist that draws an opaque background-color
  rect over the original element so it visually disappears. this version
  just BitBlt-captures the rect and has creechr carry the visual duplicate
  off to a corner. the original element is never touched. less dramatic,
  way safer (zero risk of orphaning an occluder window over a user app).
  add the occluder back in v0.4 once the rest of the rough edges are sanded.

### v1.0 — he steals stuff out of webpages (with the extension) ← in progress

WARNING: v1.0 is **not tagged** yet. the plumbing is in place; the
runtime forwarding isn't. specifically:

#### what's in the repo
- `extension/` — manifest v3 chrome / edge extension. vanilla js, no
  build step. service worker that connects to a native messaging host
  named `com.creechr.bridge`. content script that walks the dom for
  `<img>`, `<a href>`, `<button>`, `<li>`, gives each one a stable id,
  and exposes scan/steal/restore functions. opt-in per tab via popup.
- `install_extension_host.ps1` — registers the host in HKCU under
  chrome and edge `NativeMessagingHosts`, pointing at a host json that
  points at `build/creechr-bridge.exe`. you'll need to edit the
  `allowed_origins` field with your real extension id after loading
  the unpacked extension. yes that's annoying. yes it's MV3's fault.
- `uninstall_extension_host.ps1` — undoes the above.
- `creechr-bridge.exe` — second cmake target. console binary, pure
  stdlib (no Qt), reads 4-byte length-prefix json messages on stdin,
  writes the same on stdout. _binary mode_ on stdin/stdout because text
  mode would translate CRLF inside the length prefix and corrupt every
  message. logs every message to bridge.log.

#### what's MISSING for v1.0 to actually work
- the bridge currently REPLIES with a static `{"type":"ack",...}` to
  every message instead of forwarding to the always-on creechr.exe.
- there is no local named pipe (or http loopback, or shared memory,
  or anything) between the bridge and the main pet. that's the next
  thing to land. spec §7.1 wants a named pipe; either works.
- there's no `ExtensionTargetProvider` on the c++ side yet, so even
  with a working bridge the heist orchestrator wouldn't ask for dom
  targets. easy add once the pipe exists.
- the extension's `allowed_origins` host json field has a placeholder
  extension id (`__YOUR_EXTENSION_ID_HERE__`) that needs hand-editing.
  unavoidable: chrome generates the id from the extension's public
  key on first load.

#### what works end-to-end RIGHT NOW
- you can build creechr.exe + creechr-bridge.exe
- you can register the host with the powershell script
- you can load the unpacked extension in chrome dev mode
- you can opt a tab in via the popup
- the background script will connect to com.creechr.bridge
- the bridge will log incoming messages and reply with an ack
- the dom side (scan, steal, restore) all run inside content.js when
  asked, but no one's currently asking
- nothing creechr-side reacts to any of it

## known issues

- multi-monitor with mixed dpi scaling is a war zone. WindowEnumerator
  divides DWM physical pixels by the PRIMARY screen's devicePixelRatio,
  which is wrong if your secondary monitor has a different scale. on a
  single-monitor box it just works. fixing this properly is a v0.2-or
  -later thing because it'll need per-monitor lookups via MonitorFromWindow.
- the placeholder sprite is a 32x32 magenta blob with eyes. real art
  exists in my head. don't @ me.
- when creechr climbs a window that's positioned at y < 32 (eg. a
  maximized window), the sprite renders with negative y coords and qt
  clips it. you'll see his head pop above the screen. fix is to clamp
  the climb destination to >= 0.
- idle behaviors (blink, yawn, look around) STILL not implemented as
  of v0.2. he just stands there during idle. it makes him look
  constipated. allegedly v0.3.
- heist orchestrator picks targets at random, no preference for things
  that look fun. you might watch him steal the same calculator window
  three times in a row. that's the rng working as designed, sorry.
- if a stolen window's process exits while creechr is carrying its
  bitmap to the corner, the restore callback no-ops on a dead hwnd
  and we just drop the entry. visually he'll still walk it to the
  corner and "drop" it, then walk back to nothing. nbd, just looks
  silly.
- right click menus from other apps sometimes draw on top of him. fine.

## license

MIT. see LICENSE. do whatever, just don't blame me when your boss sees it.
