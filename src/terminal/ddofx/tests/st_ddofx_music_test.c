/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <math.h>
#include <stdio.h>
#include <string.h>

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
	CMD("tNONE");
	QUERY("dGETTHEME", "NONE");
#undef CMD
#undef QUERY
#undef LEVEL
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
	CHECK(command_reply(&ddo, &sink, "mMISSING", ""));
	CHECK(command_reply(&ddo, &sink, "dGETMUSIC", "A\xff"));
	CHECK(command_reply(&ddo, &sink, "MMPR", ""));
	CHECK(!has_signal(sink.audio));
	CHECK(command_reply(&ddo, &sink, "MMPAT,1", ""));
	CHECK(!has_signal(sink.audio));
	CHECK(command_reply(&ddo, &sink, "MMPR", ""));
	CHECK(has_signal(sink.audio));
	CHECK(command_reply(&ddo, &sink, "mB", ""));
	CHECK(command_reply(&ddo, &sink, "dGETMUSIC", "B\xff"));
	CHECK(has_signal(sink.audio));
	CHECK(command_reply(&ddo, &sink, "MSVOL,0", ""));
	CHECK(!has_signal(sink.audio));
	CHECK(command_reply(&ddo, &sink, "MMVOL,99", ""));
	CHECK(has_signal(sink.audio));
	CHECK(command_reply(&ddo, &sink, "mNONE", ""));
	CHECK(!has_signal(sink.audio));
	CHECK(command_reply(&ddo, &sink, "dGETMUSIC", "NONE\xff"));
	CHECK(check_samples(&ddo, &sink) == 0);
	CHECK(command_reply(&ddo, &sink, "MPREAL,LONG.RLP,8000", ""));
	CHECK(command_reply(&ddo, &sink, "mB", ""));
	st_audio_runtime_close_device(sink.audio);
	st_ddofx_reset(&ddo);
	st_audio_runtime_release_engine(sink.audio);
	st_audio_runtime_destroy(sink.audio);
	return 0;
}
