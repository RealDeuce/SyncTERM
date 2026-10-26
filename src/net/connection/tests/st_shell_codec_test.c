/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "connection_shell_codec.h"
#include <stdio.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); return 1; } } while (0)

struct stream {
	uint8_t bytes[8192];
	size_t size, used, chunk;
	bool timeout, alternate;
	int error;
};

static ssize_t
read_bytes(void *context, void *data, size_t size, int64_t timeout)
{
	struct stream *stream = context;
	(void)timeout;
	if (stream->error != 0)
		return stream->error;
	if (stream->timeout) {
		stream->timeout = false;
		return IO_ERROR_TIMED_OUT;
	}
	if (size > stream->chunk)
		size = stream->chunk;
	if (size > stream->size - stream->used)
		size = stream->size - stream->used;
	memcpy(data, stream->bytes + stream->used, size);
	stream->used += size;
	if (stream->alternate)
		stream->timeout = true;
	return (ssize_t)size;
}

static ssize_t
write_bytes(void *context, const void *data, size_t size, int64_t timeout)
{
	struct stream *stream = context;
	(void)timeout;
	if (stream->error != 0)
		return stream->error;
	if (stream->timeout) {
		stream->timeout = false;
		return IO_ERROR_TIMED_OUT;
	}
	if (size > stream->chunk)
		size = stream->chunk;
	if (size > sizeof(stream->bytes) - stream->size)
		return IO_ERROR_IO;
	memcpy(stream->bytes + stream->size, data, size);
	stream->size += size;
	if (stream->alternate)
		stream->timeout = true;
	return (ssize_t)size;
}

int
main(void)
{
	static const uint8_t cp[] = {'\x1b', '[', 'm', 0x82, 0xc4, '\r', '\n', 0, '|', '\x03'};
	static const char utf8[] = "\x1b[m\xc3\xa9\xe2\x94\x80\r\n\0|\x03";
	for (size_t chunk = 1; chunk < sizeof(utf8); ++chunk) {
		struct st_shell_codec codec = {.codepage = ST_CODEPAGE_CP437};
		struct stream stream = {.chunk = chunk, .alternate = true};
		size_t used = 0;
		for (unsigned attempts = 0; used < sizeof(cp) && attempts < 100; ++attempts) {
			ssize_t count = st_shell_codec_write(&codec, cp + used, sizeof(cp) - used,
			    100, write_bytes, &stream);
			CHECK(count > 0 || count == IO_ERROR_TIMED_OUT);
			if (count > 0)
				used += (size_t)count;
		}
		CHECK(used == sizeof(cp));
		CHECK(stream.size == sizeof(utf8) - 1);
		CHECK(memcmp(stream.bytes, utf8, stream.size) == 0);
		memset(&codec, 0, sizeof(codec));
		codec.codepage = ST_CODEPAGE_CP437;
		stream.used = 0;
		stream.timeout = false;
		uint8_t result[sizeof(cp)];
		used = 0;
		for (unsigned attempts = 0; used < sizeof(result) && attempts < 100; ++attempts) {
			ssize_t count = st_shell_codec_read(&codec, result + used, 1, 100,
			    read_bytes, &stream);
			CHECK(count > 0 || count == IO_ERROR_TIMED_OUT);
			if (count > 0)
				used += (size_t)count;
		}
		CHECK(used == sizeof(cp) && memcmp(result, cp, sizeof(cp)) == 0);
	}
	/* Truncation becomes one replacement only at EOF, not at a timeout. */
	struct st_shell_codec codec = {.codepage = ST_CODEPAGE_CP437};
	struct stream stream = {.bytes = {0xe2, 0x94}, .size = 2, .chunk = 2, .alternate = true};
	uint8_t byte;
	CHECK(st_shell_codec_read(&codec, &byte, 1, 100, read_bytes, &stream) == IO_ERROR_TIMED_OUT);
	CHECK(st_shell_codec_read(&codec, &byte, 1, 100, read_bytes, &stream) == 1 && byte == '?');
	CHECK(st_shell_codec_read(&codec, &byte, 1, 100, read_bytes, &stream) == 0);
	{
		struct st_shell_codec batch = {.codepage = ST_CODEPAGE_CP437};
		struct stream output = {.chunk = 257};
		uint8_t input[2300], result[2300];
		memset(input, 0x82, sizeof(input));
		CHECK(st_shell_codec_write(&batch, input, sizeof(input), 100,
		    write_bytes, &output) == (ssize_t)sizeof(input));
		CHECK(output.size == sizeof(input) * 2);
		for (size_t i = 0; i < output.size; i += 2)
			CHECK(output.bytes[i] == 0xc3 && output.bytes[i + 1] == 0xa9);
		size_t used = 0;
		while (used < sizeof(result)) {
			ssize_t count = st_shell_codec_read(&batch, result + used,
			    sizeof(result) - used, 100, read_bytes, &output);
			CHECK(count > 0);
			used += (size_t)count;
		}
		CHECK(memcmp(input, result, sizeof(input)) == 0);
	}
	{
		struct st_shell_codec partial = {.codepage = ST_CODEPAGE_CP437};
		struct stream output = {.chunk = 1, .alternate = true};
		const uint8_t input[] = {0x82};
		CHECK(st_shell_codec_write(&partial, input, 1, 100, write_bytes,
		    &output) == IO_ERROR_TIMED_OUT);
		output.error = IO_ERROR_CANCELLED;
		CHECK(st_shell_codec_write(&partial, input, 1, 100, write_bytes,
		    &output) == IO_ERROR_CANCELLED);
		output.error = 0;
		CHECK(st_shell_codec_write(&partial, input, 1, 100, write_bytes, &output) == 1);
		CHECK(output.size == 2 && output.bytes[0] == 0xc3 && output.bytes[1] == 0xa9);
	}
	return 0;
}
