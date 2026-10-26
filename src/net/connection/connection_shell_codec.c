/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "connection_shell_codec.h"
#include "st_time.h"

ssize_t
st_shell_codec_read(struct st_shell_codec *codec, void *data, size_t size,
	int64_t timeout_ms, st_shell_codec_read_fn read, void *context)
{
	uint8_t *result = data;
	size_t used = 0;
	struct st_deadline deadline;
	int status = st_deadline_start(&deadline, timeout_ms);
	if (status != 0)
		return status;
	while (used < size) {
		struct st_utf8_decode_event event;
		size_t consumed;
		if (codec->input_used < codec->input_size) {
			status = st_utf8_decoder_feed(&codec->decoder,
			    codec->input + codec->input_used,
			    codec->input_size - codec->input_used, &consumed, &event);
			if (status != 0)
				return used != 0 ? (ssize_t)used : status;
			codec->input_used += consumed;
		} else if (codec->eof) {
			(void)st_utf8_decoder_flush(&codec->decoder, &event);
			if (event.kind == ST_UTF8_DECODE_EVENT_NONE)
				break;
		} else {
			int64_t remaining;
			ssize_t count;
			if (used != 0)
				break;
			status = st_deadline_remaining_ms(&deadline, &remaining);
			if (status != 0)
				return status;
			count = read(context, codec->input, sizeof(codec->input), remaining);
			if (count < 0)
				return count;
			codec->eof = count == 0;
			codec->input_used = 0;
			codec->input_size = (size_t)count;
			continue;
		}
		if (event.kind == ST_UTF8_DECODE_EVENT_NONE)
			continue;
		uint8_t byte = '?';
		if (event.kind == ST_UTF8_DECODE_EVENT_SCALAR)
			(void)st_codepage_from_unicode(&byte, codec->codepage, event.data.scalar);
		result[used++] = byte;
	}
	return (ssize_t)used;
}

ssize_t
st_shell_codec_write(struct st_shell_codec *codec, const void *data, size_t size,
	int64_t timeout_ms, st_shell_codec_write_fn write, void *context)
{
	const uint8_t *source = data;
	size_t used = 0;
	struct st_deadline deadline;
	int status = st_deadline_start(&deadline, timeout_ms);
	if (status != 0)
		return status;
	while (used < size) {
		if (codec->output_used == codec->output_size) {
			codec->output_size = codec->output_used = 0;
			codec->source_size = codec->acknowledged = 0;
			while (codec->source_size < size - used
			    && codec->source_size < sizeof(codec->ends) / sizeof(codec->ends[0])) {
				uint32_t scalar = st_codepage_to_unicode(codec->codepage,
				    source[used + codec->source_size], false);
				ptrdiff_t count = st_utf8_encode_one(codec->output + codec->output_size,
				    sizeof(codec->output) - codec->output_size, scalar);
				if (count < 0)
					return used != 0 ? (ssize_t)used : (ssize_t)count;
				codec->output_size += (size_t)count;
				codec->ends[codec->source_size++] = codec->output_size;
			}
		}
		int64_t remaining;
		status = st_deadline_remaining_ms(&deadline, &remaining);
		if (status != 0)
			return used != 0 ? (ssize_t)used : status;
		ssize_t count = write(context, codec->output + codec->output_used,
		    codec->output_size - codec->output_used, remaining);
		if (count <= 0)
			return used != 0 ? (ssize_t)used : count == 0 ? IO_ERROR_NOT_CONNECTED : count;
		codec->output_used += (size_t)count;
		while (codec->acknowledged < codec->source_size
		    && codec->ends[codec->acknowledged] <= codec->output_used) {
			++codec->acknowledged;
			++used;
		}
	}
	return (ssize_t)used;
}
