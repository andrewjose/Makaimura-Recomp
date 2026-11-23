# Fair Knight: technical reference

makaimurg (Makaimura, Japan rev. G) as Makaimura Recomp translates it. Addresses are the 6809's (fixed ROM
6000-FFFF unless banked). Found with `tools/disassemble.py --listing`, then confirmed with `gng_headless`
(`--ram-watch`, `--ppm`, `--save-state` diffs) on `tests/controls/walk_right.txt`.

## RAM

| Address | What |
|---|---|
| `<$28` | a game is being played (0 in the demo) |
| `<$42/<$46/<$48` | 3000/3001/3002 inverted, copied at `6772` |
| `<$4B`, `<$4C` | the controls of the player in play, this frame and last (`691C`): 01 right, 02 left, 04 down, 08 up, 10 attack, 20 jump |
| `<$60` | lives (set `6C37`, `DEC` on death `75FD`, `INC` at `ED4D`) |
| `<$71` | stage state: 0 its start (the intro's scripted Arthur), 1 played |
| `<$73` | weapon (written at `6C0C`, and at `E702` on a pickup: item type - 1) |
| `<$A0`, `<$A2` | the player's X and Y, mirrored by `8F0E` |
| `<$AC` | set by the hit sequence's last sub-state (`99FF`) and the death's |
| `<$AD` | invincibility timer: `$78` at `9982/9984`, `DEC` at `EE67`; `8EFD` blanks the sprite on alternate frames while set |
| `$0510` | the player object (X); fields at offsets below |

Player object, `X = $0510`:

| Field | Address | What |
|---|---|---|
| `-$10,X` | `$0500` | 1, 2 while hurt (bit 0 tested by the item collision `E6B2`) |
| `-$D,X` | `$0503` | state (dispatch `8F1E`, table `8F2A`) |
| `-$C,X` | `$0504` | sub-state |
| `-$A..-$8,X` | `$0506-$0508` | X position (16.8) |
| `-$7,X` | `$0509` | Y position |
| `-$2,X` | `$050E` | horizontal direction while airborne: 0 none, 1 right, 2 left |
| `$1,X`, `$4,X` | `$0511`, `$0514` | sprite pointers (jump right `$96DE`, left `$96E6`) |
| `$E,X` | `$051E` | facing: 1 right, 2 left |
| `$13,X` | `$0523` | the routine the hit's hop returns through (`9A59`) |
| `$18,X` | `$0528` | timer of state 4 (set 8 at `931F`, -1 every 64 frames): likely the frog curse |
| `$19,X` | `$0529` | armor: 0 armored, 5 while the armor's pieces fly, 1 in boxers; 0 again at a new life |

## States (table `8F2A`)

| State | Routine | |
|---|---|---|
| 0 | `8F36` | init |
| 1 | `8FD3` | on the ground |
| 2 | `93CA` | jump: `-$2,X` selects `93E5` (right) / `9405` (left) each frame; sub-states `93DF` |
| 3 | `995F` | hit, death, and the intro's scripted armor changes; sub-states `9975`: `997F` hit, `99ED`, `9436` airborne, `9475` landing, `99FB` end |
| 4 | `930E` | (frog, unconfirmed) |
| 5 | `937D` | airborne, sharing `9436`/`9475` |

The hit, as run (walk_right, frame 2908 on): state 3 sub 0 (`997F`: `<$AD = $78`, armor pieces spawned at
`$0540 + $40n` from `99BA`), sub 1 (`99ED` → `9A2B`: B = 1 or 2 by the sign of `<$AE`, stored to `-$10,X`,
`$E,X` and `-$2,X`; `9A50` loads the airborne block `93C2` with `9004`), subs 2-3 (`9436`/`9475`: the hop, moving
sideways by `-$2,X` through `94CA`/`94A3`), sub 4 (`99FB`), then state 1. A second hit in boxers ends in the death
sequence (lives -1 at the respawn).

## Hooks

`FAIR_KNIGHT_HOOKS` in `tools/m6809/translate.py` maps an address of the fixed ROM to a C function called before
that instruction, in every path that reaches it (the region's `goto` and the trampoline's dispatch both land on the
label). It returns 0 to run the instruction, or an address the trampoline dispatches instead. The translator
refuses to write the program if a hook's address is not an instruction (another ROM revision).

| Hook | Instruction | Does |
|---|---|---|
| `fk_hook_9984` | `9984 STB <$AD` | `B = fk.invincibility` (1-255) |
| `fk_hook_9A39` | `9A39 STB -$2,X` | `B = 0` (B is reloaded at `9A3E`) |
| `fk_hook_93CA` | `93CA LDB -$2,X` | `-$2,X` from `<$4B`: 01 → 1, 02 → 2; neither: 0 in `full`, unchanged in `assist` |

All three act only while `<$28 != 0 && <$71 == 1`. They read and write RAM through `gng_read`/`gng_write` with no
bus timing and add no cycles: off, the program's time and accesses are the original's.

Dead end: knockback was first hooked at `996B` (state 3's move by `$E,X` after its sub-state). That is the hit
state's wall and screen-edge clamp (`93E5`/`9405` → `8B7D`), not the push: X still slid back from `$0300` to
`$02DD`. The push is the airborne sub-states' move by `-$2,X`.

## Settings

`gng/fair_knight.h`: `fk.invincibility` (0 = the arcade's), `fk.no_knockback`, `fk.air_control` (0 off,
1 assist, 2 full). `makaimura.ini` `[fair_knight]`: `invincibility`, `knockback` (true = the arcade's),
`air_control`; defaults 180 / false / full, applied at power-on and F3. `gng_headless`: `--fk-invincibility N`,
`--fk-no-knockback`, `--fk-air off|assist|full` (all off by default, so the checks are unchanged).

## Checks (0.1.0)

- Off: `gng_headless --bus-hashes --picture-hashes` identical to unmodified upstream (`b9f8eb8`) on walk_right,
  stage1, stage4, tower5 (12001 frames each) and long_play (40001).
- Invincibility 240: `<$AD = $EF` at frame 2909 (`$77` in the arcade).
- No knockback: X stays `$0300` frames 2909-2947 (arcade: `$0300` → `$02DD`); armor `$0529` still 5 → 1; the hop
  and the invincibility flicker as before.
- Air control full: a jump right with left held turns at once (X `$82` → `$63` by frame 2062; arcade `$82` →
  `$A2`), sprite `$96E6`; a neutral jump steers right.
- All on, long_play: 40001 frames, no fault; identical to the arcade's until frame 1974 (the demo, coin, intro).
- Air control script: `tests/controls/fair_knight_air.txt`.
- Not checked yet: steering into walls in the air; state 5 (walking off a ledge) has no air control.

## Build

```sh
python tools/makaimura_rom.py --extract build/makaimurg.regions
python tools/m6809/translate.py
python tools/z80cpu/translate.py
cmake -S . -B build/lx -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/lx --target makaimura gng_headless
```

Windows from Linux: MinGW-w64 with SDL2's `SDL2-devel-*-mingw` package and `tools/fair_knight/mingw-w64.cmake`, which sets
`-DCMAKE_C_STANDARD_LIBRARIES=<SDL2>/lib/libSDL2.dll.a` (SDL2main must come before SDL2 for ld). Two portability
fixes in this fork are upstream candidates: libm linked outside MSVC (`lround` in `sound.c`), and `main.c`'s
`tell()` renamed `notify()` (it clashes with MinGW's `io.h`).
