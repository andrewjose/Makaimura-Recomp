# Fair Knight: overview

*Ghosts'n Goblins* is famous for being hard, and some of that hardness feels unfair: you get hit, you are thrown
backwards into a pit, and once you jump you can't change your mind. Fair Knight keeps the game's challenge (the
enemies, the levels, two hits and you're dead) and removes those cheap deaths.

## What changes (version 0.1.0)

| Change | The arcade | Fair Knight's default |
|---|---|---|
| **Invincibility after a hit** | 2 seconds of flashing | 3 seconds, so you can get clear of what hit you |
| **Knockback** | A hit throws Arthur backwards, often off a ledge or into another enemy | Arthur hops on the spot instead; he still loses his armor |
| **Air control** | A jump's direction is fixed the moment you leave the ground | You can steer in the air: push left or right to change direction, centre the stick to drop straight down |

Air control has three settings: `off` (the arcade's), `assist` (left or right turns the jump, letting go keeps
going the way you were) and `full` (the jump goes wherever the stick points).

Everything else is untouched: the demo, the intro where the princess is taken, the enemies, the timer, scoring
and continues behave as in the arcade.

## Changing the settings

Open `makaimura.ini` (next to the program; it is created the first time you run the game) and find:

```
[fair_knight]
invincibility = 180
knockback = false
air_control = full
```

- `invincibility`: frames of flashing after a hit (60 a second). The arcade's is 120.
- `knockback`: `true` for the arcade's push-back, `false` for Fair Knight's hop in place.
- `air_control`: `off`, `assist` or `full`.

Changes take effect when the game starts or when you reset it (F3). For the original game, set
`invincibility = 120`, `knockback = true`, `air_control = off`.

## Good to know

- Replays and save states remember the moves you made, not these settings. Play a replay with the same settings
  it was recorded with, or it will go differently.
- Fair Knight's other changes from the console versions (fair enemy spawns, the weapon you drop when you pick
  up another, the endgame aid, markers for secrets, an in-game options menu) are planned for later versions.
