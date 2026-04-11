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

### v0.4 — he becomes a real animal

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

### v1.0 — he's a whole animal now ← you are here

everything that makes creechr feel like a creature with weight and
opinions instead of a sprite that slides on rails. v0.4 gave him a
body and physics. v1.0 makes him use them.

#### proper pendulum rappel
- rappel DOWN now anchors at the window corner he jumped off, does a
  brief free-fall, then swings on a real nonlinear pendulum with
  damping + wall bouncing. two-phase state machine inside the state.
- rappel UP picks a random CORNER of the target window (left or right)
  instead of always the top dead center, so the rope starts diagonal
  and he visibly swings while pulling himself up.
- rappel up PASSES THROUGH window edges during the climb (user ask —
  the old wall-bounce flip-flop looked like the rope was a fuse).
  lands INSIDE the target window instead of straddling the corner,
  so hasPlatformUnder accepts the landing and he doesnt immediately
  fall off.
- visible rope has a white halo pass under the dark rope so it shows
  up on dark-mode wallpapers.

#### gnaw + attach + shake off
- walks up to a window edge, latches on, chews on it for a few
  seconds. while latched hes literally tracking that hwnd's DWM
  frame every tick — drag the window around the desktop, he moves
  with it. see it catch on a snap-edge, see him slide sideways with
  it.
- shake detection: 4+ x-velocity sign reversals in an 800ms sliding
  window with nontrivial magnitude = SHAKEN OFF. computes a fling
  velocity from the shake direction and intensity, transitions to
  flung, he says one of "OW", "DICK", "fuck", "HEY", "rude" on the
  way out.

#### personality + speech
- IdleState fires a micro-behavior every 1.5-3.5s: blink, yawn,
  scratch, or a 10% chance to skip the animation and just speak an
  app-specific snark line about whatever's in the foreground.
- speech bubbles at every state transition worth narrating:
  - heist approach: "ooh" / "i want that one" / "sneaky time"
  - heist grab: "yoink" / "MINE" / "gotcha"
  - gnaw start: "om nom" / "delicious" / "this is mine now"
  - flung (shake-off): "OW" / "DICK" / "rude"
  - rappel shoot: "grapple out" / "hook ho" / "INCOMING"
  - cursor swing kick: "more!" / "harder" / "weeeee" / "yes"
  - heist toss: "bored" / "nah" / "bye"
  - mouse-hover notice: "hi" / "back off" / "personal space"
  - window drag notice: "HEY" / "oh no you dont" / "WHERE"
  - scary admin: "NO" / "EVIL" / "not the registry"
- pounces at the cursor like a cat. 1-in-20 chance during idle micro-
  behaviors — computes a direction from creechr to the cursor, lobs
  himself in that direction with an upward arc, lands in flung. "RAH"

#### cursor swing on idle (replaces nap)
- when msSinceLastInput > 30000, creechr finishes whatever he's
  currently doing first (walk completes, current scratch finishes),
  THEN shoots his grapple at the mouse cursor and rappels UP to swing
  height at the target rope length. proper approach animation with
  the grab pose, then climb pose, then hang pose at the top. not a
  teleport.
- at swing height he kicks himself going with a speech bubble. real
  low-damping pendulum physics so the swing persists. every ~2
  seconds when the amplitude decays, he auto-kicks with another
  speech bubble — "more!", "again!", "push!", "kick", "yes".
- the moment you touch the mouse or the cursor drifts more than 8 px,
  he RELEASES with his current tangential velocity (so the release
  feels continuous) and transitions to flung.

#### toss + sink
- heist wait timer shortened to 6-16s (was 30-90s — felt like
  abduction). when the boredom timer fires, creechr does a brief toss
  animation, speaks "bored" / "nah" / "meh" / "bye", and the carried
  pixmap becomes a sinking item that drifts downward under slow
  gravity (85 px/s^2) until it crosses the screen bottom.
- when the sinking item crosses the bottom edge, if it was a WINDOW
  heist, the hoard restore callback fires and the original window
  reappears at its original location. for UIA/DOM/cursor the source
  was never modified so the restore is a no-op or just clears the
  hoard entry.
- creechr clears his heist context the moment he tosses, so the
  orchestrator can queue the next heist immediately. the sinking
  item keeps going independently.
- trophy nest: successful heist returns ALSO add a small trophy
  pixmap to a pile in the bottom-right corner for the rest of the
  session. capped at 8, oldest dropped.

#### occlusion filter
- creechr only sees and interacts with windows the user can actually
  see. post-processing pass over EnumWindows results (topmost first)
  tests 3 probe points along each window's top edge against all
  higher-z windows. if all 3 are covered, the window is filtered out
  of the snapshot. no more rappelling to a terminal that's behind
  your browser.

#### reactions
- window drag noticing: if any visible window moves fast (|Δ| >= 35
  px between 10Hz world refreshes) and is within 600 px of creechr
  while he's idling or walking, he turns to face it and yells "HEY"
  / "wait" / "come back" / "rude". 4s cooldown.
- scary admin flee: taskmgr, regedit, mmc, msconfig, services,
  certmgr, perfmon, eventvwr, compmgmt, consent, logonui, winlogon,
  windows whose title starts with "Administrator:", and the
  credential dialog xaml host class — if one of those is within 350
  px, creechr panics ("NO" / "EVIL" / "RUN" / "ABORT" / "DANGER" /
  "not the registry"), gets flee velocity in the opposite direction,
  and transitions to flung. 6s cooldown. NOTE: the actual UAC
  consent.exe runs on the secure desktop and can't be seen from our
  process — creechr reacts to what typically precedes UAC rather
  than UAC itself.
- cursor hover noticing: cursor within 100 px of creechr's sprite
  center while he's idling or walking → he turns to face it and says
  "hi" / "back off" / "personal space" / "rude". 5s cooldown.

#### app-specific snark from JSON
- all snark lines live in `assets/snark.json`, a flat object mapping
  lowercase process basenames to arrays of one-liners. on first run,
  creechr copies this file to `%LOCALAPPDATA%\creechr\creechr\snark.json`
  and loads from there on every subsequent launch. edit it freely —
  no rebuild needed.
- initial seed covers 143 apps with ~750 lines of snark calibrated
  to be funny to ACTUAL USERS of each app — "VLOOKUP nightmare" for
  excel, "git blame yourself" for vs code, "youre on mute" for
  teams, "still compiling shaders" for unreal, "q4_K_M is fine
  promise" for lm studio.
- lookup order: `$CREECHR_SNARK_FILE` env var → user-local copy →
  shipped default next to the exe.

#### dark mode visibility
- arms, legs, and the grapple rope all get a 4 px white halo pass
  under the 2 px dark ink pass. shows up on light and dark
  wallpapers equally. same trick comic book artists use.

#### misc polish
- walk speed variety: 60% normal stroll (60 px/sec), 25% slow amble
  (38), 15% hurried scurry (105). picked per-session, breaks the
  metronomic back-and-forth.
- shadow ellipse under his feet so he doesnt look like he's
  hovering an inch above the surface. suppressed during flung /
  rappel_climb / rappel_descend.
- particle effects: dust puffs on hard landings, body-color splash
  on heist bite, dark puff at the grapple hook point when it bites
  in, small kchink at the rappel anchor.
- cursor drag was leaving the cursor behind on high-DPI displays
  because SetCursorPos takes physical pixels while creechr's
  position is in logical pixels. fixed via primaryScreen dpr
  multiplication.
- tray menu now has: pause/resume, fire a heist now, release
  everything he's stolen, open log folder, quit.
- CREECHR_COLOR env var picks body color: pink / teal / lime /
  orange / sky / violet / blood / moss or a `#rrggbb` hex.
- CREECHR_SPRITE=path/to/atlas.png overrides the procedural sprite.
  expected layout: 8 cols × 15 rows of 48px cells (384×720 total).

#### browser theft (structurally complete, chrome verification punted to v1.1)

the full pipe → bridge → ExtensionTargetProvider → DOM heist path is
wired end-to-end and compiles clean. i have NOT verified it against
a real chrome instance because i didnt have chrome on the build box
at tag time. if you want DOM heists to fire you can do the install
dance below; if it works, great; if it doesnt, file it against v1.1
and i'll fix it.

```
[chrome extension] ←native messaging→ [creechr-bridge.exe] ←named pipe→ [creechr.exe]
       content.js              4-byte len + json    \\.\pipe\creechr-extension
       background.js           on stdin/stdout      newline-delim json
```

install:
1. `cmake --build build` (gets you both binaries)
2. `.\install_extension_host.ps1` (writes the HKCU native host keys)
3. load `extension/` unpacked in chrome: `chrome://extensions` →
   developer mode → load unpacked
4. **copy the extension id** chrome assigns it
5. edit `creechr-bridge-host.json` and replace
   `__YOUR_EXTENSION_ID_HERE__` with `chrome-extension://<that id>/`
6. reload the extension
7. run creechr, open a tab, click the popup, click "let him in"
8. watch for `ExtensionPipeServer: bridge connected` in creechr.log

known gaps: single-tab (last-opted-in wins), DOM rect math still
uses primary-screen DPR (approximate on mixed-dpi multimon), the
extension id editing dance is unavoidable for unpacked dev
extensions (chromes fault).

## dev environment variables

knobs you can set in the environment to change creechr's behavior
without rebuilding. most are development aids.

| var | effect |
|-----|--------|
| `CREECHR_LOG_LEVEL` | `trace` / `debug` / `info` / `warn` / `error`. default `info`. |
| `CREECHR_TEST_EXIT_MS` | auto-quit after N ms. for scripted test runs. |
| `CREECHR_HEIST_NOW=1` | fire a heist every ~2s, ignore the random gate and input-idle gate |
| `CREECHR_GNAW_NOW=1` | every climb decision becomes a gnaw attempt |
| `CREECHR_RAPPEL_NOW=1` | every climb decision becomes a rappel attempt |
| `CREECHR_CHAOS=1` | ~30% of window heists skip the carry-pixmap shrink, restoring the legendary bug where creechr carries an entire 800px window across the desktop |
| `CREECHR_COLOR=name` | body color: `pink` / `teal` / `lime` / `orange` / `sky` / `violet` / `blood` / `moss`, or a `#rrggbb` hex |
| `CREECHR_SPRITE=path.png` | override the procedural atlas. must be 384×720 (8 cols × 15 rows × 48 px). |
| `CREECHR_SNARK_FILE=path.json` | override the app-snark table path. |

## known issues

- **the placeholder sprite is procedurally drawn** pink (or whatever
  CREECHR_COLOR you picked) with white halos on the arm/leg/rope
  outlines for dark-mode visibility. no real art exists. if you can
  draw, drop a 384x720 PNG somewhere and set `CREECHR_SPRITE=path` —
  the 8×15 grid layout is documented above.
- **shake-off physics** velocity numbers were picked by gut feel.
  user testing suggests they're "fine" but they havent been properly
  tuned. constants are in `rappel::` at the top of creechr.cpp.
- **if a stolen window's process exits** while creechr is carrying
  its pixmap, the restore callback no-ops on the dead hwnd. visually
  he still walks the pixmap to a corner, sinks it, and walks away.
  the window just doesnt come back. nothing crashes.
- **DOM heist coordinate math** (extension content script) still
  uses the page's devicePixelRatio divided by the primary screen
  dpr. fine on single-monitor 100% boxes, approximate everywhere
  else. native win32 sources got the per-monitor fix earlier but
  the extension side still needs the same treatment. v1.1.
- **right click menus from other apps** sometimes draw on top of
  him. nothing we can do about that without raising our z-order
  above normal top-level windows, which would make creechr steal
  focus from popups.
- **heist orchestrator picks at random**. no preference for things
  that look fun. you might watch him steal the same notepad window
  three times in a row. the rng is working as designed.
- **mixed-dpi multi-monitor is not fully tested.** the per-monitor
  dpi fix covers the native side; multi-monitor with different
  scales has not been properly verified. single-monitor users at
  any scale should be fine.
- **chrome extension verification is punted to v1.1.** see the v1.0
  section for the install dance and known gaps. structurally
  complete, not e2e verified by the author.

## license

MIT. see LICENSE. do whatever, just don't blame me when your boss sees it.
