/**
 *    Name:        main.c
 *    Description: makaimura: the game in a window (SDL2): the translated program on the board model, its picture,
 *                 its sound, the controls, the settings (makaimura.ini beside the program: settings.h)
 *    Version:     0.2.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    makaimura [ROM] [options]   (ROM: a .zip or a folder with makaimurg.zip and gng.zip; else the settings' [files]
 *                       rom, else beside the program, its rom folder, ../../rom from it, the folder it starts in, its rom)
 *      --settings FILE  another settings file than makaimura.ini beside the program
 *      --frames N       quit after N frames
 *      --hidden         (for checks) the window not shown
 *      --menu N         (for checks) the menu opened at frame N (with --shot N: its picture)
 *      --menu-keys LIST (for checks) with --menu: those keys pressed on it (SDL's names, comma separated), and the
 *                       settings saved
 *      --inputs FILE    (for checks) the controls from an input script instead, as gng_headless takes it:
 *                       "FRAME PORT VALUE" (system, p1, p2; hex, active low) from that frame's VBLANK on
 *      --shot N FILE    (for checks) the window hidden; frame N's window, as the renderer drew it, into FILE (BMP),
 *                       then quit
 *      --accuracy MODE  mame or board, instead of the settings' (not saved)
 *      --record FILE    the game recorded from the power on into FILE (an AVI: recorder.h); [keyboard] record (F12)
 *                       starts and stops one at any time, beside the settings
 *      --record-inputs FILE  the controls written as an input script from the power on (the DIP switches too):
 *                       --inputs plays the same game again, in the other accuracy mode for one
 *      --save-state N FILE   (for checks) the board's state after frame N's VBLANK into FILE (gng/state.h)
 *      --high-scores    (for checks) the kept ranking (makaimura.hi) with --inputs too
 *      --drop-shot FILE, --drop LIST   (for checks) the ROM set's window (rom_drop.h) hidden, LIST's places
 *                       (comma separated) dropped on it one after the other, its picture after the last into FILE
 *                       (--drop-shot first)
 *      --update-test REPO VERSION   (for checks) the newest release of REPO ("owner/name") looked up as if this
 *                       were VERSION, even hidden (UPDATE on the menu: downloaded and installed, not started again)
 *      --keep-replays   (for checks) each game kept as a replay (replays/) with --inputs too, without asking
 *      --load-state FILE     the game from that state instead of the power on (a slot's: states/ beside the
 *                       settings); [keyboard] save_state (F5), load_state (F7), state_slot (F6) at any time
 *
 *    The board's own pace: a frame every 384 x 262 pixels of its 6 MHz clock (59.637 Hz), kept by the clock of the
 *    host (or a frame a refresh of the display: [video] sync); the picture shown at each VBLANK (the board's picture,
 *    MAME mode), as the arcade's 4:3 monitor shows it or with square pixels; the sound the board's 125 kHz mix
 *    resampled to the device's rate (audio_out.c). Controls: settings.c's defaults (MAME's keys, and Z / X).
 */
#include <ctype.h>

#include <SDL.h>

#include "audio_out.h"
#include "hiscore.h"
#include "crt.h"
#include "hud.h"
#include "gng/board.h"
#include "gng/rom_set.h"
#include "gng/sound.h"
#include "gng/state.h"
#include "gng/video.h"
#include "m6809/cpu.h"
#include "menu.h"
#include "recorder.h"
#include "replays.h"
#include "rom_drop.h"
#include "updater.h"
#include "settings.h"
#include "gng/fair_knight.h"

#include <math.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FRAME_RATE (6000000.0 / (384.0 * 262.0))
#ifndef MAKAIMURA_VERSION
#define MAKAIMURA_VERSION "?"
#endif
#define TITLE "Makaimura Recomp v" MAKAIMURA_VERSION " - Fair Knight v" FAIR_KNIGHT_VERSION    /* (the version: CMakeLists.txt's) */

static settings options;
static char settings_path[1024];
static gng_rom_set rom_set;
static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *picture;          /* the board's 256 x 224 */
static SDL_Texture *prescaled;        /* sharp pixels: the picture enlarged a whole number of times, then smoothed */
static int prescaled_width, prescaled_height;
static SDL_GameController *pads[2];  /* player 1's (and the coins, the starts, the menu), player 2's */
static int sound_on;
static uint32_t pixels[GNG_SCREEN_WIDTH * GNG_SCREEN_HEIGHT];
static Uint64 next_frame;
static int reset_requested, quit_requested, paused, menu_shown;
static uint32_t menu_pixels[GNG_SCREEN_WIDTH * GNG_SCREEN_HEIGHT];
static double speed = 1.0;            /* the board's pace against its own time (display sync) */
static int display_sync;
static long frames_to_run = -1;
static long shot_frame = -1;
static const char *shot_path;
static int hidden;
static long menu_frame = -1;
static const char *menu_keys;
static const char *record_path;
static int accuracy_given = -1;
static char recording_name[1100];

/* the controls written as an input script (--record-inputs) */
static FILE *inputs_out;
static uint8_t logged[3];
static int dsw_given[2] = { -1, -1 };    /* an input script's DIP switches ("0 dsw1 DF") */

/* an input script (--inputs) */
typedef struct input_change { long frame; int port; uint8_t value; } input_change;
static input_change *script;
static int script_count, script_next;
static uint64_t current_frame;

/* replays (replays.h): the game being kept (its file the controls' input script: inputs_out), the one playing */
static int user_inputs;                  /* --record-inputs: its file instead */
static char segment_base[1300];          /* the replay being written: its path without .inp ("" none) */
static int segment_played, segment_sync;
static int segment_asking;               /* its game is over: kept or not, as the player answers */
/* the update (updater.h): the releases' repository, the package's end (tools/make_release.ps1) */
#define UPDATE_REPOSITORY "andrewjose/Makaimura-Recomp"    /* Fair Knight's fork: never upstream's builds over it */
#define UPDATE_PACKAGE "-win64.zip"
static int update_restart;               /* installed: the new program started as this one closes */
static const char *update_test_repository, *update_test_version;    /* --update-test */

/* a release downloaded (the player said yes): its files put in place, the game closed to start again */
static void install_update(void)
{
	if (updater_state() != UPDATER_READY)
		return;
	if (updater_install())
	{
		update_restart = !update_test_repository;    /* (a check's install: not started again) */
		quit_requested = 1;
		menu_shown = 0;
		fprintf(stderr, "makaimura: the update installed\n");
	}
}
static int keep_replays_given;
static int drop_check;                   /* --drop: the ROM window checked */
static const char *drop_shot;           /* --keep-replays: with --inputs too, kept without asking */
static uint8_t last_system = 0xFF;       /* the coins and starts at the last VBLANK */
static uint64_t segment_first;
static int replay_playing;
static char replay_pending[1300];        /* a replay to play from the next power on: its path without .inp */
static char replay_state[1300];          /* its state, "" if it starts at the power on */
static long replay_end;
static int replay_saved_accuracy, replay_saved_dsw[2];
static char replay_names[MENU_REPLAYS][REPLAY_NAME];

/* the side panels (hud.h) */
static SDL_Texture *panel_textures[2];
static uint32_t panel_pixels[2][HUD_W * HUD_H];
static hud_info hud;
static uint64_t irqs_taken, last_irq_frame;
static int irq_seen;
static uint32_t last_conflicts;
static uint32_t lost_history[60];        /* the last second's lost frames, by frame */
static Uint64 fps_since;
static int fps_frames;

/* ---- the picture ---- */

static double picture_aspect(void)
{
	return options.aspect == ASPECT_MONITOR ? 4.0 / 3.0 : (double)GNG_SCREEN_WIDTH / GNG_SCREEN_HEIGHT;
}

static SDL_Rect picture_place(int width, int height)
{
	SDL_Rect place = { 0, 0, width, height };
	if (options.scaling == SCALING_STRETCH)
		return place;
	const double aspect = picture_aspect();
	int h = 0, w = 0;
	if (options.scaling == SCALING_INTEGER)
		for (int k = height / GNG_SCREEN_HEIGHT; k >= 1 && !h; k--)
			if (lround(GNG_SCREEN_HEIGHT * k * aspect) <= width)
			{
				h = GNG_SCREEN_HEIGHT * k;
				w = (int)lround(h * aspect);
			}
	if (!h)
	{
		/* fit (and integer's fallback in a window smaller than the picture) */
		if (width / aspect <= height)
			w = width, h = (int)lround(width / aspect);
		else
			h = height, w = (int)lround(height * aspect);
	}
	place.x = (width - w) / 2;
	place.y = (height - h) / 2;
	place.w = w;
	place.h = h;
	return place;
}

/* the side panels, where the window leaves room beside the picture (at least a third of a panel's width) */
static void draw_panels(SDL_Rect place, int width, int height)
{
	const int side = place.x;
	if (!options.panels || side < HUD_W / 2 || width - (place.x + place.w) < HUD_W / 2)
		return;
	/* (--menu's frame shows the menu without stopping the run: the panels show its help too) */
	if (menu_shown || (long)current_frame == menu_frame)
	{
		hud.menu = 1;
		menu_help(&hud.help_title, &hud.help_text);
	}
	else
		hud.menu = 0;
	hud.paused = paused;
	hud.replay = replay_playing;
	hud.debug = options.panels_debug;
	hud.recording = recorder_on();
	/* the window's keys: each one's first key, its name in capitals (as the menu's) cut to fit */
	static const int hotkeys[HUD_HOTKEYS] = { HOTKEY_MENU, HOTKEY_PAUSE, HOTKEY_SAVE_STATE, HOTKEY_LOAD_STATE,
		HOTKEY_STATE_SLOT, HOTKEY_RESET, HOTKEY_RECORD };
	hud.state_slot = options.state_slot;
	for (int i = 0; i < HUD_HOTKEYS; i++)
	{
		const SDL_Scancode code = options.hot_keys[hotkeys[i]][0];
		const char *name = code != SDL_SCANCODE_UNKNOWN ? SDL_GetScancodeName(code) : "";
		size_t n = 0;
		for (; name[n] && n < sizeof(hud.hot_keys[i]) - 1; n++)
			hud.hot_keys[i][n] = (char)toupper((unsigned char)name[n]);
		hud.hot_keys[i][n] = 0;
	}
	hud_draw(panel_pixels[0], panel_pixels[1], &hud);
	/* a panel as large as its side lets it, a margin around it, centred in its side: the same room above and
	   below, left and right */
	const int margin = side / 16 + 2;
	double scale = (double)(side - 2 * margin) / HUD_W;
	if (scale * HUD_H > height - 2 * margin)
		scale = (double)(height - 2 * margin) / HUD_H;
	const int w = (int)(HUD_W * scale), h = (int)(HUD_H * scale);
	for (int i = 0; i < 2; i++)
	{
		if (!panel_textures[i])
		{
			panel_textures[i] = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
				HUD_W, HUD_H);
			SDL_SetTextureScaleMode(panel_textures[i], SDL_ScaleModeLinear);
		}
		SDL_UpdateTexture(panel_textures[i], NULL, panel_pixels[i], HUD_W * 4);
		const SDL_Rect at = { i == 0 ? (side - w) / 2 : place.x + place.w + (width - place.x - place.w - w) / 2,
			(height - h) / 2, w, h };
		SDL_RenderCopy(renderer, panel_textures[i], NULL, &at);
	}
}

/* ---- save states (gng/state.h) ---- */

/* (a path's file name, after its folders) */
static const char *file_name(const char *path)
{
	const char *slash = strrchr(path, '/'), *back = strrchr(path, '\\');
	const char *cut = back > slash ? back : slash;
	return cut ? cut + 1 : path;
}

/* a notice over the picture's bottom row for a while (the recording and the shots have the picture without it) */
static char notice[40];
static Uint64 notice_until;
static uint32_t notice_pixels[GNG_SCREEN_WIDTH * GNG_SCREEN_HEIGHT];

/* the CRT (crt.h): the picture last shown, drawn into a texture at the size it has on the screen */
static uint32_t crt_source[GNG_SCREEN_WIDTH * GNG_SCREEN_HEIGHT];
static SDL_Texture *crt_texture;
static uint32_t *crt_pixels;
static int crt_width, crt_height;

static void notify(const char *text)
{
	snprintf(notice, sizeof(notice), "%s", text);
	notice_until = SDL_GetPerformanceCounter() + 2 * SDL_GetPerformanceFrequency();
	fprintf(stderr, "makaimura: %s\n", text);
}

/* the picture (or the menu's) into the window's texture, the notice over it while it lasts */
static void update_picture(const uint32_t *source)
{
	if (notice[0] && SDL_GetPerformanceCounter() < notice_until)
	{
		memcpy(notice_pixels, source, sizeof(notice_pixels));
		menu_draw_notice(notice_pixels, notice);
		source = notice_pixels;
	}
	SDL_UpdateTexture(picture, NULL, source, GNG_SCREEN_WIDTH * 4);
	memcpy(crt_source, source, sizeof(crt_source));    /* (what the CRT draws from) */
}

/* the slot's file: states/makaimura-SLOT-MODE.state beside makaimura.ini (a state is for the mode it was saved
   in) */
static void state_path(char *path, size_t size, int slot)
{
	const int folder = (int)(file_name(settings_path) - settings_path);
	snprintf(path, size, "%.*sstates/makaimura-%d-%s.state", folder, settings_path, slot,
		gng.accuracy == GNG_ACCURACY_BOARD ? "board" : "mame");
}

static void make_states_folder(void)
{
	char path[1300];
	snprintf(path, sizeof(path), "%.*sstates", (int)(file_name(settings_path) - settings_path), settings_path);
#ifdef _WIN32
	_mkdir(path);
#else
	mkdir(path, 0777);
#endif
}

static void begin_segment(const char *state_file, uint64_t first, int pc);
static void segment_here(uint16_t pc);
static void keep_segment(void);
static int save_requested, load_requested;
static int hiscore_after_load;           /* a state loaded: the kept ranking into its RAM at the next VBLANK */
static int high_scores_given;            /* --high-scores */
static int practice_requested;          /* a practice chosen on the menu (practice_chosen) */
static practice_setup practice_chosen;
static int practice_making = -1;       /* the area whose start is being reached, the board unpaced, unseen and silent */
static int practice_made;              /* its start saved: the practice from it at the next VBLANK */
static const char *save_to, *load_from;     /* --save-state, --load-state: a file instead of the slot */
static long save_frame = -1;
static int save_quietly;                    /* (the practice's start: no notice) */
static int boundary_save, boundary_segment;    /* what the next instruction boundary does */

/* at the instruction boundary after the request: the state into the slot's file */
static void save_state(uint16_t pc)
{
	char path[1300], text[40];
	if (save_to)
		snprintf(path, sizeof(path), "%s", save_to);
	else
	{
		make_states_folder();
		state_path(path, sizeof(path), options.state_slot);
	}
	const size_t size = gng_state_size();
	uint8_t *state = malloc(size);
	FILE *file = state ? fopen(path, "wb") : NULL;
	int written = 0;
	if (file)
	{
		gng_state_save(state, pc);
		written = fwrite(state, 1, size, file) == size;
		written &= fclose(file) == 0;
	}
	free(state);
	if (save_to)
		snprintf(text, sizeof(text), written ? "STATE SAVED" : "STATE NOT SAVED");
	else
		snprintf(text, sizeof(text), written ? "STATE %d SAVED" : "STATE %d NOT SAVED", options.state_slot);
	if (!save_quietly || !written)
		notify(text);
	if (save_quietly)
	{
		/* (the practice's start: from it at the next VBLANK; not saved, the board the player's as it is) */
		practice_made = written;
		if (!written)
			practice_making = -1;
	}
	save_to = NULL;
	save_quietly = 0;
}

/* the next instruction boundary's jobs: a state saved, a replay begun */
static void at_boundary(uint16_t pc)
{
	if (boundary_save)
	{
		boundary_save = 0;
		save_state(pc);
	}
	if (boundary_segment)
	{
		boundary_segment = 0;
		segment_here(pc);
	}
}

static void request_boundary(int *job)
{
	*job = 1;
	m6809_at_boundary(at_boundary);
}

static void show_picture(void);
static void read_controls(void);

/* ---- replays (replays.h) ---- */

static void replays_folder(char *path, size_t size)
{
	snprintf(path, size, "%.*sreplays", (int)(file_name(settings_path) - settings_path), settings_path);
}

static int copy_file(const char *from, const char *to)
{
	FILE *in = fopen(from, "rb"), *out = in ? fopen(to, "wb") : NULL;
	int good = in && out;
	char buffer[65536];
	size_t got;
	while (good && (got = fread(buffer, 1, sizeof(buffer), in)) > 0)
		good = fwrite(buffer, 1, got, out) == got;
	if (in)
		fclose(in);
	if (out)
		good &= fclose(out) == 0;
	return good;
}

/* the replay being written given up (a reset, a state loaded, quitting before its game over): its files removed */
static void drop_segment(void)
{
	if (!segment_base[0])
		return;
	char path[1400];
	if (inputs_out)
		fclose(inputs_out);    /* (closed already if its question was open) */
	inputs_out = NULL;
	snprintf(path, sizeof(path), "%s.part", segment_base);
	remove(path);
	snprintf(path, sizeof(path), "%s.state", segment_base);
	remove(path);
	segment_base[0] = 0;
	segment_asking = 0;
}

/* Its game over: the replay closed, the player asked whether to keep it (menu_ask_keep_replay) */
static void finish_segment(void)
{
	fprintf(inputs_out, "# end %llu\n", (unsigned long long)current_frame);    /* (its last frame: the replay's) */
	fclose(inputs_out);
	inputs_out = NULL;
	segment_asking = 1;
	if (keep_replays_given)
	{
		/* (--keep-replays: kept without asking) */
		keep_segment();
		return;
	}
	menu_ask_keep_replay();
	menu_shown = 1;
	audio_out_pause(1);
}

/* the answer: kept (its length in its name: .inp, .state) or let go */
static void keep_segment(void)
{
	char from[1400], to[1400];
	const unsigned seconds = (unsigned)((current_frame + 1 - segment_first) / FRAME_RATE);
	snprintf(from, sizeof(from), "%s.part", segment_base);
	snprintf(to, sizeof(to), "%s-%04u.inp", segment_base, seconds);
	rename(from, to);
	snprintf(from, sizeof(from), "%s.state", segment_base);
	snprintf(to, sizeof(to), "%s-%04u.state", segment_base, seconds);
	rename(from, to);
	segment_base[0] = 0;
	segment_asking = 0;
	notify("REPLAY KEPT");
}

/* A replay starts being written, from a state: the board's now at this instruction boundary ('pc': a game just
   begun) or a file's ('state_file': a slot's or a practice's, copied beside it); the game on from frame 'first' */
static void begin_segment(const char *state_file, uint64_t first, int pc)
{
	drop_segment();
	if (user_inputs || !options.keep_replays || replay_playing || (script && !keep_replays_given))
		return;
	char folder[1300], inp[1400], state[1400];
	replays_folder(folder, sizeof(folder));
#ifdef _WIN32
	_mkdir(folder);
#else
	mkdir(folder, 0777);
#endif
	const time_t now = time(NULL);
	char stamp[32];
	strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", localtime(&now));
	snprintf(segment_base, sizeof(segment_base), "%s/%s-%s", folder, stamp,
		gng.accuracy == GNG_ACCURACY_BOARD ? "board" : "mame");
	snprintf(state, sizeof(state), "%s.state", segment_base);
	int written = 0;
	if (state_file)
		written = copy_file(state_file, state);
	else
	{
		const size_t size = gng_state_size();
		uint8_t *bytes = malloc(size);
		FILE *file = bytes ? fopen(state, "wb") : NULL;
		if (file)
		{
			gng_state_save(bytes, (uint16_t)pc);
			written = fwrite(bytes, 1, size, file) == size;
			written &= fclose(file) == 0;
		}
		free(bytes);
	}
	snprintf(inp, sizeof(inp), "%s.part", segment_base);    /* (.inp once kept) */
	if (!written || !(inputs_out = fopen(inp, "w")))
	{
		remove(state);
		segment_base[0] = 0;
		return;
	}
	fprintf(inputs_out, "# makaimura replay (%s mode)\n# %s (frame %llu)\n",
		gng.accuracy == GNG_ACCURACY_BOARD ? "board" : "MAME",
		state_file ? "from a loaded state beside it" : "from the state beside it, as the game began",
		(unsigned long long)first);
	segment_sync = 1;    /* (the controls as the state has them: the changes from there) */
	segment_first = first;
	segment_played = 0;
}

/* at the boundary after a coin or a start with no game on: the replay from here */
static void segment_here(uint16_t pc)
{
	m6809_state now;
	m6809_state_get(&now);
	begin_segment(NULL, now.frame, pc);
}

/* the replays for the menu: their names, labels and values */
static void refresh_replays(void)
{
	char folder[1300], labels[MENU_REPLAYS][16], values[MENU_REPLAYS][16];
	replays_folder(folder, sizeof(folder));
	const int count = replays_list(folder, replay_names, MENU_REPLAYS);
	for (int i = 0; i < count; i++)
		replays_describe(folder, replay_names[i], labels[i], sizeof(labels[i]), values[i], sizeof(values[i]));
	menu_replays(count, labels, values);
}

static int read_script(const char *path);

/* A replay chosen: from the next power on, its mode, its DIP switches and its controls (its state first if it
   has one) */
static void prepare_replay(void)
{
	char inp[1400], line[256];
	snprintf(inp, sizeof(inp), "%s.inp", replay_pending);
	snprintf(replay_state, sizeof(replay_state), "%s.state", replay_pending);
	replay_pending[0] = 0;
	FILE *file = fopen(inp, "r");
	if (!file)
	{
		notify("NO SUCH REPLAY");
		return;
	}
	int board = 0;
	long end = -1;
	while (fgets(line, sizeof(line), file))
		if (line[0] == '#')
		{
			board |= strstr(line, "(board mode)") != NULL;
			sscanf(line, "# end %ld", &end);
		}
	fclose(file);
	FILE *state = fopen(replay_state, "rb");
	if (state)
		fclose(state);
	else
		replay_state[0] = 0;
	drop_segment();
	if (!replay_playing)
	{
		replay_saved_accuracy = accuracy_given;
		replay_saved_dsw[0] = dsw_given[0];
		replay_saved_dsw[1] = dsw_given[1];
	}
	free(script);
	script = NULL;
	script_count = 0;
	if (!read_script(inp))
		return;
	accuracy_given = board;
	replay_end = end >= 0 ? end : script_count ? script[script_count - 1].frame : 0;
	replay_playing = 1;
}

/* the replay over (or ended by a reset, a state, the practice): the controls the player's again */
static void stop_replay(void)
{
	if (!replay_playing)
		return;
	replay_playing = 0;
	free(script);
	script = NULL;
	script_count = script_next = 0;
	accuracy_given = replay_saved_accuracy;
	dsw_given[0] = replay_saved_dsw[0];
	dsw_given[1] = replay_saved_dsw[1];
	logged[0] = gng.inputs.system;
	logged[1] = gng.inputs.p1;
	logged[2] = gng.inputs.p2;
}

/* The practice's area starts: states/practice-start-AREA-MODE-DSW1DSW2.state (the DIP switches' lives, difficulty...
   are in it), each the board one frame before the area is played, the weapon and the lives not drawn yet; and the
   practice itself, the start with the player's choices: states/practice-MODE.state (the replay's start) */
static void practice_path(char *path, size_t size, int area)
{
	const int folder = (int)(file_name(settings_path) - settings_path);
	snprintf(path, size, "%.*sstates/practice-start-%d-%s-%02x%02x.state", folder, settings_path, area + 1,
		gng.accuracy == GNG_ACCURACY_BOARD ? "board" : "mame", gng.inputs.dsw1, gng.inputs.dsw2);
}

static void practice_game_path(char *path, size_t size)
{
	const int folder = (int)(file_name(settings_path) - settings_path);
	snprintf(path, size, "%.*sstates/practice-%s.state", folder, settings_path,
		gng.accuracy == GNG_ACCURACY_BOARD ? "board" : "mame");
}

static int load_state(const char *path, int slot, const char *done);

/* The practice from its area's kept start: the loop, the weapon and the lives written into the state's RAM as the
   game is about to draw them (the area's first frame draws the weapon's frame and the lives at the screen's bottom
   from <$73 and <$60; the loop <$7F, written there or with the stage, makes the same game: the states at frame
   2500 the same, stages 0 and 5), saved as the practice's state and loaded: the replay is from it. 0 if the start
   is not kept (or is not this build's) */
static int start_practice(const practice_setup *setup)
{
	char path[1300], text[64];
	practice_path(path, sizeof(path), setup->area);
	FILE *file = fopen(path, "rb");
	if (!file)
		return 0;
	const size_t size = gng_state_size();
	uint8_t *state = malloc(size + 1);
	const size_t got = state ? fread(state, 1, size + 1, file) : 0;
	fclose(file);
	if (!state || gng_state_check(state, got))
	{
		free(state);
		return 0;
	}
	uint8_t *ram = gng_state_ram(state);
	ram[0x7F] = (uint8_t)setup->loop;
	ram[0x73] = (uint8_t)setup->weapon;
	if (setup->lives)
		ram[0x60] = (uint8_t)setup->lives;
	make_states_folder();
	practice_game_path(path, sizeof(path));
	file = fopen(path, "wb");
	int written = file && fwrite(state, 1, size, file) == size;
	if (file)
		written &= fclose(file) == 0;
	free(state);
	if (!written)
	{
		notify("PRACTICE NOT SAVED");
		return 0;
	}
	snprintf(text, sizeof(text), "PRACTICE: AREA %d%s, %s", setup->area + 1, setup->loop ? " (2ND LOOP)" : "",
		menu_weapon_name(setup->weapon));
	return load_state(path, 0, text);
}

/* An area's start reached from the power on, at each VBLANK, unseen and unheard (the window shows how far it is):
   a coin, 1 player's start, the area written into the stage (<$72, right after the start:
   tests/controls/make_stages.py's), the controls left alone; through the game's opening to the VBLANK where the
   stage's start (<$71 0) is at its last step (<$0E 2: the next frame draws the weapon, the lives, the time and the
   area, and <$71 is 1, playing). There the state is saved (kept for the next time) and, at the next VBLANK, the
   practice starts from it (start_practice) */
static void make_practice(uint64_t frame)
{
	static const struct { uint64_t frame; uint8_t system; } presses[] = {
		{ 1300, 0xBF }, { 1306, 0xFF }, { 1400, 0xFE }, { 1406, 0xFF } };
	enum { THERE = 1918 };    /* (about: the frame it is reached at, for the progress shown) */
	if (practice_made)
	{
		practice_made = 0;
		practice_making = -1;
		next_frame = 0;
		read_controls();
		if (!start_practice(&practice_chosen))
			notify("PRACTICE NOT STARTED");
		return;
	}
	gng.inputs.system = 0xFF;
	gng.inputs.p1 = gng.inputs.p2 = 0xFF;
	for (size_t i = 0; i < sizeof(presses) / sizeof(presses[0]); i++)
		if (frame >= presses[i].frame)
			gng.inputs.system = presses[i].system;
	if (frame == 1410)
		gng.ram[0x72] = (uint8_t)practice_making;
	/* (the window kept alive: its events now and then, a dark picture with how far it is; the game itself not
	   shown, its boot and opening run far faster than they play) */
	if (frame % 30 == 0)
	{
		SDL_Event event;
		char text[40];
		while (SDL_PollEvent(&event))
			if (event.type == SDL_QUIT)
				quit_requested = 1;
		snprintf(text, sizeof(text), "PREPARING AREA %d: %d%%", practice_making + 1,
			(int)(frame < THERE ? frame * 100 / THERE : 99));
		memset(pixels, 0, sizeof(pixels));
		menu_draw_notice(pixels, text);
		SDL_UpdateTexture(picture, NULL, pixels, GNG_SCREEN_WIDTH * 4);
		memcpy(crt_source, pixels, sizeof(crt_source));
		show_picture();
	}
	if (quit_requested)
	{
		practice_making = -1;
		m6809_request_stop();
		return;
	}
	if (frame > 1410 && gng.ram[0x71] == 0 && gng.ram[0x0E] == 2 && gng.ram[0x72] == (uint8_t)practice_making
		&& !save_to)
	{
		static char path[1300];
		make_states_folder();
		practice_path(path, sizeof(path), practice_making);
		save_to = path;
		save_quietly = 1;
		request_boundary(&boundary_save);
	}
}

/* the state back from 'path', the program on from it (from the end of on_vblank: this does not return if it
   loads), 'done' (or "STATE n LOADED") told; 0 and a notice if it cannot */
static int load_state(const char *path, int slot, const char *done)
{
	char text[64];
	FILE *file = fopen(path, "rb");
	if (!file)
	{
		snprintf(text, sizeof(text), "NO STATE IN SLOT %d", slot);
		notify(slot ? text : "NO STATE FILE");
		return 0;
	}
	const size_t size = gng_state_size();
	uint8_t *state = malloc(size + 1);
	const size_t got = state ? fread(state, 1, size + 1, file) : 0;
	fclose(file);
	const char *problem = state ? gng_state_check(state, got) : "no memory";
	if (problem)
	{
		snprintf(text, sizeof(text), "%s", problem);
		for (char *c = text; *c; c++)
			*c = (char)toupper((unsigned char)*c);
		notify(text);
		free(state);
		return 0;
	}
	/* the port's own count of the frames, the panels' slowdown, an input script's place: from the state's frame */
	const uint64_t frame = gng_state_frame(state);
	current_frame = frame - 1;
	last_irq_frame = frame - 1;
	memset(lost_history, 0, sizeof(lost_history));
	script_next = 0;
	while (script && script_next < script_count && script[script_next].frame < (int64_t)frame)
		script_next++;
	snprintf(text, sizeof(text), "STATE %d LOADED", slot);
	notify(done ? done : slot ? text : "STATE LOADED");
	hiscore_after_load = 1;
	irq_seen = 1;
	begin_segment(path, frame, -1);
	static uint8_t *loaded;
	free(loaded);
	loaded = state;
	gng_state_load(loaded);
	return 1;
}

static void show_picture(void)
{
	const int shot_now = shot_path && (long)current_frame == shot_frame;
	/* not drawn while the window is not seen: a present there may wait far longer than a refresh (Windows
	   throttles a hidden window's vsync), and the board would slow down with it */
	if (!shot_now && (SDL_GetWindowFlags(window) & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED)))
		return;
	int width, height;
	SDL_GetRendererOutputSize(renderer, &width, &height);
	const SDL_Rect place = picture_place(width, height);
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	const int kx = (place.w + GNG_SCREEN_WIDTH - 1) / GNG_SCREEN_WIDTH;
	const int ky = (place.h + GNG_SCREEN_HEIGHT - 1) / GNG_SCREEN_HEIGHT;
	if (options.crt)
	{
		/* the CRT at the picture's size on the screen (at most 1440 lines: a larger screen enlarges it) */
		int w = place.w, h = place.h;
		if (h > 1440)
		{
			w = w * 1440 / h;
			h = 1440;
		}
		if (!crt_texture || crt_width != w || crt_height != h)
		{
			if (crt_texture)
				SDL_DestroyTexture(crt_texture);
			free(crt_pixels);
			crt_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
			SDL_SetTextureScaleMode(crt_texture, SDL_ScaleModeLinear);
			crt_pixels = malloc(sizeof(uint32_t) * (size_t)w * (size_t)h);
			crt_width = w;
			crt_height = h;
		}
		if (crt_texture && crt_pixels)
		{
			crt_render(crt_source, GNG_SCREEN_WIDTH, GNG_SCREEN_HEIGHT, crt_pixels, w, h, options.crt);
			SDL_UpdateTexture(crt_texture, NULL, crt_pixels, w * 4);
			SDL_RenderCopy(renderer, crt_texture, NULL, &place);
		}
	}
	else if (options.smooth || (place.w == kx * GNG_SCREEN_WIDTH && place.h == ky * GNG_SCREEN_HEIGHT))
	{
		SDL_SetTextureScaleMode(picture, options.smooth ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
		SDL_RenderCopy(renderer, picture, NULL, &place);
	}
	else
	{
		/* sharp pixels at a size that is not a whole number of times the picture's: each pixel enlarged by whole
		   numbers first, then the small rest smoothed (every pixel the same width, its edges soft by a screen
		   pixel at most) */
		if (!prescaled || prescaled_width != kx * GNG_SCREEN_WIDTH || prescaled_height != ky * GNG_SCREEN_HEIGHT)
		{
			if (prescaled)
				SDL_DestroyTexture(prescaled);
			prescaled_width = kx * GNG_SCREEN_WIDTH;
			prescaled_height = ky * GNG_SCREEN_HEIGHT;
			prescaled = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET,
				prescaled_width, prescaled_height);
			SDL_SetTextureScaleMode(prescaled, SDL_ScaleModeLinear);
		}
		SDL_SetTextureScaleMode(picture, SDL_ScaleModeNearest);
		SDL_SetRenderTarget(renderer, prescaled);
		SDL_RenderCopy(renderer, picture, NULL, NULL);
		SDL_SetRenderTarget(renderer, NULL);
		SDL_RenderCopy(renderer, prescaled, NULL, &place);
	}
	draw_panels(place, width, height);
	if (shot_now)
	{
		SDL_Surface *shot = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_XRGB8888);
		if (shot && SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_XRGB8888, shot->pixels, shot->pitch) == 0)
			SDL_SaveBMP(shot, shot_path);
		SDL_FreeSurface(shot);
		quit_requested = 1;
	}
	SDL_RenderPresent(renderer);
}

static void toggle_fullscreen(void)
{
	options.fullscreen = !options.fullscreen;
	SDL_SetWindowFullscreen(window, options.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
	SDL_ShowCursor(options.fullscreen ? SDL_DISABLE : SDL_ENABLE);
}

static void update_title(void)
{
	char title[1300];
	if (recorder_on())
		snprintf(title, sizeof(title), "%s%s (recording: %s)", TITLE, paused ? " (paused)" : "", recording_name);
	else
		snprintf(title, sizeof(title), "%s%s", TITLE, paused ? " (paused)" : "");
	SDL_SetWindowTitle(window, title);
}

/* ---- the recordings ---- */

static void start_recording(const char *path)
{
	if (recorder_open(path))
	{
		snprintf(recording_name, sizeof(recording_name), "%s", file_name(path));
		fprintf(stderr, "makaimura: recording into %s\n", path);
	}
	else
		fprintf(stderr, "makaimura: %s could not be made\n", path);
	update_title();
}

static void stop_recording(void)
{
	if (!recorder_on())
		return;
	const uint32_t frames = recorder_frames();
	recorder_close();
	fprintf(stderr, "makaimura: %s recorded: %u frames (%.1f s)\n", recording_name, frames, frames / FRAME_RATE);
	update_title();
}

/* F12: a recording beside the settings, named by the time and the accuracy mode */
static void toggle_recording(void)
{
	if (recorder_on())
	{
		stop_recording();
		return;
	}
	char path[1300];
	const time_t now = time(NULL);
	char stamp[32];
	strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", localtime(&now));
	snprintf(path, sizeof(path), "%.*smakaimura-%s-%s.avi", (int)(file_name(settings_path) - settings_path),
		settings_path, stamp, gng.accuracy == GNG_ACCURACY_BOARD ? "board" : "mame");
	start_recording(path);
}

static void log_input(uint64_t frame, const char *port, unsigned value)
{
	if (inputs_out)
		fprintf(inputs_out, "%llu %s %02x\n", (unsigned long long)frame, port, value);
}

/* a byte the port wrote into the work RAM (the kept ranking): as an input script's "mADDR" */
static void log_ram_write(uint64_t frame, uint16_t address, uint8_t value)
{
	char name[8];
	snprintf(name, sizeof(name), "m%04x", address);
	log_input(frame, name, value);
}

/* the controls as the board reads them from this VBLANK on: their changes written */
static void log_controls(uint64_t frame)
{
	const uint8_t now[3] = { gng.inputs.system, gng.inputs.p1, gng.inputs.p2 };
	static const char *const names[3] = { "system", "p1", "p2" };
	for (int i = 0; i < 3; i++)
		if (now[i] != logged[i])
		{
			log_input(frame, names[i], now[i]);
			logged[i] = now[i];
		}
}

/* ---- the controls ---- */

static int key_down(const Uint8 *keys, const SDL_Scancode *slot)
{
	for (int i = 0; i < BINDINGS_MAX; i++)
		if (slot[i] != SDL_SCANCODE_UNKNOWN && keys[slot[i]])
			return 1;
	return 0;
}

static int is_hotkey(SDL_Scancode code, int hotkey)
{
	for (int i = 0; i < BINDINGS_MAX; i++)
		if (code != SDL_SCANCODE_UNKNOWN && options.hot_keys[hotkey][i] == code)
			return 1;
	return 0;
}

/* a player's port (3001, 3002) from its six actions, from 'first' (ACTION_UP or ACTION_P2_FIRST) */
static uint8_t player_port(int *on, int first)
{
	/* the board's joystick is 4-way (PORT_4WAY): one direction at a time, the vertical one first */
	if (on[first + ACTION_UP] || on[first + ACTION_DOWN])
		on[first + ACTION_LEFT] = on[first + ACTION_RIGHT] = 0;
	uint8_t player = 0xFF;
	if (on[first + ACTION_RIGHT]) player &= ~0x01;
	if (on[first + ACTION_LEFT]) player &= ~0x02;
	if (on[first + ACTION_DOWN]) player &= ~0x04;
	if (on[first + ACTION_UP]) player &= ~0x08;
	if (on[first + ACTION_ATTACK]) player &= ~0x10;
	if (on[first + ACTION_JUMP]) player &= ~0x20;
	return player;
}

static void read_controls(void)
{
	const Uint8 *keys = SDL_GetKeyboardState(NULL);
	int on[ACTIONS];
	for (int a = 0; a < ACTIONS; a++)
	{
		/* player 2's buttons are the second gamepad's; the rest the first one's */
		SDL_GameController *pad = pads[a >= ACTION_P2_FIRST];
		on[a] = key_down(keys, options.keys[a]);
		if (pad)
			for (int i = 0; i < BINDINGS_MAX; i++)
				on[a] |= settings_pad_pressed(pad, options.buttons[a][i]);
	}
	for (int p = 0; p < 2; p++)
		if (pads[p] && options.left_stick)
		{
			const int first = p ? ACTION_P2_FIRST : ACTION_UP;
			const int dead = 32767 * options.stick_dead / 100;
			const int x = SDL_GameControllerGetAxis(pads[p], SDL_CONTROLLER_AXIS_LEFTX);
			const int y = SDL_GameControllerGetAxis(pads[p], SDL_CONTROLLER_AXIS_LEFTY);
			on[first + ACTION_LEFT] |= x < -dead;
			on[first + ACTION_RIGHT] |= x > dead;
			on[first + ACTION_UP] |= y < -dead;
			on[first + ACTION_DOWN] |= y > dead;
		}
	uint8_t system = 0xFF;
	if (on[ACTION_START1]) system &= ~0x01;
	if (on[ACTION_START2]) system &= ~0x02;
	if (on[ACTION_SERVICE]) system &= ~0x20;
	if (on[ACTION_COIN1]) system &= ~0x40;
	if (on[ACTION_COIN2]) system &= ~0x80;
	gng.inputs.system = system;
	/* each player's own, as the board's: the program reads 3002 for a cocktail cabinet's second player (691C:
	   <$48 on player 2's turn when <$53 is set) and for either player's buttons at the continue (76D8: <$46 | <$48);
	   the upright cabinet's players take turns on player 1's */
	gng.inputs.p1 = player_port(on, ACTION_UP);
	gng.inputs.p2 = player_port(on, ACTION_P2_FIRST);
}

/* a gamepad plugged in: player 1's if it has none, else player 2's (each opened once) */
static void open_pad(int device)
{
	if (!SDL_IsGameController(device))
		return;
	const SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(device);
	for (int p = 0; p < 2; p++)
		if (pads[p] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[p])) == id)
			return;
	for (int p = 0; p < 2; p++)
		if (!pads[p])
		{
			pads[p] = SDL_GameControllerOpen(device);
			return;
		}
}

static int read_script(const char *path)
{
	FILE *file = fopen(path, "r");
	if (!file)
		return 0;
	char line[256], port[16];
	long frame;
	unsigned value;
	int capacity = 0;
	while (fgets(line, sizeof(line), file))
	{
		if (line[0] == '#' || sscanf(line, "%ld %15s %x", &frame, port, &value) != 3)
			continue;
		/* system, p1, p2; or mADDR: the work RAM's byte at ADDR (a test's cheat) */
		int which = !strcmp(port, "system") ? 0 : !strcmp(port, "p1") ? 1 : !strcmp(port, "p2") ? 2 : -1;
		if (!strcmp(port, "dsw1") || !strcmp(port, "dsw2"))
		{
			dsw_given[port[3] - '1'] = (int)(value & 0xFF);    /* (set at the power on) */
			continue;
		}
		if (port[0] == 'm' && strtoul(port + 1, NULL, 16) < 0x1E00u)
			which = 3 + (int)strtoul(port + 1, NULL, 16);
		if (which < 0)
			continue;
		if (script_count == capacity)
		{
			capacity = capacity ? capacity * 2 : 256;
			script = realloc(script, (size_t)capacity * sizeof(*script));
		}
		script[script_count].frame = frame;
		script[script_count].port = which;
		script[script_count].value = (uint8_t)value;
		script_count++;
	}
	fclose(file);
	return 1;
}

static void play_script(uint64_t frame)
{
	while (script_next < script_count && script[script_next].frame <= (long)frame)
	{
		const input_change *c = &script[script_next++];
		if (c->port == 0) gng.inputs.system = c->value;
		else if (c->port == 1) gng.inputs.p1 = c->value;
		else if (c->port == 2) gng.inputs.p2 = c->value;
		else
		{
			char name[8];
			gng.ram[c->port - 3] = c->value;
			snprintf(name, sizeof(name), "m%04x", c->port - 3);
			log_input(frame, name, c->value);
		}
	}
}

static void open_menu(void)
{
	refresh_replays();
	menu_open();
	menu_shown = 1;
	audio_out_pause(1);
}

/* The menu closed: its settings kept in makaimura.ini, the DIP switches set on the board (the program reads most
   of them as the board starts: F3) */
static void close_menu(void)
{
	menu_shown = 0;
	gng.inputs.dsw1 = options.dsw1;
	gng.inputs.dsw2 = options.dsw2;
	settings_save(&options, settings_path);
	audio_out_pause(paused);
}

static void menu_input(const SDL_Event *event)
{
	int changed = 0;
	const int result = menu_event(event, &options, &changed);
	if (changed & MENU_CHANGED_FULLSCREEN)
		toggle_fullscreen();
	if (changed & MENU_CHANGED_VOLUME)
		audio_out_volume(options.volume);
	if (result == MENU_STAY)
		return;
	if (result == MENU_DELETE_REPLAY)
	{
		char folder[1300];
		replays_folder(folder, sizeof(folder));
		replays_delete(folder, replay_names[menu_replay_chosen()]);
		refresh_replays();
		menu_show_replays(menu_replay_chosen());
		notify("REPLAY DELETED");
		return;
	}
	close_menu();
	if (result == MENU_KEEP_REPLAY)
		keep_segment();
	else if (result == MENU_DROP_REPLAY)
		drop_segment();
	else if (result == MENU_REPLAY)
	{
		char folder[1300];
		replays_folder(folder, sizeof(folder));
		snprintf(replay_pending, sizeof(replay_pending), "%s/%s", folder, replay_names[menu_replay_chosen()]);
		reset_requested = 1;
		menu_frame = -1;    /* (--menu's, once) */
	}
	else if (result == MENU_PRACTICE)
	{
		practice_requested = 1;
		practice_chosen = menu_practice_setup();
	}
	else if (result == MENU_SAVE_STATE)
		save_requested = 1;
	else if (result == MENU_LOAD_STATE)
		load_requested = 1;
	else if (result == MENU_RESET)
		reset_requested = 1;
	else if (result == MENU_QUIT)
		quit_requested = 1;
}

static int is_menu_button(int button)
{
	for (int i = 0; i < BINDINGS_MAX; i++)
		if (options.menu_buttons[i].kind == PAD_BUTTON && options.menu_buttons[i].index == button)
			return 1;
	return 0;
}

static void handle_event(const SDL_Event *event)
{
	if (event->type == SDL_QUIT)
		quit_requested = 1;
	else if (event->type == SDL_KEYDOWN && event->key.keysym.scancode == SDL_SCANCODE_RETURN &&
		(event->key.keysym.mod & KMOD_ALT))
	{
		if (!event->key.repeat)
			toggle_fullscreen();
	}
	else if (menu_shown && (event->type == SDL_KEYDOWN || event->type == SDL_CONTROLLERBUTTONDOWN ||
		event->type == SDL_CONTROLLERAXISMOTION))
		menu_input(event);
	else if (event->type == SDL_CONTROLLERBUTTONDOWN && is_menu_button(event->cbutton.button))
		open_menu();
	else if (event->type == SDL_KEYDOWN && !event->key.repeat)
	{
		const SDL_Scancode code = event->key.keysym.scancode;
		if (is_hotkey(code, HOTKEY_MENU))
			open_menu();
		else if (is_hotkey(code, HOTKEY_QUIT))
			quit_requested = 1;
		else if (is_hotkey(code, HOTKEY_RESET))
			reset_requested = 1;
		else if (is_hotkey(code, HOTKEY_FULLSCREEN))
			toggle_fullscreen();
		else if (is_hotkey(code, HOTKEY_RECORD))
			toggle_recording();
		else if (is_hotkey(code, HOTKEY_SAVE_STATE))
			save_requested = 1;
		else if (is_hotkey(code, HOTKEY_LOAD_STATE))
			load_requested = 1;
		else if (is_hotkey(code, HOTKEY_STATE_SLOT))
		{
			char text[32];
			options.state_slot = options.state_slot % STATE_SLOTS + 1;
			snprintf(text, sizeof(text), "STATE SLOT %d", options.state_slot);
			notify(text);
		}
		else if (is_hotkey(code, HOTKEY_PAUSE))
		{
			paused = !paused;
			audio_out_pause(paused);
			update_title();
		}
	}
	else if (event->type == SDL_CONTROLLERDEVICEADDED)
		open_pad(event->cdevice.which);
	else if (event->type == SDL_CONTROLLERDEVICEREMOVED)
	{
		for (int p = 0; p < 2; p++)
			if (pads[p] && event->cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[p])))
			{
				SDL_GameControllerClose(pads[p]);
				pads[p] = NULL;
			}
	}
}

/* ---- the board's VBLANK: the sound, the picture, the controls, the pace ---- */

static void on_sample(int16_t sample)
{
	if (practice_making >= 0)
		return;
	audio_out_sample(sample);
	recorder_sample(sample);
}

static void on_latch(gng_board *board, uint8_t value)
{
	(void)board;
	gng_sound_latch(2 * cpu.cycles, value);
}

static void on_ls259(gng_board *board, unsigned output, unsigned value)
{
	(void)board;
	if (output == 1)
		gng_sound_q1(2 * cpu.cycles, (int)value);
}

static void wait_for_next_frame(void)
{
	const Uint64 frequency = SDL_GetPerformanceFrequency();
	/* display sync: the display's refresh paces the frames (vsync); the clock only keeps them from running away
	   if it does not */
	const double rate = display_sync ? FRAME_RATE * speed * 1.05 : FRAME_RATE;
	const Uint64 period = (Uint64)(frequency / rate);
	Uint64 now = SDL_GetPerformanceCounter();
	if (next_frame == 0 || now > next_frame + 4 * period)
		next_frame = now;    /* (after a pause or a stall: from now) */
	next_frame += period;
	if (display_sync && now >= next_frame - period)
		return;
	while ((now = SDL_GetPerformanceCounter()) < next_frame)
	{
		const Uint64 left = next_frame - now;
		if (left * 1000 / frequency > 2)
			SDL_Delay((Uint32)(left * 1000 / frequency - 1));
	}
}

static void on_irq(uint16_t pc, uint64_t cycles)
{
	(void)pc;
	(void)cycles;
	irqs_taken++;
	irq_seen = 1;
	last_irq_frame = current_frame;
}

/* the panels' facts at a VBLANK: a frame lost (the last VBLANK's IRQ not taken by now: the program's slowdown,
   counted from its first IRQ on: the boot waits with it masked), the palette's flashes, the pace */
static void panel_facts(uint64_t frame)
{
	const int lost = irq_seen && frame > 0 && last_irq_frame + 1 < frame;
	if (lost)
		hud.lost_frames++;
	lost_history[frame % 60] = (uint32_t)lost;
	hud.recent_lost = 0;
	for (int i = 0; i < 60; i++)
	{
		hud.lost_last[i] = (uint8_t)lost_history[(frame + 1 + i) % 60];
		hud.recent_lost += lost_history[i];
	}
	if (lost)
		last_irq_frame = frame - 1;    /* (one lost a VBLANK) */
	if (gng.palette_conflicts != last_conflicts)
		hud.flash_frames++;
	last_conflicts = gng.palette_conflicts;
	hud.board_mode = gng.accuracy == GNG_ACCURACY_BOARD;
	hud.p1 = gng.inputs.p1;
	hud.p2 = gng.inputs.p2;
	hud.system = gng.inputs.system;
	const Uint64 now = SDL_GetPerformanceCounter();
	if (!fps_since)
		fps_since = now;
	fps_frames++;
	const double seconds = (double)(now - fps_since) / SDL_GetPerformanceFrequency();
	if (seconds >= 1.0)
	{
		hud.fps = fps_frames / seconds;
		fps_frames = 0;
		fps_since = now;
	}
}

static void on_vblank(uint64_t frame)
{
	SDL_Event event;
	current_frame = frame;
	panel_facts(frame);
	if (frames_to_run >= 0 && (long)frame >= frames_to_run)
		quit_requested = 1;
	gng_sound_run_until(2 * (GNG_VBLANK_CYCLE + frame * GNG_CPU_CYCLES_PER_FRAME));
	if (practice_making >= 0)
	{
		make_practice(frame);
		return;
	}
	if (sound_on)
		audio_out_frame(speed);
	gng_video_render(pixels);
	recorder_frame(pixels);
	if ((long)frame == menu_frame)
	{
		refresh_replays();
		menu_open();
		if (menu_keys)
		{
			char list[512];
			snprintf(list, sizeof(list), "%s", menu_keys);
			for (char *name = strtok(list, ","); name; name = strtok(NULL, ","))
			{
				SDL_Event key;
				SDL_zero(key);
				key.type = SDL_KEYDOWN;
				key.key.keysym.scancode = SDL_GetScancodeFromName(name);
				/* (as on the open menu: a choice that closes it is made, a state's load at this VBLANK's end) */
				menu_input(&key);
			}
			settings_save(&options, settings_path);
		}
		memcpy(menu_pixels, pixels, sizeof(pixels));
		menu_draw(menu_pixels, &options);
		update_picture(menu_pixels);
	}
	else if (menu_shown)
	{
		/* (opened by the program's game over: its replay's question) */
		memcpy(menu_pixels, pixels, sizeof(pixels));
		menu_draw(menu_pixels, &options);
		update_picture(menu_pixels);
	}
	else
		update_picture(pixels);
	show_picture();

	while (SDL_PollEvent(&event))
		handle_event(&event);
	while ((paused || menu_shown) && !quit_requested && !reset_requested && !load_requested)
	{
		if (SDL_WaitEventTimeout(&event, 50))
			handle_event(&event);
		install_update();
		if (menu_shown)
		{
			memcpy(menu_pixels, pixels, sizeof(pixels));
			menu_draw(menu_pixels, &options);
			update_picture(menu_pixels);
		}
		else
			update_picture(pixels);
		show_picture();
		next_frame = 0;
	}
	install_update();    /* (a download the player chose, once it is there) */
	if (quit_requested || reset_requested)
	{
		paused = 0;
		menu_shown = 0;
		audio_out_pause(0);
		update_title();
		m6809_request_stop();
	}
	if (segment_sync)
	{
		/* (a replay from a state: its controls as the state has them, the changes from there) */
		logged[0] = gng.inputs.system;
		logged[1] = gng.inputs.p1;
		logged[2] = gng.inputs.p2;
		segment_sync = 0;
	}
	if (replay_playing && script_next >= script_count && (long)frame > replay_end)
	{
		stop_replay();
		notify("REPLAY OVER: YOUR GAME");
	}
	if (script)
		play_script(frame);
	else
		read_controls();
	/* the replays: with no game on (<$28; the boot's RAM checks are before the first IRQ), a coin begins one at the
	   next instruction boundary (again if no game came of the last coin), a start too if none is being written
	   (free play); its game over (<$28 cleared) ends it */
	const uint8_t pressed = (uint8_t)(last_system & ~gng.inputs.system);
	last_system = gng.inputs.system;
	if (irq_seen && !gng.ram[0x28] && !replay_playing && !segment_asking &&
		(((pressed & 0xC0) && (!segment_base[0] || !segment_played)) || ((pressed & 0x03) && !segment_base[0])))
		request_boundary(&boundary_segment);
	if (segment_base[0] && !segment_asking && irq_seen)
	{
		if (gng.ram[0x28])
			segment_played = 1;
		else if (segment_played)
			finish_segment();
	}
	/* the ranking kept between sessions (not with an input script but with --high-scores: the checks' runs are
	   the board's own) */
	if (options.keep_high_scores && (!script || high_scores_given))
	{
		if (hiscore_after_load)
			hiscore_state_loaded(gng.ram);
		hiscore_vblank(gng.ram, frame, log_ram_write);
	}
	hiscore_after_load = 0;
	log_controls(frame);
	wait_for_next_frame();
	/* a state: saved at the next instruction boundary; loaded now, the program going on from it (leaving this
	   VBLANK's instruction) */
	if ((long)frame == save_frame)
	{
		save_requested = 1;
		save_frame = -1;
	}
	if (save_requested)
	{
		save_requested = 0;
		request_boundary(&boundary_save);
	}
	if ((practice_requested || load_requested) && replay_playing)
		stop_replay();
	if (practice_requested)
	{
		/* the area's start kept: the practice from it; else reached from the power on first (make_practice) */
		practice_requested = 0;
		menu_frame = -1;    /* (--menu's, once) */
		if (start_practice(&practice_chosen))
			return;
		practice_making = practice_chosen.area;
		reset_requested = 1;
		m6809_request_stop();
		return;
	}
	if (load_requested)
	{
		char path[1300];
		load_requested = 0;
		menu_frame = -1;    /* (--menu's, once: the state may go back before its frame) */
		state_path(path, sizeof(path), options.state_slot);
		load_state(path, options.state_slot, NULL);
	}
}

static void on_fault(const char *what, uint16_t pc)
{
	char message[256];
	snprintf(message, sizeof(message), "The program went where the translation has no code:\n%s at %04X (bank %u)",
		what, pc, gng.bank);
	fprintf(stderr, "makaimura: %s\n", message);
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, TITLE, message, window);
}

static void power_on(void)
{
	const gng_roms roms = gng_rom_set_roms(&rom_set);
	gng_power_on(&roms);
	gng.accuracy = (accuracy_given >= 0 ? accuracy_given : options.accuracy) ? GNG_ACCURACY_BOARD : GNG_ACCURACY_MAME;
	gng.inputs.dsw1 = dsw_given[0] >= 0 ? (uint8_t)dsw_given[0] : options.dsw1;
	fk.invincibility = options.fk_invincibility;
	fk.no_knockback = options.fk_no_knockback;
	fk.air_control = options.fk_air_control;
	gng.inputs.dsw2 = dsw_given[1] >= 0 ? (uint8_t)dsw_given[1] : options.dsw2;
	/* the controls at the power on: none pressed (an input script's changes are from there) */
	logged[0] = logged[1] = logged[2] = 0xFF;
	if (inputs_out && user_inputs)
		fprintf(inputs_out, "# makaimura --record-inputs (%s mode)\n0 dsw1 %02x\n0 dsw2 %02x\n",
			gng.accuracy == GNG_ACCURACY_BOARD ? "board" : "MAME", gng.inputs.dsw1, gng.inputs.dsw2);
	gng_video_decode();
	hiscore_power_on();
	gng_snd.on_sample = on_sample;
	gng_sound_power_on(roms.audiocpu, gng.accuracy == GNG_ACCURACY_MAME);
	gng.on_sound_latch = on_latch;
	gng.on_ls259 = on_ls259;
	m6809_host host = { on_vblank, on_irq, on_fault };
	irqs_taken = 0;
	irq_seen = 0;
	last_irq_frame = 0;
	hud.lost_frames = hud.flash_frames = 0;
	memset(lost_history, 0, sizeof(lost_history));
	last_conflicts = 0;
	m6809_power_on(&host);
}

/* ---- the start ---- */

static int find_rom_set(const char *given, const char *base, char *searched, size_t size)
{
	char place[1200];
	const char *places[8];
	int count = 0;
	static char from_base[3][1100];
	if (given && *given)
		places[count++] = given;
	if (options.rom[0])
		places[count++] = options.rom;
	if (base)
	{
		snprintf(from_base[0], sizeof(from_base[0]), "%s.", base);
		snprintf(from_base[1], sizeof(from_base[1]), "%srom", base);
		places[count++] = from_base[0];
		places[count++] = from_base[1];
		/* the repository's rom/ only for the program built in its build/bin (a package elsewhere asks for the set) */
		const size_t length = strlen(base);
		if (length >= 10 && (!SDL_strcasecmp(base + length - 10, "build\\bin\\") ||
			!SDL_strcasecmp(base + length - 10, "build/bin/")))
		{
			snprintf(from_base[2], sizeof(from_base[2]), "%s../../rom", base);
			places[count++] = from_base[2];
		}
	}
	places[count++] = ".";
	places[count++] = "rom";
	gng_rom_set_clear(&rom_set);
	size_t used = 0;
	searched[0] = 0;
	for (int i = 0; i < count && gng_rom_set_missing(&rom_set, NULL, 0); i++)
	{
		snprintf(place, sizeof(place), "%s", places[i]);
		gng_rom_set_add(&rom_set, place);
		if (used < size)
			used += (size_t)snprintf(searched + used, size - used, "  %s\n", places[i]);
	}
	return gng_rom_set_missing(&rom_set, NULL, 0) == 0;
}

int main(int argc, char *argv[])
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0)
	{
		fprintf(stderr, "makaimura: %s\n", SDL_GetError());
		return 1;
	}
	char *base = SDL_GetBasePath();
	const char *rom_given = NULL;
	snprintf(settings_path, sizeof(settings_path), "%smakaimura.ini", base ? base : "");
	for (int i = 1; i < argc; i++)
	{
		if (!strcmp(argv[i], "--settings") && i + 1 < argc)
			snprintf(settings_path, sizeof(settings_path), "%s", argv[++i]);
		else if (!strcmp(argv[i], "--inputs") && i + 1 < argc)
		{
			if (!read_script(argv[++i]))
			{
				fprintf(stderr, "makaimura: no input script %s\n", argv[i]);
				return 2;
			}
		}
		else if (!strcmp(argv[i], "--menu") && i + 1 < argc)
			menu_frame = atol(argv[++i]);
		else if (!strcmp(argv[i], "--menu-keys") && i + 1 < argc)
			menu_keys = argv[++i];
		else if (!strcmp(argv[i], "--hidden"))
			hidden = 1;
		else if (!strcmp(argv[i], "--record") && i + 1 < argc)
			record_path = argv[++i];
		else if (!strcmp(argv[i], "--save-state") && i + 2 < argc)
		{
			save_frame = atol(argv[++i]);
			save_to = argv[++i];
		}
		else if (!strcmp(argv[i], "--drop") && i + 1 < argc)
		{
			drop_check = 1;
			rom_drop_check(argv[++i], drop_shot);
		}
		else if (!strcmp(argv[i], "--drop-shot") && i + 1 < argc)
			drop_shot = argv[++i];
		else if (!strcmp(argv[i], "--update-test") && i + 2 < argc)
		{
			update_test_repository = argv[++i];
			update_test_version = argv[++i];
		}
		else if (!strcmp(argv[i], "--keep-replays"))
			keep_replays_given = 1;
		else if (!strcmp(argv[i], "--high-scores"))
			high_scores_given = 1;
		else if (!strcmp(argv[i], "--load-state") && i + 1 < argc)
			load_from = argv[++i];
		else if (!strcmp(argv[i], "--record-inputs") && i + 1 < argc)
		{
			user_inputs = 1;
			if (!(inputs_out = fopen(argv[++i], "w")))
			{
				fprintf(stderr, "makaimura: %s could not be made\n", argv[i]);
				return 2;
			}
		}
		else if (!strcmp(argv[i], "--accuracy") && i + 1 < argc)
		{
			++i;
			accuracy_given = !strcmp(argv[i], "board") ? 1 : !strcmp(argv[i], "mame") ? 0 : -1;
			if (accuracy_given < 0)
			{
				fprintf(stderr, "makaimura: --accuracy mame or board\n");
				return 2;
			}
		}
		else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
			frames_to_run = atol(argv[++i]);
		else if (!strcmp(argv[i], "--shot") && i + 2 < argc)
		{
			shot_frame = atol(argv[++i]);
			shot_path = argv[++i];
		}
		else
			rom_given = argv[i];
	}
	settings_defaults(&options);
	bool written = false;
	const char *problem = settings_load(&options, settings_path, &written);
	if (problem && !written)
	{
		char message[1200];
		snprintf(message, sizeof(message), "%s: %s.\nThose settings keep their defaults.", settings_path, problem);
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, TITLE, message, NULL);
	}

	char searched[2048];
	int asked = 0;    /* (the window asked for it: closed, the program ends with no message) */
	if (!find_rom_set(rom_given, base, searched, sizeof(searched)) &&
		(drop_check || !(shot_path || hidden || frames_to_run >= 0)))
	{
		asked = 1;
		/* the window that asks for it (rom_drop.h): the dropped .zips kept in the program's rom folder, a dropped
		   folder named in makaimura.ini */
		char keep[1300], folder[1100];
		snprintf(keep, sizeof(keep), "%srom", base ? base : "");
		if (rom_drop_screen(&rom_set, keep, folder, sizeof(folder)) && folder[0])
		{
			snprintf(options.rom, sizeof(options.rom), "%s", folder);
			settings_save(&options, settings_path);
		}
	}
	if (gng_rom_set_missing(&rom_set, NULL, 0))
	{
		char missing[512], message[3000];
		gng_rom_set_missing(&rom_set, missing, sizeof(missing));
		snprintf(message, sizeof(message),
			"The game's ROM set was not found: MAME's makaimurg.zip and its parent gng.zip.\n\n"
			"Missing: %s\n\nLooked for in:\n%s\nPut them beside the program or in its rom folder, or name the "
			"folder or zip in makaimura.ini ([files] rom).", missing, searched);
		if (asked)
			fprintf(stderr, "makaimura: the ROM set's window closed: %s missing\n", missing);
		else
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, TITLE, message, NULL);
		SDL_free(base);
		SDL_Quit();
		return 1;
	}
	SDL_free(base);

	/* the update: what an update before left cleared away; the newest release looked up (not in a check's run) */
	{
		char *folder = SDL_GetBasePath();
		if (folder)
		{
			updater_cleanup(folder);
			if (update_test_repository)
				updater_check(folder, update_test_repository, UPDATE_PACKAGE, update_test_version);
			else if (options.check_updates && !(shot_path || hidden || frames_to_run >= 0 || script || drop_check))
				updater_check(folder, UPDATE_REPOSITORY, UPDATE_PACKAGE, MAKAIMURA_VERSION);
			SDL_free(folder);
		}
	}

	/* the kept ranking: makaimura.hi beside the settings */
	{
		char path[1300];
		snprintf(path, sizeof(path), "%.*smakaimura.hi", (int)(file_name(settings_path) - settings_path), settings_path);
		hiscore_open(path);
	}

	const int height = GNG_SCREEN_HEIGHT * options.scale;
	window = SDL_CreateWindow(TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		(int)lround(height * (options.panels ? 16.0 / 9.0 : picture_aspect())), height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
		(shot_path || hidden ? SDL_WINDOW_HIDDEN : options.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
	if (options.fullscreen)
		SDL_ShowCursor(SDL_DISABLE);
	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE |
		(options.vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
	if (!renderer)
		renderer = SDL_CreateRenderer(window, -1, 0);
	picture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
		GNG_SCREEN_WIDTH, GNG_SCREEN_HEIGHT);

	/* a frame a refresh: only with vsync, and a display near the board's rate */
	SDL_DisplayMode mode;
	if (options.sync == SYNC_DISPLAY && options.vsync &&
		SDL_GetWindowDisplayMode(window, &mode) == 0 && mode.refresh_rate >= 57 && mode.refresh_rate <= 62)
	{
		display_sync = 1;
		speed = mode.refresh_rate / FRAME_RATE;
	}

	sound_on = options.sound && audio_out_open(options.volume, options.latency);
	for (int i = 0; i < SDL_NumJoysticks(); i++)
		open_pad(i);

	if (record_path)
		start_recording(record_path);
	const Uint64 started = SDL_GetPerformanceCounter();
	do
	{
		reset_requested = 0;
		script_next = 0;
		/* a replay: from its power on (or state); a reset ends one */
		if (replay_pending[0])
			prepare_replay();
		else
			stop_replay();
		power_on();
		drop_segment();
		last_system = 0xFF;
		if (replay_playing && replay_state[0])
			load_state(replay_state, 0, "REPLAY");
		/* (--load-state: the first run from it) */
		if (load_from)
		{
			load_state(load_from, 0, NULL);
			load_from = NULL;
		}
		m6809_run(UINT64_MAX);
	} while (reset_requested && !quit_requested);
	if (frames_to_run >= 0 || shot_path)
		printf("%llu frames in %.3f s\n", (unsigned long long)current_frame,
			(double)(SDL_GetPerformanceCounter() - started) / SDL_GetPerformanceFrequency());

	if (frames_to_run >= 0 || shot_path)
		audio_out_report();
	stop_recording();
	drop_segment();
	if (update_restart && !updater_restart(argv))
		fprintf(stderr, "makaimura: updated: the new program could not be started, start it again\n");
	hiscore_close();
	if (inputs_out)
		fclose(inputs_out);
	audio_out_close();
	SDL_Quit();
	return 0;
}
