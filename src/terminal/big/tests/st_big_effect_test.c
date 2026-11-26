/* Hand-derived BIG14/66 event counts, boundary records and native PCM timing. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io_error.h"
#include "st_big_effect.h"
#define CHECK(x)                                                                \
	do {                                                                    \
		if (!(x)) {                                                     \
			fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); \
			exit(1);                                                \
		}                                                               \
	} while (0)

static const struct expectation {
	unsigned    events, frequencies, stops, delays, milliseconds;
	const char *prefix, *suffix;
} expected[] = {
    {50,    24,    2,    24,   120,  "Z D5 F1450 D5 F1400",                   "D5 F350 D5 F300 Z"                      },
    {50,    24,    2,    24,   120,  "Z D5 F250 D5 F300",                     "D5 F1350 D5 F1400 Z"                    },
    {382,   190,   2,    190,  900,  "Z F900 D0 F22 D0 F20 D0",               "F10 D9 F8 D9 F6 D9 Z"                   },
    {22,    10,    2,    10,   930,  "Z F660 D93 F990 D93",                   "F660 D93 F990 D93 Z"                    },
    {201,   50,    51,   100,  150,  "Z F3000 D2 Z D1",                       "F60 D2 Z D1"                            },
    {691,   276,   139,  276,  1104, "Z F750 D5 F160 D3 Z",                   "F1550 D5 F650 D3 Z"                     },
    {259,   86,    87,   86,   430,  "Z F6000 D5 Z F6000 D5 Z",               "F5750 D5 Z F5750 D5 Z"                  },
    {32702, 32700, 2,    0,    0,    "Z F5536 F2080",                         NULL                                     },
    {162,   80,    2,    80,   1440, "Z F1700 D18 F1675 D18 F1650 D18 F1675", "F1700 D18 F1675 D18 F1650 D18 F1675 D18"},
    {162,   80,    2,    80,   560,  "Z F1700 D7 F1675 D7 F1650 D7 F1675",    "F3075 D7 F3100 D7 Z"                    },
    {314,   156,   2,    156,  1092, "Z F4200 D7 F4175 D7 F4150 D7 F4175",    "F1300 D7 F1325 D7 Z"                    },
    {202,   100,   27,   75,   175,  "Z F2100 F1975 D2 Z D2 F1650 F1675",     "F300 F3775 D2 Z D2 F3450 F65411 D3"     },
    {488,   324,   2,    162,  324,  "Z F4100 D2 F4100 F4075 D2 F4125",       "F75 D2 F8125 Z"                         },
    {200,   66,    35,   99,   495,  "Z F7400 D7 Z D1 F1400 D7",              "F8424 D7 Z D1 F376 D7 Z"                },
    {322,   160,   2,    160,  800,  "Z F0 D5 F100 D5 F1 D5 F99",             "F79 D5 F21 D5 Z"                        },
    {10,    3,     3,    4,    175,  "Z F1440 D50 F770 D25 Z D75 F330",       "F770 D25 Z D75 F330 D25 Z"              },
    {1502,  500,   252,  750,  2750, "Z F40 D6 Z D2 F9660 D3",                "F40 D6 Z D2 F10656 D3 Z"                },
    {4002,  1000,  1002, 2000, 4000, "Z F1660 D1 Z D3 F1661 D1 Z",            "F2659 D1 Z D3 Z"                        },
    {258,   64,    66,   128,  192,  "Z F16660 D1 Z D2 F16438 D1 Z",          "F2674 D1 Z D2 Z"                        },
    {82,    30,    12,   40,   400,  "Z F1440 D1 F770 D1 Z D1 F330",          "F1440 D19 F770 D19 Z D19 F330 D19"      },
};

static void
pattern(const struct st_big_effect_event *events, size_t count, const char *text, bool suffix)
{
	if (text == NULL)
		return;
	size_t words = 1;
	for (const char *p = text; *p; ++p)
		if (*p == ' ')
			++words;
	CHECK(words <= count);
	size_t start = suffix ? count - words : 0;
	for (size_t i = 0; i < words; ++i) {
		char                              actual[24];
		const struct st_big_effect_event *e = events + start + i;
		if (e->operation == ST_BIG_EFFECT_STOP)
			strcpy(actual, "Z");
		else
			snprintf(actual, sizeof(actual), "%c%u",
			    e->operation == ST_BIG_EFFECT_FREQUENCY ? 'F' : 'D', (unsigned)e->value);
		const char *end    = strchr(text, ' ');
		size_t      length = end == NULL ? strlen(text) : (size_t)(end - text);
		CHECK(strlen(actual) == length && memcmp(actual, text, length) == 0);
		if (end != NULL)
			text = end + 1;
	}
}

static void
original_events(void)
{
	struct st_big_effect_event *events = malloc(32702 * sizeof(*events));
	CHECK(events != NULL);
	for (unsigned selector = 0; selector < 20; ++selector) {
		struct st_big_effect e;
		CHECK(st_big_effect_begin(&e, (uint16_t)selector, true) == 0);
		uint32_t rng         = 1;
		size_t   count       = 0;
		unsigned frequencies = 0, stops = 0, delays = 0, sum = 0;
		for (;;) {
			struct st_big_effect_event event;
			bool                       complete;
			CHECK(st_big_effect_next(&e, &rng, &event, &complete) == 0);
			if (complete)
				break;
			CHECK(count < 32702);
			events[count++] = event;
			if (event.operation == ST_BIG_EFFECT_FREQUENCY)
				++frequencies;
			else if (event.operation == ST_BIG_EFFECT_STOP)
				++stops;
			else {
				++delays;
				sum += event.value;
			}
		}
		const struct expectation *x = expected + selector;
		CHECK(count == x->events && frequencies == x->frequencies && stops == x->stops
		      && delays == x->delays && sum == x->milliseconds);
		pattern(events, count, x->prefix, false);
		/* Some last-row patterns precede the independently following final Z. */
		pattern(events, count - ((selector == 8 || selector == 11 || selector == 19) ? 1 : 0), x->suffix,
		    true);
		CHECK(events[0].operation == ST_BIG_EFFECT_STOP);
		CHECK(events[count - 1].operation == (selector == 4 ? ST_BIG_EFFECT_DELAY : ST_BIG_EFFECT_STOP));
		if (selector != 7)
			CHECK(rng == 1);
		else
			CHECK(rng
			      == UINT32_C(
				  0x11bbdc95)); /* Closed-form affine power/geometric sum, seed1,32700 updates. */
		if (selector == 5)
			pattern(events + 71, count - 71, "F910 D5 F10 D3 Z", false);
		if (selector == 11)
			pattern(events + 185, count - 185, "F375 F3700 D2 Z D2 F3375 F65486 D3", false);
	}
	free(events);
}

static void
selection_and_errors(void)
{
	for (unsigned word = 0; word < 65536; ++word) {
		unsigned selected = word < 20 ? word : 19;
		CHECK(st_big_effect_selector((uint16_t)word, true) == selected);
		CHECK(st_big_effect_selector((uint16_t)word, false) == (selected > 9 ? 9 : selected));
	}
	struct st_big_effect       e;
	struct st_big_effect_event out;
	bool                       done;
	CHECK(st_big_effect_begin(NULL, 0, true) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_effect_next(NULL, NULL, &out, &done) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_effect_begin(&e, 65535, true) == 0 && st_big_effect_next(&e, NULL, &out, &done) == 0 && !done
	      && out.operation == ST_BIG_EFFECT_STOP);
	CHECK(st_big_effect_next(&e, NULL, &out, &done) == 0 && done);
	CHECK(st_big_effect_begin(&e, 0, false) == 0 && st_big_effect_next(&e, NULL, &out, &done) == 0 && done);
	CHECK(st_big_effect_begin(&e, 7, true) == 0 && st_big_effect_next(&e, NULL, &out, &done) == 0 && !done);
	struct st_big_effect saved = e;
	CHECK(st_big_effect_next(&e, NULL, &out, &done) == IO_ERROR_INVALID_ARGUMENT
	      && memcmp(&e, &saved, sizeof(e)) == 0);
}

static void
playback(void)
{
	for (unsigned selector = 0; selector < 20; ++selector) {
		const struct expectation *x      = expected + selector;
		size_t                    frames = x->milliseconds * 441 / 10 + x->frequencies;
		int16_t                  *whole  = malloc((frames + 64) * 2 * sizeof(*whole)),
			*parts                   = malloc((frames + 64) * 2 * sizeof(*parts));
		CHECK(whole && parts);
		struct st_big_effect_player a, b;
		struct st_big_speaker       sa, sb;
		CHECK(st_big_speaker_init(&sa) == 0 && st_big_speaker_init(&sb) == 0);
		CHECK(st_big_effect_player_begin(&a, (uint16_t)selector, true, 1) == 0
		      && st_big_effect_player_begin(&b, (uint16_t)selector, true, 1) == 0);
		CHECK(st_big_effect_render(&a, &sa, whole, frames + 64) == 0 && !a.active && !sa.playing
		      && a.fraction == (x->milliseconds * 44100u) % 1000);
		for (size_t offset = 0; offset < frames + 64;) {
			size_t count = offset % 83 + 1;
			if (count > frames + 64 - offset)
				count = frames + 64 - offset;
			CHECK(st_big_effect_render(&b, &sb, parts + offset * 2, count) == 0);
			offset += count;
		}
		CHECK(memcmp(whole, parts, (frames + 64) * 2 * sizeof(*whole)) == 0
		      && memcmp(&sa, &sb, sizeof(sa)) == 0);
		CHECK(a.sequence.position == x->events && b.sequence.position == x->events
		      && a.random_state == b.random_state);
		CHECK(st_big_effect_player_begin(&b, (uint16_t)selector, true, 1) == 0
		      && st_big_speaker_init(&sb) == 0);
		CHECK(st_big_effect_skip(&b, &sb, frames - 1) == 0 && b.active);
		CHECK(st_big_effect_skip(&b, &sb, 1) == 0 && !b.active && !sb.playing
		      && b.random_state == a.random_state);
		CHECK(st_big_effect_skip(&b, &sb, UINT64_MAX) == 0);
		free(whole);
		free(parts);
	}
	struct st_big_effect_player p;
	struct st_big_speaker       s;
	CHECK(st_big_speaker_init(&s) == 0 && st_big_speaker_frequency(&s, 440));
	CHECK(st_big_effect_player_begin(&p, 0, false, 123) == 0 && st_big_effect_render(&p, &s, NULL, 0) == 0
	      && s.playing && s.frequency == 440 && p.random_state == 123);
	CHECK(st_big_effect_render(NULL, &s, NULL, 0) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_effect_render(&p, &s, NULL, 1) == IO_ERROR_INVALID_ARGUMENT);
}

int
main(void)
{
	original_events();
	selection_and_errors();
	playback();
	return 0;
}
