#include "transfer_protocol.h"
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

/* Standard headers */
#include <stdio.h>
#include <stdarg.h>     /* va_list */

#define getcom(t)   xm->recv_byte(xm->cbdata, t)
#define putcom(ch)  xm->send_byte(xm->cbdata, ch, xm->send_timeout)

static int lprintf(xmodem_t* xm, int level, const char *fmt, ...)
{
	va_list argptr;
	char    sbuf[1024];

	if (xm->lputs == NULL)
		return -1;
	if (xm->log_level != NULL)
		if (level > *xm->log_level)
			return 0;

	va_start(argptr, fmt);
	vsnprintf(sbuf, sizeof(sbuf), fmt, argptr);
	sbuf[sizeof(sbuf) - 1] = 0;
	va_end(argptr);
	return xm->lputs(xm->cbdata, level, sbuf);
}

static bool is_connected(xmodem_t* xm)
{
	if (xm->is_connected != NULL)
		return xm->is_connected(xm->cbdata);
	return true;
}

static bool is_cancelled(xmodem_t* xm)
{
	if (xm->is_cancelled != NULL)
		return xm->cancelled = xm->is_cancelled(xm->cbdata);
	return xm->cancelled;
}

static void xmodem_flush(xmodem_t* xm)
{
	if (xm->flush != NULL)
		xm->flush(xm->cbdata);
}

static char *chr(uint8_t ch)
{
	static _Thread_local char str[25];

	switch (ch) {
		case SOH:   return "SOH";
		case STX:   return "STX";
		case ETX:   return "ETX";
		case EOT:   return "EOT";
		case ACK:   return "ACK";
		case NAK:   return "NAK";
		case CAN:   return "CAN";
	}
	if (ch >= ' ' && ch <= '~')
		sprintf(str, "'%c' (%02Xh)", ch, ch);
	else
		sprintf(str, "%u (%02Xh)", ch, ch);
	return str;
}


int xmodem_put_ack(xmodem_t* xm)
{
	int result;

	result = putcom(ACK);

	xmodem_flush(xm);

	return result;
}

int xmodem_put_nak(xmodem_t* xm, unsigned block_num)
{
	int i, dump_count = 0;
	int result;

	/* wait for any trailing data */
	while ((i = getcom(0)) != NOINP && is_connected(xm)) {
		dump_count++;
		lprintf(xm, LOG_DEBUG, "Block %u: Dumping byte: %02Xh"
		        , block_num, (uint8_t)i);
		st_transfer_sleep(1);
		if (is_cancelled(xm))
			break;
	}
	if (dump_count)
		lprintf(xm, LOG_INFO, "Block %u: Dumped %u bytes"
		        , block_num, dump_count);

	if (block_num <= 1) {
		if (*(xm->mode) & GMODE) {     /* G for X/Ymodem-G */
			lprintf(xm, LOG_INFO, "Block %u: Requesting mode: Streaming, 16-bit CRC", block_num);
			result = putcom('G');
		} else if (*(xm->mode) & CRC) {    /* C for CRC */
			lprintf(xm, LOG_INFO, "Block %u: Requesting mode: 16-bit CRC", block_num);
			result = putcom('C');
		} else {                /* NAK for checksum */
			lprintf(xm, LOG_INFO, "Block %u: Requesting mode: 8-bit Checksum", block_num);
			result = putcom(NAK);
		}
	} else
		result = putcom(NAK);

	xmodem_flush(xm);

	return result;
}

int xmodem_cancel(xmodem_t* xm)
{
	int i;
	int result;

	if (!is_cancelled(xm) && is_connected(xm)) {
		xm->cancelled = true;
		for (i = 0; i < 8 && is_connected(xm); i++)
			if ((result = putcom(CAN)) != 0)
				return result;
		for (i = 0; i < 10 && is_connected(xm); i++)
			if ((result = putcom('\b')) != 0)
				return result;
	}

	xmodem_flush(xm);

	return SUCCESS;
}

/****************************************************************************/
/* Return 0 on success														*/
/****************************************************************************/
int xmodem_get_block(xmodem_t* xm, uint8_t* block, unsigned expected_block_num)
{
	uint8_t    block_num;                 /* Block number received in header	*/
	uint8_t    block_inv;
	uint8_t    chksum = 0, calc_chksum;
	int      i, eot = 0, can = 0;
	unsigned     b, errors;
	uint16_t crc = 0, calc_crc;

	lprintf(xm, LOG_DEBUG, "Requesting data block %u", expected_block_num);
	for (errors = 0; errors <= xm->max_errors && is_connected(xm) && !is_cancelled(xm); errors++) {

		i = getcom(expected_block_num <= 1 ? 3 : 10);
		if (eot && i != EOT && i != NOINP)
			eot = 0;
		if (can && i != CAN)
			can = 0;
		switch (i) {
			case SOH: /* 128-byte blocks */
				xm->block_size = XMODEM_MIN_BLOCK_SIZE;
				break;
			case STX: /* 1024-byte blocks */
				if (xm->max_block_size < XMODEM_MAX_BLOCK_SIZE) {
					lprintf(xm, LOG_WARNING, "Block %u: 1024-byte blocks not supported"
					        , expected_block_num);
					return FAILURE;
				}
				xm->block_size = XMODEM_MAX_BLOCK_SIZE;
				break;
			case EOT:
				lprintf(xm, LOG_DEBUG, "Block %u: EOT received", expected_block_num);
				if (!((*xm->mode) & GMODE) && !eot) {
					lprintf(xm, LOG_INFO, "NAKing first EOT");
					eot = 1;
					xmodem_put_nak(xm, expected_block_num); /* chuck's double EOT trick */
					continue;
				}
				return EOT;
			case CAN:
				if (!can) {          /* must get two CANs in a row */
					can = 1;
					lprintf(xm, LOG_WARNING, "Block %u: Received CAN  Expected SOH, STX, or EOT"
					        , expected_block_num);
					continue;
				}
				lprintf(xm, LOG_WARNING, "Block %u: Canceled remotely", expected_block_num);
				return CAN;
			default:
				lprintf(xm, LOG_WARNING, "Block %u: Received %s  Expected SOH, STX, or EOT"
				        , expected_block_num, chr((uint8_t)i));
			/* Fall-through */
			case NOINP:     /* Nothing came in */
				if (eot)
					return EOT;
				return NOINP;
		}
		if ((i = getcom(xm->byte_timeout)) == NOINP)
			break;
		block_num = (uint8_t)i;
		if ((i = getcom(xm->byte_timeout)) == NOINP)
			break;
		block_inv = (uint8_t)i;
		calc_crc = calc_chksum = 0;
		for (b = 0; b < xm->block_size && is_connected(xm); b++) {
			if ((i = getcom(xm->byte_timeout)) == NOINP)
				break;
			block[b] = (uint8_t)i;
			if ((*xm->mode) & CRC)
				calc_crc = st_transfer_crc16_byte(block[b], calc_crc);
			else
				calc_chksum += block[b];
		}

		if (b < xm->block_size)
			break;

		if ((*xm->mode) & CRC) {
			if ((i = getcom(xm->byte_timeout)) == NOINP)
				break;
			crc = (uint16_t)((unsigned)i << 8);
			if ((i = getcom(xm->byte_timeout)) == NOINP)
				break;
			crc |= (uint16_t)i;
		}
		else {
			if ((i = getcom(xm->byte_timeout)) == NOINP)
				break;
			chksum = (uint8_t)i;
		}

		if (block_num != (uint8_t) ~block_inv) {
			lprintf(xm, LOG_WARNING, "Block %u: Block number bit error (0x%02X vs 0x%02x)"
			        , expected_block_num, block_num, (uint8_t) ~block_inv);
			break;
		}

		if ((*xm->mode) & CRC) {
			if (crc != calc_crc) {
				lprintf(xm, LOG_WARNING, "Block %u: CRC ERROR", block_num);
				break;
			}
		}
		else    /* CHKSUM */
		{
			if (chksum != calc_chksum) {
				lprintf(xm, LOG_WARNING, "Block %u: CHECKSUM ERROR", block_num);
				break;
			}
		}

		if (block_num != (uint8_t)(expected_block_num & 0xff)) {
			lprintf(xm, LOG_WARNING, "Block number error (%u received, expected %u)"
			        , block_num, expected_block_num & 0xff);
			if ((*xm->mode) & XMODEM && expected_block_num == 1 && block_num == 0)
				return NOT_XMODEM;
			if (expected_block_num == 0 && block_num == 1)
				return NOT_YMODEM;
			if (expected_block_num && block_num == (uint8_t)((expected_block_num - 1) & 0xff)) {
				/* A lost ACK causes a retransmission. Acknowledge it again
				 * without writing the payload twice or purging queued input. */
				if (!((*xm->mode) & GMODE))
					xmodem_put_ack(xm);
				continue;
			}
			break;
		}

		return SUCCESS; /* Success */
	}

	return FAILURE;     /* Failure */
}

/*****************/
/* Sends a block */
/*****************/
int xmodem_put_block(xmodem_t* xm, uint8_t* block, unsigned block_size, unsigned block_num)
{
	int      result;
	uint8_t    ch, chksum;
	unsigned     i;
	uint16_t crc;

	if (block_size == XMODEM_MIN_BLOCK_SIZE)
		result = putcom(SOH);
	else            /* 1024 */
		result = putcom(STX);
	if (result != 0)
		return result;
	ch = (uint8_t)(block_num & 0xff);
	if ((result = putcom(ch)) != 0)
		return result;
	if ((result = putcom((uint8_t) ~ch)) != 0)
		return result;
	chksum = 0;
	crc = 0;
	for (i = 0; i < block_size && is_connected(xm); i++) {
		if ((result = putcom(block[i])) != 0)
			return result;
		if ((*xm->mode) & CRC)
			crc = st_transfer_crc16_byte(block[i], crc);
		else
			chksum += block[i];
	}

	if ((*xm->mode) & CRC) {
		if ((result = putcom((uint8_t)(crc >> 8))) != 0)
			return result;
		result = putcom((uint8_t)(crc & 0xff));
	} else
		result = putcom(chksum);

	xmodem_flush(xm);

	return result;
}

/************************************************************/
/* Gets an acknowledgement - usually after sending a block	*/
/* Returns ACK if ack received								*/
/************************************************************/
int xmodem_get_ack(xmodem_t* xm, unsigned tries, unsigned block_num)
{
	int      i = NOINP, can = 0;
	unsigned errors;

	for (errors = 0; errors < tries && is_connected(xm) && !is_cancelled(xm);) {

		if ((*xm->mode) & GMODE) {     /* Don't wait for ACK on X/Ymodem-G */
			st_transfer_sleep(xm->g_delay);
			if (getcom(0) == CAN) {
				lprintf(xm, LOG_WARNING, "Block %u: !Canceled remotely", block_num);
				xmodem_cancel(xm);
				return CAN;
			}
			return ACK;
		}

		i = getcom(xm->ack_timeout);
		if (can && i != CAN)
			can = 0;
		if (i == ACK)
			break;
		if (i == CAN) {
			if (can) {   /* 2 CANs in a row */
				lprintf(xm, LOG_WARNING, "Block %u: !Canceled remotely", block_num);
				xmodem_cancel(xm);
				return CAN;
			}
			can = 1;
		}
		if (i != NOINP) {
			lprintf(xm, LOG_WARNING, "Block %u: !Received %s  Expected ACK"
			        , block_num, chr((uint8_t)i));
			if (i != CAN)
				return i;
		}
		if (i != CAN)
			errors++;
	}

	return i;
}

bool xmodem_get_mode(xmodem_t* xm)
{
	int      i;
	unsigned errors;
	unsigned can;

	lprintf(xm, LOG_INFO, "Waiting for transfer mode request...");

	*(xm->mode) &= ~(GMODE | CRC);
	for (errors = can = 0; errors <= xm->max_errors && is_connected(xm) && !is_cancelled(xm); errors++) {
		i = getcom(xm->recv_timeout);
		if (can && i != CAN)
			can = 0;
		switch (i) {
			case NAK:       /* checksum */
				lprintf(xm, LOG_INFO, "Receiver requested mode: 8-bit Checksum");
				return true;
			case 'C':
				lprintf(xm, LOG_INFO, "Receiver requested mode: 16-bit CRC");
				if (!xm->crc_mode_supported)
					continue;
				*(xm->mode) |= CRC;
				return true;
			case 'G':
				lprintf(xm, LOG_INFO, "Receiver requested mode: Streaming, 16-bit CRC");
				if (!xm->crc_mode_supported || !xm->g_mode_supported)
					continue;
				*(xm->mode) |= (GMODE | CRC);
				return true;
			case CAN:
				if (can) {
					lprintf(xm, LOG_WARNING, "Canceled remotely");
					return false;
				}
				can = 1;
				break;
			case NOINP:
				break;
			default:
				lprintf(xm, LOG_WARNING, "Received %s  Expected NAK, C, or G"
				        , chr((uint8_t)i));
				break;
		}
	}

	if (!xm->cancelled)
		lprintf(xm, LOG_ERR, "Failed to get transfer mode request from receiver");
	return false;
}

bool xmodem_put_eot(xmodem_t* xm)
{
	int      ch;
	unsigned errors;
	unsigned cans = 0;

	for (errors = 0; errors <= xm->max_errors && is_connected(xm) && !is_cancelled(xm); errors++) {

		lprintf(xm, LOG_INFO, "Sending End-of-Text (EOT) indicator (%d)", errors + 1);

		while ((ch = getcom(0)) != NOINP && is_connected(xm))
			lprintf(xm, LOG_INFO, "Throwing out received: %s", chr((uint8_t)ch));

		putcom(EOT);
		xmodem_flush(xm);
		if ((ch = getcom(xm->recv_timeout)) == NOINP)
			continue;
		lprintf(xm, LOG_INFO, "Received %s", chr((uint8_t)ch));
		if (ch == ACK)
			return true;
		if (ch == CAN && ++cans > 1)
			break;
		if (ch == NAK && errors == 0 && (*(xm->mode) & (YMODEM | GMODE)) == YMODEM) {
			continue;  /* chuck's double EOT trick so don't complain */
		}
		lprintf(xm, LOG_WARNING, "Expected ACK");
	}
	return false;
}

bool xmodem_send_file(xmodem_t* xm, const char* fname, FILE* fp, time_t* start, uint64_t* sent)
{
	bool             success = false;
	int64_t          sent_bytes = 0;
	char             block[XMODEM_MAX_BLOCK_SIZE];
	size_t           block_len;
	unsigned         block_num;
	size_t           i;
	size_t           rd;
	time_t           startfile;
	struct fs_info st;
	bool             sent_header = false;

	if (sent != NULL)
		*sent = 0;

	if (start != NULL)
		*start = time(NULL);

	if (fs_get_info(&st, fname) != 0) {
		lprintf(xm, LOG_ERR, "Failed to fstat file");
		return false;
	}

	if (xm->total_files == 0)
		xm->total_files = 1;

	if (xm->total_bytes == 0)
		xm->total_bytes = fs_stream_size(fp);

	strlcpy(xm->current_file_name, path_untrusted_basename(fname), sizeof(xm->current_file_name));

	do {
		/* try */
		if (*(xm->mode) & YMODEM) {

			if (!xmodem_get_mode(xm))
				break;

			memset(block, 0, sizeof(block));
			const char *name = path_untrusted_basename(fname);
			size_t length = strlen(name);
			if (length + 2 >= sizeof(block)) break;
			memcpy(block, name, length + 1);
			int metadata = snprintf(block + length + 1, sizeof(block) - length - 1,
			    "%" PRIu64 " %" PRIoMAX " 0 0 %lu %" PRId64,
			    (uint64_t)fs_stream_size(fp), (uintmax_t)st.modified_time,
			    xm->total_files - xm->sent_files, xm->total_bytes - xm->sent_bytes);
			if (metadata < 0 || (size_t)metadata >= sizeof(block) - length - 1) break;
			lprintf(xm, LOG_INFO, "Sending YMODEM header block: '%s'", block + length + 1);
			block_len = length + 2 + (size_t)metadata;

			for (xm->errors = 0; xm->errors <= xm->max_errors && !is_cancelled(xm) && is_connected(xm); xm->errors++) {
				int ch;
				while ((ch = getcom(0)) != NOINP && is_connected(xm))
					lprintf(xm, LOG_INFO, "Throwing out received: %s", chr((uint8_t)ch));
				xmodem_put_block(xm, (uint8_t*)block, block_len <= XMODEM_MIN_BLOCK_SIZE ? XMODEM_MIN_BLOCK_SIZE:XMODEM_MAX_BLOCK_SIZE, 0 /* block_num */);
				if ((i = xmodem_get_ack(xm, /* tries: */ 1, /* block_num: */ 0)) == ACK) {
					sent_header = true;
					break;
				}
				if ((i == NAK || i == 'C' || i == 'G')
				    && xm->fallback_to_xmodem && xm->errors + 1 == xm->fallback_to_xmodem) {
					lprintf(xm, LOG_NOTICE, "Falling back to XMODEM mode after %u attempts"
					        , xm->fallback_to_xmodem);
					*(xm->mode) &= ~YMODEM;
					break;
				}
			}
			if (xm->errors > xm->max_errors || is_cancelled(xm)) {
				lprintf(xm, LOG_ERR, "Failed to send header block");
				break;
			}
		}

		if (!xmodem_get_mode(xm))
			break;

		startfile = time(NULL);   /* reset time, don't count header block */
		if (start != NULL)
			*start = startfile;

		block_num = 1;
		xm->errors = 0;
		while (sent_bytes < fs_stream_size(fp) && xm->errors <= xm->max_errors && !is_cancelled(xm)
		       && is_connected(xm)) {
			fs_stream_seek(fp, (int64_t)sent_bytes);
			memset(block, CPMEOF, xm->block_size);
			if (!sent_header) {
				if (xm->block_size > XMODEM_MIN_BLOCK_SIZE) {
					if ((sent_bytes + xm->block_size) > fs_stream_size(fp)) {
						if ((sent_bytes + xm->block_size - XMODEM_MIN_BLOCK_SIZE) >= fs_stream_size(fp)) {
							lprintf(xm, LOG_INFO, "Falling back to 128-byte blocks for end of file");
							xm->block_size = XMODEM_MIN_BLOCK_SIZE;
						}
					}
				}
			}
			if ((rd = fread(block, 1, xm->block_size, fp)) != xm->block_size
			    && (sent_bytes + (int64_t)rd) != fs_stream_size(fp)) {
				lprintf(xm, LOG_ERR, "ERROR %d reading %u bytes at file offset %" PRId64
				        , errno, xm->block_size, (int64_t)fs_stream_position(fp));
				xm->errors++;
				continue;
			}
			if (xm->progress != NULL)
				xm->progress(xm->cbdata, block_num, fs_stream_position(fp), fs_stream_size(fp), startfile);
			xmodem_put_block(xm, (uint8_t*)block, xm->block_size, block_num);
			if (xmodem_get_ack(xm, /* tries: */ 5, block_num) != ACK) {
				xm->errors++;
				lprintf(xm, LOG_WARNING, "Block %u: Error #%d at offset %" PRId64
				        , block_num, xm->errors, (int64_t)(fs_stream_position(fp) - xm->block_size));
				if (xm->errors == 3 && block_num == 1 && xm->block_size > XMODEM_MIN_BLOCK_SIZE) {
					lprintf(xm, LOG_NOTICE, "Block %u: Falling back to 128-byte blocks", block_num);
					xm->block_size = XMODEM_MIN_BLOCK_SIZE;
				}
			} else {
				block_num++;
				sent_bytes += rd;
			}
		}
		if (sent_bytes >= fs_stream_size(fp) && !is_cancelled(xm) && is_connected(xm)) {

	#if 0 /* !SINGLE_THREADED */
			lprintf(LOG_DEBUG, "Waiting for output buffer to empty... ");
			if (WaitForEvent(outbuf_empty, 5000) != WAIT_OBJECT_0)
				lprintf(xm, LOG_WARNING, "FAILURE");
	#endif
			if (xmodem_put_eot(xm))  /* end-of-text, wait for ACK */
				success = true;
		}
	} while (0);
	/* finally */

	if (!success)
		xmodem_cancel(xm);

	if (sent != NULL)
		*sent = sent_bytes;

	return success;
}


void xmodem_init(xmodem_t* xm, void* cbdata, long* mode
                 , int (*lputs)(void*, int level, const char* str)
                 , void (*progress)(void* unused, unsigned block_num, int64_t offset, int64_t fsize, time_t t)
                 , int (*send_byte)(void*, uint8_t ch, unsigned timeout)
                 , int (*recv_byte)(void*, unsigned timeout)
                 , bool (*is_connected)(void*)
                 , bool (*is_cancelled)(void*)
                 , void (*flush)(void*))
{
	memset(xm, 0, sizeof(xmodem_t));

	/* Use sane default values */
	xm->send_timeout = 10;        /* seconds */
	xm->recv_timeout = 10;        /* seconds */
	xm->byte_timeout = 3;         /* seconds */
	xm->ack_timeout = 10;         /* seconds */

	xm->block_size = XMODEM_MAX_BLOCK_SIZE;
	xm->max_block_size = XMODEM_MAX_BLOCK_SIZE;
	xm->max_errors = 9;
	xm->g_delay = 0;

	xm->cbdata = cbdata;
	xm->mode = mode;
	xm->g_mode_supported = true;
	xm->crc_mode_supported = true;
	xm->lputs = lputs;
	xm->progress = progress;
	xm->send_byte = send_byte;
	xm->recv_byte = recv_byte;
	xm->is_connected = is_connected;
	xm->is_cancelled = is_cancelled;
	xm->flush = flush;
}
