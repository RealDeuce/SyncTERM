/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "st_cterm.h"
#include "st_ddofx.h"

#define CHECK(e)                                                   \
	do {                                                       \
		if (!(e)) {                                        \
			fprintf(stderr, "%d: %s\n", __LINE__, #e); \
			return 1;                                  \
		}                                                  \
	} while (0)

static void
make_mod(uint8_t *bytes)
{
	/* One looping 64-byte sample and one four-channel pattern, C-3 on channel 0. */
	bytes[43] = bytes[49] = 32;
	bytes[45]             = 64;
	bytes[950]            = 1;
	memcpy(bytes + 1080, "M.K.", 4);
	memcpy(bytes + 1084, "\x01\xac\x10", 3);
	for (int i = 0; i < 64; ++i)
		bytes[2108 + i] = i < 32 ? 0x50 : 0xb0;
}

static void
make_s3m(uint8_t *bytes)
{
	bytes[28] = 0x1a;
	bytes[29] = 16;
	bytes[32] = bytes[34] = bytes[36] = 1;
	bytes[41]                         = 0x13;
	bytes[42]                         = 2;
	memcpy(bytes + 44, "SCRM", 4);
	bytes[48] = 64;
	bytes[49] = 6;
	bytes[50] = 125;
	bytes[51] = 64;
	memset(bytes + 65, 255, 31);
	bytes[97]           = 8; /* Instrument at paragraph 8, pattern at paragraph 16. */
	bytes[99]           = 16;
	uint8_t *instrument = bytes + 128;
	instrument[0]       = 1;
	instrument[14]      = 24;
	instrument[16] = instrument[24] = 64;
	instrument[28]                  = 64;
	instrument[31]                  = 1;
	instrument[32]                  = 0xab;
	instrument[33]                  = 0x20;
	memcpy(instrument + 76, "SCRS", 4);
	bytes[256] = 69;
	memcpy(bytes + 258, "\x20\x40\x01", 3); /* C-4 and instrument 1. */
	for (int i = 0; i < 64; ++i)
		bytes[384 + i] = i < 32 ? 208 : 48;
}

static FILE *
sample_asset(const char *name)
{
	static const struct {
		const char *name;
		uint8_t     sample;
	} entries[] = {
	    {"SAM2.SAM", 40 },
            {"SAM1.SAM", 32 },
            {"SAM8.SAM", 224},
            {"SEG1.SAM", 2  },
            {"SEG2.SAM", 254}
        };

	if (strcmp(name, "BANK.THE") && strcmp(name, "LONG.RLP"))
		return NULL;
	FILE *file = tmpfile();
	if (file == NULL)
		return NULL;
	if (!strcmp(name, "LONG.RLP")) {
		uint8_t block[2048];
		for (int i = 1; i <= 8; ++i) {
			memset(block, i, sizeof(block));
			if (fwrite(block, 1, sizeof(block), file) != sizeof(block))
				goto failed;
		}
	}
	else {
		uint8_t directory[792] = {0};
		for (unsigned i = 0; i < 33; ++i)
			directory[24 * i] = 0xff;
		for (unsigned i = 0; i < sizeof(entries) / sizeof(entries[0]); ++i) {
			uint8_t *record = directory + i * 24;
			memcpy(record, entries[i].name, strlen(entries[i].name));
			size_t offset = sizeof(directory) + i * 4;
			record[12]    = (uint8_t)offset;
			record[13]    = (uint8_t)(offset >> 8);
			record[16]    = 4;
		}
		if (fwrite(directory, 1, sizeof(directory), file) != sizeof(directory))
			goto failed;
		for (unsigned i = 0; i < sizeof(entries) / sizeof(entries[0]); ++i) {
			uint8_t data[4];
			memset(data, entries[i].sample, sizeof(data));
			if (fwrite(data, 1, sizeof(data), file) != sizeof(data))
				goto failed;
		}
	}
	rewind(file);
	return file;
failed:
	fclose(file);
	return NULL;
}

static FILE *
open_music(void *context, const char *category, const char *client, const char *name)
{
	(void)context;
	if (client == NULL && !strcmp(category, "BBSTHEME"))
		return sample_asset(name);
	if (client != NULL || strcmp(category, "MUSIC") != 0)
		return NULL;
	uint8_t data[2172] = {0};
	size_t  size;
	if (strcmp(name, "A") == 0) {
		make_mod(data);
		size = sizeof(data);
	}
	else if (strcmp(name, "B") == 0) {
		make_s3m(data);
		size = 448;
	}
	else if (!strcmp(name, "FM") || !strcmp(name, "DRUM") || !strcmp(name, "DAMAGED")) {
		bool drum = !strcmp(name, "DRUM");
		memcpy(data, "CTMF", 4);
		data[4]                    = !drum;
		data[5]                    = 1;
		data[6]                    = 40;
		data[8]                    = 56;
		data[10]                   = 48;
		data[12]                   = 100;
		data[20 + (drum ? 11 : 0)] = 1;
		data[36]                   = 1;
		const uint8_t patch[]      = {0x21, 0x21, 0x3f, 0, 0xf3, 0xf3, 0x05, 0x05};
		memcpy(data + 40, patch, sizeof(patch));
		const uint8_t melody[] = {0, 0xc0, 0, 10, 0x90, 69, 127, 5, 69, 0, 5, 0xff, 0x2f, 0};
		const uint8_t drums[]  = {0, 0xb0, 103, 1, 0, 0xcb, 0, 0, 0x9b, 48, 110, 4, 0x8b, 48, 0, 1, 0xfc};
		memcpy(data + 56, drum ? drums : melody, drum ? sizeof(drums) : sizeof(melody));
		size = 56 + (drum ? sizeof(drums) : sizeof(melody));
		if (!strcmp(name, "DAMAGED"))
			size -= 2;
	}
	else
		return NULL;
	FILE *file = tmpfile();
	if (file != NULL) {
		if (fwrite(data, 1, size, file) != size || fseek(file, 0, SEEK_SET) != 0) {
			fclose(file);
			return NULL;
		}
	}
	return file;
}

struct response {
	uint8_t bytes[256];
	size_t  size;
};

static void
reply(void *context, const uint8_t *bytes, size_t size)
{
	struct response *response = context;
	response->size            = size;
	if (size <= sizeof(response->bytes))
		memcpy(response->bytes, bytes, size);
}

static bool
command_reply(struct st_ddofx *ddo, struct st_ddofx_sink *sink, const char *command, const char *expected)
{
	char             frame[256];
	int              n = snprintf(frame, sizeof(frame), "\x1d{%s}", command);
	size_t           used;
	struct response *response = sink->context;
	response->size            = 0;
	if (n <= 0 || (size_t)n >= sizeof(frame)
	    || st_ddofx_feed(ddo, sink, (const uint8_t *)frame, (size_t)n, &used) != 0 || used != (size_t)n)
		return false;
	return response->size == strlen(expected) && memcmp(response->bytes, expected, response->size) == 0;
}

static bool
has_signal(st_audio_runtime_t *audio)
{
	int16_t pcm[2048];
	if (st_audio_runtime_pull(audio, pcm, 1024) != 0)
		return false;
	for (size_t i = 0; i < 2048; ++i)
		if (pcm[i] > 8 || pcm[i] < -8)
			return true;
	return false;
}

static bool
pull_level(st_audio_runtime_t *audio, size_t frames, int expected)
{
	int16_t pcm[4096];
	if (frames > 2048 || st_audio_runtime_pull(audio, pcm, frames) != 0)
		return false;
	/* Unity envelope is Q15 32767. The final mix soft-clips both channels. */
	double scaled = floor(expected * 256.0 * 32767.0 / 32768.0);
	int    level  = (int)(32767.0 * tanh(scaled / 32767.0));
	for (size_t i = 0; i < frames * 2; ++i)
		if (pcm[i] != level) {
			fprintf(stderr, "PCM index %zu: %d expected %d\n", i, (int)pcm[i], level);
			return false;
		}
	return true;
}

static int
check_samples(struct st_ddofx *ddo, struct st_ddofx_sink *sink)
{
#define CMD(body)             CHECK(command_reply(ddo, sink, body, ""))
#define QUERY(body, expected) CHECK(command_reply(ddo, sink, body, expected "\xff"))
#define LEVEL(frames, v)      CHECK(pull_level(sink->audio, frames, v))
	QUERY("dSOUNDON", "YES");
	QUERY("dGETTHEME", "NONE");
	CMD("tBANK.THE");
	CMD("tMISSING");
	QUERY("dGETTHEME", "BANK.THE");
	CMD("p1");
	LEVEL(21, 32); /* ceil(4 * 44100 / 8500) */
	LEVEL(2, 0);
	CMD("P2,22050");
	LEVEL(8, 40);
	LEVEL(1, 0);
	CMD("p8"); /* Whole-theme loading stopped at the missing SAM3.SAM. */
	LEVEL(1, 0);
	CMD("tb,BANK.THE");
	CMD("tb8");
	CMD("tb1");
	CMD("pb1");
	LEVEL(1, -32);
	CMD("PB2,44100");
	LEVEL(4, 32);
	LEVEL(1, 0);
	CMD("tbLOAD");
	CMD("PB2,44100");
	LEVEL(3, 40);
	CMD("tbNONE");
	LEVEL(2, 0);

	QUERY("6CHECKBS,150", "N/A");
	CMD("6DEFBSND,150,2");
	CMD("6ADDBSND,150,SAM8.SAM");
	CMD("6ADDBSND,150,MISSING");
	CMD("6PLAYBS,150,44100");
	QUERY("6CHECKBS,150", "N/A");
	LEVEL(1, 0);
	CMD("6ADDBSND,150,SAM2.SAM");
	QUERY("6CHECKBS,150", "0");
	CMD("6PLAYBS,150,44100");
	QUERY("6CHECKBS,150", "1");
	LEVEL(4, -32);
	LEVEL(4, 40);
	QUERY("6CHECKBS,150", "0");
	CMD("6LBSOUND,1,SEG");
	CMD("6PLAYBS,1,44100");
	LEVEL(4, 2);
	LEVEL(1, -2);
	CMD("P1,44100");
	QUERY("6CHECKBS,1", "0");
	LEVEL(4, 32);
	CMD("6PLAYBS,1,1");
	CMD("6UNLOADBS,1");
	LEVEL(4, 0);
	QUERY("6CHECKBS,1", "N/A");
	CMD("6UNLOADBS,0");
	QUERY("6CHECKBS,150", "N/A");

	CMD("MPREAL,LONG.RLP,44100");
	for (int i = 1; i <= 8; ++i) {
		LEVEL(2048, i);
		CHECK(st_ddofx_poll(ddo, sink) == 0);
	}
	LEVEL(1, 0);
	CMD("MPREAL,LONG.RLP,44100");
	LEVEL(5, 1);
	CMD("P1,44100");
	LEVEL(4, 32);
	CHECK(st_ddofx_poll(ddo, sink) == 0);
	LEVEL(1, 0);
	CMD("tBANK.THE");
	CMD("6LBSOUND,1,SEG");
	QUERY("6CHECKBS,1", "0");
	CMD("mB");
	CMD("MSVOL,0");
	CMD("2RESET");
	QUERY("dGETMUSIC", "B");
	QUERY("dGETTHEME", "BANK.THE");
	QUERY("6CHECKBS,1", "N/A");
	CMD("P1,44100");
	LEVEL(4, 32);
	CMD("mNONE");
	CMD("tNONE");
	QUERY("dGETTHEME", "NONE");
#undef CMD
#undef QUERY
#undef LEVEL
	return 0;
}

static int
check_animation_sound(struct st_ddofx *ddo, struct st_ddofx_sink *sink)
{
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {.columns = 80, .rows = 25, .cell_width = 8, .cell_height = 16};
	struct st_cterm_config         text     = {.codepage = ST_CODEPAGE_CP437, .quiet = true};
	CHECK(st_console_create(&text.console, &display, &geometry) == 0);
	sink->text_config   = &text;
	const char *scene[] = {"LIGMODE", "LIGMODE", "tBANK.THE", "2S,0,1,0,1,A",
	    "2MOTION,1,10,10,10,10,10,10,10,10,1", "2MFRAME,1,1,1,1,2,22050", "2ANIM,1,10,10,4,A,A,1,1,0,1,1,1",
	    "2ANIMSOUND,1,19,1,44100,1", "2PLAYA,1"};
	for (size_t i = 0; i < sizeof(scene) / sizeof(*scene); ++i)
		CHECK(command_reply(ddo, sink, scene[i], ""));
	CHECK(ddo->graphics && st_ddofx_poll(ddo, sink) == 0);
	/* Each voice has its own Q15 unity envelope before soft clipping. */
	int16_t both[8];
	CHECK(st_audio_runtime_pull(sink->audio, both, 4) == 0);
	int mixed = (int)(32767.0 * tanh((8191.0 + 10239.0) / 32767.0));
	for (unsigned i = 0; i < 8; ++i)
		CHECK(both[i] == mixed);
	CHECK(pull_level(sink->audio, 4, 40));
	CHECK(pull_level(sink->audio, 1, 0));
	CHECK(command_reply(ddo, sink, "2GEXIT", ""));
	sink->text_config = NULL;
	st_console_destroy(text.console);
	return 0;
}

static int
check_cmf(struct st_ddofx *ddo, struct st_ddofx_sink *sink)
{
	CHECK(command_reply(ddo, sink, "MICMF", "N\xff"));
	CHECK(command_reply(ddo, sink, "mFM", ""));
	CHECK(command_reply(ddo, sink, "MICMF", "Y\xff"));
	CHECK(command_reply(ddo, sink, "dGETMUSIC", "FM\xff"));
	CHECK(command_reply(ddo, sink, "MSVOL,0", ""));
	CHECK(command_reply(ddo, sink, "MMPR", ""));
	/* Ten ticks at 100 Hz before the note, then a 20-tick loop. */
	int16_t pcm[8820 * 2], repeat[8820 * 2];
	CHECK(st_audio_runtime_pull(sink->audio, pcm, 8820) == 0);
	bool sounded = false;
	for (size_t i = 0; i < 8820; ++i) {
		CHECK(pcm[2 * i] == pcm[2 * i + 1]);
		if (i < 4410)
			CHECK(pcm[2 * i] == 0);
		else if (pcm[2 * i] > 100 || pcm[2 * i] < -100)
			sounded = true;
	}
	CHECK(sounded);
	CHECK(command_reply(ddo, sink, "mDAMAGED", ""));
	CHECK(command_reply(ddo, sink, "dGETMUSIC", "FM\xff"));
	CHECK(st_audio_runtime_pull(sink->audio, repeat, 8820) == 0 && !memcmp(pcm, repeat, sizeof(pcm)));
	CHECK(command_reply(ddo, sink, "mDRUM", ""));
	CHECK(command_reply(ddo, sink, "dGETMUSIC", "DRUM\xff"));
	CHECK(has_signal(sink->audio));
	CHECK(command_reply(ddo, sink, "mA", ""));
	CHECK(command_reply(ddo, sink, "MICMF", "N\xff"));
	CHECK(command_reply(ddo, sink, "mNONE", ""));
	CHECK(!has_signal(sink->audio));
	return 0;
}

int
main(void)
{
	struct st_ddofx      ddo      = {0};
	struct response      response = {0};
	struct st_ddofx_sink sink     = {.context = &response, .open_asset = open_music, .reply = reply};
	CHECK(st_audio_runtime_create_pull(&sink.audio) == 0);
	st_ddofx_reset(&ddo);
	CHECK(command_reply(&ddo, &sink, "mA", ""));
	CHECK(command_reply(&ddo, &sink, "dGETMUSIC", "A\xff"));
	CHECK(has_signal(sink.audio));
	uint8_t levels[ST_TRACKER_METER_CHANNELS];
	CHECK(st_tracker_levels(ddo.player, levels) == 4 && levels[0] > 0 && !levels[1] && !levels[2] && !levels[3]);
	CHECK(command_reply(&ddo, &sink, "mMISSING", ""));
	CHECK(command_reply(&ddo, &sink, "dGETMUSIC", "A\xff"));
	CHECK(command_reply(&ddo, &sink, "MMPR", ""));
	CHECK(!has_signal(sink.audio));
	CHECK(st_tracker_levels(ddo.player, levels) == 4 && levels[0] == 0);
	CHECK(command_reply(&ddo, &sink, "MMPAT,1", ""));
	CHECK(!has_signal(sink.audio));
	CHECK(command_reply(&ddo, &sink, "MMPR", ""));
	CHECK(has_signal(sink.audio));
	CHECK(command_reply(&ddo, &sink, "mB", ""));
	CHECK(command_reply(&ddo, &sink, "dGETMUSIC", "B\xff"));
	CHECK(has_signal(sink.audio));
	CHECK(st_tracker_levels(ddo.player, levels) >= 1 && levels[0] > 0);
	CHECK(command_reply(&ddo, &sink, "MSVOL,0", ""));
	CHECK(!has_signal(sink.audio));
	CHECK(command_reply(&ddo, &sink, "MMVOL,99", ""));
	CHECK(has_signal(sink.audio));
	CHECK(command_reply(&ddo, &sink, "mNONE", ""));
	CHECK(!has_signal(sink.audio));
	CHECK(command_reply(&ddo, &sink, "dGETMUSIC", "NONE\xff"));
	CHECK(check_cmf(&ddo, &sink) == 0);
	CHECK(check_samples(&ddo, &sink) == 0);
	CHECK(check_animation_sound(&ddo, &sink) == 0);
	CHECK(command_reply(&ddo, &sink, "MPREAL,LONG.RLP,8000", ""));
	CHECK(command_reply(&ddo, &sink, "mB", ""));
	st_audio_runtime_close_device(sink.audio);
	st_ddofx_reset(&ddo);
	st_audio_runtime_release_engine(sink.audio);
	st_audio_runtime_destroy(sink.audio);
	return 0;
}
