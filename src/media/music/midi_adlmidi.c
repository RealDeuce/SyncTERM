/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "music_internal.h"
#include "music_config.h"
#if ST_MIDI_LINK_ADLMIDI
#include "dynamic_library.h"
#include "st_threads.h"
#include <adlmidi.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "adl_symbols.h"
#define MEMBER(result, name, args) result (*name) args;
struct adl_api { ST_ADL_SYMBOLS(MEMBER) };
#undef MEMBER
#if ST_MIDI_LINK_ADLMIDI == 2
#define ADDRESS(result, name, args) .name = name,
static struct adl_api api = { ST_ADL_SYMBOLS(ADDRESS) };
#undef ADDRESS
static bool loaded = true;
#else
static struct adl_api api;
static bool loaded;
static st_once_flag_t once = ST_ONCE_FLAG_INIT;
static void load(void)
{
	const char *const names[] = {"ADLMIDI", "adlmidi", "libADLMIDI"};
	dynamic_library_t *library;
	if (dynamic_library_open(&library, names, sizeof(names) / sizeof(names[0]), 1, DYNAMIC_LIBRARY_LOCAL) != 0) return;
#define RESOLVE(result, name, args) do { \
	void *symbol = dynamic_library_symbol(library, #name); \
	_Static_assert(sizeof(api.name) == sizeof(symbol), "loader function representation"); \
	if (symbol == NULL) { (void)dynamic_library_close(library); return; } \
	memcpy(&api.name, &symbol, sizeof(symbol)); \
} while (0);
	ST_ADL_SYMBOLS(RESOLVE)
#undef RESOLVE
	loaded = true;
}
#endif
static int send_message(void *context, const uint8_t *b, size_t size)
{
	struct ADL_MIDIPlayer *device = context;
	uint8_t ch = b[0] & 15;
	if (b[0] == 0xf0) { (void)api.adl_rt_systemExclusive(device, b, size); return 0; }
	switch (b[0] & 0xf0) {
	case 0x80: api.adl_rt_noteOff(device, ch, b[1]); break;
	case 0x90: if (b[2]) (void)api.adl_rt_noteOn(device, ch, b[1], b[2]); else api.adl_rt_noteOff(device, ch, b[1]); break;
	case 0xa0: api.adl_rt_noteAfterTouch(device, ch, b[1], b[2]); break;
	case 0xb0: api.adl_rt_controllerChange(device, ch, b[1], b[2]); break;
	case 0xc0: api.adl_rt_patchChange(device, ch, b[1]); break;
	case 0xd0: api.adl_rt_channelAfterTouch(device, ch, b[1]); break;
	case 0xe0: api.adl_rt_pitchBend(device, ch, (uint16_t)(b[1] | b[2] << 7)); break;
	default: break;
	}
	return 0;
}
static int render(void *context, int16_t *frames, size_t count)
{
	_Static_assert(sizeof(short) == sizeof(int16_t) && SHRT_MIN == INT16_MIN && SHRT_MAX == INT16_MAX, "16-bit audio samples");
	if (count > INT_MAX / 2) return IO_ERROR_OUT_OF_RANGE;
	/* ADLMIDI counts interleaved samples, not stereo frames. */
	return api.adl_generate(context, (int)count * 2, frames) == (int)count * 2 ? 0 : IO_ERROR_IO;
}
static void close_synth(void *context) { api.adl_close(context); }
int st_midi_adl_open(struct st_midi_sink *sink)
{
#if ST_MIDI_LINK_ADLMIDI == 1
	st_call_once(&once, load);
#endif
	if (!loaded) return IO_ERROR_NOT_SUPPORTED;
	struct ADL_MIDIPlayer *device = api.adl_init(AUDIO_SAMPLE_RATE);
	if (device == NULL) return IO_ERROR_IO;
	if (api.adl_getBanksCount() < 1 || api.adl_setBank(device, 0) != 0) {
		api.adl_close(device); return IO_ERROR_NOT_FOUND;
	}
	sink->state = device; sink->send = send_message; sink->render = render; sink->close = close_synth;
	return 0;
}
#else
int st_midi_adl_open(struct st_midi_sink *sink) { (void)sink; return IO_ERROR_NOT_SUPPORTED; }
#endif
