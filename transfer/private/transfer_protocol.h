/* Private declarations for the extracted protocol engines. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_TRANSFER_PROTOCOL_H
#define ST_TRANSFER_PROTOCOL_H
#include "filesystem.h"
#include "path.h"
#include "string_compat.h"
#include "st_threads.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include "zmodem.h"
#include "xmodem.h"
#define ETX 3
#define CTRL_Z 26
#define SEND (1L << 0)
#define RECV (1L << 1)
#define XMODEM (1L << 2)
#define YMODEM (1L << 3)
#define CRC (1L << 5)
#define GMODE (1L << 6)
#define NOINP (-1)
#define FAILURE (-2)
#define NOT_YMODEM (-3)
#define NOT_XMODEM (-4)
#define SUCCESS 0
#define LOG_ERR 3
#define LOG_WARNING 4
#define LOG_NOTICE 5
#define LOG_INFO 6
#define LOG_DEBUG 7
uint16_t st_transfer_crc16_byte(uint8_t byte, uint16_t crc);
uint32_t st_transfer_crc32_byte(uint8_t byte, uint32_t crc);
uint32_t st_transfer_crc32_update(uint32_t crc, const void *data, size_t size);
uint32_t st_transfer_crc32_four(const uint8_t *data, uint32_t crc);
uint32_t st_transfer_file_crc32(FILE *file, size_t size);
static inline void st_transfer_sleep(unsigned ms)
{
	const struct timespec pause = {.tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000};
	(void)st_thread_sleep(&pause, NULL);
}
#endif
