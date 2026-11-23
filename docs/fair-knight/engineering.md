# Fair Knight: for engineers

This explains how Fair Knight changes the arcade game, for software engineers who haven't done game hacking.

## What Makaimura Recomp is

The 1985 arcade board runs a program for a Motorola 6809 CPU, stored in ROM chips. An emulator would interpret
that program instruction by instruction at run time. A **recomp** (static recompilation) instead translates the
whole program into C ahead of time, once, from the player's own ROM, and compiles it into a normal executable that
runs on a C model of the rest of the board (video, sound, inputs). The project's translator
(`tools/m6809/translate.py`) turns each 6809 instruction into a few lines of C that do exactly what the CPU did,
cycle for cycle, and the result is checked frame by frame against MAME, the reference arcade emulator.

The C is generated from the ROM and is never committed (it is Capcom's code), so a mod can't simply edit it.

## How Fair Knight hooks in

Fair Knight adds a small table to the translator: "before the instruction at this ROM address, call this C
function". The generated code then contains, at those three places only:

```c
{ const uint16_t fk_next_ = fk_hook_93CA(); if (fk_next_) return fk_next_; }
```

A hook is ordinary C (`runtime/src/fair_knight.c`). It can read and change the game's RAM and the CPU's registers
before the original instruction runs, or (by returning an address) send execution somewhere else. This is the same
idea as a function hook or a monkey patch, applied at the level of single machine instructions. Because the table
is keyed by ROM address, it survives changes to the translator itself.

Each hook first checks its setting and whether a real game is being played; if not, it returns 0 and the original
instruction runs untouched. So with the settings off the program is bit-for-bit the arcade's. That was verified by
running the repository's input scripts (recorded button presses, 88,000 frames in total) through the modified and
the unmodified builds and comparing a hash of the bus and of the picture at every frame: identical.

## How the right places were found

- **Static analysis:** the project's disassembler lists the whole program. The game keeps the player as an
  "object" (a struct) at RAM address `$0510`, with fields such as its state (standing, jumping, hit), position and
  direction. The player's code is a state machine: a jump table picks a routine by the state number, like a
  `switch` over an enum.
- **Dynamic checks:** the headless build can log chosen RAM bytes every frame and save the whole machine's state.
  Replaying the repository's "walk right" script showed the frame where Arthur gets hit (2909) and which bytes
  change then: the invincibility timer, the armor flag, the direction. Diffing save states before and after found
  fields the static reading had guessed wrong (the armor flag is at `$0529`, not where first assumed).

## The three changes

- **Invincibility:** the hit routine loads the number 120 into a register and stores it in a countdown timer. The
  hook replaces the register's value just before the store. One byte of behaviour, no new logic.
- **Knockback:** when hit, the game records which side the hit came from in the player's "horizontal direction"
  field, then reuses the jump's mid-air code to make Arthur hop that way. The hook sets that direction to 0
  ("none") before it's stored, so the same hop goes straight up and down. The facing of the sprite and the armor
  loss are unchanged.
- **Air control:** during a jump the game reads the direction field every frame to decide whether to move Arthur
  left or right; it only sets the field at take-off. The hook writes the field from the joystick each frame. The
  movement code's own wall check still runs, so steering into a wall stops at the wall. The game already turns
  Arthur's sprite to face the stick in mid-air, so only the momentum needed changing.

## Trade-offs and limits

- Hooks are guarded so they never act in the attract demo or the scripted intro (which also use the jump and hit
  code). Otherwise the demo, which replays recorded inputs, would go off course.
- The hooks change the game's behaviour, so a Fair Knight game can no longer be compared with MAME. That is why
  each is behind a setting and the off state is checked to be unchanged.
- Upstream's main window now runs a second, more readable C version of the program (a decompilation). The hooks
  exist only in the instruction-by-instruction translation, so this fork builds the window on the translation
  (`MAKAIMURA_FAIR_KNIGHT`, on by default).
- Replays store inputs, not settings, so a replay made with other settings goes differently.
