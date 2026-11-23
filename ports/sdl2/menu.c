/**
 *    Name:        menu.c
 *    Description: the window's menu (menu.h)
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    The letters are the game's own: its text layer's characters (gng/video.h's gng_video_char_pen), whose codes
 *    are ASCII's (upper and lower case, digits, punctuation); pen 0 the letter, 1 and 2 its shading, 3 clear.
 *    32 columns of 8 pixels, 28 rows. Two pages: the main one, and the controls (each action and the window's own
 *    keys: Enter, then the key or the pad's button that takes it; Delete gives it its defaults back).
 */
#include "gng/fair_knight.h"
#include "menu.h"

#include "gng/video.h"
#include "updater.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

enum { HEADER, BLANK, DIP, ACCURACY, ASPECT, SCALING, SMOOTH, FULLSCREEN, VOLUME, CONTROLS, RESUME, RESET, QUIT,
	ACTION, HOTKEY, DEFAULTS, BACK, GAME_PAGE, VIDEO_PAGE, PANELS, SAVE_STATE, LOAD_STATE,
	PRACTICE_PAGE, PRACTICE, PRACTICE_AREA, PRACTICE_LOOP, PRACTICE_WEAPON, PRACTICE_LIVES, REPLAYS_PAGE, REPLAY, KEEP_REPLAY, DROP_REPLAY, UPDATE, UPDATE_YES, UPDATE_NO,
	CHECK_UPDATES, CRT };

typedef struct item { int kind; int dip; const char *label; } item;

static item main_items[40], control_items[40], game_items[40], video_items[40], practice_items[16];
static int main_count, control_count, game_count, video_count, practice_count;
static practice_setup practice = { 0, 0, 1, 0 };    /* (kept while the window is open) */
static item replay_items[MENU_REPLAYS + 4];
static int replay_count, replay_chosen;
static char replay_labels[MENU_REPLAYS][16], replay_values[MENU_REPLAYS][16];
static int confirming = -1;           /* the replay entry whose deletion waits for Enter */
static item question_items[6];
static int question_count;
static item update_items[6];
static int update_count;
static char update_label[32], update_header[40];

#ifndef MAKAIMURA_VERSION
#define MAKAIMURA_VERSION "?"
#endif
static item *items = main_items;
static int item_count;
static int selected, main_selected;
static int capturing;                 /* the selected control waits for its key or button */

#define COLUMNS 32
#define VALUE_COLUMN 14

static const uint32_t white = 0xF0F0F0, yellow = 0xF8D040, cyan = 0x60D8F0, red = 0xF06070, grey = 0x9098A0,
	bar = 0x203070;

static void add(int kind, int dip, const char *label)
{
	items[item_count].kind = kind;
	items[item_count].dip = dip;
	items[item_count].label = label;
	item_count++;
}

static const char *const action_labels[ACTIONS] = {
	"UP", "DOWN", "LEFT", "RIGHT", "ATTACK", "JUMP", "START 1P", "START 2P", "COIN 1", "COIN 2", "SERVICE",
	"UP", "DOWN", "LEFT", "RIGHT", "ATTACK", "JUMP" };
static const char *const hotkey_labels[HOTKEYS] = { "RESET", "PAUSE", "FULL SCREEN", "QUIT", "MENU", "RECORD",
	"SAVE STATE", "LOAD STATE", "STATE SLOT" };

static void build_controls(void)
{
	items = control_items;
	item_count = 0;
	add(HEADER, 0, "CONTROLS");
	for (int a = 0; a < ACTION_P2_FIRST; a++)
		add(ACTION, a, action_labels[a]);
	add(HEADER, 0, "PLAYER 2: 2ND PAD");
	for (int a = ACTION_P2_FIRST; a < ACTIONS; a++)
		add(ACTION, a, action_labels[a]);
	add(HEADER, 0, "THE WINDOW'S KEYS");
	for (int h = 0; h < HOTKEYS; h++)
		add(HOTKEY, h, hotkey_labels[h]);
	add(DEFAULTS, 0, "ALL TO DEFAULTS");
	add(BACK, 0, "BACK");
	control_count = item_count;
}

static int selectable(int i)
{
	return items[i].kind != HEADER && items[i].kind != BLANK;
}

static void build_video(void)
{
	items = video_items;
	item_count = 0;
	add(HEADER, 0, "PICTURE AND SOUND");
	add(ASPECT, 0, "SHAPE");
	add(SCALING, 0, "SCALING");
	add(SMOOTH, 0, "PIXELS");
	add(CRT, 0, "CRT");
	add(FULLSCREEN, 0, "FULL SCREEN");
	add(PANELS, 0, "SIDE PANELS");
	add(VOLUME, 0, "VOLUME");
	add(BLANK, 0, "");
	add(BACK, 0, "BACK");
	video_count = item_count;
}

static void build_practice(void)
{
	items = practice_items;
	item_count = 0;
	add(HEADER, 0, "PRACTICE");
	add(PRACTICE_AREA, 0, "AREA");
	add(PRACTICE_LOOP, 0, "LOOP");
	add(PRACTICE_WEAPON, 0, "WEAPON");
	add(PRACTICE_LIVES, 0, "LIVES");
	add(BLANK, 0, "");
	add(PRACTICE, 0, "START");
	add(BACK, 0, "BACK");
	practice_count = item_count;
}

practice_setup menu_practice_setup(void)
{
	return practice;
}

/* the weapons in the menu's order: the lance first (the game's own start), then as the items come in stage 1 */
static const int weapon_order[PRACTICE_WEAPONS] = { 1, 4, 0, 2, 3 };

const char *menu_weapon_name(int weapon)
{
	static const char *const names[PRACTICE_WEAPONS] = { "TORCH", "LANCE", "AXE", "CROSS", "DAGGER" };
	return weapon >= 0 && weapon < PRACTICE_WEAPONS ? names[weapon] : "?";
}

void menu_replays(int count, const char labels[][16], const char values[][16])
{
	item *const kept = items;
	const int kept_count = item_count;
	count = count < MENU_REPLAYS ? count : MENU_REPLAYS;
	items = replay_items;
	item_count = 0;
	add(HEADER, 0, count ? "REPLAYS: THE NEWEST FIRST" : "NO REPLAYS YET");
	for (int i = 0; i < count; i++)
	{
		memcpy(replay_labels[i], labels[i], sizeof(replay_labels[i]));
		memcpy(replay_values[i], values[i], sizeof(replay_values[i]));
		add(REPLAY, i, replay_labels[i]);
	}
	add(BLANK, 0, "");
	add(BACK, 0, "BACK");
	replay_count = item_count;
	items = kept;
	item_count = kept_count;
}

int menu_replay_chosen(void)
{
	return replay_chosen;
}

static void show_page(item *page, int count, int first);

void menu_show_replays(int entry)
{
	show_page(replay_items, replay_count, 0);
	/* (the entry where the deleted one was, or the last) */
	for (int i = 0; i < item_count; i++)
		if (items[i].kind == REPLAY && items[i].dip <= entry)
			selected = i;
}

void menu_ask_keep_replay(void)
{
	if (!main_count)
		menu_open();
	items = question_items;
	item_count = 0;
	add(HEADER, 0, "GAME OVER");
	add(BLANK, 0, "");
	add(KEEP_REPLAY, 0, "KEEP REPLAY");
	add(DROP_REPLAY, 0, "LET IT GO");
	question_count = item_count;
	show_page(question_items, question_count, 0);
}

/* a newer release (updater.h): the page that asks, and shows the download */
static void build_update(void)
{
	items = update_items;
	item_count = 0;
	snprintf(update_header, sizeof(update_header), "UPDATE: V%s TO V%s", MAKAIMURA_VERSION, updater_latest());
	add(HEADER, 0, update_header);
	add(BLANK, 0, "");
	add(UPDATE_YES, 0, "YES, UPDATE");
	add(UPDATE_NO, 0, "NOT NOW");
	update_count = item_count;
}

static void build_main(void)
{
	items = main_items;
	item_count = 0;
	add(HEADER, 0, "PAUSED");
	/* (a newer release first, where it is seen at once) */
	const int update_state = updater_state();
	if (update_state == UPDATER_AVAILABLE || update_state == UPDATER_DOWNLOADING || update_state == UPDATER_READY ||
		(update_state == UPDATER_FAILED && updater_latest()[0]))
	{
		snprintf(update_label, sizeof(update_label), "UPDATE TO V%s", updater_latest());
		add(UPDATE, 0, update_label);
	}
	add(RESUME, 0, "RESUME");
	add(SAVE_STATE, 0, "SAVE STATE");
	add(LOAD_STATE, 0, "LOAD STATE");
	add(PRACTICE_PAGE, 0, "PRACTICE");
	add(REPLAYS_PAGE, 0, "REPLAYS");
	add(BLANK, 0, "");
	add(GAME_PAGE, 0, "GAME OPTIONS");
	add(VIDEO_PAGE, 0, "PICTURE AND SOUND");
	add(CONTROLS, 0, "CONTROLS");
	add(ACCURACY, 0, "ACCURACY");
	add(BLANK, 0, "");
	add(RESET, 0, "RESET THE BOARD");
	add(QUIT, 0, "QUIT");
	main_count = item_count;
}

static void build(void)
{
	static char dip_labels[16][24];
	items = game_items;
	item_count = 0;
	add(HEADER, 0, "GAME OPTIONS");
	for (int d = 0; d < settings_dip_count() && d < 16; d++)
	{
		/* "bonus_life" as "BONUS LIFE" */
		const char *name = settings_dip_name(d);
		size_t i = 0;
		for (; name[i] && i < sizeof(dip_labels[d]) - 1; i++)
			dip_labels[d][i] = name[i] == '_' ? ' ' : (char)toupper((unsigned char)name[i]);
		dip_labels[d][i] = 0;
		/* (the labels end before the values' column) */
		add(DIP, d, !strcmp(name, "coinage_affects") ? "COINAGE FOR" : dip_labels[d]);
	}
	add(BLANK, 0, "");
	add(CHECK_UPDATES, 0, "CHECK FOR UPDATES");
	add(BLANK, 0, "");
	add(BACK, 0, "BACK");
	game_count = item_count;
}

static void show_page(item *page, int count, int first)
{
	items = page;
	item_count = count;
	selected = first;
	while (!selectable(selected))
		selected++;
	capturing = 0;
}

void menu_open(void)
{
	/* (the main page again each time: a newer release may have been found since) */
	if (main_count)
		build_main();
	if (!main_count)
	{
		build_controls();
		build_video();
		build_practice();
		build();
		build_main();
	}
	show_page(main_items, main_count, 0);
}

/* ---- the controls ---- */

static void clear_key(SDL_Scancode *slot, SDL_Scancode code)
{
	for (int i = 0; i < BINDINGS_MAX; i++)
		if (slot[i] == code)
		{
			for (int j = i; j < BINDINGS_MAX - 1; j++)
				slot[j] = slot[j + 1];
			slot[BINDINGS_MAX - 1] = SDL_SCANCODE_UNKNOWN;
			i--;
		}
}

static void clear_pad(pad_input *slot, pad_input input)
{
	for (int i = 0; i < BINDINGS_MAX; i++)
		if (slot[i].kind == input.kind && slot[i].index == input.index)
		{
			for (int j = i; j < BINDINGS_MAX - 1; j++)
				slot[j] = slot[j + 1];
			slot[BINDINGS_MAX - 1].kind = PAD_NONE;
			slot[BINDINGS_MAX - 1].index = 0;
			i--;
		}
}

/* The key now the selected control's only key, taken from every other control (one key, one thing) */
static void bind_key(settings *s, SDL_Scancode code)
{
	for (int a = 0; a < ACTIONS; a++)
		clear_key(s->keys[a], code);
	for (int h = 0; h < HOTKEYS; h++)
		clear_key(s->hot_keys[h], code);
	SDL_Scancode *slot = items[selected].kind == ACTION ? s->keys[items[selected].dip] :
		s->hot_keys[items[selected].dip];
	for (int i = 0; i < BINDINGS_MAX; i++)
		slot[i] = SDL_SCANCODE_UNKNOWN;
	slot[0] = code;
}

/* The pad's button or trigger now the selected action's only one (the menu's own: its button), taken from the
   others of the same gamepad (player 2's actions are the second one's) */
static void bind_pad(settings *s, pad_input input)
{
	const int second = items[selected].kind == ACTION && items[selected].dip >= ACTION_P2_FIRST;
	for (int a = 0; a < ACTIONS; a++)
		if ((a >= ACTION_P2_FIRST) == second)
			clear_pad(s->buttons[a], input);
	if (!second)
		clear_pad(s->menu_buttons, input);
	pad_input *slot = NULL;
	if (items[selected].kind == ACTION)
		slot = s->buttons[items[selected].dip];
	else if (items[selected].dip == HOTKEY_MENU)
		slot = s->menu_buttons;
	if (!slot)
		return;    /* (the other window keys have no buttons) */
	for (int i = 0; i < BINDINGS_MAX; i++)
		slot[i].kind = PAD_NONE, slot[i].index = 0;
	slot[0] = input;
}

/* The selected control's defaults back (settings.c's) */
static void control_defaults(settings *s, const item *it)
{
	settings defaults;
	settings_defaults(&defaults);
	if (it->kind == ACTION)
	{
		memcpy(s->keys[it->dip], defaults.keys[it->dip], sizeof(s->keys[0]));
		memcpy(s->buttons[it->dip], defaults.buttons[it->dip], sizeof(s->buttons[0]));
	}
	else if (it->kind == HOTKEY)
	{
		memcpy(s->hot_keys[it->dip], defaults.hot_keys[it->dip], sizeof(s->hot_keys[0]));
		if (it->dip == HOTKEY_MENU)
			memcpy(s->menu_buttons, defaults.menu_buttons, sizeof(s->menu_buttons));
	}
	else if (it->kind == DEFAULTS)
	{
		memcpy(s->keys, defaults.keys, sizeof(s->keys));
		memcpy(s->buttons, defaults.buttons, sizeof(s->buttons));
		memcpy(s->hot_keys, defaults.hot_keys, sizeof(s->hot_keys));
		memcpy(s->menu_buttons, defaults.menu_buttons, sizeof(s->menu_buttons));
	}
}

/* An event while a control waits: its key, button or trigger (Escape: left as it was) */
static void capture(const SDL_Event *event, settings *s)
{
	if (event->type == SDL_KEYDOWN)
	{
		if (event->key.repeat)
			return;
		if (event->key.keysym.scancode != SDL_SCANCODE_ESCAPE)
			bind_key(s, event->key.keysym.scancode);
		capturing = 0;
	}
	else if (event->type == SDL_CONTROLLERBUTTONDOWN)
	{
		bind_pad(s, (pad_input){ PAD_BUTTON, event->cbutton.button });
		capturing = 0;
	}
	else if (event->type == SDL_CONTROLLERAXISMOTION && event->caxis.value > 16384 &&
		(event->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || event->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT))
	{
		bind_pad(s, (pad_input){ PAD_TRIGGER, event->caxis.axis });
		capturing = 0;
	}
}

static void step(settings *s, int direction, int *changed)
{
	const item *it = &items[selected];
	switch (it->kind)
	{
	case DIP:
		settings_dip_step(s, it->dip, direction);
		break;
	case ACCURACY:
		s->accuracy = !s->accuracy;
		break;
	case ASPECT:
		s->aspect = (s->aspect + direction + ASPECTS) % ASPECTS;
		break;
	case SCALING:
		s->scaling = (s->scaling + direction + SCALINGS) % SCALINGS;
		break;
	case SMOOTH:
		s->smooth = !s->smooth;
		break;
	case PANELS:
		s->panels = !s->panels;
		break;
	case CHECK_UPDATES:
		s->check_updates = !s->check_updates;    /* (from the next start) */
		break;
	case CRT:
		s->crt = (s->crt + direction + 4) % 4;
		break;
	case PRACTICE_AREA:
		practice.area = (practice.area + direction + PRACTICE_AREAS) % PRACTICE_AREAS;
		break;
	case PRACTICE_LOOP:
		practice.loop = !practice.loop;
		break;
	case PRACTICE_WEAPON:
	{
		int at = 0;
		while (at < PRACTICE_WEAPONS - 1 && weapon_order[at] != practice.weapon)
			at++;
		practice.weapon = weapon_order[(at + direction + PRACTICE_WEAPONS) % PRACTICE_WEAPONS];
		break;
	}
	case PRACTICE_LIVES:
		practice.lives = (practice.lives + direction + PRACTICE_MOST_LIVES + 1) % (PRACTICE_MOST_LIVES + 1);
		break;
	case SAVE_STATE: case LOAD_STATE:
		s->state_slot = (s->state_slot - 1 + direction + STATE_SLOTS) % STATE_SLOTS + 1;
		break;
	case FULLSCREEN:
		*changed |= MENU_CHANGED_FULLSCREEN;
		break;
	case VOLUME:
		s->volume += direction * 10;
		s->volume = s->volume < 0 ? 0 : s->volume > 100 ? 100 : s->volume;
		*changed |= MENU_CHANGED_VOLUME;
		break;
	default:
		break;
	}
}

static void move(int direction)
{
	do
		selected = (selected + direction + item_count) % item_count;
	while (!selectable(selected));
}

int menu_event(const SDL_Event *event, settings *s, int *changed)
{
	*changed = MENU_CHANGED_NONE;
	if (capturing)
	{
		capture(event, s);
		return MENU_STAY;
	}
	int up = 0, down = 0, left = 0, right = 0, accept = 0, back = 0, erase = 0;
	const int was_confirming = confirming;
	confirming = -1;
	if (event->type == SDL_KEYDOWN)
	{
		switch (event->key.keysym.scancode)
		{
		case SDL_SCANCODE_DELETE: erase = 1; break;
		case SDL_SCANCODE_UP: up = 1; break;
		case SDL_SCANCODE_DOWN: down = 1; break;
		case SDL_SCANCODE_LEFT: left = 1; break;
		case SDL_SCANCODE_RIGHT: right = 1; break;
		case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_SPACE: accept = 1; break;
		case SDL_SCANCODE_BACKSPACE: back = 1; break;
		default:
			for (int i = 0; i < BINDINGS_MAX; i++)
				if (s->hot_keys[HOTKEY_MENU][i] == event->key.keysym.scancode && event->key.keysym.scancode)
					back = 1;
			break;
		}
	}
	else if (event->type == SDL_CONTROLLERBUTTONDOWN)
	{
		switch (event->cbutton.button)
		{
		case SDL_CONTROLLER_BUTTON_DPAD_UP: up = 1; break;
		case SDL_CONTROLLER_BUTTON_DPAD_DOWN: down = 1; break;
		case SDL_CONTROLLER_BUTTON_DPAD_LEFT: left = 1; break;
		case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: right = 1; break;
		case SDL_CONTROLLER_BUTTON_A: accept = 1; break;
		case SDL_CONTROLLER_BUTTON_B: case SDL_CONTROLLER_BUTTON_START: back = 1; break;
		default:
			for (int i = 0; i < BINDINGS_MAX; i++)
				if (s->menu_buttons[i].kind == PAD_BUTTON && s->menu_buttons[i].index == event->cbutton.button)
					back = 1;
			break;
		}
	}
	if (items == question_items)
		back = erase = 0;    /* (the question waits for its answer) */
	if (erase && items[selected].kind == REPLAY)
	{
		/* a replay deleted: Delete, then Enter */
		confirming = selected;
		return MENU_STAY;
	}
	if (accept && was_confirming == selected && items[selected].kind == REPLAY)
	{
		replay_chosen = items[selected].dip;
		return MENU_DELETE_REPLAY;
	}
	if (back && items != main_items)
	{
		show_page(main_items, main_count, main_selected);
		return MENU_STAY;
	}
	if (back)
		return MENU_RESUME;
	if (erase)
		control_defaults(s, &items[selected]);
	if (up)
		move(-1);
	if (down)
		move(1);
	if (left)
		step(s, -1, changed);
	if (right)
		step(s, 1, changed);
	if (accept)
	{
		switch (items[selected].kind)
		{
		case RESUME: return MENU_RESUME;
		case RESET: return MENU_RESET;
		case QUIT: return MENU_QUIT;
		case SAVE_STATE: return MENU_SAVE_STATE;
		case LOAD_STATE: return MENU_LOAD_STATE;
		case PRACTICE:
			show_page(main_items, main_count, main_selected);
			return MENU_PRACTICE;
		case PRACTICE_PAGE:
			main_selected = selected;
			show_page(practice_items, practice_count, 0);
			break;
		case REPLAYS_PAGE:
			main_selected = selected;
			show_page(replay_items, replay_count, 0);
			break;
		case REPLAY:
			replay_chosen = items[selected].dip;
			show_page(main_items, main_count, main_selected);
			return MENU_REPLAY;
		case UPDATE:
			main_selected = selected;
			build_update();
			show_page(update_items, update_count, 0);
			break;
		case UPDATE_YES:
			updater_download();    /* (on its thread: the entry shows how far it is; main.c installs it once there) */
			break;
		case UPDATE_NO:
			show_page(main_items, main_count, main_selected);
			break;
		case KEEP_REPLAY:
			show_page(main_items, main_count, 1);
			return MENU_KEEP_REPLAY;
		case DROP_REPLAY:
			show_page(main_items, main_count, 1);
			return MENU_DROP_REPLAY;
		case CONTROLS:
			main_selected = selected;
			show_page(control_items, control_count, 0);
			break;
		case GAME_PAGE:
			main_selected = selected;
			show_page(game_items, game_count, 0);
			break;
		case VIDEO_PAGE:
			main_selected = selected;
			show_page(video_items, video_count, 0);
			break;
		case ACTION: case HOTKEY: capturing = 1; break;
		case DEFAULTS: control_defaults(s, &items[selected]); break;
		case BACK: show_page(main_items, main_count, main_selected); break;
		default: step(s, 1, changed); break;
		}
	}
	return MENU_STAY;
}

/* ---- drawing ---- */

static void put_char(uint32_t *pixels, int column, int row, char c, uint32_t colour)
{
	for (int y = 0; y < 8; y++)
		for (int x = 0; x < 8; x++)
		{
			const int pen = gng_video_char_pen((unsigned char)c, x, y);
			if (pen == 3)
				continue;
			/* pen 0 the letter, 1 and 2 its shading */
			const int level = pen == 0 ? 256 : pen == 1 ? 170 : 110;
			const uint32_t r = ((colour >> 16) & 0xFF) * level >> 8, g = ((colour >> 8) & 0xFF) * level >> 8,
				b = (colour & 0xFF) * level >> 8;
			pixels[(row * 8 + y) * GNG_SCREEN_WIDTH + column * 8 + x] = r << 16 | g << 8 | b;
		}
}

static void put_text(uint32_t *pixels, int column, int row, const char *text, uint32_t colour)
{
	for (; *text && column < COLUMNS; text++, column++)
		if (*text != ' ')
			put_char(pixels, column, row, *text, colour);
}

static void fill_row(uint32_t *pixels, int row, uint32_t colour)
{
	for (int y = row * 8; y < row * 8 + 8; y++)
		for (int x = 0; x < GNG_SCREEN_WIDTH; x++)
			pixels[y * GNG_SCREEN_WIDTH + x] = colour;
}

static void upper(char *out, size_t size, const char *in)
{
	size_t i = 0;
	for (; in[i] && i < size - 1; i++)
		out[i] = (char)toupper((unsigned char)in[i]);
	out[i] = 0;
}

/* a pad's input, short: "A", "LB", "LT", "D UP" */
static const char *pad_name(pad_input input, char *buffer, size_t size)
{
	static const struct { const char *sdl, *shown; } names[] = {
		{ "leftshoulder", "LB" }, { "rightshoulder", "RB" }, { "lefttrigger", "LT" }, { "righttrigger", "RT" },
		{ "leftstick", "LS" }, { "rightstick", "RS" }, { "dpup", "D UP" }, { "dpdown", "D DOWN" },
		{ "dpleft", "D LEFT" }, { "dpright", "D RIGHT" } };
	const char *name = input.kind == PAD_BUTTON ?
		SDL_GameControllerGetStringForButton((SDL_GameControllerButton)input.index) :
		SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis)input.index);
	if (!name)
		return "?";
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
		if (!strcmp(name, names[i].sdl))
			return names[i].shown;
	upper(buffer, size, name);
	return buffer;
}

/* "Z, LEFT CTRL / B, X": the keys, then the pad's */
static const char *bindings(const SDL_Scancode *keys, const pad_input *pad, char *buffer, size_t size)
{
	char name[32];
	size_t used = 0;
	buffer[0] = 0;
	for (int i = 0; i < BINDINGS_MAX && keys[i] != SDL_SCANCODE_UNKNOWN; i++)
	{
		upper(name, sizeof(name), SDL_GetScancodeName(keys[i]));
		used += (size_t)snprintf(buffer + used, size - used, "%s%s", i ? ", " : "", name);
		if (used >= size) return buffer;
	}
	for (int i = 0; pad && i < BINDINGS_MAX && pad[i].kind != PAD_NONE; i++)
	{
		used += (size_t)snprintf(buffer + used, size - used, "%s%s", i ? ", " : used ? " / " : "",
			pad_name(pad[i], name, sizeof(name)));
		if (used >= size) return buffer;
	}
	if (!buffer[0])
		snprintf(buffer, size, "NONE");
	return buffer;
}

static const char *value_of(const item *it, const settings *s, char *buffer, size_t size)
{
	switch (it->kind)
	{
	case ACTION:
		return bindings(s->keys[it->dip], s->buttons[it->dip], buffer, size);
	case HOTKEY:
		return bindings(s->hot_keys[it->dip], it->dip == HOTKEY_MENU ? s->menu_buttons : NULL, buffer, size);
	case DIP: upper(buffer, size, settings_dip_value(s, it->dip)); return buffer;
	case ACCURACY: return s->accuracy ? "BOARD" : "MAME 1:1";
	case ASPECT: return s->aspect == ASPECT_MONITOR ? "4:3 MONITOR" : "SQUARE PIXELS";
	case SCALING: upper(buffer, size, settings_scaling_names[s->scaling]); return buffer;
	case SMOOTH: return s->smooth ? "SMOOTH" : "SHARP";
	case FULLSCREEN: return s->fullscreen ? "ON" : "OFF";
	case VOLUME: snprintf(buffer, size, "%d", s->volume); return buffer;
	case CONTROLS: case GAME_PAGE: case VIDEO_PAGE: case PRACTICE_PAGE: case REPLAYS_PAGE: return ">";
	case REPLAY: return confirming >= 0 && it == &items[confirming] ? "DELETE? ENTER" : replay_values[it->dip];
	case CHECK_UPDATES: return s->check_updates ? "ON" : "OFF";
	case CRT: upper(buffer, size, settings_crt_names[s->crt]); return buffer;
	case UPDATE: return ">";
	case UPDATE_YES:
		switch (updater_state())
		{
		case UPDATER_DOWNLOADING: snprintf(buffer, size, "%d%%", updater_progress()); return buffer;
		case UPDATER_READY: return "INSTALLING";
		case UPDATER_FAILED: return "FAILED: AGAIN?";
		default: return "";
		}
	case PANELS: return s->panels ? "ON" : "OFF";
	case SAVE_STATE: case LOAD_STATE: snprintf(buffer, size, "SLOT %d", s->state_slot); return buffer;
	case PRACTICE_AREA: snprintf(buffer, size, "%d", practice.area + 1); return buffer;
	case PRACTICE_LOOP: return practice.loop ? "2ND" : "1ST";
	case PRACTICE_WEAPON: return menu_weapon_name(practice.weapon);
	case PRACTICE_LIVES:
		if (!practice.lives)
			return "GAME OPTIONS";
		snprintf(buffer, size, "%d", practice.lives);
		return buffer;
	default: return "";
	}
}

static const char *hint_of(const item *it)
{
	switch (it->kind)
	{
	case DIP: return "READ AS THE BOARD STARTS: F3";
	case ACCURACY: return "FROM THE BOARD'S START: F3";
	case ASPECT: return "AS THE ARCADE'S MONITOR, OR 1:1";
	case RESET: return "THE BOARD'S POWER CYCLED";
	case SAVE_STATE: case LOAD_STATE: return "LEFT, RIGHT: THE SLOT";
	case PRACTICE_AREA: case PRACTICE_LOOP: case PRACTICE_WEAPON: case PRACTICE_LIVES: return "LEFT, RIGHT: CHANGE";
	case REPLAY: return "ENTER: PLAY  DELETE: DELETE IT";
	case ACTION: case HOTKEY: return "ENTER: CHANGE  DELETE: DEFAULT";
	default: return "";
	}
}

/* what an entry does, for the side panel */
static const char *help_of(const item *it)
{
	switch (it->kind)
	{
	case RESUME: return "BACK TO THE GAME, FROM THE SAME FRAME.";
	case SAVE_STATE: return "THE WHOLE BOARD INTO THE SLOT (LEFT, RIGHT: WHICH ONE), TO GO ON FROM LATER EXACTLY AS IT "
		"WOULD HAVE. KEYS: F5 SAVES, F6 THE NEXT SLOT, F7 LOADS.";
	case PRACTICE_PAGE: return "A GAME FROM THE START OF ANY AREA, IN EITHER LOOP, WITH THE WEAPON AND THE LIVES CHOSEN.";
	case PRACTICE_AREA: return "WHERE THE GAME STARTS. AREA 7 IS THE LAST ROOM: ONLY THE CROSS GETS THROUGH IT.";
	case PRACTICE_LOOP: return "THE FIRST TIME THROUGH THE GAME, OR THE SECOND (WHERE THE LAST ROOM LEADS TO THE ENDING).";
	case PRACTICE_WEAPON: return "WHAT ARTHUR CARRIES AS THE AREA STARTS. THE ITEMS ON THE WAY CHANGE IT AS IN THE GAME.";
	case PRACTICE_LIVES: return "ARTHUR'S LIVES, OR AS MANY AS THE GAME OPTIONS GIVE.";
	case REPLAYS_PAGE: return "THE GAMES KEPT, AS THEY WERE PLAYED: AT EACH GAME OVER, KEEP ITS REPLAY OR LET IT GO. "
		"THEY ARE IN REPLAYS/.";
	case REPLAY: return "THIS GAME AGAIN, FRAME FOR FRAME, FROM ITS COIN (OR THE STATE IT STARTED FROM: +) TO ITS GAME "
		"OVER, IN THE MODE IT WAS PLAYED IN. THE KEYS WAIT; AT ITS END THE GAME IS YOURS. RESET (F3) ENDS IT. "
		"DELETE, THEN ENTER: GONE.";
	case KEEP_REPLAY: return "THE GAME JUST PLAYED KEPT IN REPLAYS/, FROM ITS COIN TO ITS GAME OVER: THE MENU'S "
		"REPLAYS PLAYS IT.";
	case DROP_REPLAY: return "THE GAME JUST PLAYED NOT KEPT.";
	case UPDATE: return "A NEWER VERSION OF THE GAME IS OUT: DOWNLOADED AND PUT IN PLACE OF THIS ONE IF YOU SAY SO, "
		"THEN THE GAME STARTS AGAIN. YOUR SETTINGS, SCORES, STATES, REPLAYS AND ROM SET ARE KEPT.";
	case UPDATE_YES: return updater_state() == UPDATER_FAILED ? updater_problem() :
		"THE NEW VERSION DOWNLOADED FROM GITHUB, CHECKED, PUT IN PLACE; THE GAME STARTS AGAIN.";
	case UPDATE_NO: return "LATER: IT STAYS ON THE MENU.";
	case CHECK_UPDATES: return "THE NEWEST VERSION LOOKED UP ON GITHUB AS THE GAME STARTS (NOTHING IS SENT BUT THE "
		"REQUEST): OFFERED ON THE MENU, INSTALLED ONLY IF YOU CHOOSE IT. FROM THE NEXT START.";
	case PRACTICE: return "A NEW GAME FROM THE AREA'S START: THE BOARD'S OWN GAME WITH ITS STAGE, LOOP, WEAPON AND "
		"LIVES SET AS THE PLAYER STARTS. THE FIRST TIME AN AREA IS PREPARED (A FEW SECONDS, UNSEEN), THEN KEPT IN "
		"STATES/.";
	case LOAD_STATE: return "THE GAME BACK AS THE SLOT KEPT IT (LEFT, RIGHT: WHICH ONE). A STATE IS FOR THE ACCURACY MODE "
		"IT WAS SAVED IN.";
	case GAME_PAGE: return "THE BOARD'S DIP SWITCHES: LIVES, DIFFICULTY, BONUS LIFE, COINAGE... READ AS THE BOARD STARTS (F3).";
	case VIDEO_PAGE: return "THE PICTURE'S SHAPE AND SIZE, SHARP OR SMOOTH PIXELS, FULL SCREEN, THESE PANELS, THE VOLUME.";
	case CONTROLS: return "EVERY KEY AND BUTTON, PLAYER 2'S TOO, AND THE WINDOW'S OWN KEYS.";
	case ACCURACY: return "MAME 1:1: THE BOARD AS MAME 0.289 MODELS IT. BOARD: WHAT THE REAL BOARD ALSO DOES: THE SPRITE "
		"COPY HALTING THE CPU (MORE SLOWDOWN) AND THE PALETTE'S FLASHES. FROM THE BOARD'S START (F3).";
	case RESET: return "THE BOARD'S POWER CYCLED: ITS CHECKS AND THE BOOT AGAIN.";
	case QUIT: return "THE SETTINGS ARE KEPT IN MAKAIMURA.INI.";
	case PANELS: return "THESE PANELS, WHERE THE WINDOW IS WIDER THAN THE PICTURE.";
	case ASPECT: return "4:3 AS THE ARCADE'S MONITOR SHOWS IT, OR EACH PIXEL SQUARE.";
	case SCALING: return "FIT: AS LARGE AS IT GOES. INTEGER: WHOLE TIMES THE PICTURE'S LINES. STRETCH: THE WHOLE WINDOW.";
	case SMOOTH: return "SHARP PIXELS, OR A SOFT PICTURE.";
	case CRT: return "THE PICTURE AS A CRT SHOWS IT. SCANLINES: THE BEAM'S LINES, WIDER WHERE IT IS BRIGHT. ARCADE: A "
		"SLOT MASK TOO. PVM: AN APERTURE GRILLE, A SHARPER BEAM. BEST FULL SCREEN OR IN A LARGE WINDOW.";
	case DIP: return "READ AS THE BOARD STARTS: RESET IT (F3) FOR A CHANGE TO COUNT.";
	case ACTION: case HOTKEY: return "ENTER: CHANGE IT (A KEY, OR THE PAD'S BUTTON). DELETE: ITS DEFAULT.";
	case DEFAULTS: return "EVERY CONTROL BACK TO ITS DEFAULT.";
	default: return hint_of(it);
	}
}

void menu_help(const char **title, const char **text)
{
	*title = items[selected].label;
	*text = capturing ? "PRESS THE KEY OR THE PAD'S BUTTON FOR IT. ESC: AS IT WAS" : help_of(&items[selected]);
}

void menu_draw_notice(uint32_t *pixels, const char *text)
{
	const int length = (int)strlen(text) < COLUMNS ? (int)strlen(text) : COLUMNS;
	const int column = (COLUMNS - length) / 2;
	for (int y = 27 * 8 - 2; y < 28 * 8; y++)
		for (int x = (column - 1) * 8; x < (column + length + 1) * 8; x++)
			if (x >= 0 && x < GNG_SCREEN_WIDTH)
				pixels[y * GNG_SCREEN_WIDTH + x] = bar;
	put_text(pixels, column, 27, text, yellow);
}

void menu_draw(uint32_t *pixels, const settings *s)
{
	/* the game behind, dark */
	for (int i = 0; i < GNG_SCREEN_WIDTH * GNG_SCREEN_HEIGHT; i++)
		pixels[i] = (pixels[i] >> 4) & 0x0F0F0F;
#ifndef MAKAIMURA_VERSION
#define MAKAIMURA_VERSION "?"
#endif
	/* the title and the version (CMakeLists.txt's) */
	char title[COLUMNS + 1];
	snprintf(title, sizeof(title), "FAIR KNIGHT V%s RECOMP V%s", FAIR_KNIGHT_VERSION, MAKAIMURA_VERSION);
	put_text(pixels, (COLUMNS - (int)strlen(title)) / 2, 0, title, yellow);
	int row = 2;
	char buffer[64];
	/* rows 2-26: a longer page scrolls with the selection */
	const int rows = 25;
	int first = 0;
	if (item_count > rows)
	{
		first = selected - rows / 2;
		first = first < 0 ? 0 : first > item_count - rows ? item_count - rows : first;
	}
	for (int i = first; i < item_count && row < 27; i++, row++)
	{
		const item *it = &items[i];
		if (it->kind == BLANK)
			continue;
		if (it->kind == HEADER)
		{
			put_text(pixels, 0, row, it->label, red);
			continue;
		}
		if (i == selected)
			fill_row(pixels, row, bar);
		put_text(pixels, 1, row, it->label, i == selected ? yellow : white);
		const char *value = i == selected && capturing ? "PRESS A KEY" : value_of(it, s, buffer, sizeof(buffer));
		/* a value too long for its column starts earlier, over the label's spaces (and ends at the screen's) */
		const int room = COLUMNS - 1 - (int)strlen(it->label) - 1;
		if ((int)strlen(value) > room && value == buffer)
			buffer[room] = 0;
		const int length = (int)strlen(value);
		const int after_label = 1 + (int)strlen(it->label) + 1;
		int column = after_label > VALUE_COLUMN ? after_label : VALUE_COLUMN;
		if (column + length > COLUMNS)
			column = COLUMNS - length;
		put_text(pixels, column, row, value, i == selected ? yellow : cyan);
	}
	const char *hint = capturing ? "OR A BUTTON. ESC: AS IT WAS" : hint_of(&items[selected]);
	put_text(pixels, (COLUMNS - (int)strlen(hint)) / 2, 27, hint, grey);
}
