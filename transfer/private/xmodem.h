/*
 * Copyright (c) 2003-2026 Rob Swindell
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* Synchronet X/YMODEM Functions */

#ifndef XMODEM_H_
#define XMODEM_H_


#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#define CPMEOF      CTRL_Z  /* CP/M End of file (^Z)					*/
#define XMODEM_MIN_BLOCK_SIZE   128
#define XMODEM_MAX_BLOCK_SIZE   1024

typedef struct {

	void* cbdata;
	long* mode;
	bool cancelled;
	bool crc_mode_supported;        /* for send */
	bool g_mode_supported;          /* for send */
	unsigned block_size;
	unsigned max_block_size;        /* for recv */
	unsigned ack_timeout;
	unsigned byte_timeout;
	unsigned send_timeout;
	unsigned recv_timeout;
	unsigned errors;
	unsigned max_errors;
	unsigned fallback_to_xmodem;    /* fallback to Xmodem after this many Ymodem send attempts */
	unsigned g_delay;
	unsigned long total_files;
	int64_t total_bytes;
	unsigned sent_files;
	int64_t sent_bytes;
	unsigned current_file_num;
	char current_file_name[1024];
	int *log_level;
	int (*lputs)(void*, int level, const char* str);
	void (*progress)(void*, unsigned block_num, int64_t offset, int64_t fsize, time_t t);
	int (*send_byte)(void*, uint8_t ch, unsigned timeout);
	int (*recv_byte)(void*, unsigned timeout);
	bool (*is_connected)(void*);
	bool (*is_cancelled)(void*);
	void (*flush)(void*);

} xmodem_t;


void        xmodem_init(xmodem_t*, void* cbdata, long* mode
                        , int (*lputs)(void*, int level, const char* str)
                        , void (*progress)(void* unused, unsigned block_num, int64_t offset, int64_t fsize, time_t t)
                        , int (*send_byte)(void*, uint8_t ch, unsigned timeout)
                        , int (*recv_byte)(void*, unsigned timeout)
                        , bool (*is_connected)(void*)
                        , bool (*is_cancelled)(void*)
                        , void (*flush)(void*)
                        );
int         xmodem_cancel(xmodem_t*);
int         xmodem_get_ack(xmodem_t*, unsigned tries, unsigned block_num);
bool        xmodem_get_mode(xmodem_t*);
bool        xmodem_put_eot(xmodem_t*);
int         xmodem_put_ack(xmodem_t*);
int         xmodem_put_nak(xmodem_t*, unsigned block_num);
int         xmodem_get_block(xmodem_t*, uint8_t* block, unsigned block_num);
int         xmodem_put_block(xmodem_t*, uint8_t* block, unsigned block_size, unsigned block_num);
bool        xmodem_send_file(xmodem_t* xm, const char* fname, FILE* fp, time_t* start, uint64_t* sent);

#endif  /* Don't add anything after this line */
