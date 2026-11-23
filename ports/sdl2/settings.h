/**
 *    Name:        settings.h
 *    Description: the window port's settings: makaimura.ini beside the program (the game's options, the window,
 *                 the picture, the sound, where the ROM set is, the keys and the gamepad's buttons), read at the
 *                 start, written with every setting and what it does when it is not there
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    The file is text: "name = value" lines under [sections], ; or # comments on their own lines. The keys are
 *    SDL's names ("Z", "Left", "Left Ctrl", "Keypad 8"), the gamepad's SDL's game controller names in the Xbox
 *    layout ("a", "b", "x", "y", "back", "start", "leftshoulder", "rightshoulder", "dpup", "dpdown", "dpleft",
 *    "dpright", "leftstick", "rightstick") and the triggers ("lefttrigger", "righttrigger"); several with commas.
 *    The game's options are the board's DIP switches, by the names MAME's makaimurg gives them (mame -listxml).
 */
#ifndef MAKAIMURA_SETTINGS_H
#define MAKAIMURA_SETTINGS_H

#include <SDL.h>

#include <stdbool.h>
#include <stdint.h>

/* What a key or button does on the board: its inputs (gng/board.h's gng_inputs) */
enum
{
	ACTION_UP, ACTION_DOWN, ACTION_LEFT, ACTION_RIGHT, ACTION_ATTACK, ACTION_JUMP,
	ACTION_START1, ACTION_START2, ACTION_COIN1, ACTION_COIN2, ACTION_SERVICE,
	/* the second player's (3002): a cocktail cabinet's second player, and either player's buttons where the
	   program takes both (the continue: 76D8); their gamepad buttons are the second gamepad's */
	ACTION_P2_UP, ACTION_P2_DOWN, ACTION_P2_LEFT, ACTION_P2_RIGHT, ACTION_P2_ATTACK, ACTION_P2_JUMP,
	ACTIONS
};
#define ACTION_P2_FIRST ACTION_P2_UP

/* What the port does itself */
enum
{
	HOTKEY_RESET,          /* the board's reset (its power cycled: the boot's checks again) */
	HOTKEY_PAUSE,
	HOTKEY_FULLSCREEN,
	HOTKEY_QUIT,
	HOTKEY_MENU,           /* the menu (menu.h): the game's options, the picture, the sound */
	HOTKEY_RECORD,         /* a recording (recorder.h) started or stopped */
	HOTKEY_SAVE_STATE,     /* the board's state into the slot (gng/state.h) */
	HOTKEY_LOAD_STATE,     /* the slot's state back */
	HOTKEY_STATE_SLOT,     /* the next slot */
	HOTKEYS
};

#define BINDINGS_MAX 4

enum { ASPECT_MONITOR, ASPECT_PIXELS, ASPECTS };                 /* 4:3, as the arcade's monitor; square pixels */
enum { SCALING_FIT, SCALING_INTEGER, SCALING_STRETCH, SCALINGS };
enum { SYNC_BOARD, SYNC_DISPLAY, SYNCS };
enum { PAD_NONE, PAD_BUTTON, PAD_TRIGGER };

typedef struct pad_input
{
	int kind;     /* PAD_BUTTON: an SDL_GameControllerButton; PAD_TRIGGER: an SDL_GameControllerAxis */
	int index;
} pad_input;

typedef struct settings
{
	/* [game]: the DIP switches as the board reads them (3003, 3004) */
	uint8_t dsw1, dsw2;
	int accuracy;         /* gng/board.h's gng_accuracy_mode: 0 MAME 1:1, 1 the board's (docs/accuracy.md) */
	bool keep_high_scores;    /* the ranking kept between sessions (hiscore.h) */
	bool keep_replays;        /* each game recorded, kept as a replay if the player says so (replays.h) */
	bool check_updates;       /* the newest release looked up as the program starts (updater.h) */
	/* [fair_knight]: gng/fair_knight.h's changes (docs/fair-knight/), applied as the board starts */
	int fk_invincibility;     /* frames after a hit; 120 is the arcade's */
	bool fk_no_knockback;
	int fk_air_control;       /* fk_air_control: off, assist, full */
	/* [window] */
	int scale;            /* the window's height, in the picture's lines (224 each) */
	bool fullscreen;
	/* [video] */
	int aspect;
	int scaling;
	bool smooth;          /* a soft picture instead of sharp pixels */
	int crt;              /* crt.h's style: off, scanlines, arcade, pvm */
	bool vsync;
	bool panels;          /* the side panels (hud.h) where the window is wider than the picture */
	bool panels_debug;    /* the panels' sections for checking the port (the palette's flashes) */
	int sync;             /* SYNC_BOARD: the board's 59.637 Hz by the clock; SYNC_DISPLAY: a frame a refresh */
	/* [sound] */
	bool sound;
	int volume;           /* 0-100 */
	int latency;          /* the sound queued ahead, in ms */
	/* [files] */
	char rom[512];        /* the ROM set: a .zip or a folder (empty: looked for beside the program) */
	/* [keyboard], [gamepad] */
	SDL_Scancode keys[ACTIONS][BINDINGS_MAX];
	SDL_Scancode hot_keys[HOTKEYS][BINDINGS_MAX];
	pad_input buttons[ACTIONS][BINDINGS_MAX];
	pad_input menu_buttons[BINDINGS_MAX];
	bool left_stick;      /* the left stick moves too */
	int stick_dead;       /* how far it goes before it counts, in % */
	/* (not kept in the file) */
	int state_slot;       /* the save states' slot, 1-STATE_SLOTS */
} settings;

#define STATE_SLOTS 8

extern const char *const settings_action_names[ACTIONS];
extern const char *const settings_aspect_names[ASPECTS];
extern const char *const settings_scaling_names[SCALINGS];
extern const char *const settings_crt_names[4];

/* The game's options (the DIP switches) for the menu: how many, each one's name ("bonus_life") and value's name
   ("20k 70k every 70k"), the next or previous value (direction 1 or -1) */
int settings_dip_count(void);
const char *settings_dip_name(int dip);
const char *settings_dip_value(const settings *s, int dip);
void settings_dip_step(settings *s, int dip, int direction);

void settings_defaults(settings *s);

/* The file over 's' (which holds the defaults): NULL, or what was wrong in it (those lines keep their defaults).
   A missing file is written with the defaults, and *written set */
const char *settings_load(settings *s, const char *path, bool *written);

/* Every setting into the file, with what it does: NULL, or what went wrong */
const char *settings_save(const settings *s, const char *path);

bool settings_pad_pressed(SDL_GameController *pad, pad_input input);

#endif
