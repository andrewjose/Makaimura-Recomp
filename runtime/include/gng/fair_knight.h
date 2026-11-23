/**
 *    Name:        fair_knight.h
 *    Description: Fair Knight: optional gameplay changes (a "fair" Ghosts'n Goblins), as hooks the translation
 *                 calls before chosen instructions of the 6809 program (tools/m6809/translate.py, FAIR_KNIGHT_HOOKS)
 *
 *    license:BSD-3-Clause
 *
 *    Every hook does nothing unless its option is on: with fk_settings all off (the default) the program runs
 *    exactly as the board's, and the translation still matches MAME frame for frame. A hook gives back 0 to let
 *    the instruction run, or the address execution goes on at instead (the trampoline dispatches it).
 *    docs/fair-knight/ describes each change at three levels.
 */
#ifndef GNG_FAIR_KNIGHT_H
#define GNG_FAIR_KNIGHT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FAIR_KNIGHT_VERSION "0.1.0"

typedef enum fk_air_control
{
	FK_AIR_OFF = 0,     /* the arcade's: the jump's direction is fixed at take-off */
	FK_AIR_ASSIST = 1,  /* left or right in the air turns the jump; letting go keeps its direction */
	FK_AIR_FULL = 2     /* the jump goes where the stick says, and straight up with it centred */
} fk_air_control;

typedef struct fk_settings
{
	int invincibility;  /* frames of invincibility after a hit (the arcade's: 120); 0 keeps the arcade's */
	int no_knockback;   /* 1: a hit no longer pushes Arthur back (he still hops in place) */
	int air_control;    /* fk_air_control */
} fk_settings;

extern fk_settings fk;

/* Names for the settings and command lines: "off", "assist", "full" */
const char *fk_air_control_name(int mode);
int fk_air_control_parse(const char *name);   /* -1 if not a name */

/* The hooks (FAIR_KNIGHT_HOOKS in tools/m6809/translate.py), by the address they run before */
uint16_t fk_hook_9984(void);   /* STB <$AD: the hit's invincibility */
uint16_t fk_hook_9A39(void);   /* the hit's hop: its sideways direction */
uint16_t fk_hook_93CA(void);   /* the jump state: its horizontal direction */

#ifdef __cplusplus
}
#endif

#endif
