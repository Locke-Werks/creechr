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

### v0.1 — he exists and he walks
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

### v0.2 — he steals small windows and the cursor
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

### v0.3 — he steals individual ui controls (best effort)
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

### v0.4 — he becomes a real animal ← you are here

everything between v0.3 and v1.0 thats not browser-related. this is
the version where creechr stops being a sprite-on-rails and starts
being something with body language.

#### anatomy + animation
- 48x48 sprite cell (was 32x32). real legs, real arms, a mouth that
  opens / closes / yawns / bites with visible teeth.
- 8-frame walk cycle with arm swing, 6-frame climbs, 6-frame grab,
  6-frame bite, 6-frame carry. idle has a 2-frame breathing bob plus
  a personality micro-behavior loop (blink / yawn / scratch / mutter)
  firing every 1.5-3.5 seconds during idle.
- speech bubbles. small white rounded rect with a tail, drawn above
  his head, anchored to the sprite. triggered by state transitions:
  "yoink" when grabbing, "om nom" when gnawing, "OW" / "DICK" when
  shaken off, "weee" when shooting a rappel line, "hi" / "back off"
  when the cursor gets close.

#### physics
- unified 60Hz tick (was 10Hz logic + 30Hz render on separate timers).
  movement is now visibly continuous instead of hopping in 6-pixel
  chunks. expensive stuff (EnumWindows, fullscreen check) is rate-
  limited to 100ms inside the tick.
- gravity-from-loss-of-support. if the window he was standing on
  closes, moves significantly, or the user yanks it out from under
  him, he detects the missing platform and falls into the flung
  state. when standing on a window thats moved a few pixels he RIDES
  it instead of dropping (8px tolerance).
- flung state: real gravity (1500 px/s^2), bounce restitution (0.42),
  friction on bounce (0.62). bounces off the screen edges. dust puff
  particles spawn on hard landings.
- mouse-hover noticing. if the cursor gets within 100 px of him while
  hes idling or walking, he turns to face it and says something. 5s
  cooldown so wiggling doesnt spam.

#### gnaw + shake-off
- new state path: walk -> approach_gnaw -> gnaw. 50% of climb decisions
  become gnaw decisions. when gnawing, creechr is LATCHED to the
  windows actual frame each tick — drag the window, drag him with it.
- shake detection: if the user shakes the window vigorously (4+
  direction reversals in 800ms with non-trivial magnitude), creechr is
  flung off with a velocity computed from the shake direction +
  intensity. he says something rude on the way out.

#### rappel
- new state path: walk -> shoot_rappel -> rappel_climb -> walk. fires
  when creechr finds a window whose top is well above his current floor
  AND whose x-range contains him. ~40% chance to take the rappel path.
- visible rope: 2px dark line drawn from his hands up to the anchor
  point, with a small grappling-hook square at the anchor end. drawn
  UNDER creechr so the rope visually comes out of his hands.
- rappel down too: 40% chance to rappel from a window-top to the
  floor instead of climbing the wall.

#### targets
- taskbar UIA scan. UiaTargetProvider now does TWO scans per pickRandom:
  the foreground window AND Shell_TrayWnd via FindWindowW. start menu
  icons / pinned apps / system tray buttons all become UIA heist
  targets. spec §8.4 cleared because explorer is user shell, not
  secure desktop, and the §6.2 deviation never modifies anything.
- per-monitor DPI fix. every win32 physical-rect to qt logical-rect
  conversion now divides by GetDpiForWindow(hwnd) instead of always
  the primary screen's devicePixelRatio. mixed-dpi multi-monitor
  setups are now correct (single-monitor users wont notice).

#### dev knobs
- `CREECHR_HEIST_NOW=1` — fire heists every ~2 seconds, ignore the
  random gate and the input-idle gate
- `CREECHR_GNAW_NOW=1` — every climb decision becomes a gnaw
- `CREECHR_RAPPEL_NOW=1` — every climb decision becomes a rappel
- `CREECHR_CHAOS=1` — ~30% of window heists skip the carry-pixmap
  shrink, restoring the legendary v0.x bug where creechr trundled
  across the screen with an entire 800px window held aloft. on purpose
  this time. the user laughed at it and said keep it.

### v1.0 — he steals stuff out of webpages (with the extension)

still **not tagged** because i havent verified the full pipeline against
a real chrome instance and i dont tag things i havent watched work. but
the plumbing is wired end-to-end now and structurally complete.

#### the pipeline

```
[chrome extension] ←native messaging→ [creechr-bridge.exe] ←named pipe→ [creechr.exe]
       content.js              4-byte len + json    \\.\pipe\creechr-extension
       background.js           on stdin/stdout      newline-delim json
```

- **chrome extension** in `extension/`. manifest v3, vanilla js, no
  build step. service worker connects to the native messaging host
  named `com.creechr.bridge` on demand. content script walks the dom
  for `<img>`, `<a href>`, `<button>`, `<li>`, gives each one a stable
  `data-creechr-id`, returns rect + label. on `steal` it removes the
  element and stashes parent + sibling on `window.__creechrStash`. on
  `restore` it puts it back exactly. opt-in per tab via popup.
- **creechr-bridge.exe** is a small pure-stdlib console binary that
  chrome spawns when the extension calls `connectNative()`. it reads
  the length-prefix native messaging frames from stdin and forwards
  them to creechr's named pipe. a worker thread reads the pipe and
  writes length-prefix frames back to chrome's stdout. _binary mode_
  on stdio is required — text mode would mangle the length prefix.
- **creechr.exe** runs an `ExtensionPipeServer` (QLocalServer wrapping
  a windows named pipe at `\\.\pipe\creechr-extension`, current-user
  only). last bridge connection wins. on top of that lives an
  `ExtensionTargetProvider` that auto-scans every 3 seconds while a
  bridge is connected and caches DOM target snapshots.
- **heist flow** for `TargetKind::DomElement`: BitBlt the rect for the
  carried pixmap, send `requestSteal(opaqueId)`, wait for `steal_ack`
  (with a 4-second hard timeout), then proceed through the same
  approach → grab → bite → carry → stash → wait → return state machine
  as window/uia heists. on return: `requestRestore(opaqueId)` from a
  hoard restore lambda, which the extension routes to the content
  script's `contentRestore()`.

#### installing it

once, after a clean build of both binaries:

```
cmake --build build
.\install_extension_host.ps1
```

then load the unpacked extension in chrome (`chrome://extensions` →
developer mode → load unpacked → pick the `extension/` directory).
chrome will assign the extension a long random id like
`pgkfajdljekloeoknobcdpfbgmldlbjk`. **copy that id**, then open
`creechr-bridge-host.json` (the install script wrote it next to the
ps1) and replace `__YOUR_EXTENSION_ID_HERE__` with `chrome-extension://<that id>/`.

yes this dance is annoying. yes its mv3s fault. there is no way to
register a native host that accepts an extension whose id you dont
know yet. production extensions ship a public key in the manifest so
the id is deterministic — i havent done that for the dev build because
it would mean either committing a private key or running through a
key generation step every install. neither is great.

after editing the host json, reload the extension once. then run
creechr.exe, open a tab, click the creechr popup, click "let him in".
you should see in `creechr.log`:

```
ExtensionPipeServer: bridge connected
ext provider: bridge connected, starting scans
ext provider: scan_result, N items cached
```

at which point the orchestrator can roll a dom heist (20% chance per
attempt) and creechr will pick a random `<img>` or `<a>` from the
opted-in tab and try to eat it.

#### remaining rough edges
- the chrome extension id editing dance (above)
- the content scripts rect math uses `screenX/Y` plus the page's
  `devicePixelRatio` divided by the OS dpr — right on single-monitor
  100%-scale boxes, approximate everywhere else
- if the page reflows between scan and steal, the captured pixmap is
  what was at that screen position at scan time, which may not be
  what's there at steal time (creechr will visually carry off whatever
  was at those coords)
- only one tab at a time. last opted-in wins.
- multi-tab tracking would mean the bridge needs to remember which tab
  context each native-port message came from, which it doesnt
- closing a tab mid-heist orphans the dom restore. the hoard entry
  still calls requestRestore on quit but the content script wont be
  there anymore — the element is just gone. nothing else breaks.

## known issues

- the placeholder sprite is procedurally drawn pink + black with white
  eyes. real art exists in my head. don't @ me.
- when creechr climbs onto a maximized window (top y = 0), his sprite
  ends up at y = -48 (above the screen) and qt clips him. you'll see
  his head pop above the top of the desktop. fix is to clamp climb
  destinations to y >= 0 but i havent.
- shake-off physics tuning is approximate. the velocity numbers in
  GnawState's shake-detect path were chosen by gut feel and may need
  adjusting once enough people actually shake windows around with him
  attached.
- if a stolen window's process exits while creechr is carrying its
  bitmap to the corner, the restore callback no-ops on a dead hwnd
  and we just drop the entry. visually he'll still walk it to the
  corner and "drop" it, then walk back to nothing. nbd, just looks
  silly.
- the dom heist coordinate math (extension content script) still uses
  the page's devicePixelRatio divided by the primary screen dpr. fine
  on single-monitor 100% boxes, approximate everywhere else. native
  win32 sources got the per-monitor fix in v0.4 but the extension
  side still needs the same treatment.
- right click menus from other apps sometimes draw on top of him. fine.
- heist orchestrator picks targets at random, no preference for things
  that look fun. you might watch him steal the same notepad window
  three times in a row. that's the rng working as designed, sorry.

## license

MIT. see LICENSE. do whatever, just don't blame me when your boss sees it.
