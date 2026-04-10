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

### v0.2 — he steals small windows and the cursor
### v0.3 — he steals individual ui controls (best effort)
### v1.0 — he steals stuff out of webpages (with the extension)

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
- idle behaviors (blink, yawn, look around) are not in v0.1. he just
  stands there during idle. it makes him look constipated. v0.2.
- right click menus from other apps sometimes draw on top of him. fine.

## license

MIT. see LICENSE. do whatever, just don't blame me when your boss sees it.
