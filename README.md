# Makaimura Recomp

> **This fork adds [Fair Knight](docs/fair-knight/README.md)**: optional fairer gameplay (longer invincibility
> after a hit, no knockback, air control), set in `makaimura.ini`'s `[fair_knight]`. Off, the game is upstream's.

A native PC port of Capcom's 1985 arcade game **Makaimura** (*Ghosts'n Goblins* in the West), Japan
revision G. It is being made by translating the arcade board's MC6809 program into C ahead of time and running
it on a model of the board, aiming at exactly what the arcade does, frame for frame. It uses the approach and
tools of [DonPachi Recomp](https://github.com/CapsLock4284/DonPachi-Recomp) and
[arcade-decomp](https://github.com/cheviman/arcade-decomp).

**Status: playable in a window, the same as MAME.** The 6809 program is translated to C an
instruction at a time (`tools/m6809/translate.py`). It runs on a model of the board (`runtime/`), and it is the
same as MAME 0.289 at every frame checked: the CPU's bus at every VBLANK and every interrupt, and the picture,
pixel for pixel. The sound program (the Z80) is translated too, and the game's own YM2203s make the same sound as MAME's (ymfm),
sample for sample. `build/bin/makaimura` is the game in a window (SDL2), its picture as the arcade's 4:3 monitor
shows it, its options in `makaimura.ini`. The plan is in [TODO.md](TODO.md), the
board is described in [docs/hardware.md](docs/hardware.md), and [docs/accuracy.md](docs/accuracy.md) explains
the two accuracy modes: MAME's, and the real board's, which is the goal.

## Download and play

1. Get the latest release from [**Releases**](https://github.com/alexgbor/Makaimura-Recomp/releases/latest):
   `Makaimura-x.y.z-win64.zip`.
2. Unzip it somewhere you can write to (not inside *Program Files*) and run `makaimura.exe`. Windows may show
   *"Windows protected your PC"* because the program isn't signed: click **More info → Run anyway**.
3. On the first run a window asks for the ROM set: drag MAME's `makaimurg.zip` and its parent `gng.zip` onto it
   (together or one after the other). The game keeps them in a `rom` folder beside it. Merged and split MAME sets
   both work.

From then on the game looks up its newest release as it starts and offers it on the menu (UPDATE TO V...): it asks
first, then downloads it, checks it and starts again, keeping your settings, scores, states, replays and ROM set
(GAME OPTIONS > CHECK FOR UPDATES turns the look-up off).

The release's `README.txt` has the controls, the menu and the files the game keeps. What's new in each version:
[CHANGELOG.md](CHANGELOG.md). A Linux build is made from the sources (below); there is no Linux package yet.

## Building

**This repository contains no game data.** You need your own copy of MAME's ROM sets: `makaimurg.zip` and its
parent `gng.zip`, in `rom/`. The game reads them as they are (`runtime/src/rom_set.c`); the tools that check it
read them extracted into a regions file (`build/makaimurg.regions`).

```sh
git clone --recursive <this repository>
python tools/makaimura_rom.py --extract build/makaimurg.regions   # the set's files found in rom/, extracted
python tools/disassemble.py --listing     # the static analysis, and build/makaimurg.lst
python tools/m6809/translate.py           # the program as C: build/generated/program (never committed)
cmake -S . -B build/msvc && cmake --build build/msvc --config Release
python tools/z80cpu/translate.py          # the sound program as C: build/generated/sound
python tools/check_translation.py         # the translation against the reference (gref), frame by frame
build/bin/makaimura                       # the game (SDL2: on Windows SDL2-devel-2.x.x-VC.zip in build/downloads/sdl2)
python tools/check_resampler.py BOARD.wav # the window's resampler against scipy's (BOARD.wav: gng_headless --wav)
python tools/mame_trace.py --frames 7001 --inputs tests/controls/walk_right.txt --pictures 1 --against gng_headless
```

`tools/mame_trace.py` needs MAME 0.289 (`MAME` environment variable, or `D:/repositories/mame0289/mame.exe`).

## Playing

`makaimura` looks for `makaimurg.zip` and `gng.zip` beside itself, in a `rom` folder beside it, in the repository's
`rom/` (from `build/bin`) and in the folder it is started from; or give it a zip or a folder
(`makaimura D:/roms`), or name one in `makaimura.ini`. A folder of MAME's loose files works too. If the set is not
found whole, a window asks for it (`ports/sdl2/rom_drop.c`): the dropped zips are copied into the `rom` folder.

Besides the arcade game, the window has:

- **Save states**: F5 saves the whole board into a slot, F7 loads it, F6 picks the slot (8); SAVE STATE and LOAD STATE
  on the menu. A game loaded from a state goes on exactly as it would have (`tools/check_states.py`).
- **Practice**: PRACTICE on the menu starts a game at any area of the first loop (prepared once, then kept in `states/`).
- **Replays**: at each game over the game asks whether to keep the game's replay (from its coin, or the state it was
  played from); REPLAYS on the menu plays them again, frame for frame (Delete, then Enter, deletes one).
- **High scores** kept between sessions (`makaimura.hi`): the game's own ranking written back as the board starts.
- **A CRT look**: PICTURE AND SOUND > CRT draws the picture as a tube's beam does (SCANLINES), with a phosphor mask
  if wanted (ARCADE, PVM).
- **Side panels** on a wide window: the score, lives, weapon, area, loop, rank, both players' controls, the frames
  lost to slowdown, the window's keys, the menu's help.

The menu (Esc) pauses the game and changes its options, the picture and the volume as it plays; it keeps them in
`makaimura.ini`. That file, beside the program, is written with every setting and what it does the first time the
game starts:

- **[game]**: `accuracy`: `mame` (the board as MAME 0.289 models it, checked frame by frame against MAME) or
  `board` (what the real board does and MAME leaves out: so far the sprite DMA halting the CPU,
  `docs/accuracy.md`); and the board's DIP switches, by MAME's names: lives (3, 4, 5, 7), difficulty, bonus life, coinage
  (free play too), demo sounds, flip screen, cabinet, service mode. They are read as the board starts (and at F3).
- **[window]**, **[video]**: the size, full screen; the picture's shape (`4:3`, as the arcade's monitor, or
  `pixels`, square), its scaling (`fit`, `integer`, `stretch`), sharp or smooth pixels, vsync, and the pace
  (`sync = board`: the board's own 59.637 frames a second, exact; `display`: a frame a refresh, smoother on a 60 Hz
  display but 0.6% fast).
- **[sound]**: on or off, the volume, the latency.
- **[keyboard]**, **[gamepad]**: every control, up to four keys or buttons each.

## Controls

| | Keyboard | Gamepad |
|---|---|---|
| Move | arrows | left stick or D-pad |
| Attack (throw) | Z or left Ctrl | B or X |
| Jump | X or left Alt | A |
| Coin | 5 (6: the second coin slot) | Back |
| Start | 1 (one player), 2 (two players) | Start (one player) |
| Service switch | 9 | |
| Player 2 (cocktail cabinet; the continue's buttons) | R F D G, A (attack), S (jump) | the second gamepad |
| Reset the board | F3 | |
| Menu (options, picture, sound, reset, quit) | Esc or F1 | right stick (pressed) or guide |
| Pause | P or Pause | |
| Full screen | F11 or Alt+Enter | |
| Record (start, stop) | F12 | |
| Save state, load it, next slot | F5, F7, F6 | |

The joystick is 4-way, like the board's: one direction at a time, up or down winning over left or right. In the
upright cabinet (the default) the two players take turns on player 1's controls; in the cocktail cabinet player 2
plays on their own, as on the board. Every control can be changed in
the menu (CONTROLS: Enter, then the key or the pad's button; Delete gives it its default back) or in
`makaimura.ini` (up to four keys or buttons each).

## Recording

F12 starts a recording beside `makaimura.ini` (`makaimura-DATE-TIME-MODE.avi`) and stops it; the window's title says
while it records. It is the board's own picture (256 x 224, shown 4:3) and sound (48 kHz), frame for frame in the
board's time, uncompressed (about 10 MB a second: ffmpeg makes it small). From the command line:

```
makaimura --accuracy mame --record mame.avi --record-inputs game.txt     # play; the controls kept too
makaimura --accuracy board --inputs game.txt --record board.avi          # the same controls, the other mode
python tools/side_by_side.py mame.avi board.avi comparison.mp4           # side by side (ffmpeg)
```

The same controls at the same frames give the same game in the same mode (the same file, byte for byte); in the
other mode the game can part ways where board mode slows down.

## License

BSD 3-Clause (`LICENSE`). The game is not included and not covered by it.

## Credits

- **Makaimura** is © 1985 Capcom. This is an unofficial fan project, not connected with or endorsed by Capcom.
- The hardware model follows [MAME](https://github.com/mamedev/mame)'s `capcom/gng.cpp` (Pierpaolo Prazzoli and
  the MAME team, BSD-3-Clause), and Jose Tejada's (jotego) research on the board for jt_gng.
- The verification tools run MAME 0.289's MC6809 and Z80 cores and Aaron Giles's
  [ymfm](https://github.com/aaronsgiles/ymfm) (BSD-3-Clause), in `tools/reference/mame`: never in the game.
- The decompiler core is [arcade-decomp](https://github.com/cheviman/arcade-decomp) (in `tools/core`), from
  [CapsLock4284's fork](https://github.com/CapsLock4284/arcade-decomp/tree/timing-model).
