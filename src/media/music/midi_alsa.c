/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "music_internal.h"
#include "music_config.h"
#if ST_MIDI_LINK_ALSA
#include "dynamic_library.h"
#include "st_threads.h"
#include "string_compat.h"
#include <alsa/asoundlib.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "alsa_symbols.h"
#define MEMBER(result, name, args) result (*name) args;
struct alsa_api { ST_ALSA_SYMBOLS(MEMBER) };
#undef MEMBER
#if ST_MIDI_LINK_ALSA == 2
#define ADDRESS(result, name, args) .name = name,
static struct alsa_api api = { ST_ALSA_SYMBOLS(ADDRESS) };
#undef ADDRESS
static bool loaded = true;
#else
static struct alsa_api api;
static bool loaded;
static st_once_flag_t once = ST_ONCE_FLAG_INIT;
static void load(void)
{
	const char *const names[] = {"asound"};
	dynamic_library_t *library;
	if (dynamic_library_open(&library, names, 1, 2, DYNAMIC_LIBRARY_LOCAL) != 0) return;
#define RESOLVE(result, name, args) do { \
	void *symbol = dynamic_library_symbol(library, #name); \
	_Static_assert(sizeof(api.name) == sizeof(symbol), "loader function representation"); \
	if (symbol == NULL) { (void)dynamic_library_close(library); return; } \
	memcpy(&api.name, &symbol, sizeof(symbol)); \
} while (0);
	ST_ALSA_SYMBOLS(RESOLVE)
#undef RESOLVE
	loaded = true;
}
#endif
struct alsa_state { snd_seq_t *seq; snd_midi_event_t *encoder; snd_seq_addr_t *ports; size_t count; int source; struct st_midi_sink *sink; };
static void close_seq(void *context)
{
	struct alsa_state *a = context;
	if (a->encoder != NULL) api.snd_midi_event_free(a->encoder);
	if (a->seq != NULL) (void)api.snd_seq_close(a->seq);
	free(a->ports); free(a);
}
static int send_message(void *context, const uint8_t *bytes, size_t size)
{
	struct alsa_state *a = context;
	snd_seq_event_t event;
	memset(&event, 0, sizeof(event));
	if (bytes[0] == 0xf0) {
		if (size > UINT_MAX) return IO_ERROR_OUT_OF_RANGE;
		event.type = SND_SEQ_EVENT_SYSEX;
		event.flags = SND_SEQ_EVENT_LENGTH_VARIABLE;
		event.data.ext.len = (unsigned)size; event.data.ext.ptr = (void *)bytes;
	} else {
		if (size > LONG_MAX || api.snd_midi_event_encode(a->encoder, bytes, (long)size, &event) != (long)size)
			return IO_ERROR_INVALID_FORMAT;
		if (event.type == SND_SEQ_EVENT_NONE) return 0;
	}
	event.source.port = (unsigned char)a->source;
	event.queue = SND_SEQ_QUEUE_DIRECT;
	int result = 0;
	for (size_t i = 0; i < a->count; ++i) {
		event.dest = a->ports[i];
		for (;;) {
			int status = api.snd_seq_event_output_direct(a->seq, &event);
			if (status >= 0) break;
			if (status != -EAGAIN) { result = IO_ERROR_IO; break; }
			if (a->sink->cancelled != NULL && a->sink->cancelled(a->sink->cancel_context)) { result = IO_ERROR_CANCELLED; break; }
			struct timespec pause = {0, 2000000}; (void)st_thread_sleep(&pause, NULL);
		}
	}
	/* A failed route must not prevent cleanup reaching surviving routes. */
	return result;
}
int st_midi_alsa_open(struct st_midi_sink *sink)
{
	const char *routes = getenv("ALSA_OUTPUT_PORTS");
	if (routes == NULL || *routes == 0) return IO_ERROR_NOT_FOUND;
#if ST_MIDI_LINK_ALSA == 1
	st_call_once(&once, load);
#endif
	if (!loaded) return IO_ERROR_NOT_SUPPORTED;
	struct alsa_state *a = calloc(1, sizeof(*a));
	if (a == NULL) return IO_ERROR_OUT_OF_MEMORY;
	a->sink = sink;
	if (api.snd_seq_open(&a->seq, "default", SND_SEQ_OPEN_OUTPUT, SND_SEQ_NONBLOCK) < 0) { close_seq(a); return IO_ERROR_IO; }
	(void)api.snd_seq_set_client_name(a->seq, "SyncTERM");
	a->source = api.snd_seq_create_simple_port(a->seq, "MIDI playback", SND_SEQ_PORT_CAP_READ, SND_SEQ_PORT_TYPE_APPLICATION | SND_SEQ_PORT_TYPE_MIDI_GENERIC);
	if (a->source < 0 || a->source > UCHAR_MAX || api.snd_midi_event_new(256, &a->encoder) < 0) { close_seq(a); return IO_ERROR_IO; }
	char *copy = strdup(routes), *p = copy;
	if (copy == NULL) { close_seq(a); return IO_ERROR_OUT_OF_MEMORY; }
	while (p != NULL) {
		char *next = strchr(p, ','); if (next != NULL) *next++ = 0;
		if (a->count >= SIZE_MAX / sizeof(*a->ports)) { free(copy); close_seq(a); return IO_ERROR_OUT_OF_RANGE; }
		snd_seq_addr_t address;
		if (api.snd_seq_parse_address(a->seq, &address, p) < 0
		    || api.snd_seq_connect_to(a->seq, a->source, address.client, address.port) < 0) { free(copy); close_seq(a); return IO_ERROR_NOT_FOUND; }
		snd_seq_addr_t *ports = realloc(a->ports, (a->count + 1) * sizeof(*ports));
		if (ports == NULL) { free(copy); close_seq(a); return IO_ERROR_OUT_OF_MEMORY; }
		a->ports = ports; a->ports[a->count++] = address; p = next;
	}
	free(copy);
	sink->state = a; sink->send = send_message; sink->close = close_seq;
	return 0;
}
#else
int st_midi_alsa_open(struct st_midi_sink *sink) { (void)sink; return IO_ERROR_NOT_SUPPORTED; }
#endif
