/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "st_term.h"
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
		CHECK(st_time_monotonic_ms(&now) == 0 && now - start < 5000);
	} while (state.x != column);
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
	CHECK(argc == 2);
	CHECK(st_console_create(&console, &display, &geometry) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	CHECK(st_audio_runtime_create_pull(&audio) == 0);
	struct st_profile profile;
	st_profile_init(&profile);
	profile.connection_type = ST_CONNECTION_SHELL;
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

	st_term_session_destroy(&session);
	CHECK(st_connection_close(connection) == 0);
	st_profile_dispose(&profile);
	st_audio_runtime_destroy(audio);
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
	return EXIT_SUCCESS;
}
