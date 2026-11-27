/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "st_term.h"
#include "st_threads.h"
#include "st_time.h"

#define CHECK(e)                                                   \
	do {                                                       \
		if (!(e)) {                                        \
			fprintf(stderr, "%d: %s\n", __LINE__, #e); \
			return EXIT_FAILURE;                       \
		}                                                  \
	} while (0)

static int
wait_for_marker(st_term_session_t *session, int column)
{
	struct st_cterm_state state;
	uint64_t              now, start;
	CHECK(st_time_monotonic_ms(&start) == 0);
	do {
		enum st_term_disposition disposition;
		bool                     finished;
		CHECK(st_term_session_pump(session, &disposition, &finished) == 0 && !finished);
		CHECK(st_cterm_state(st_term_session_cterm(session), &state) == 0);
		CHECK(st_time_monotonic_ms(&now) == 0 && now - start < 15000);
	} while (state.x != column);
	return EXIT_SUCCESS;
}

static bool
bars_match(st_console_t *console, bool visible)
{
	/* Tracks 1, 3 and 4 play a looping note. Track 2 rests, and the song
	 * has no fifth track. All four directions share a background of 9. */
	const struct st_console_rect r      = {0, 40, 128, 42};
	struct st_pixel_buffer      *pixels = NULL;
	if (st_console_get_pixels(&pixels, console, &r, false) != 0)
		return false;
	bool ok = true;
	for (unsigned plane = 0; plane < 2; ++plane) {
		const uint32_t *p = plane ? pixels->alternate : pixels->primary;
		for (unsigned y = 0; y < r.height; ++y) {
			for (unsigned x = 0; x < r.width; ++x) {
				uint32_t v = p[y * r.width + x];
				if (visible && x >= 20 && x <= 22 && y >= 5 && y <= 20) {
					if (y >= 19 ? v != 30 : (v != 30 && v != 9))
						ok = false;
				}
				else if (visible && y >= 18 && y <= 20 && x >= 60 && x <= 75) {
					if (x <= 61 ? v != 50 : (v != 50 && v != 9))
						ok = false;
				}
				else if (visible && y >= 18 && y <= 20 && x >= 85 && x <= 100) {
					if (x >= 99 ? v != 60 : (v != 60 && v != 9))
						ok = false;
				}
				else if (v != 9)
					ok = false;
			}
		}
	}
	st_pixel_buffer_destroy(pixels);
	return ok;
}

static bool
pixel_is(st_console_t *console, unsigned x, unsigned y, unsigned colour)
{
	struct st_console_rect area = {x, y, 1, 1};
	struct st_pixel_buffer *pixels = NULL;
	if (st_console_get_pixels(&pixels, console, &area, false) != 0)
		return false;
	bool matches = pixels->primary[0] == colour && pixels->alternate[0] == colour;
	st_pixel_buffer_destroy(pixels);
	return matches;
}

static ssize_t
accept_reply(void *context, const void *bytes, size_t size, int64_t timeout)
{
	(void)context;
	(void)bytes;
	(void)timeout;
	return (ssize_t)size;
}

static int
check_autoplay(st_term_session_t *session, st_audio_runtime_t *audio, const char *downloads, bool complete)
{
	uint8_t wire[8192];
	size_t  size = fread(wire, 1, sizeof(wire), stdin);
	CHECK(size && size < sizeof(wire) && feof(stdin));
	struct st_transfer_config config = {.protocol = ST_TRANSFER_ZMODEM,
	    .directory                                = downloads,
	    .send                                     = accept_reply};
	config.receive_completion_events = !complete;
	st_term_session_configure_download(session, &config);
	st_transfer_t *worker = NULL;
	CHECK(st_transfer_create(&worker, &config) == 0);
	CHECK(st_term_session_begin_transfer(session, worker) == 0);
	CHECK(st_transfer_launch(worker) == 0);
	CHECK(st_transfer_feed(worker, wire, size) == size);
	const struct timespec poll     = {.tv_nsec = 1000000};
	unsigned              received = 0;
	for (unsigned i = 0; i < 3000 && !st_transfer_done(worker); ++i) {
		struct st_transfer_receive_event event;
		while (st_transfer_take_received(worker, &event)) {
			CHECK(event.kind == ST_TRANSFER_RECEIVE_END && event.success);
			if (++received == 2)
				CHECK(st_transfer_request_abort(worker));
		}
		st_thread_sleep(&poll, NULL);
	}
	CHECK(st_transfer_done(worker));
	CHECK(st_term_session_end_transfer(session) == 0);
	CHECK((st_transfer_result(worker) == 0) == complete);
	st_transfer_destroy(&worker);

	int16_t pcm[10];
	CHECK(st_audio_runtime_pull(audio, pcm, 5) == 0);
	if (!complete) {
		for (unsigned i = 0; i < 10; ++i)
			CHECK(pcm[i] == 0);
		return EXIT_SUCCESS;
	}
	CHECK(pcm[0] > 0 && pcm[2] > pcm[0] && pcm[4] < 0 && pcm[6] < pcm[4] && pcm[8] == 0);
	for (unsigned i = 0; i < 5; ++i)
		CHECK(pcm[i * 2] == pcm[i * 2 + 1]);
	return EXIT_SUCCESS;
}

static int
check_sample_frame(st_audio_runtime_t *audio, unsigned sample)
{
	int16_t pcm[2];
	CHECK(st_audio_runtime_pull(audio, pcm, 1) == 0 && pcm[0] == pcm[1]);
	/* Each fixture member contains its one-byte sample number. Soft clipping
	 * reduces these amplitudes by less than half a sample level. */
	int amplitude = (int)sample * 256;
	CHECK(sample ? pcm[0] > amplitude - 128 && pcm[0] <= amplitude : pcm[0] == 0);
	return EXIT_SUCCESS;
}

static int
check_sample_banks(st_term_session_t *session, st_audio_runtime_t *audio)
{
	/* An 8500 Hz byte spans six mixer frames, and a 22050 Hz byte spans two.
	 * The two-part profile uses the mixer rate. Stop the indexed profile
	 * after its first three members so the final step checks unloading. */
	static const unsigned expected[][7] = {
	    {8, 8, 8, 8, 8, 8, 0}, {3, 3, 0}, {17, 4, 0}, {1, 2, 3}, {0}
	};
	static const size_t lengths[] = {7, 3, 3, 3, 1};
	for (unsigned mode = 0; mode < 2; ++mode) {
		for (size_t i = 0; i < sizeof(lengths) / sizeof(*lengths); ++i) {
			CHECK(wait_for_marker(session, (int)i + 2) == EXIT_SUCCESS);
			for (size_t j = 0; j < lengths[i]; ++j)
				CHECK(check_sample_frame(audio, expected[i][j]) == EXIT_SUCCESS);
			CHECK(st_term_session_send_text(session, ".", 1) == 0);
		}
	}
	CHECK(wait_for_marker(session, 7) == EXIT_SUCCESS);
	CHECK(check_sample_frame(audio, 11) == EXIT_SUCCESS);
	CHECK(check_sample_frame(audio, 0) == EXIT_SUCCESS);
	return EXIT_SUCCESS;
}

static int
check_slide_sounds(st_term_session_t *session, st_audio_runtime_t *audio, bool first)
{
	const unsigned samples[] = {20, 19, 15, 0};
	int            column    = 2;
	for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) {
		if (!first && samples[i] == 15)
			continue;
		CHECK(wait_for_marker(session, column++) == 0);
		CHECK(check_sample_frame(audio, samples[i]) == EXIT_SUCCESS);
		if (samples[i])
			CHECK(st_term_session_send_text(session, ".", 1) == 0);
	}
	return EXIT_SUCCESS;
}

/* ORDERS.DAT is an S3M with two 64-row patterns. C-4 and C-5 play a
 * 64-sample square wave at 8363 and 16726 Hz. The sequence has two skip
 * markers between the patterns and a second subsong after the stop marker. */
static int
check_orders(st_term_session_t *session, st_audio_runtime_t *audio)
{
	const unsigned expected[] = {13, 26, 13, 13, 0, 26, 13, 13, 0};
	for (unsigned mode = 0; mode < 2; ++mode) {
		for (unsigned i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
			CHECK(wait_for_marker(session, (int)i + 2) == EXIT_SUCCESS);
			int16_t pcm[4410 * 2];
			CHECK(st_audio_runtime_pull(audio, pcm, 4410) == 0);
			unsigned cycles = 0;
			for (unsigned f = 1; f < 4410; ++f)
				if (pcm[(f - 1) * 2] <= 0 && pcm[f * 2] > 0)
					++cycles;
			if (!expected[i])
				CHECK(!memcmp(pcm, (int16_t[4410 * 2]){0}, sizeof(pcm)));
			else if (cycles + 1 < expected[i] || cycles > expected[i] + 1) {
				fprintf(stderr, "Mode %u step %u produced %u cycles, expected %u\n",
				    mode, i, cycles, expected[i]);
				return EXIT_FAILURE;
			}
			CHECK(st_term_session_send_text(session, ".", 1) == 0);
		}
	}
	return EXIT_SUCCESS;
}

int
main(int argc, char **argv)
{
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {.columns = 80, .rows = 25, .cell_width = 8, .cell_height = 16};
	st_console_t                  *console  = NULL;
	st_font_catalog_t             *fonts    = NULL;
	st_connection_t               *connection = NULL;
	st_term_session_t             *session    = NULL;
	st_audio_runtime_t            *audio      = NULL;
	CHECK(argc >= 2 && argc <= 4);
	CHECK(st_console_create(&console, &display, &geometry) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	CHECK(st_audio_runtime_create_pull(&audio) == 0);
	struct st_profile profile;
	st_profile_init(&profile);
	profile.connection_type = ST_CONNECTION_SHELL;
	bool theme = argc == 3 && (!strcmp(argv[2], "select-theme") || !strcmp(argv[2], "reuse-theme"));
	bool first = theme && !strcmp(argv[2], "select-theme");
	bool orders = argc == 3 && !strcmp(argv[2], "orders");
	bool samples = argc == 3 && !strcmp(argv[2], "samples");
	if (samples)
		strcpy(profile.address, "sh term-fixture.sh ddofx-samples");
	else if (orders)
		strcpy(profile.address, "sh term-fixture.sh ddofx-orders");
	else if (theme)
		strcpy(profile.address,
		    first ? "sh term-fixture.sh ddofx-theme select" : "sh term-fixture.sh ddofx-theme reuse");
	else
		strcpy(profile.address, "sh term-fixture.sh ddofx-audio");
	CHECK(st_term_connect_profile(&connection, &profile, console, NULL, NULL) == 0);
	const struct st_term_config config = {
	    .console         = console,
	    .fonts           = fonts,
	    .connection      = connection,
	    .audio           = audio,
	    .graphics_mode   = ST_GRAPHICS_DDOFX_20,
	    .codepage        = ST_CODEPAGE_CP437,
	    .hide_status     = true,
	    .ddofx_directory = argv[1],
	};
	CHECK(st_term_session_create(&session, &config) == 0);
	if (samples) {
		CHECK(check_sample_banks(session, audio) == EXIT_SUCCESS);
		goto finished;
	}
	if (orders) {
		CHECK(check_orders(session, audio) == EXIT_SUCCESS);
		goto finished;
	}
	if (theme) {
		CHECK(check_slide_sounds(session, audio, first) == EXIT_SUCCESS);
		goto finished;
	}
	if (argc >= 3) {
		CHECK(check_autoplay(session, audio, argv[2], argc == 3) == EXIT_SUCCESS);
		goto finished;
	}
	CHECK(wait_for_marker(session, 2) == 0);

	/* The peer sends its marker only after a successful query and the play
	 * request. RAMP.RLP contains the ten ASCII digits followed by LF. */
	int16_t pcm[24];
	CHECK(st_audio_runtime_pull(audio, pcm, 12) == 0);
	CHECK(pcm[0] > 0);
	for (unsigned i = 0; i < 11; ++i) {
		CHECK(pcm[i * 2] == pcm[i * 2 + 1]);
		if (i > 0 && i < 10)
			CHECK(pcm[i * 2] > pcm[i * 2 - 2]);
	}
	CHECK(pcm[20] > 0 && pcm[20] < pcm[0] && pcm[22] == 0 && pcm[23] == 0);

	CHECK(st_term_session_send_text(session, ".", 1) == 0);
	CHECK(wait_for_marker(session, 3) == 0);
	/* TONE.DAT has a CMF header and a single A4 with a 1x carrier. */
	int16_t tone[4410 * 2];
	CHECK(st_audio_runtime_pull(audio, tone, 4410) == 0);
	unsigned cycles = 0;
	for (unsigned i = 1; i < 4410; ++i) {
		CHECK(tone[2 * i] == tone[2 * i + 1]);
		if (tone[2 * i - 2] <= 0 && tone[2 * i] > 0)
			++cycles;
	}
	CHECK(cycles >= 43 && cycles <= 45);
	if (!ST_TEST_OPENMPT)
		goto finished;

	CHECK(st_term_session_send_text(session, ".", 1) == 0);
	/* Normal mode starts a fresh text page. */
	CHECK(wait_for_marker(session, 2) == 0);
	CHECK(st_audio_runtime_pull(audio, tone, 4410) == 0);
	uint64_t start, now;
	CHECK(st_time_monotonic_ms(&start) == 0);
	do {
		CHECK(st_term_session_poll_display(session) == 0);
		CHECK(st_time_monotonic_ms(&now) == 0 && now - start < 3000);
		CHECK(st_term_session_wait(session, 200) == 0);
	} while (!bars_match(console, true));
	CHECK(st_term_session_send_text(session, ".", 1) == 0);
	CHECK(wait_for_marker(session, 3) == 0);
	/* Redefining track 1 moves it to the silent track's base, growing
	 * downwards and cycling. Its previous footprint must be restored. */
	unsigned seen = 0;
	CHECK(st_time_monotonic_ms(&start) == 0);
	do {
		CHECK(st_term_session_poll_display(session) == 0);
		const struct st_console_rect area = {20, 60, 24, 2};
		struct st_pixel_buffer      *p    = NULL;
		CHECK(st_console_get_pixels(&p, console, &area, false) == 0);
		unsigned colour = p->primary[20];
		if (colour >= 32 && colour <= 34) {
			CHECK(p->primary[0] == 9 && p->alternate[0] == 9);
			CHECK(p->primary[44] == colour && p->alternate[46] == colour && p->primary[47] == 9);
			seen |= 1u << (colour - 32);
		}
		st_pixel_buffer_destroy(p);
		CHECK(st_time_monotonic_ms(&now) == 0 && now - start < 3000);
		CHECK(st_term_session_wait(session, 200) == 0);
	} while (seen != 7);
	CHECK(st_term_session_send_text(session, ".", 1) == 0);
	CHECK(wait_for_marker(session, 4) == 0);
	CHECK(bars_match(console, false));
	CHECK(st_term_session_send_text(session, ".", 1) == 0);
	CHECK(wait_for_marker(session, 5) == 0);
	CHECK(pixel_is(console, 150, 100, 80) && pixel_is(console, 40, 60, 9));
	CHECK(st_term_session_send_text(session, ".", 1) == 0);
	CHECK(wait_for_marker(session, 6) == 0);
	CHECK(pixel_is(console, 140, 90, 81) && pixel_is(console, 139, 90, 27));
	CHECK(st_term_session_send_text(session, ".", 1) == 0);
	CHECK(wait_for_marker(session, 7) == 0);
	CHECK(pixel_is(console, 140, 90, 18) && pixel_is(console, 139, 90, 27));

finished:
	st_term_session_destroy(&session);
	CHECK(st_connection_close(connection) == 0);
	st_profile_dispose(&profile);
	st_audio_runtime_destroy(audio);
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
	return EXIT_SUCCESS;
}
