/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "music_internal.h"
#include "music_config.h"
#if ST_MIDI_LINK_FLUIDSYNTH
#include "dynamic_library.h"
#include "st_threads.h"
#include "filesystem.h"
#include <fluidsynth.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "fluid_symbols.h"
#define MEMBER(result, name, args) result (*name) args;
struct fluid_api { ST_FLUID_SYMBOLS(MEMBER) };
#undef MEMBER
#if ST_MIDI_LINK_FLUIDSYNTH == 2
#define ADDRESS(result, name, args) .name = name,
static struct fluid_api api = { ST_FLUID_SYMBOLS(ADDRESS) };
#undef ADDRESS
static bool loaded = true;
#else
static struct fluid_api api;
static bool loaded;
static st_once_flag_t once = ST_ONCE_FLAG_INIT;
static void load(void)
{
	const char *const names[] = {"fluidsynth", "libfluidsynth", "libfluidsynth-3"};
	dynamic_library_t *library;
	if (dynamic_library_open(&library, names, sizeof(names) / sizeof(names[0]), 3, DYNAMIC_LIBRARY_LOCAL) != 0) return;
#define RESOLVE(result, name, args) do { \
	void *symbol = dynamic_library_symbol(library, #name); \
	_Static_assert(sizeof(api.name) == sizeof(symbol), "loader function representation"); \
	if (symbol == NULL) { (void)dynamic_library_close(library); return; } \
	memcpy(&api.name, &symbol, sizeof(symbol)); \
} while (0);
	ST_FLUID_SYMBOLS(RESOLVE)
#undef RESOLVE
	loaded = true;
}
#endif
struct fluid_state { fluid_settings_t *settings; fluid_synth_t *synth; int sf, bank, preset; };
static int send_message(void *context, const uint8_t *b, size_t size)
{
	struct fluid_state *f = context;
	int ch = b[0] & 15, status = FLUID_OK;
	if (b[0] == 0xf0 && size >= 2 && size - 2 <= INT_MAX) {
		/* An unsupported vendor message is not a transport failure. */
		(void)api.fluid_synth_sysex(f->synth, (const char *)b + 1, (int)size - 2, NULL, NULL, NULL, 0);
		return 0;
	}
	switch (b[0] & 0xf0) {
	case 0x80: status = api.fluid_synth_noteoff(f->synth, ch, b[1]); break;
	case 0x90: status = b[2] ? api.fluid_synth_noteon(f->synth, ch, b[1], b[2]) : api.fluid_synth_noteoff(f->synth, ch, b[1]); break;
	case 0xa0: status = api.fluid_synth_key_pressure(f->synth, ch, b[1], b[2]); break;
	case 0xb0: status = api.fluid_synth_cc(f->synth, ch, b[1], b[2]); break;
	case 0xc0: status = api.fluid_synth_program_change(f->synth, ch, b[1]); break;
	case 0xd0: status = api.fluid_synth_channel_pressure(f->synth, ch, b[1]); break;
	case 0xe0: status = api.fluid_synth_pitch_bend(f->synth, ch, b[1] | b[2] << 7); break;
	default: break;
	}
	/* Missing presets and unmatched Note Offs are valid file playback. */
	(void)status;
	return 0;
}
static int render(void *context, int16_t *frames, size_t count)
{
	struct fluid_state *f = context;
	if (count > INT_MAX) return IO_ERROR_OUT_OF_RANGE;
	return api.fluid_synth_write_s16(f->synth, (int)count, frames, 0, 2, frames, 1, 2) == FLUID_OK ? 0 : IO_ERROR_IO;
}
static void ansi(void *context)
{
	struct fluid_state *f = context;
	(void)api.fluid_synth_program_select(f->synth, 0, f->sf, f->bank, f->preset);
}
static void close_synth(void *context)
{
	struct fluid_state *f = context;
	if (f->synth != NULL) api.delete_fluid_synth(f->synth);
	if (f->settings != NULL) api.delete_fluid_settings(f->settings);
	free(f);
}
int st_midi_fluid_open(struct st_midi_sink *sink)
{
#if ST_MIDI_LINK_FLUIDSYNTH == 1
	st_call_once(&once, load);
#endif
	if (!loaded) return IO_ERROR_NOT_SUPPORTED;
	struct fluid_state *f = calloc(1, sizeof(*f));
	if (f == NULL) return IO_ERROR_OUT_OF_MEMORY;
	f->settings = api.new_fluid_settings();
	if (f->settings == NULL) { close_synth(f); return IO_ERROR_OUT_OF_MEMORY; }
	char path[32768];
	char *config = api.fluid_get_userconf(path, sizeof(path));
	if (config != NULL) {
		FILE *check = NULL;
		if (fs_open(&check, config, "rb") != 0) config = NULL; else fclose(check);
	}
	fluid_cmd_handler_t *handler = api.new_fluid_cmd_handler2(f->settings, NULL, NULL, NULL);
	if (handler == NULL) { close_synth(f); return IO_ERROR_OUT_OF_MEMORY; }
	/* FluidSynth's own early pass applies settings before construction. The
	 * internal renderer's sample format belongs to its PCM host. */
	if (config != NULL) (void)api.fluid_source(handler, config);
	api.delete_fluid_cmd_handler(handler);
	(void)api.fluid_settings_setnum(f->settings, "synth.sample-rate", AUDIO_SAMPLE_RATE);
	f->synth = api.new_fluid_synth(f->settings);
	if (f->synth == NULL) { close_synth(f); return IO_ERROR_IO; }
	handler = api.new_fluid_cmd_handler2(f->settings, f->synth, NULL, NULL);
	if (handler == NULL) { close_synth(f); return IO_ERROR_OUT_OF_MEMORY; }
	if (config != NULL) (void)api.fluid_source(handler, config);
	api.delete_fluid_cmd_handler(handler);
	(void)api.fluid_settings_setnum(f->settings, "synth.sample-rate", AUDIO_SAMPLE_RATE);
	if (api.fluid_synth_sfcount(f->synth) == 0) {
		char *bank = NULL;
		if (api.fluid_settings_dupstr(f->settings, "synth.default-soundfont", &bank) == FLUID_OK && bank != NULL && *bank != 0)
			(void)api.fluid_synth_sfload(f->synth, bank, 1);
		api.fluid_free(bank);
	}
	if (api.fluid_synth_sfcount(f->synth) == 0) { close_synth(f); return IO_ERROR_NOT_FOUND; }
	(void)api.fluid_synth_get_program(f->synth, 0, &f->sf, &f->bank, &f->preset);
	sink->state = f; sink->send = send_message; sink->render = render; sink->close = close_synth; sink->ansi = ansi;
	/* Calibrate at the library's default gain, then restore the user's gain.
	 * Otherwise calibration would cancel an explicit synth.gain setting. */
	double configured = api.fluid_synth_get_gain(f->synth), baseline;
	if (api.fluid_settings_getnum_default(f->settings, "synth.gain", &baseline) != FLUID_OK)
		return IO_ERROR_IO;
	(void)api.fluid_settings_setnum(f->settings, "synth.gain", baseline);
	int status = st_midi_sink_calibrate(sink);
	(void)api.fluid_settings_setnum(f->settings, "synth.gain", configured);
	return status;
}
#else
int st_midi_fluid_open(struct st_midi_sink *sink) { (void)sink; return IO_ERROR_NOT_SUPPORTED; }
#endif
