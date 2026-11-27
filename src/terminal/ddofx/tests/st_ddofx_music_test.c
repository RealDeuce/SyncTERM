/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
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
open_music(void *context, const char *name)
{
	(void)context;
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

int
main(void)
{
	struct st_ddofx      ddo      = {0};
	struct response      response = {0};
	struct st_ddofx_sink sink     = {.context = &response, .open_music = open_music, .reply = reply};
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
	CHECK(command_reply(&ddo, &sink, "mB", ""));
	st_audio_runtime_close_device(sink.audio);
	st_ddofx_reset(&ddo);
	st_audio_runtime_release_engine(sink.audio);
	st_audio_runtime_destroy(sink.audio);
	return 0;
}
