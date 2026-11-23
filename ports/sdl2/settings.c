/**
 *    Name:        settings.c
 *    Description: makaimura.ini: the window port's settings (settings.h)
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    The game's options are makaimurg's DIP switches as MAME 0.289 lists them (mame makaimurg -listxml, from
 *    gng.cpp's INPUT_PORTS_START( gng ), BSD-3-Clause, Pierpaolo Prazzoli): their names, values and defaults.
 */
#include "settings.h"

#include "gng/board.h"
#include "gng/fair_knight.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const settings_action_names[ACTIONS] = {
	"up", "down", "left", "right", "attack", "jump", "start1", "start2", "coin1", "coin2", "service",
	"p2_up", "p2_down", "p2_left", "p2_right", "p2_attack", "p2_jump" };
static const char *const action_help[ACTIONS] = {
	NULL, NULL, NULL, NULL, "button 1: the weapon thrown", "button 2", "one player", "two players",
	"the coin slots", NULL, "the service switch",
	"the second player's controls (a cocktail cabinet's second player; on the gamepad, the second gamepad)",
	NULL, NULL, NULL, NULL, NULL };
static const char *const hotkey_names[HOTKEYS] = { "reset", "pause", "fullscreen", "quit", "menu", "record",
	"save_state", "load_state", "state_slot" };
const char *const settings_aspect_names[ASPECTS] = { "4:3", "pixels" };
const char *const settings_scaling_names[SCALINGS] = { "fit", "integer", "stretch" };
const char *const settings_crt_names[4] = { "off", "scanlines", "arcade", "pvm" };    /* (crt.h's order) */
#define aspect_names settings_aspect_names
#define scaling_names settings_scaling_names
static const char *const sync_names[SYNCS] = { "board", "display" };

/* ---- the DIP switches (mame makaimurg -listxml) ---- */

typedef struct dip_value { const char *name; uint8_t bits; } dip_value;
typedef struct dip
{
	const char *name;
	int port;              /* 1: DSW1 (3003), 2: DSW2 (3004) */
	uint8_t mask;
	const dip_value *values;
	int count;
	const char *help;
} dip;

static const dip_value coinage[] = {
	{ "4 coins 1 credit", 0x02 }, { "3 coins 1 credit", 0x05 }, { "2 coins 1 credit", 0x08 },
	{ "3 coins 2 credits", 0x04 }, { "4 coins 3 credits", 0x01 }, { "1 coin 1 credit", 0x0F },
	{ "3 coins 4 credits", 0x03 }, { "2 coins 3 credits", 0x07 }, { "1 coin 2 credits", 0x0E },
	{ "2 coins 5 credits", 0x06 }, { "1 coin 3 credits", 0x0D }, { "1 coin 4 credits", 0x0C },
	{ "1 coin 5 credits", 0x0B }, { "1 coin 6 credits", 0x0A }, { "1 coin 7 credits", 0x09 },
	{ "free play", 0x00 } };
static const dip_value coinage_affects[] = { { "coin a", 0x10 }, { "coin b", 0x00 } };
static const dip_value off_on_20[] = { { "off", 0x20 }, { "on", 0x00 } };
static const dip_value off_on_40[] = { { "off", 0x40 }, { "on", 0x00 } };
static const dip_value off_on_80[] = { { "off", 0x80 }, { "on", 0x00 } };
static const dip_value lives[] = { { "3", 0x03 }, { "4", 0x02 }, { "5", 0x01 }, { "7", 0x00 } };
static const dip_value cabinet[] = { { "upright", 0x00 }, { "cocktail", 0x04 } };
static const dip_value bonus_life[] = {
	{ "20k 70k every 70k", 0x18 }, { "30k 80k every 80k", 0x10 }, { "20k and 80k only", 0x08 },
	{ "30k and 80k only", 0x00 } };
static const dip_value difficulty[] = {
	{ "easy", 0x40 }, { "normal", 0x60 }, { "difficult", 0x20 }, { "very difficult", 0x00 } };

#define DIP(name, port, mask, values, help) { name, port, mask, values, (int)(sizeof(values) / sizeof(values[0])), help }
static const dip dips[] = {
	DIP("lives", 2, 0x03, lives, "3, 4, 5 or 7"),
	DIP("difficulty", 2, 0x60, difficulty, "easy, normal, difficult or very difficult"),
	DIP("bonus_life", 2, 0x18, bonus_life,
		"20k 70k every 70k, 30k 80k every 80k, 20k and 80k only or 30k and 80k only"),
	DIP("coinage", 1, 0x0F, coinage, "free play, 1 coin 1 credit, 1 coin 2 credits ... 1 coin 7 credits, "
		"2 coins 1 credit, 2 coins 3 credits, 2 coins 5 credits, 3 coins 1 credit, 3 coins 2 credits, "
		"3 coins 4 credits, 4 coins 1 credit, 4 coins 3 credits"),
	DIP("coinage_affects", 1, 0x10, coinage_affects, "coin a or coin b: the slot the coinage is for"),
	DIP("demo_sounds", 1, 0x20, off_on_20, "on or off: the attract mode's sound"),
	DIP("flip_screen", 1, 0x80, off_on_80, "on or off: the picture upside down"),
	DIP("cabinet", 2, 0x04, cabinet,
		"upright or cocktail (the second player's own controls, the picture turned for their turn)"),
	DIP("service_mode", 1, 0x40, off_on_40, "on: the board's test mode from the start"),
};
#define DIPS ((int)(sizeof(dips) / sizeof(dips[0])))

static uint8_t *dip_port(settings *s, const dip *d) { return d->port == 1 ? &s->dsw1 : &s->dsw2; }

/* the value's index, or -1 if the bits are none of its values */
static int dip_index(const settings *s, int i)
{
	const uint8_t port = dips[i].port == 1 ? s->dsw1 : s->dsw2;
	for (int v = 0; v < dips[i].count; v++)
		if ((port & dips[i].mask) == dips[i].values[v].bits)
			return v;
	return -1;
}

int settings_dip_count(void) { return DIPS; }

const char *settings_dip_name(int i) { return dips[i].name; }

const char *settings_dip_value(const settings *s, int i)
{
	const int v = dip_index(s, i);
	return v < 0 ? "?" : dips[i].values[v].name;
}

void settings_dip_step(settings *s, int i, int direction)
{
	const int count = dips[i].count;
	int v = dip_index(s, i);
	v = v < 0 ? 0 : (v + direction + count) % count;
	uint8_t *port = dip_port(s, &dips[i]);
	*port = (uint8_t)((*port & ~dips[i].mask) | dips[i].values[v].bits);
}

/* ---- defaults ---- */

static void keys(SDL_Scancode *slot, SDL_Scancode a, SDL_Scancode b)
{
	for (int i = 0; i < BINDINGS_MAX; i++)
		slot[i] = SDL_SCANCODE_UNKNOWN;
	slot[0] = a;
	slot[1] = b;
}

static void buttons(pad_input *slot, int a, int b)
{
	for (int i = 0; i < BINDINGS_MAX; i++)
		slot[i].kind = PAD_NONE, slot[i].index = 0;
	if (a >= 0)
		slot[0].kind = PAD_BUTTON, slot[0].index = a;
	if (b >= 0)
		slot[1].kind = PAD_BUTTON, slot[1].index = b;
}

void settings_defaults(settings *s)
{
	memset(s, 0, sizeof(*s));
	s->dsw1 = GNG_DSW1_DEFAULT;
	s->dsw2 = GNG_DSW2_DEFAULT;
	s->scale = 3;
	s->fullscreen = false;
	s->aspect = ASPECT_MONITOR;
	s->scaling = SCALING_FIT;
	s->smooth = false;
	s->crt = 0;
	s->vsync = true;
	s->panels = true;
	s->keep_high_scores = true;
	s->keep_replays = true;
	s->check_updates = true;
	/* Fair Knight: its KNIGHT preset (3 s after a hit, no knockback, full air control) */
	s->fk_invincibility = 180;
	s->fk_no_knockback = true;
	s->fk_air_control = FK_AIR_FULL;
	s->panels_debug = false;
	s->sync = SYNC_BOARD;
	s->sound = true;
	s->volume = 100;
	s->latency = 50;
	/* MAME's keys, and Z / X */
	keys(s->keys[ACTION_UP], SDL_SCANCODE_UP, 0);
	keys(s->keys[ACTION_DOWN], SDL_SCANCODE_DOWN, 0);
	keys(s->keys[ACTION_LEFT], SDL_SCANCODE_LEFT, 0);
	keys(s->keys[ACTION_RIGHT], SDL_SCANCODE_RIGHT, 0);
	keys(s->keys[ACTION_ATTACK], SDL_SCANCODE_LCTRL, SDL_SCANCODE_Z);
	keys(s->keys[ACTION_JUMP], SDL_SCANCODE_LALT, SDL_SCANCODE_X);
	keys(s->keys[ACTION_START1], SDL_SCANCODE_1, 0);
	keys(s->keys[ACTION_START2], SDL_SCANCODE_2, 0);
	keys(s->keys[ACTION_COIN1], SDL_SCANCODE_5, 0);
	keys(s->keys[ACTION_COIN2], SDL_SCANCODE_6, 0);
	keys(s->keys[ACTION_SERVICE], SDL_SCANCODE_9, 0);
	keys(s->keys[ACTION_P2_UP], SDL_SCANCODE_R, 0);          /* (MAME's player 2 keys) */
	keys(s->keys[ACTION_P2_DOWN], SDL_SCANCODE_F, 0);
	keys(s->keys[ACTION_P2_LEFT], SDL_SCANCODE_D, 0);
	keys(s->keys[ACTION_P2_RIGHT], SDL_SCANCODE_G, 0);
	keys(s->keys[ACTION_P2_ATTACK], SDL_SCANCODE_A, 0);
	keys(s->keys[ACTION_P2_JUMP], SDL_SCANCODE_S, 0);
	keys(s->hot_keys[HOTKEY_RESET], SDL_SCANCODE_F3, 0);
	keys(s->hot_keys[HOTKEY_PAUSE], SDL_SCANCODE_P, SDL_SCANCODE_PAUSE);
	keys(s->hot_keys[HOTKEY_FULLSCREEN], SDL_SCANCODE_F11, 0);
	keys(s->hot_keys[HOTKEY_QUIT], 0, 0);
	keys(s->hot_keys[HOTKEY_MENU], SDL_SCANCODE_ESCAPE, SDL_SCANCODE_F1);
	keys(s->hot_keys[HOTKEY_RECORD], SDL_SCANCODE_F12, 0);
	keys(s->hot_keys[HOTKEY_SAVE_STATE], SDL_SCANCODE_F5, 0);
	keys(s->hot_keys[HOTKEY_LOAD_STATE], SDL_SCANCODE_F7, 0);
	keys(s->hot_keys[HOTKEY_STATE_SLOT], SDL_SCANCODE_F6, 0);
	s->state_slot = 1;
	buttons(s->buttons[ACTION_UP], SDL_CONTROLLER_BUTTON_DPAD_UP, -1);
	buttons(s->buttons[ACTION_DOWN], SDL_CONTROLLER_BUTTON_DPAD_DOWN, -1);
	buttons(s->buttons[ACTION_LEFT], SDL_CONTROLLER_BUTTON_DPAD_LEFT, -1);
	buttons(s->buttons[ACTION_RIGHT], SDL_CONTROLLER_BUTTON_DPAD_RIGHT, -1);
	buttons(s->buttons[ACTION_ATTACK], SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X);
	buttons(s->buttons[ACTION_JUMP], SDL_CONTROLLER_BUTTON_A, -1);
	buttons(s->buttons[ACTION_START1], SDL_CONTROLLER_BUTTON_START, -1);
	buttons(s->buttons[ACTION_START2], -1, -1);
	buttons(s->buttons[ACTION_COIN1], SDL_CONTROLLER_BUTTON_BACK, -1);
	buttons(s->buttons[ACTION_COIN2], -1, -1);
	buttons(s->buttons[ACTION_SERVICE], -1, -1);
	buttons(s->buttons[ACTION_P2_UP], SDL_CONTROLLER_BUTTON_DPAD_UP, -1);
	buttons(s->buttons[ACTION_P2_DOWN], SDL_CONTROLLER_BUTTON_DPAD_DOWN, -1);
	buttons(s->buttons[ACTION_P2_LEFT], SDL_CONTROLLER_BUTTON_DPAD_LEFT, -1);
	buttons(s->buttons[ACTION_P2_RIGHT], SDL_CONTROLLER_BUTTON_DPAD_RIGHT, -1);
	buttons(s->buttons[ACTION_P2_ATTACK], SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X);
	buttons(s->buttons[ACTION_P2_JUMP], SDL_CONTROLLER_BUTTON_A, -1);
	buttons(s->menu_buttons, SDL_CONTROLLER_BUTTON_RIGHTSTICK, SDL_CONTROLLER_BUTTON_GUIDE);
	s->left_stick = true;
	s->stick_dead = 50;
}

/* ---- reading ---- */

static char *trim(char *text)
{
	while (*text == ' ' || *text == '\t')
		text++;
	char *end = text + strlen(text);
	while (end > text && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
		*--end = 0;
	return text;
}

static int choice(const char *value, const char *const *names, int count)
{
	for (int i = 0; i < count; i++)
		if (!SDL_strcasecmp(value, names[i]))
			return i;
	return -1;
}

static int boolean(const char *value)
{
	if (!SDL_strcasecmp(value, "true") || !SDL_strcasecmp(value, "on") || !SDL_strcasecmp(value, "yes") ||
		!strcmp(value, "1"))
		return 1;
	if (!SDL_strcasecmp(value, "false") || !SDL_strcasecmp(value, "off") || !SDL_strcasecmp(value, "no") ||
		!strcmp(value, "0"))
		return 0;
	return -1;
}

static int number(const char *value, int low, int high, int *out)
{
	char *end;
	const long n = strtol(value, &end, 10);
	if (end == value || *trim(end) || n < low || n > high)
		return 0;
	*out = (int)n;
	return 1;
}

/* "a, b, c" into its items, in place */
static int items(char *value, char **out, int most)
{
	int count = 0;
	char *item = value;
	while (item && count < most)
	{
		char *comma = strchr(item, ',');
		if (comma)
			*comma = 0;
		item = trim(item);
		if (*item && SDL_strcasecmp(item, "none"))
			out[count++] = item;
		item = comma ? comma + 1 : NULL;
	}
	return count;
}

static int parse_keys(char *value, SDL_Scancode *slot)
{
	char *names[BINDINGS_MAX];
	SDL_Scancode parsed[BINDINGS_MAX] = { SDL_SCANCODE_UNKNOWN };
	const int count = items(value, names, BINDINGS_MAX);
	for (int i = 0; i < count; i++)
		if ((parsed[i] = SDL_GetScancodeFromName(names[i])) == SDL_SCANCODE_UNKNOWN)
			return 0;
	for (int i = 0; i < BINDINGS_MAX; i++)
		slot[i] = parsed[i];
	return 1;
}

static int parse_buttons(char *value, pad_input *slot)
{
	char *names[BINDINGS_MAX];
	pad_input parsed[BINDINGS_MAX] = { { PAD_NONE, 0 } };
	const int count = items(value, names, BINDINGS_MAX);
	for (int i = 0; i < count; i++)
	{
		const SDL_GameControllerButton button = SDL_GameControllerGetButtonFromString(names[i]);
		const SDL_GameControllerAxis axis = SDL_GameControllerGetAxisFromString(names[i]);
		if (button != SDL_CONTROLLER_BUTTON_INVALID)
			parsed[i].kind = PAD_BUTTON, parsed[i].index = button;
		else if (axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
			parsed[i].kind = PAD_TRIGGER, parsed[i].index = axis;
		else
			return 0;
	}
	for (int i = 0; i < BINDINGS_MAX; i++)
		slot[i] = parsed[i];
	return 1;
}

/* One "name = value" of [section]: 0 if it is not known or not taken */
static int set(settings *s, const char *section, const char *name, char *value)
{
	int n, i;
	if (!SDL_strcasecmp(section, "game"))
	{
		if (!SDL_strcasecmp(name, "accuracy"))
		{
			if (!SDL_strcasecmp(value, "mame"))
				return s->accuracy = 0, 1;
			if (!SDL_strcasecmp(value, "board"))
				return s->accuracy = 1, 1;
			return 0;
		}
		if (!SDL_strcasecmp(name, "keep_high_scores") && (n = boolean(value)) >= 0)
			return s->keep_high_scores = n, 1;
		if (!SDL_strcasecmp(name, "keep_replays") && (n = boolean(value)) >= 0)
			return s->keep_replays = n, 1;
		if (!SDL_strcasecmp(name, "check_updates") && (n = boolean(value)) >= 0)
			return s->check_updates = n, 1;
		for (i = 0; i < DIPS; i++)
			if (!SDL_strcasecmp(name, dips[i].name))
			{
				for (int v = 0; v < dips[i].count; v++)
					if (!SDL_strcasecmp(value, dips[i].values[v].name))
					{
						uint8_t *port = dip_port(s, &dips[i]);
						*port = (uint8_t)((*port & ~dips[i].mask) | dips[i].values[v].bits);
						return 1;
					}
				return 0;
			}
		return 0;
	}
	if (!SDL_strcasecmp(section, "fair_knight"))
	{
		if (!SDL_strcasecmp(name, "invincibility") && number(value, 0, 255, &n))
			return s->fk_invincibility = n, 1;
		if (!SDL_strcasecmp(name, "knockback") && (n = boolean(value)) >= 0)
			return s->fk_no_knockback = !n, 1;
		if (!SDL_strcasecmp(name, "air_control") && (n = fk_air_control_parse(value)) >= 0)
			return s->fk_air_control = n, 1;
		return 0;
	}
	if (!SDL_strcasecmp(section, "window"))
	{
		if (!SDL_strcasecmp(name, "scale"))
			return number(value, 1, 8, &s->scale);
		if (!SDL_strcasecmp(name, "fullscreen") && (n = boolean(value)) >= 0)
			return s->fullscreen = n, 1;
		return 0;
	}
	if (!SDL_strcasecmp(section, "video"))
	{
		if (!SDL_strcasecmp(name, "aspect") && (n = choice(value, aspect_names, ASPECTS)) >= 0)
			return s->aspect = n, 1;
		if (!SDL_strcasecmp(name, "scaling") && (n = choice(value, scaling_names, SCALINGS)) >= 0)
			return s->scaling = n, 1;
		if (!SDL_strcasecmp(name, "smooth") && (n = boolean(value)) >= 0)
			return s->smooth = n, 1;
		if (!SDL_strcasecmp(name, "crt") && (n = choice(value, settings_crt_names, 4)) >= 0)
			return s->crt = n, 1;
		if (!SDL_strcasecmp(name, "vsync") && (n = boolean(value)) >= 0)
			return s->vsync = n, 1;
		if (!SDL_strcasecmp(name, "panels") && (n = boolean(value)) >= 0)
			return s->panels = n, 1;
		if (!SDL_strcasecmp(name, "panels_debug") && (n = boolean(value)) >= 0)
			return s->panels_debug = n, 1;
		if (!SDL_strcasecmp(name, "sync") && (n = choice(value, sync_names, SYNCS)) >= 0)
			return s->sync = n, 1;
		return 0;
	}
	if (!SDL_strcasecmp(section, "sound"))
	{
		if (!SDL_strcasecmp(name, "sound") && (n = boolean(value)) >= 0)
			return s->sound = n, 1;
		if (!SDL_strcasecmp(name, "volume"))
			return number(value, 0, 100, &s->volume);
		if (!SDL_strcasecmp(name, "latency"))
			return number(value, 20, 250, &s->latency);
		return 0;
	}
	if (!SDL_strcasecmp(section, "files"))
	{
		if (!SDL_strcasecmp(name, "rom"))
		{
			SDL_strlcpy(s->rom, value, sizeof(s->rom));
			return 1;
		}
		return 0;
	}
	if (!SDL_strcasecmp(section, "keyboard"))
	{
		if ((i = choice(name, settings_action_names, ACTIONS)) >= 0)
			return parse_keys(value, s->keys[i]);
		if ((i = choice(name, hotkey_names, HOTKEYS)) >= 0)
			return parse_keys(value, s->hot_keys[i]);
		return 0;
	}
	if (!SDL_strcasecmp(section, "gamepad"))
	{
		if ((i = choice(name, settings_action_names, ACTIONS)) >= 0)
			return parse_buttons(value, s->buttons[i]);
		if (!SDL_strcasecmp(name, "menu"))
			return parse_buttons(value, s->menu_buttons);
		if (!SDL_strcasecmp(name, "left_stick") && (n = boolean(value)) >= 0)
			return s->left_stick = n, 1;
		if (!SDL_strcasecmp(name, "stick_dead_zone"))
			return number(value, 5, 95, &s->stick_dead);
		return 0;
	}
	return 0;
}

const char *settings_load(settings *s, const char *path, bool *written)
{
	static char problems[512];
	FILE *file = fopen(path, "r");
	*written = false;
	if (!file)
	{
		const char *error = settings_save(s, path);
		*written = error == NULL;
		return error;
	}
	char line[1024], section[64] = "";
	int number_of_line = 0;
	size_t used = 0;
	problems[0] = 0;
	while (fgets(line, sizeof(line), file))
	{
		number_of_line++;
		char *text = trim(line);
		if (!*text || *text == ';' || *text == '#')
			continue;
		if (*text == '[')
		{
			char *end = strchr(text, ']');
			if (end)
			{
				*end = 0;
				SDL_strlcpy(section, trim(text + 1), sizeof(section));
				continue;
			}
		}
		char *equals = strchr(text, '=');
		if (equals)
		{
			*equals = 0;
			if (set(s, section, trim(text), trim(equals + 1)))
				continue;
		}
		if (used < sizeof(problems))
			used += (size_t)snprintf(problems + used, sizeof(problems) - used, "%sline %d", used ? ", " : "",
				number_of_line);
	}
	fclose(file);
	return problems[0] ? problems : NULL;
}

/* ---- writing ---- */

static void write_keys(FILE *file, const char *name, const SDL_Scancode *slot)
{
	fprintf(file, "%s = ", name);
	int written = 0;
	for (int i = 0; i < BINDINGS_MAX; i++)
		if (slot[i] != SDL_SCANCODE_UNKNOWN)
			fprintf(file, "%s%s", written++ ? ", " : "", SDL_GetScancodeName(slot[i]));
	fprintf(file, "%s\n", written ? "" : "none");
}

static void write_buttons(FILE *file, const char *name, const pad_input *slot)
{
	fprintf(file, "%s = ", name);
	int written = 0;
	for (int i = 0; i < BINDINGS_MAX; i++)
	{
		const char *text = slot[i].kind == PAD_BUTTON
			? SDL_GameControllerGetStringForButton((SDL_GameControllerButton)slot[i].index)
			: slot[i].kind == PAD_TRIGGER ? SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis)slot[i].index)
			: NULL;
		if (text)
			fprintf(file, "%s%s", written++ ? ", " : "", text);
	}
	fprintf(file, "%s\n", written ? "" : "none");
}

const char *settings_save(const settings *s, const char *path)
{
	FILE *file = fopen(path, "w");
	if (!file)
		return "it could not be written";
	fprintf(file,
		"; Makaimura Recomp's settings. Lines are \"name = value\"; a line it cannot take keeps its default, and\n"
		"; deleting this file brings every default back.\n\n");
	fprintf(file, "[game]\n; The board's DIP switches, as MAME's makaimurg names them. They are read as the board starts\n"
		"; (and at a reset, F3)\n");
	fprintf(file, "; mame: the board exactly as MAME 0.289 models it (checked against MAME frame by frame); board: what\n"
		"; the real board does and MAME leaves out (docs/accuracy.md: the sprite DMA halting the CPU, the palette flashes). From the\n"
		"; board's start (F3)\naccuracy = %s\n", s->accuracy ? "board" : "mame");
	fprintf(file, "; the ranking kept between sessions in makaimura.hi beside this file (written back as the board "
		"starts)\nkeep_high_scores = %s\n", s->keep_high_scores ? "true" : "false");
	fprintf(file, "; each game recorded, and kept as a replay in replays/ beside this file if you say so at its game "
		"over\n; (the menu's REPLAYS plays them)\nkeep_replays = %s\n", s->keep_replays ? "true" : "false");
	fprintf(file, "; the newest release looked up on GitHub as the game starts: offered on the menu (UPDATE TO ...),\n"
		"; downloaded and installed only when it is chosen (this file, the scores, states and replays are kept)\n"
		"check_updates = %s\n", s->check_updates ? "true" : "false");
	for (int i = 0; i < DIPS; i++)
	{
		const uint8_t port = dips[i].port == 1 ? s->dsw1 : s->dsw2;
		const char *value = NULL;
		for (int v = 0; v < dips[i].count; v++)
			if ((port & dips[i].mask) == dips[i].values[v].bits)
				value = dips[i].values[v].name;
		fprintf(file, "; %s\n%s = %s\n", dips[i].help, dips[i].name, value ? value : "");
	}
	fprintf(file, "\n[fair_knight]\n; Fair Knight's changes to the game (docs/fair-knight/), from the board's start (F3). The arcade's\n"
		"; own game: invincibility = 120, knockback = true, air_control = off\n");
	fprintf(file, "; frames of invincibility after a hit (60 a second; the arcade's 120)\ninvincibility = %d\n",
		s->fk_invincibility);
	fprintf(file, "; false: a hit no longer pushes Arthur back (he hops in place)\nknockback = %s\n",
		s->fk_no_knockback ? "false" : "true");
	fprintf(file, "; off (the arcade's: a jump's direction is fixed), assist (left or right in the air turns it), full (the\n"
		"; jump goes where the stick says, straight up with it centred)\nair_control = %s\n",
		fk_air_control_name(s->fk_air_control));
	fprintf(file, "\n[window]\n; the window's size: the picture's height times this (224 lines each)\nscale = %d\n"
		"; full screen (also F11 or Alt+Enter)\nfullscreen = %s\n", s->scale, s->fullscreen ? "true" : "false");
	fprintf(file, "\n[video]\n; 4:3 (the shape of the arcade's monitor: its 256 x 224 picture's pixels a little wider "
		"than tall)\n; or pixels (each pixel square)\naspect = %s\n", aspect_names[s->aspect]);
	fprintf(file, "; fit (as large as the window lets it), integer (a whole number of times the picture's lines) or"
		" stretch\nscaling = %s\n", scaling_names[s->scaling]);
	fprintf(file, "; true: a soft picture; false: sharp pixels\nsmooth = %s\n", s->smooth ? "true" : "false");
	fprintf(file, "; the picture as a CRT shows it: off; scanlines (the beam's lines); arcade (a slot mask, a soft\n"
		"; beam); pvm (an aperture grille, a sharper beam). Best full screen or in a large window\ncrt = %s\n",
		settings_crt_names[s->crt]);
	fprintf(file, "; the picture shown at the display's refresh (no tearing)\nvsync = %s\n", s->vsync ? "true" : "false");
	fprintf(file, "; the side panels beside the picture (the player, the board; the menu's help), where the window "
		"is wider\n; than the picture (full screen on a wide display; the window opens wide for them)\npanels = %s\n",
		s->panels ? "true" : "false");
	fprintf(file, "; the panels' sections for checking the port too: board mode's palette flashes (docs/accuracy.md)\n"
		"panels_debug = %s\n", s->panels_debug ? "true" : "false");
	fprintf(file, "; board: the board's own pace, 59.637 frames a second, by the clock (exact: on a 60 Hz display a\n"
		"; frame is shown twice every 2.7 seconds); display: a frame a refresh of the display when it is near\n"
		"; 59.637 Hz (smooth, but at 60 Hz the game runs 0.6%% fast, the sound with it)\nsync = %s\n",
		sync_names[s->sync]);
	fprintf(file, "\n[sound]\nsound = %s\n; 0-100\nvolume = %d\n; the sound queued ahead, in ms (20-250): lower "
		"answers sooner, higher is safer from gaps\nlatency = %d\n", s->sound ? "true" : "false", s->volume,
		s->latency);
	fprintf(file, "\n[files]\n; the ROM set: a .zip or a folder holding makaimurg.zip and gng.zip (MAME's). Empty: "
		"looked for\n; beside the program, in its rom folder, and in the folder it is started from\nrom = %s\n",
		s->rom);
	fprintf(file, "\n[keyboard]\n; SDL's key names (\"Z\", \"Left Ctrl\", \"Keypad 8\", \"Space\"...), up to %d each, "
		"with commas; none for none\n", BINDINGS_MAX);
	for (int i = 0; i < ACTIONS; i++)
	{
		if (action_help[i])
			fprintf(file, "; %s\n", action_help[i]);
		write_keys(file, settings_action_names[i], s->keys[i]);
	}
	fprintf(file, "; the port's own: the board's reset, pause, full screen, quit (also in the menu), the menu\n");
	for (int i = 0; i < HOTKEYS; i++)
		write_keys(file, hotkey_names[i], s->hot_keys[i]);
	fprintf(file, "\n[gamepad]\n; the Xbox layout's names: a, b, x, y, back, start, leftshoulder, rightshoulder, "
		"lefttrigger,\n; righttrigger, dpup, dpdown, dpleft, dpright, leftstick, rightstick. The first gamepad "
		"is player 1's (and\n; the coins, the starts, the menu), the second one player 2's (p2_...)\n");
	for (int i = 0; i < ACTIONS; i++)
		write_buttons(file, settings_action_names[i], s->buttons[i]);
	fprintf(file, "; the menu (pressing the right stick, or the guide button)\n");
	write_buttons(file, "menu", s->menu_buttons);
	fprintf(file, "; the left stick moves too (each gamepad's); how far it goes before it counts, in %%\nleft_stick = %s\n"
		"stick_dead_zone = %d\n", s->left_stick ? "true" : "false", s->stick_dead);
	return fclose(file) == 0 ? NULL : "it could not be written";
}

bool settings_pad_pressed(SDL_GameController *pad, pad_input input)
{
	if (input.kind == PAD_BUTTON)
		return SDL_GameControllerGetButton(pad, (SDL_GameControllerButton)input.index) != 0;
	if (input.kind == PAD_TRIGGER)
		return SDL_GameControllerGetAxis(pad, (SDL_GameControllerAxis)input.index) > 16384;
	return false;
}
