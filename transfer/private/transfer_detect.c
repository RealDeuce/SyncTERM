/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_transfer_detect.h"
#include "transfer_protocol.h"
static int hex_value(uint8_t byte)
{
	if (byte >= '0' && byte <= '9') return byte - '0';
	if (byte >= 'a' && byte <= 'f') return byte - 'a' + 10;
	if (byte >= 'A' && byte <= 'F') return byte - 'A' + 10;
	return -1;
}
int st_transfer_detect(struct st_transfer_detector *d, uint8_t byte,
	uint8_t *output, size_t *size)
{
	*size = 0;
	if (d->size == 0) {
		if (byte == '*') d->bytes[d->size++] = byte;
		else { output[0] = byte; *size = 1; }
		return 0;
	}
	if (d->size == 1 && byte == '*') { output[0] = '*'; *size = 1; return 0; }
	bool match = d->size == 1 ? byte == 24 : d->size == 2 ? byte == 'B' : hex_value(byte) >= 0;
	if (match) {
		d->bytes[d->size++] = byte;
		if (d->size < 17) return 0;
		uint8_t header[7];
		for (unsigned i = 0; i < 7; ++i) header[i] = (uint8_t)((hex_value(d->bytes[3 + i * 2]) << 4) | hex_value(d->bytes[4 + i * 2]));
		uint16_t crc = 0;
		for (unsigned i = 0; i < 5; ++i) crc = st_transfer_crc16_byte(header[i], crc);
		if ((header[0] == 0 || header[0] == 1) && crc == ((unsigned)header[5] << 8 | header[6])) {
			memcpy(output, d->bytes, d->size); *size = d->size; d->size = 0;
			return header[0] == 0 ? 1 : 2;
		}
		/* A complete but invalid candidate is literal terminal input. */
		memcpy(output, d->bytes, d->size); *size = d->size; d->size = 0;
		return 0;
	}
	memcpy(output, d->bytes, d->size); *size = d->size; d->size = 0;
	if (byte == '*') d->bytes[d->size++] = byte;
	else output[(*size)++] = byte;
	return 0;
}
