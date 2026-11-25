# Chopagotchi

Version 0.0.1

A story-mode mod for GTA V (built for Enhanced, should also load on Legacy) that
replaces what the iFruit app did for Chop. You earn his happiness and tricks by
spending time with him in the game.

## How it plays

Chop's happiness runs from 0 to 100, shown on the game's own bar when you
interact with him. Above 66 he is happy, above 33 content, otherwise unhappy:
he whines, wanders off and refuses tricks.

| What you do | Happiness | Training |
| --- | --- | --- |
| Take Chop out for a walk (once per in-game day) | +5 | |
| Each minute moving with him (on foot or driving) | +2 | +1 |
| Each ball he brings back (1 min cooldown) | +3 | +2 |
| Each trick he performs (5 min cooldown per trick) | +1 | +1 |
| Petting him (10 min cooldown) | +5 | |
| Every 10 minutes of play without him | -1 | |
| Chop gets killed | -25 | |

Training unlocks tricks: Sit at 20, Paw at 60, Beg at 120, Pet at 200.
A fresh Chop starts unhappy with no tricks, so begin with walks and fetch.

Whenever his happiness goes up, the game's bar in the bottom right fills up
smoothly and "Chop Happiness +N" floats up beside it; training gains show
"Chop Training +N" just above. Move them with `GainX`/`GainY` or hide them with
`GainPopup=0` under `[Hud]` in `Chopagotchi.ini`.

### Earning collars

The first (green) collar is Chop's from the start; the other four are earned,
with a message above the minimap when one is:

| Collar | How |
| --- | --- |
| 2 | Take Chop for 10 walks (one counts per in-game day, the same walks that earn the walk bonus) |
| 3 | 50 games of fetch (every ball counts, cooldown or not) |
| 4 | Keep him at 80+ happiness through 3 in-game days of play in a row (48 minutes of play each; sleeping or skipping time does not count, dropping below 80 starts over) |
| 5 | A rare find: now and then Chop digs it up while you walk together |

Shift+F9 only switches between collars he has. Every number is under
`[Collars]` in `Chopagotchi.ini`, and `Locked=0` makes all five available.
Progress is kept in `Chopagotchi.state.ini`; a collar he already wore before
this update stays his.

Chop's happiness, training, collar and reward cooldowns are kept in `Chopagotchi.state.ini` in
the game folder, not in the save game: on an account that once used iFruit the
game keeps overwriting its own copy with the app's old numbers, so the mod
holds the values and re-applies them. They are shared by all your saves.

Every number and the key can be changed in `Chopagotchi.ini`.

## Chopagotchi on Franklin's phone

Take out Franklin's phone in story mode and open its Checklist app: below
"Open Missions" and "Progress" there is a third row, "Chopagotchi". Choose it
and Chop's pages appear on the phone's own list screen, with the phone's
own controls. Back steps up a page, and from the first page back to the
Checklist menu. Notes such as "Chop comes running!" show above the minimap.

- **Overview**: mood and happiness, training points, tricks he knows, progress
  to the next trick, and whether Chop is with you, at home or hurt.
- **Cooldowns**: when the next walk bonus (in game time), fetch, each trick and
  petting will count again.
- **Collar**: pick one of his five collars. The ones he has not earned yet
  say "Locked" (the app does not say what earns them).
- **Whistle for Chop**: calls him back when he has wandered off on a walk.
- **Settings**: switch the mod, the messages and the happiness popup on or
  off. Changes are saved to `Chopagotchi.ini`.

It is the phone's list screen, so every row is one line of text (happiness is
a number here, not a bar).

Settings, under `[Phone]` in the ini:

- `InPhone=0` leaves the real phone alone.
- `InChecklist=0` gives Chopagotchi an icon of its own on the home screen
  instead, in the free cell at the bottom right. The next two settings are for
  that icon.
- `Icon=` picks another of the phone's home-screen icons.
- `Cell=` puts the icon on a cell of your choice (0-8), replacing the game's
  icon there. Left at -1 it uses the free cell; when the home screen is full
  (Trackify shows up during some missions) it takes the Checklist app's place.

The mod finds the phone's data by its contents each time, not by numbers tied
to one game version, and checks it before writing anything. If it does not
recognise the data, for example after a game update, the phone is left alone,
`Chopagotchi.log` says why ("In-phone app: ..."), and the overlay below still
works.

This part was written from the game's decompiled phone scripts (Enhanced
1.0.1158.13) and still needs to be confirmed in the game.

## Fallback: the Chopagotchi overlay (F9)

When the real phone can't be used, F9 opens the same pages in a phone-style
overlay where Franklin's phone sits. Move with the arrow keys (or the d-pad),
Enter (A) to choose, Backspace (B) to go back or close. Where the in-phone app
is not possible, a small "Chopagotchi [F9]" tab appears above the real phone as
a reminder of the key. The overlay follows the game's HUD area and safe-zone
setting, so it sits by the phone on 16:9 and ultrawide screens alike; nudge it
with `[Phone]` in the ini.

Other keys: **Ctrl+F9** switches the mod on or off, **Shift+F9** changes his
collar. Set `App=0` under `[Phone]` to go back to the one-line status message
on F9.

## Install (Steam on Linux / Proton)

1. Download Script Hook V from its author's site (dev-c.com) and check that it
   lists your game version (this install is 1.0.1158.16). Copy `ScriptHookV.dll`,
   `dinput8.dll`, `xinput1_4.dll` and `args.txt` from its `bin` folder into the
   game folder, next to `GTA5_Enhanced.exe`. `args.txt` switches BattlEye off.
2. Copy `Chopagotchi.asi` and `Chopagotchi.ini` into the same folder.
3. In Steam, add this in front of `%command%` in the game's launch options:
   `WINEDLLOVERRIDES="xinput1_4=n,b;dinput8=n,b"`
   Without it Proton uses its own copies of those files and no mod loads.
   Mods only load with BattlEye off, which also means story mode only.
4. Start story mode. After a few seconds a message above the minimap reports
   Chop's mood. If nothing appears, read `Chopagotchi.log` in the game folder.

To remove the mod, delete the `Chopagotchi.*` files.

## Building

`build.sh` needs MinGW-w64. From this folder:

    podman run --rm -v "$PWD":/src:Z ubuntu:24.04 sh -c 'apt-get update && apt-get install -y --no-install-recommends g++-mingw-w64-x86-64-win32 binutils-mingw-w64-x86-64 && sh /src/build.sh'

## Tests

The tests build with the normal Linux compiler (no MinGW needed) and run on the
host:

    sh tests/run.sh

Each `tests/test_*.cpp` is built and run on its own. The reward cooldown rules
live in `rules/Cooldowns.h` so they can be tested without the game.
`tests/test_mod.cpp` compiles `Chopagotchi.cpp` itself against a fake
`windows.h` (`tests/fake/`) and fake Script Hook V natives that play the game
(Chop, animations, clock, HUD), so it checks the real happiness and training
rules, finding Chop's data, saving, settings and the happiness bar animation.

## How it works

The game keeps Chop's happiness, trick level and collar in a block of saved
script data that only the iFruit sync ever wrote. The mod finds that block by
its structure (it does not rely on a version-specific address), checks the
values look right, and writes its own values there every frame. It watches the game's own
walk counter and Chop's fetch, trick and petting animations to see what you do.

The phone's home screen is a table in the game's script data with one row per
app. The mod recognises the table by the apps the game itself registers in it,
and from there knows which app is open. The Checklist app's menu is a list the
game's `appChecklist` script draws; the mod adds its row right after that, and
when Select is pressed asks the phone which row is highlighted, as the script
does. If it is the Chopagotchi row the mod ends the script and draws its own
pages on the same list screen; Back on the first page has the phone start the
Checklist app again. With `InChecklist=0` the mod instead adds a row for
Chopagotchi to the app table, in a slot the game never uses, so the game draws
an icon for it. The method follows the research of the iFruit Jailbreak project
(github.com/sruckstar/iFruitJailbreak). It lives in `rules/RealPhone.h` and
`rules/PhoneScreens.h`, tested against a fake phone in
`tests/test_real_phone.cpp`.
