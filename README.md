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
- transparent overlay across all monitors
- walks along the bottom of the desktop and the tops of windows
- climbs vertical window edges
- naps after a while of you not touching the mouse
- wakes up when you do
- knows to hide during fullscreen apps so he doesn't ruin your movie

### v0.2 — he steals small windows and the cursor
### v0.3 — he steals individual ui controls (best effort)
### v1.0 — he steals stuff out of webpages (with the extension)

## known issues

- multi-monitor with mixed dpi scaling is a war zone. some of his sprite
  positions will be off by a few pixels until i sit down with a real
  multi-monitor rig and fix it properly.
- if you alt-tab to a fullscreen game during a heist he'll just freeze
  in place mid-carry. when you tab back he picks up where he left off.
  i'm not sure if that's a bug or my favorite feature.
- right click menus from other apps sometimes draw on top of him. fine.
- the placeholder sprite is a colored blob. real art is coming. don't @ me.

## license

MIT. see LICENSE. do whatever, just don't blame me when your boss sees it.
