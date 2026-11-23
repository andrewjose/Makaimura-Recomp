/**
 *    Name:        fair_knight.c
 *    Description: Fair Knight's hooks into the translated 6809 program (gng/fair_knight.h)
 *
 *    license:BSD-3-Clause
 *
 *    The player is the object at X = $0510 (its fields at offsets from X); the addresses are makaimurg's.
 *      <$28        a game is being played (0: the demo)
 *      <$71        the stage's state: 1 while it is played (0: its start, the intro's scripted Arthur)
 *      <$4B        the player's controls this frame: 01 right, 02 left, 04 down, 08 up, 10 attack, 20 jump
 *      <$AC        set through the death's sequence
 *      -$2,X       the player's horizontal direction in the air: 0 none, 1 right, 2 left
 *      $E,X        the way the player faces: 1 right, 2 left (the arcade turns it in the air already)
 *    A hook acts only while a stage is played (<$28 and <$71 = 1), never in the demo or the intro.
 */
#include <string.h>

#include "gng/board.h"
#include "gng/fair_knight.h"
#include "m6809/cpu.h"

fk_settings fk = { 0, 0, FK_AIR_OFF };

#define PLAYER 0x0510u

static const char *const air_names[] = { "off", "assist", "full" };

const char *fk_air_control_name(int mode)
{
	return mode >= 0 && mode <= 2 ? air_names[mode] : "off";
}

int fk_air_control_parse(const char *name)
{
	for (int i = 0; i < 3; i++)
		if (!strcmp(name, air_names[i]))
			return i;
	return -1;
}

/* RAM as the program sees it, read and written with no bus timing (the hooks are outside the board's time) */
static uint8_t peek(uint16_t address) { return gng_read(address); }
static void poke(uint16_t address, uint8_t value) { gng_write(address, value); }

static int stage_played(void)
{
	return peek(0x0028) != 0 && peek(0x0071) == 1;
}

/* 997F, the hit: LDB #$78 / STB <$AD (here) sets the invincibility's frames, <$AD counted down at EE67 */
uint16_t fk_hook_9984(void)
{
	if (fk.invincibility > 0 && stage_played())
		cpu.b = (uint32_t)(fk.invincibility > 255 ? 255 : fk.invincibility);
	return 0;
}

/* 9A2B, the hit's hop: B is 1 or 2 by the side the hit came from (<$AE), stored in -$10,X, $E,X (Arthur's facing)
   and, here, -$2,X: the airborne sub-states (9436, 9475) then carry him that way (94CA right, 94A3 left) while he
   hops. No knockback: -$2,X is 0, so the hop goes straight up and down; the facing stays the game's. The death's
   hop (9A12 falls into the same code) stays in place too */
uint16_t fk_hook_9A39(void)
{
	if (fk.no_knockback && stage_played())
		cpu.b = 0;
	return 0;
}

/* 93CA, the jump state (2): LDB -$2,X picks the sideways move (1 right: 93E5, 2 left: 9405, 0 none) each frame.
   Air control sets it from the stick first; the move's own wall check (8B7D) still stops Arthur at walls */
uint16_t fk_hook_93CA(void)
{
	if (fk.air_control == FK_AIR_OFF || !stage_played())
		return 0;
	const uint8_t controls = peek(0x004B) & 0x03;
	const uint16_t direction = (uint16_t)(PLAYER - 2);
	if (controls == 0x01 || controls == 0x02)
		poke(direction, controls);   /* 1 right, 2 left: the stick's bits are the directions' numbers (the game
		                                turns Arthur's facing, $E,X, in the air by itself) */
	else if (fk.air_control == FK_AIR_FULL)
		poke(direction, 0);
	return 0;
}
