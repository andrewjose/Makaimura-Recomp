# Fair Knight for Makaimura Recomp

Fair Knight is a set of optional changes that make *Ghosts'n Goblins* fairer without making it easy. It already
exists as ROM hacks for the NES, Game Boy Color, Genesis and SNES versions (andrewjose/fair-knight); this fork
brings it to the arcade original through Makaimura Recomp.

Version **0.1.0** (on Makaimura Recomp 0.2.2): longer invincibility after a hit, no knockback, air control.

The same changes explained three ways:

1. [Overview](overview.md): what changes and what you will notice, no jargon.
2. [For engineers](engineering.md): how it works, for software engineers who don't know game hacking.
3. [Technical reference](technical.md): addresses, hooks, data, build and checks, for ROM hackers.

Settings are in `makaimura.ini`, section `[fair_knight]`. With every change off the game is the arcade's,
checked frame by frame against unmodified Makaimura Recomp.
