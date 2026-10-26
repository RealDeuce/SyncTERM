/* Strict, locale-independent UTF-8 encoding and decoding. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_utf8.h"

#include "io_error.h"

#include <string.h>

static size_t
utf8_expected(uint8_t byte)
{
	if (byte >= 0xc2 && byte <= 0xdf)
		return 2;
	if (byte >= 0xe0 && byte <= 0xef)
		return 3;
	if (byte >= 0xf0 && byte <= 0xf4)
		return 4;
	return 0;
}

static bool
continuation_valid(const struct st_utf8_decoder *decoder, uint8_t byte)
{
	if (byte < 0x80 || byte > 0xbf)
		return false;
	if (decoder->pending_size != 1)
		return true;
	if (decoder->pending[0] == 0xe0)
		return byte >= 0xa0;
	if (decoder->pending[0] == 0xed)
		return byte <= 0x9f;
	if (decoder->pending[0] == 0xf0)
		return byte >= 0x90;
	if (decoder->pending[0] == 0xf4)
		return byte <= 0x8f;
	return true;
}

static void
clear_decoder(struct st_utf8_decoder *decoder)
{
	decoder->pending_size = 0;
	decoder->expected_size = 0;
}

static void
set_malformed(struct st_utf8_decode_event *event, const uint8_t *bytes,
	size_t size)
{
	event->kind = ST_UTF8_DECODE_EVENT_MALFORMED;
	memcpy(event->data.malformed.bytes, bytes, size);
	event->data.malformed.size = size;
}

bool
st_unicode_scalar_valid(uint32_t value)
{
	return value <= UINT32_C(0x10ffff)
	    && !(value >= UINT32_C(0xd800) && value <= UINT32_C(0xdfff));
}

int
st_utf8_decode_one(uint32_t *result, size_t *consumed,
	const void *data, size_t size)
{
	const unsigned char *text = data;
	uint32_t value;
	size_t length;
	uint32_t minimum;

	if (result == NULL || consumed == NULL || (data == NULL && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	if (size == 0)
		return IO_ERROR_INVALID_ENCODING;
	if (text[0] < 0x80) {
		*result = text[0];
		*consumed = 1;
		return 0;
	}
	if (text[0] >= 0xc2 && text[0] <= 0xdf) {
		value = text[0] & 0x1f;
		minimum = 0x80;
		length = 2;
	}
	else if (text[0] >= 0xe0 && text[0] <= 0xef) {
		value = text[0] & 0x0f;
		minimum = 0x800;
		length = 3;
	}
	else if (text[0] >= 0xf0 && text[0] <= 0xf4) {
		value = text[0] & 0x07;
		minimum = 0x10000;
		length = 4;
	}
	else
		return IO_ERROR_INVALID_ENCODING;
	if (size < length)
		return IO_ERROR_INVALID_ENCODING;
	for (size_t i = 1; i < length; ++i) {
		if ((text[i] & 0xc0) != 0x80)
			return IO_ERROR_INVALID_ENCODING;
		value = (value << 6) | (text[i] & 0x3f);
	}
	if (value < minimum || !st_unicode_scalar_valid(value))
		return IO_ERROR_INVALID_ENCODING;
	*result = value;
	*consumed = length;
	return 0;
}

ptrdiff_t
st_utf8_encode_one(char *result, size_t result_size, uint32_t value)
{
	unsigned char encoded[4];
	size_t length;

	if ((result == NULL && result_size != 0) || !st_unicode_scalar_valid(value))
		return IO_ERROR_INVALID_ARGUMENT;
	if (value < 0x80) {
		encoded[0] = (unsigned char)value;
		length = 1;
	}
	else if (value < 0x800) {
		encoded[0] = (unsigned char)(0xc0 | value >> 6);
		encoded[1] = (unsigned char)(0x80 | (value & 0x3f));
		length = 2;
	}
	else if (value < 0x10000) {
		encoded[0] = (unsigned char)(0xe0 | value >> 12);
		encoded[1] = (unsigned char)(0x80 | ((value >> 6) & 0x3f));
		encoded[2] = (unsigned char)(0x80 | (value & 0x3f));
		length = 3;
	}
	else {
		encoded[0] = (unsigned char)(0xf0 | value >> 18);
		encoded[1] = (unsigned char)(0x80 | ((value >> 12) & 0x3f));
		encoded[2] = (unsigned char)(0x80 | ((value >> 6) & 0x3f));
		encoded[3] = (unsigned char)(0x80 | (value & 0x3f));
		length = 4;
	}
	if (result != NULL && result_size >= length)
		memcpy(result, encoded, length);
	return (ptrdiff_t)length;
}

void
st_utf8_decoder_init(struct st_utf8_decoder *decoder)
{
	if (decoder != NULL)
		memset(decoder, 0, sizeof(*decoder));
}

void
st_utf8_decoder_reset(struct st_utf8_decoder *decoder)
{
	st_utf8_decoder_init(decoder);
}

bool
st_utf8_decoder_incomplete(const struct st_utf8_decoder *decoder)
{
	return decoder != NULL && decoder->pending_size != 0;
}

int
st_utf8_decoder_feed(struct st_utf8_decoder *decoder, const void *data,
	size_t size, size_t *consumed, struct st_utf8_decode_event *event)
{
	const uint8_t *input = data;
	size_t offset = 0;

	if (decoder == NULL || consumed == NULL || event == NULL
	    || (data == NULL && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	event->kind = ST_UTF8_DECODE_EVENT_NONE;
	*consumed = 0;
	while (offset < size) {
		uint8_t byte = input[offset];

		if (decoder->pending_size == 0) {
			if (byte < 0x80) {
				event->kind = ST_UTF8_DECODE_EVENT_SCALAR;
				event->data.scalar = byte;
				*consumed = offset + 1;
				return 0;
			}
			decoder->expected_size = utf8_expected(byte);
			if (decoder->expected_size == 0) {
				set_malformed(event, &byte, 1);
				*consumed = offset + 1;
				return 0;
			}
			decoder->pending[0] = byte;
			decoder->pending_size = 1;
			++offset;
			continue;
		}
		if (!continuation_valid(decoder, byte)) {
			set_malformed(event, decoder->pending,
			    decoder->pending_size);
			clear_decoder(decoder);
			*consumed = offset;
			return 0;
		}
		decoder->pending[decoder->pending_size++] = byte;
		++offset;
		if (decoder->pending_size == decoder->expected_size) {
			uint32_t scalar;
			size_t decoded;
			int status = st_utf8_decode_one(&scalar, &decoded,
			    decoder->pending, decoder->pending_size);

			if (status != 0) {
				set_malformed(event, decoder->pending,
				    decoder->pending_size);
				clear_decoder(decoder);
				*consumed = offset;
				return 0;
			}
			clear_decoder(decoder);
			event->kind = ST_UTF8_DECODE_EVENT_SCALAR;
			event->data.scalar = scalar;
			*consumed = offset;
			return 0;
		}
	}
	*consumed = offset;
	return 0;
}

int
st_utf8_decoder_flush(struct st_utf8_decoder *decoder,
	struct st_utf8_decode_event *event)
{
	if (decoder == NULL || event == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (decoder->pending_size == 0) {
		event->kind = ST_UTF8_DECODE_EVENT_NONE;
		return 0;
	}
	set_malformed(event, decoder->pending, decoder->pending_size);
	clear_decoder(decoder);
	return 0;
}
