/* CRC-16/CCITT and reflected CRC-32/IEEE, with uncomplemented running states. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "transfer_protocol.h"
static st_once_flag_t crc_once = ST_ONCE_FLAG_INIT;
static uint16_t crc16_table[256];
static uint32_t crc32_table[256];
static void initialize_crc(void)
{
	for (unsigned i = 0; i < 256; ++i) {
		uint16_t a = (uint16_t)(i << 8);
		uint32_t b = i;
		for (unsigned bit = 0; bit < 8; ++bit) {
			a = (uint16_t)((a << 1) ^ ((a & 0x8000) ? 0x1021 : 0));
			b = (b >> 1) ^ ((b & 1) ? UINT32_C(0xedb88320) : 0);
		}
		crc16_table[i] = a;
		crc32_table[i] = b;
	}
}
uint16_t st_transfer_crc16_byte(uint8_t byte, uint16_t crc)
{
	st_call_once(&crc_once, initialize_crc);
	return (uint16_t)((crc << 8) ^ crc16_table[(crc >> 8) ^ byte]);
}
uint32_t st_transfer_crc32_byte(uint8_t byte, uint32_t crc)
{
	st_call_once(&crc_once, initialize_crc);
	return crc32_table[(crc ^ byte) & 255] ^ (crc >> 8);
}
uint32_t st_transfer_crc32_update(uint32_t crc, const void *data, size_t size)
{
	const uint8_t *bytes = data;
	st_call_once(&crc_once, initialize_crc);
	for (size_t i = 0; i < size; ++i) crc = crc32_table[(crc ^ bytes[i]) & 255] ^ (crc >> 8);
	return crc;
}
uint32_t st_transfer_crc32_four(const uint8_t *data, uint32_t crc)
{
	return st_transfer_crc32_update(crc, data, 4);
}
uint32_t st_transfer_file_crc32(FILE *file, size_t size)
{
	uint8_t bytes[16384];
	uint32_t crc = UINT32_MAX;
	int64_t previous = fs_stream_position(file);
	if (previous < 0 || fs_stream_seek(file, 0) != 0) return 0;
	bool unlimited = size == 0;
	while (unlimited || size != 0) {
		size_t count = sizeof(bytes);
		if (!unlimited && count > size) count = size;
		count = fread(bytes, 1, count, file);
		if (count == 0) break;
		crc = st_transfer_crc32_update(crc, bytes, count);
		if (!unlimited) size -= count;
	}
	(void)fs_stream_seek(file, previous);
	return ~crc;
}
