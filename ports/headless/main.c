/**
 *    Name:        main.c
 *    Description: gng_headless: the translated program (build/generated/program) on the board model, with no
 *                 window: frame by frame, the same outputs as the reference's (tools/reference/gref.cpp), to be
 *                 compared with it and with MAME
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    gng_headless [options]
 *      --regions FILE    the ROM regions (python tools/makaimura_rom.py --extract FILE); default build/makaimurg.regions
 *      --rom PLACE       the ROM set itself instead (runtime/src/rom_set.c): a .zip, or a folder with makaimurg.zip
 *                        and gng.zip (or the files); may be given more than once
 *      --write-regions FILE   the regions loaded, written as tools/makaimura_rom.py --extract writes them
 *      --frames N        how many frames (VBLANKs) to run (default 600)
 *      --accuracy MODE   mame or board (docs/accuracy.md)
 *      --bus-hashes FILE at each VBLANK the bus 0000-2FFF's FNV-1a: "frame hash pc" (pc 0000: the translation has
 *                        no PC in the middle of an instruction), as gref --bus-hashes writes it
 *      --irq-hashes FILE as the CPU takes each IRQ (its state pushed): "n hash pc cycle", as gref --irq-hashes
 *      --inputs FILE     an input script: "FRAME PORT VALUE" (system, p1, p2, dsw1, dsw2; hex, active low; or
 *                        mADDR: the work RAM's byte at ADDR, hex, written: a test's cheat, AGENTS.md),
 *                        applied at that frame's VBLANK for the frames after
 *      --dsw1 HEX, --dsw2 HEX   the DIP switches from power-on
 *      --sound FILE      the sound board too (the translated sound program, runtime/src/sound.c), and at each
 *                        VBLANK "frame hash 0000 time" (the Z80's RAM, its time), as gref --sound
 *      --ym-writes FILE  with --sound: every YM2203 write, "time chip offset value" (Z80 cycles), as gref's
 *      --sound-marks FILE  with --sound: the Z80's RAM at each YM2203 write, "write hash 0000", as gref's (the PC
 *                        not kept: the translation has none inside an instruction)
 *      --wav FILE        with --sound: the mix (MAME's routes) at the chips' 125 kHz, as gref --wav writes it
 *      --pictures N FILE every Nth frame's picture, sampled as gref's (frame N's indexes in frame N+1's colours)
 *      --picture-hashes FILE   each frame's picture sampled as gref's, its FNV-1a: "frame hash"
 *      --ppm FRAME FILE  that frame's picture as a PPM image
 *      --charset FILE    the text layer's 1024 characters, 32 a row, as a PPM image (pens as greys), and quit
 *      --palette-conflicts FILE  board mode: at each VBLANK where the frame had palette writes in the active
 *                        display, "frame writes" (docs/accuracy.md #2)
 *      --save-state FRAME FILE   the board's state at the first instruction boundary after that frame's VBLANK
 *                        (gng/state.h), the run going on (the sound board run, as in every state)
 *      --ram-watch LIST FILE   at each VBLANK where one of the work RAM's bytes at LIST (hex, comma separated)
 *                        changed, "frame byte=value ..." (all of them): for finding what the program keeps where
 *      --fk-invincibility N, --fk-no-knockback, --fk-air off|assist|full
 *                        Fair Knight's changes (gng/fair_knight.h, docs/fair-knight/); all off by default
 *      --sample-phase N  (an experiment) the YM2203s' sample grid at Z80 time 24 n + N (MAME mode: 3, board: 0)
 *      --load-state FILE the run from that state instead of power-on (the same options as the run that saved it:
 *                        the outputs are then those of its frames after the state's; tools/check_states.py)
 */
#include "gng/fair_knight.h"
#include "gng/board.h"
#include "gng/rom_set.h"
#include "gng/state.h"
#include "gng/video.h"
#include "m6809/cpu.h"
#include "gng/sound.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct region { char name[17]; uint32_t size; uint8_t *data; } region;
static region regions[16];
static int region_count;

static const uint8_t *find_region(const char *name)
{
	for (int i = 0; i < region_count; i++)
		if (!strcmp(regions[i].name, name))
			return regions[i].data;
	return NULL;
}

/* The ROM set's regions (--rom), as the regions file's */
static gng_rom_set rom_set;

static void add_region(const char *name, uint8_t *data, uint32_t size)
{
	region *r = &regions[region_count++];
	memset(r->name, 0, sizeof(r->name));
	strncpy(r->name, name, 16);
	r->data = data;
	r->size = size;
}

static int write_regions(const char *path)
{
	FILE *file = fopen(path, "wb");
	if (!file)
		return 0;
	fwrite("MKRG", 1, 4, file);
	for (int i = 0; i < region_count; i++)
	{
		const uint8_t size[4] = { (uint8_t)regions[i].size, (uint8_t)(regions[i].size >> 8),
			(uint8_t)(regions[i].size >> 16), (uint8_t)(regions[i].size >> 24) };
		fwrite(regions[i].name, 1, 16, file);
		fwrite(size, 1, 4, file);
		fwrite(regions[i].data, 1, regions[i].size, file);
	}
	return fclose(file) == 0;
}

static int read_regions(const char *path)
{
	FILE *file = fopen(path, "rb");
	if (!file)
		return 0;
	char magic[4];
	if (fread(magic, 1, 4, file) != 4 || memcmp(magic, "MKRG", 4))
	{
		fclose(file);
		return 0;
	}
	while (region_count < 16)
	{
		region *r = &regions[region_count];
		memset(r->name, 0, sizeof(r->name));
		if (fread(r->name, 1, 16, file) != 16)
			break;
		uint8_t size[4];
		if (fread(size, 1, 4, file) != 4)
			break;
		r->size = size[0] | (size[1] << 8) | (size[2] << 16) | ((uint32_t)size[3] << 24);
		r->data = malloc(r->size);
		if (!r->data || fread(r->data, 1, r->size, file) != r->size)
			break;
		region_count++;
	}
	fclose(file);
	return region_count > 0;
}

typedef struct input_change { int frame; char port[16]; uint8_t value; } input_change;
static input_change *inputs;
static int input_count, next_input;

static void read_inputs(const char *path)
{
	FILE *file = fopen(path, "r");
	if (!file)
	{
		fprintf(stderr, "gng_headless: no input script %s\n", path);
		exit(2);
	}
	char line[256];
	int capacity = 0;
	while (fgets(line, sizeof(line), file))
	{
		if (line[0] == '#')
			continue;
		input_change change;
		unsigned value;
		if (sscanf(line, "%d %15s %x", &change.frame, change.port, &value) == 3)
		{
			change.value = (uint8_t)value;
			if (input_count == capacity)
			{
				capacity = capacity ? capacity * 2 : 64;
				inputs = realloc(inputs, (size_t)capacity * sizeof(*inputs));
			}
			inputs[input_count++] = change;
		}
	}
	fclose(file);
}

static void apply_input(const input_change *change)
{
	if (!strcmp(change->port, "system")) gng.inputs.system = change->value;
	else if (!strcmp(change->port, "p1")) gng.inputs.p1 = change->value;
	else if (!strcmp(change->port, "p2")) gng.inputs.p2 = change->value;
	else if (!strcmp(change->port, "dsw1")) gng.inputs.dsw1 = change->value;
	else if (!strcmp(change->port, "dsw2")) gng.inputs.dsw2 = change->value;
	else if (change->port[0] == 'm' && strtoul(change->port + 1, NULL, 16) < 0x1E00u)
		gng.ram[strtoul(change->port + 1, NULL, 16)] = change->value;
	else fprintf(stderr, "gng_headless: unknown port %s\n", change->port);
}

static uint32_t bus_hash(void)
{
	uint32_t hash = 2166136261u;
	for (unsigned address = 0; address < 0x3000; address++)
		hash = (hash ^ gng_read((uint16_t)address)) * 16777619u;
	return hash;
}

static FILE *bus_hashes, *irq_hashes, *pictures, *picture_hashes;
static FILE *sound_out, *ym_out, *marks_out, *wav_out;
static FILE *conflicts_out;
static uint32_t wav_samples;

static void on_sample(int16_t sample)
{
	const uint8_t bytes[2] = { (uint8_t)sample, (uint8_t)((uint16_t)sample >> 8) };
	fwrite(bytes, 1, 2, wav_out);
	wav_samples++;
}

static void put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* gsound's WAV: 16-bit mono at 125 kHz */
static void close_wav(void)
{
	uint8_t header[44];
	memcpy(header, "RIFF", 4);
	put32(header + 4, 36 + wav_samples * 2);
	memcpy(header + 8, "WAVEfmt ", 8);
	put32(header + 16, 16);
	header[20] = 1; header[21] = 0;
	header[22] = 1; header[23] = 0;
	put32(header + 24, GNG_SOUND_RATE);
	put32(header + 28, GNG_SOUND_RATE * 2);
	header[32] = 2; header[33] = 0;
	header[34] = 16; header[35] = 0;
	memcpy(header + 36, "data", 4);
	put32(header + 40, wav_samples * 2);
	fseek(wav_out, 0, SEEK_SET);
	fwrite(header, 1, sizeof(header), wav_out);
	fclose(wav_out);
}
static int sound;

static void on_latch(gng_board *board, uint8_t value)
{
	(void)board;
	if (sound)
		gng_sound_latch(2 * cpu.cycles, value);    /* (Z80 cycles: two a 6809 cycle) */
}

static void on_ls259(gng_board *board, unsigned output, unsigned value)
{
	(void)board;
	if (sound && output == 1)
		gng_sound_q1(2 * cpu.cycles, (int)value);
}

static void on_ym(uint64_t time, int chip, int offset, uint8_t value)
{
	if (marks_out)
		fprintf(marks_out, "%llu %08x 0000\n", (unsigned long long)(gng_snd.ym_writes - 1), gng_sound_ram_hash());
	if (ym_out)
		fprintf(ym_out, "%llu %d %d %02x\n", (unsigned long long)time, chip, offset, value);
}
static int pictures_every, ppm_frame = -1;
static const char *ppm_path;
static uint8_t check_indexes[GNG_SCREEN_WIDTH * GNG_SCREEN_HEIGHT];
static uint64_t irqs;

static uint64_t picture_frame;

static void write_check_picture(void)
{
	static uint8_t rgb[GNG_SCREEN_WIDTH * GNG_SCREEN_HEIGHT * 3];
	for (size_t i = 0; i < sizeof(check_indexes); i++)
	{
		const uint8_t index = check_indexes[i];
		rgb[3 * i] = (uint8_t)((gng.palette_ext[index] >> 4) * 17);
		rgb[3 * i + 1] = (uint8_t)((gng.palette_ext[index] & 15) * 17);
		rgb[3 * i + 2] = (uint8_t)((gng.palette_base[index] >> 4) * 17);
	}
	if (pictures)
		fwrite(rgb, 1, sizeof(rgb), pictures);
	if (picture_hashes)
	{
		uint32_t hash = 2166136261u;
		for (size_t i = 0; i < sizeof(rgb); i++)
			hash = (hash ^ rgb[i]) * 16777619u;
		fprintf(picture_hashes, "%llu %08x\n", (unsigned long long)picture_frame, hash);
	}
}

/* --ram-watch */
static uint16_t watched[32];
static int watched_count;
static FILE *watch_out;

static void watch_ram(uint64_t frame)
{
	static uint8_t last[32];
	static int started;
	int changed = !started;
	for (int i = 0; i < watched_count; i++)
		changed |= gng.ram[watched[i]] != last[i];
	if (!changed)
		return;
	started = 1;
	fprintf(watch_out, "%llu", (unsigned long long)frame);
	for (int i = 0; i < watched_count; i++)
	{
		last[i] = gng.ram[watched[i]];
		fprintf(watch_out, " %02x=%02x", watched[i], last[i]);
	}
	fputc('\n', watch_out);
}

/* --save-state: the state at the boundary after its frame's VBLANK */
static long save_frame = -1;
static const char *save_path;

static void save_state(uint16_t pc)
{
	const size_t size = gng_state_size();
	uint8_t *state = malloc(size);
	gng_state_save(state, pc);
	FILE *file = fopen(save_path, "wb");
	if (!file || fwrite(state, 1, size, file) != size)
	{
		fprintf(stderr, "gng_headless: could not write %s\n", save_path);
		exit(1);
	}
	fclose(file);
	printf("state saved in frame %llu (pc %04X, cycle %llu, sound sample %llu)\n",
		(unsigned long long)gng_state_frame(state), pc, (unsigned long long)cpu.cycles,
		(unsigned long long)gng_snd.samples);
	free(state);
}

static void on_vblank(uint64_t frame)
{
	if ((long)frame == save_frame)
		m6809_at_boundary(save_state);
	if (watch_out)
		watch_ram(frame);
	if (sound)
	{
		/* the sound board up to the VBLANK */
		if (gng_sound_run_until(2 * (GNG_VBLANK_CYCLE + frame * GNG_CPU_CYCLES_PER_FRAME)) < 0)
			fprintf(stderr, "gng_headless: the sound program faulted\n");
		if (sound_out)
			fprintf(sound_out, "%llu %08x 0000 %llu\n", (unsigned long long)frame, gng_sound_ram_hash(),
				(unsigned long long)gng_sound_time());
	}
	if (bus_hashes)
		fprintf(bus_hashes, "%llu %08x %04x\n", (unsigned long long)frame, bus_hash(), 0);
	if (conflicts_out)
	{
		static uint32_t seen;
		if (gng.palette_conflicts != seen)
			fprintf(conflicts_out, "%llu %u\n", (unsigned long long)frame, gng.palette_conflicts - seen);
		seen = gng.palette_conflicts;
	}
	if ((pictures || picture_hashes) && gng.accuracy == GNG_ACCURACY_BOARD)
	{
		/* board mode: the frame the beam drew, at its own VBLANK (no MAME sampling to follow) */
		if (frame % pictures_every == 0)
		{
			static uint32_t colours[GNG_SCREEN_WIDTH * GNG_SCREEN_HEIGHT];
			static uint8_t rgb[GNG_SCREEN_WIDTH * GNG_SCREEN_HEIGHT * 3];
			gng_video_render(colours);
			for (size_t i = 0; i < GNG_SCREEN_WIDTH * GNG_SCREEN_HEIGHT; i++)
			{
				rgb[3 * i] = (uint8_t)(colours[i] >> 16);
				rgb[3 * i + 1] = (uint8_t)(colours[i] >> 8);
				rgb[3 * i + 2] = (uint8_t)colours[i];
			}
			if (pictures)
				fwrite(rgb, 1, sizeof(rgb), pictures);
			if (picture_hashes)
			{
				uint32_t hash = 2166136261u;
				for (size_t i = 0; i < sizeof(rgb); i++)
					hash = (hash ^ rgb[i]) * 16777619u;
				fprintf(picture_hashes, "%llu %08x\n", (unsigned long long)frame, hash);
			}
		}
	}
	else
	{
		if ((pictures || picture_hashes) && frame >= 1 && (frame - 1) % pictures_every == 0)
		{
			picture_frame = frame - 1;
			write_check_picture();
		}
		if ((pictures || picture_hashes) && frame % pictures_every == 0)
			gng_video_render_indexed(check_indexes);
	}
	if ((int64_t)frame == ppm_frame)
	{
		static uint32_t picture[GNG_SCREEN_WIDTH * GNG_SCREEN_HEIGHT];
		gng_video_render(picture);
		FILE *ppm = fopen(ppm_path, "wb");
		fprintf(ppm, "P6\n%d %d\n255\n", GNG_SCREEN_WIDTH, GNG_SCREEN_HEIGHT);
		for (size_t i = 0; i < sizeof(picture) / 4; i++)
		{
			const uint8_t rgb[3] = { (uint8_t)(picture[i] >> 16), (uint8_t)(picture[i] >> 8), (uint8_t)picture[i] };
			fwrite(rgb, 1, 3, ppm);
		}
		fclose(ppm);
	}
	while (next_input < input_count && inputs[next_input].frame <= (int64_t)frame)
		apply_input(&inputs[next_input++]);
}

static FILE *irq_state, *irq_dumps;

static void on_irq(uint16_t pc, uint64_t cycles)
{
	if (irq_state)
		fprintf(irq_state, "%llu %08x %llu\n", (unsigned long long)irqs, gng_bus_hash_without_stack(),
			(unsigned long long)cycles);
	if (irq_dumps)
	{
		uint8_t bus[0x3000];
		for (unsigned address = 0; address < sizeof(bus); address++)
			bus[address] = gng_read((uint16_t)address);
		fwrite(bus, 1, sizeof(bus), irq_dumps);
	}
	if (irq_hashes)
		fprintf(irq_hashes, "%llu %08x %04x %llu\n", (unsigned long long)irqs, bus_hash(), pc, (unsigned long long)cycles);
	irqs++;
}

static void on_fault(const char *what, uint16_t pc)
{
	fprintf(stderr, "gng_headless: %s at %04X (bank %u, cycle %llu)\n", what, pc, gng.bank, (unsigned long long)cpu.cycles);
}

int main(int argc, char **argv)
{
	const char *regions_path = "build/makaimurg.regions";
	const char *write_path = NULL;
	const char *charset_path = NULL;
	const char *load_path = NULL;
	int sample_phase = -1;
	int rom_places = 0;
	gng_rom_set_clear(&rom_set);
	long frames = 600;
	int dsw1 = GNG_DSW1_DEFAULT, dsw2 = GNG_DSW2_DEFAULT;
	gng_accuracy_mode accuracy = GNG_ACCURACY_MAME;
	for (int i = 1; i < argc; i++)
	{
		const char *a = argv[i];
#define NEXT() (i + 1 < argc ? argv[++i] : (fprintf(stderr, "gng_headless: %s needs a value\n", a), exit(2), ""))
		if (!strcmp(a, "--regions")) regions_path = NEXT();
		else if (!strcmp(a, "--rom")) { gng_rom_set_add(&rom_set, NEXT()); rom_places++; }
		else if (!strcmp(a, "--write-regions")) write_path = NEXT();
		else if (!strcmp(a, "--frames")) frames = atol(NEXT());
		else if (!strcmp(a, "--accuracy")) accuracy = !strcmp(NEXT(), "board") ? GNG_ACCURACY_BOARD : GNG_ACCURACY_MAME;
		else if (!strcmp(a, "--bus-hashes")) bus_hashes = fopen(NEXT(), "w");
		else if (!strcmp(a, "--irq-hashes")) irq_hashes = fopen(NEXT(), "w");
		else if (!strcmp(a, "--irq-state")) irq_state = fopen(NEXT(), "w");
		else if (!strcmp(a, "--irq-dumps")) irq_dumps = fopen(NEXT(), "wb");
		else if (!strcmp(a, "--sound")) { sound = 1; sound_out = fopen(NEXT(), "w"); }
		else if (!strcmp(a, "--ym-writes")) ym_out = fopen(NEXT(), "w");
		else if (!strcmp(a, "--sound-marks")) marks_out = fopen(NEXT(), "w");
		else if (!strcmp(a, "--wav")) wav_out = fopen(NEXT(), "wb");
		else if (!strcmp(a, "--inputs")) read_inputs(NEXT());
		else if (!strcmp(a, "--dsw1")) dsw1 = (int)strtoul(NEXT(), NULL, 16) & 0xFF;
		else if (!strcmp(a, "--dsw2")) dsw2 = (int)strtoul(NEXT(), NULL, 16) & 0xFF;
		else if (!strcmp(a, "--pictures")) { pictures_every = atoi(NEXT()); pictures = fopen(NEXT(), "wb"); }
		else if (!strcmp(a, "--picture-hashes")) { picture_hashes = fopen(NEXT(), "w"); pictures_every = 1; }
		else if (!strcmp(a, "--ppm")) { ppm_frame = atoi(NEXT()); ppm_path = NEXT(); }
		else if (!strcmp(a, "--palette-conflicts")) conflicts_out = fopen(NEXT(), "w");
		else if (!strcmp(a, "--charset")) charset_path = NEXT();
		else if (!strcmp(a, "--save-state")) { save_frame = atol(NEXT()); save_path = NEXT(); }
		else if (!strcmp(a, "--load-state")) load_path = NEXT();
		else if (!strcmp(a, "--sample-phase")) sample_phase = atoi(NEXT());
		else if (!strcmp(a, "--fk-invincibility")) fk.invincibility = atoi(NEXT());
		else if (!strcmp(a, "--fk-no-knockback")) fk.no_knockback = 1;
		else if (!strcmp(a, "--fk-air"))
		{
			const char *mode = NEXT();
			if ((fk.air_control = fk_air_control_parse(mode)) < 0)
			{
				fprintf(stderr, "gng_headless: --fk-air off, assist or full\n");
				return 2;
			}
		}
		else if (!strcmp(a, "--ram-watch"))
		{
			char list[256];
			snprintf(list, sizeof(list), "%s", NEXT());
			for (char *item = strtok(list, ","); item && watched_count < 32; item = strtok(NULL, ","))
				watched[watched_count++] = (uint16_t)(strtoul(item, NULL, 16) % sizeof(gng.ram));
			watch_out = fopen(NEXT(), "w");
		}
		else { fprintf(stderr, "gng_headless: unknown option %s\n", a); return 2; }
#undef NEXT
	}
	/* (a state is the whole board's, its sound board too: makaimura loads it) */
	if (save_frame >= 0 || load_path)
		sound = 1;
	if (rom_places)
	{
		char missing[512];
		if (gng_rom_set_missing(&rom_set, missing, sizeof(missing)))
		{
			fprintf(stderr, "gng_headless: missing from the ROM set: %s\n", missing);
			return 2;
		}
		add_region("maincpu", rom_set.maincpu, sizeof(rom_set.maincpu));
		add_region("audiocpu", rom_set.audiocpu, sizeof(rom_set.audiocpu));
		add_region("chars", rom_set.chars, sizeof(rom_set.chars));
		add_region("tiles", rom_set.tiles, sizeof(rom_set.tiles));
		add_region("sprites", rom_set.sprites, sizeof(rom_set.sprites));
		add_region("proms", rom_set.proms, sizeof(rom_set.proms));
	}
	else if (!read_regions(regions_path) || !find_region("maincpu"))
	{
		fprintf(stderr, "gng_headless: no ROM regions in %s (python tools/makaimura_rom.py --extract %s)\n",
			regions_path, regions_path);
		return 2;
	}
	if (write_path && !write_regions(write_path))
	{
		fprintf(stderr, "gng_headless: could not write %s\n", write_path);
		return 2;
	}
	gng_roms roms = { 0 };
	roms.maincpu = find_region("maincpu");
	roms.audiocpu = find_region("audiocpu");
	roms.chars = find_region("chars");
	roms.tiles = find_region("tiles");
	roms.sprites = find_region("sprites");
	gng_power_on(&roms);
	gng.accuracy = accuracy;
	gng.inputs.dsw1 = (uint8_t)dsw1;
	gng.inputs.dsw2 = (uint8_t)dsw2;
	gng_video_decode();
	if (charset_path)
	{
		FILE *ppm = fopen(charset_path, "wb");
		if (!ppm)
			return 2;
		fprintf(ppm, "P6\n%d %d\n255\n", 32 * 9, 32 * 9);
		for (int y = 0; y < 32 * 9; y++)
			for (int x = 0; x < 32 * 9; x++)
			{
				const int code = (y / 9) * 32 + x / 9;
				const int edge = x % 9 == 8 || y % 9 == 8;
				const int pen = edge ? -1 : gng_video_char_pen(code, x % 9, y % 9);
				const uint8_t grey = (uint8_t)(edge ? 0x40 : pen == 3 ? 0 : 255 - pen * 70);
				const uint8_t rgb[3] = { edge ? (uint8_t)0x80 : grey, grey, grey };
				fwrite(rgb, 1, 3, ppm);
			}
		fclose(ppm);
		return 0;
	}
	if (sound)
	{
		if (!roms.audiocpu)
		{
			fprintf(stderr, "gng_headless: no audiocpu region\n");
			return 2;
		}
		gng_snd.on_ym_write = on_ym;
		if (wav_out)
		{
			static const uint8_t blank[44] = { 0 };
			fwrite(blank, 1, sizeof(blank), wav_out);
			gng_snd.on_sample = on_sample;
		}
		gng_sound_power_on(roms.audiocpu, accuracy == GNG_ACCURACY_MAME);
		if (sample_phase >= 0)
			gng_snd.sample_phase = (uint32_t)sample_phase;    /* (--sample-phase: an experiment's) */
		gng.on_sound_latch = on_latch;
		gng.on_ls259 = on_ls259;
	}

	m6809_host host = { on_vblank, on_irq, on_fault };
	m6809_power_on(&host);
	if (load_path)
	{
		FILE *file = fopen(load_path, "rb");
		const size_t size = gng_state_size();
		uint8_t *state = malloc(size + 1);
		const size_t got = file ? fread(state, 1, size + 1, file) : 0;
		if (file)
			fclose(file);
		const char *problem = gng_state_check(state, got);
		if (problem)
		{
			fprintf(stderr, "gng_headless: %s: %s\n", load_path, file ? problem : "cannot be read");
			return 2;
		}
		/* the script's changes up to the state's frame are in it already */
		while (next_input < input_count && inputs[next_input].frame < (int64_t)gng_state_frame(state))
			next_input++;
		gng_state_load(state);
		free(state);
	}
	const int result = m6809_run((uint64_t)frames);
	if (gng.accuracy == GNG_ACCURACY_BOARD)
		printf("board mode: %u sprite DMAs, %u palette writes in the active display\n", gng.dma_count,
			gng.palette_conflicts);
	if (bus_hashes) fclose(bus_hashes);
	if (irq_hashes) fclose(irq_hashes);
	if (irq_state) fclose(irq_state);
	if (irq_dumps) fclose(irq_dumps);
	if (pictures) fclose(pictures);
	if (picture_hashes) fclose(picture_hashes);
	if (sound_out) fclose(sound_out);
	if (ym_out) fclose(ym_out);
	if (marks_out) fclose(marks_out);
	if (wav_out) close_wav();
	return result ? 1 : 0;
}
