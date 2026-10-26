/* Private ConPTY UTF-8/entry-codepage boundary.
 * Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_CONNECTION_SHELL_CODEC_H_
#define ST_CONNECTION_SHELL_CODEC_H_

#include "st_connection.h"
#include "st_utf8.h"

struct st_shell_codec {
	enum st_codepage codepage;
	struct st_utf8_decoder decoder;
	uint8_t input[4096];
	size_t input_used, input_size;
	bool eof;
	char output[4096];
	size_t output_used, output_size;
	/* Encoded end offsets for each source byte; acknowledge only whole bytes. */
	size_t ends[1024];
	size_t acknowledged, source_size;
};

typedef ssize_t (*st_shell_codec_read_fn)(void *, void *, size_t, int64_t);
typedef ssize_t (*st_shell_codec_write_fn)(void *, const void *, size_t, int64_t);
/* One reader and one writer may operate independently. Zero-initialize state
 * and set codepage before use. After partial writes/timeouts retry the same
 * unacknowledged source bytes, as the connection output worker does. */
ssize_t st_shell_codec_read(struct st_shell_codec *codec, void *data,
	size_t size, int64_t timeout_ms, st_shell_codec_read_fn read, void *context);
ssize_t st_shell_codec_write(struct st_shell_codec *codec, const void *data,
	size_t size, int64_t timeout_ms, st_shell_codec_write_fn write, void *context);
#endif
