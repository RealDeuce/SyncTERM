#include "transfer_protocol.h"
/*
 * Copyright (c) 2003-2026 Rob Swindell
 * Copyright (c) 2005-2026 Stephen Hurd
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

/*
 * Special thanks to Jacques Mattheij, formerly of Mattheij Computer Service,
 * and original author of zmtx/zmrx.
 */

/* zmodem.c */

/* Synchronet ZMODEM Functions */

/*
 * zmodem primitives and other code common to zmtx and zmrx
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h> /* va_list */


#include "zmodem.h"


#define ENDOFFRAME  2
#define FRAMEOK     1
#define TIMEOUT         -1  /* rx routine did not receive a character within timeout */
#define INVHDR          -2  /* invalid header received; but within timeout */
#define ABORTED         -3  /* Aborted *or* disconnected */
#define SUBPKTOVERFLOW  -4  /* Subpacket received more than block length */
#define CRCFAILED       -5  /* Failed CRC comparison */
#define INVALIDSUBPKT   -6  /* Invalid Subpacket Type */
#define ZDLEESC 0x8000  /* one of ZCRCE; ZCRCG; ZCRCQ or ZCRCW was received; ZDLE escaped */

#define BADSUBPKT   0x80

#define SEND_SUCCESS    0

#define HDRLEN     5    /* size of a zmodem header */

#define SET_PARITY(c)	((c) | 0x80)
#define STRIPPED_PARITY(c)  ((c) & 0x7f)

static int lprintf(zmodem_t* zm, int level, const char *fmt, ...)
{
	va_list argptr;
	char    sbuf[1024];

	if (zm->lputs == NULL)
		return -1;
	if (zm->log_level != NULL)
		if (level > *zm->log_level)
			return 0;

	va_start(argptr, fmt);
	vsnprintf(sbuf, sizeof(sbuf), fmt, argptr);
	sbuf[sizeof(sbuf) - 1] = 0;
	va_end(argptr);
	return zm->lputs(zm->cbdata, level, sbuf);
}

static bool is_connected(zmodem_t* zm)
{
	if (zm->is_connected != NULL)
		return zm->is_connected(zm->cbdata);
	return true;
}

static bool is_cancelled(zmodem_t* zm)
{
	if (zm->is_cancelled != NULL)
		return zm->cancelled = zm->is_cancelled(zm->cbdata);
	return zm->cancelled;
}

/* The one definition of "this byte needs no zmodem_rx() handling": every byte
   except ZDLE, the four flow-control forms, and -- when the peer negotiated
   ZF0_ESCCTL -- control characters, which arrive escaped and are dropped if
   they do not.  zmodem_rx() tests it per byte and recv_span copies while it
   holds, so the two paths cannot disagree about what a plain byte is.

   escape_ctrl_chars is a public struct field a consumer may set directly, so
   the table is a cache that can go stale.  It is rebuilt everywhere the flag
   becomes authoritative: zmodem_init() for the default, zmodem_send_zrinit()
   for a receive session (where the consumer's value is what we announce), and
   zmodem_parse_zrinit() for a send session (where the peer's value overrides
   ours).  Anything else that assigns escape_ctrl_chars must call this. */
static void zmodem_build_rx_plain_tab(zmodem_t* zm)
{
	int c;

	for (c = 0; c < 256; c++) {
		uint8_t plain = 1;
		if (c == ZDLE || c == XON || c == (XON | 0x80) || c == XOFF || c == (XOFF | 0x80))
			plain = 0;
		else if (zm->escape_ctrl_chars && (c & 0x60) == 0)
			plain = 0;
		zm->rx_plain_tab[c] = plain;
	}
}

static bool is_data_waiting(zmodem_t* zm, unsigned timeout)
{
	if (zm->data_waiting)
		return zm->data_waiting(zm->cbdata, timeout);
	return false;
}

static char *chr(int ch)
{
	static _Thread_local char str[25];

	switch (ch) {
		case TIMEOUT:           return "TIMEOUT";
		case ABORTED:           return "ABORTED";
		case SUBPKTOVERFLOW:    return "Subpacket Overflow";
		case CRCFAILED:         return "CRC ERROR";
		case INVALIDSUBPKT:     return "Invalid Subpacket";
		case ZRQINIT:           return "ZRQINIT";
		case ZRINIT:            return "ZRINIT";
		case ZSINIT:            return "ZSINIT";
		case ZACK:              return "ZACK";
		case ZFILE:             return "ZFILE";
		case ZSKIP:             return "ZSKIP";
		case ZCRC:              return "ZCRC";
		case ZNAK:              return "ZNAK";
		case ZABORT:            return "ZABORT";
		case ZFIN:              return "ZFIN";
		case ZRPOS:             return "ZRPOS";
		case ZDATA:             return "ZDATA";
		case ZEOF:              return "ZEOF";
		case ZFERR:             return "ZFERR";
		case ZPAD:              return "ZPAD";
		case ZCAN:              return "ZCAN";
		case ZDLE:              return "ZDLE";
		case ZDLEE:             return "ZDLEE";
		case ZBIN:              return "ZBIN";
		case ZHEX:              return "ZHEX";
		case ZBIN32:            return "ZBIN32";
		case ZRESC:             return "ZRESC";
		case ZCRCE:             return "ZCRCE";
		case ZCRCG:             return "ZCRCG";
		case ZCRCQ:             return "ZCRCQ";
		case ZCRCW:             return "ZCRCW";
		case XON:               return "XON";
		case XOFF:              return "XOFF";
	}
	if (ch < 0)
		sprintf(str, "%d", ch);
	else if (ch >= ' ' && ch <= '~')
		sprintf(str, "'%c' (%02Xh)", (uint8_t)ch, (uint8_t)ch);
	else
		sprintf(str, "%u (%02Xh)", (uint8_t)ch, (uint8_t)ch);
	return str;
}

static char* frame_desc(int frame)
{
	static _Thread_local char str[25];

	if (frame == TIMEOUT)
		return "TIMEOUT";

	if (frame == INVHDR)
		return "Invalid Header";

	if (frame == ABORTED)
		return "Aborted";

	if (frame >= 0 && (frame & BADSUBPKT)) {
		strcpy(str, "BAD ");
		switch (frame & ~BADSUBPKT) {
			case ZRQINIT:       strcat(str, "ZRQINIT");      break;
			case ZRINIT:        strcat(str, "ZRINIT");       break;
			case ZSINIT:        strcat(str, "ZSINIT");       break;
			case ZACK:          strcat(str, "ZACK");         break;
			case ZFILE:         strcat(str, "ZFILE");        break;
			case ZSKIP:         strcat(str, "ZSKIP");        break;
			case ZNAK:          strcat(str, "ZNAK");         break;
			case ZABORT:        strcat(str, "ZABORT");       break;
			case ZFIN:          strcat(str, "ZFIN");         break;
			case ZRPOS:         strcat(str, "ZRPOS");        break;
			case ZDATA:         strcat(str, "ZDATA");        break;
			case ZEOF:          strcat(str, "ZEOF");         break;
			case ZFERR:         strcat(str, "ZFERR");        break;
			case ZCRC:          strcat(str, "ZCRC");         break;
			case ZCHALLENGE:    strcat(str, "ZCHALLENGE");   break;
			case ZCOMPL:        strcat(str, "ZCOMPL");       break;
			case ZCAN:          strcat(str, "ZCAN");         break;
			case ZFREECNT:      strcat(str, "ZFREECNT");     break;
			case ZCOMMAND:      strcat(str, "ZCOMMAND");     break;
			case ZSTDERR:       strcat(str, "ZSTDERR");      break;
			default:
				sprintf(str, "Unknown (%08X)", frame);
				break;
		}
	} else
		return chr(frame);
	return str;
}

unsigned long frame_pos(zmodem_t* zm, int type)
{
	switch (type) {
		case ZRPOS:
		case ZACK:
		case ZEOF:
		case ZDATA:
			return zm->rxd_header_pos;
	}

	return 0;
}

/*
 * read bytes as long as rdchk indicates that
 * more data is available.
 */

void zmodem_recv_purge(zmodem_t* zm, unsigned timeout)
{
	int c;

	while ((c = zm->recv_byte(zm->cbdata, timeout)) >= 0) {
		lprintf(zm, LOG_DEBUG, "Purging data in receive buffer: %s", chr(c));
	}
}

/*
 * Flush the output buffer
 */
void zmodem_flush(zmodem_t* zm)
{
	if (zm->flush != NULL)
		zm->flush(zm->cbdata);
}

/*
 * transmit a character.
 * this is the raw modem interface
 */

/* Keep error reporting and uncommon escaping out of the inline data path. */

static int zmodem_send_raw_error(zmodem_t* zm, int result)
{
	lprintf(zm, LOG_ERR, "zmodem_send_raw ERROR: %d", result);
	return result;
}

static inline int zmodem_send_raw_fast(zmodem_t* zm, unsigned char ch)
{
	int result = zm->send_byte(zm->cbdata, ch, zm->send_timeout);

	if (result != SEND_SUCCESS)
		return zmodem_send_raw_error(zm, result);
	zm->last_sent = ch;
	return result;
}

/* Returns 0 on success */
int zmodem_send_raw(zmodem_t* zm, unsigned char ch)
{
	return zmodem_send_raw_fast(zm, ch);
}

/*
 * transmit a character ZDLE escaped
 */

int zmodem_send_esc(zmodem_t* zm, unsigned char c)
{
	int result;

	if ((result = zmodem_send_raw(zm, ZDLE)) != SEND_SUCCESS) {
		lprintf(zm, LOG_ERR, "%s ERROR: %d", __func__, result);
		return result;
	}
	/*
	 * exclusive or; not an or so ZDLE becomes ZDLEE
	 */
	return zmodem_send_raw(zm, (uint8_t)(c ^ 0x40));
}

/*
 * transmit a character; ZDLE escaping if appropriate
 */

enum zmodem_tx_class {
	ZMODEM_TX_NORMAL        = 0,
	ZMODEM_TX_ESCAPE_ALWAYS = 1 << 0,
	ZMODEM_TX_ESCAPE_CTRL   = 1 << 1,
	ZMODEM_TX_ESCAPE_IAC    = 1 << 3,
};

/*
 * Classify bytes without a data-dependent decision tree.  Each byte has
 * exactly one class bit; zmodem_tx() masks that class against the escape
 * modes active for the current session and previous byte.
 */
#define TXN ZMODEM_TX_NORMAL
#define TXA ZMODEM_TX_ESCAPE_ALWAYS
#define TXC ZMODEM_TX_ESCAPE_CTRL
#define TXI ZMODEM_TX_ESCAPE_IAC
static const unsigned char zmodem_tx_classes[256] = {
	/* 00 */ TXC, TXC, TXC, TXC, TXC, TXC, TXC, TXC, TXC, TXC, TXC, TXC, TXC, TXC, TXC, TXC,
	/* 10 */ TXA, TXA, TXC, TXA, TXC, TXC, TXC, TXC, TXA, TXC, TXC, TXC, TXC, TXC, TXC, TXC,
	/* 20 */ TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN,
	/* 30 */ TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN,
	/* 40 */ TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN,
	/* 50 */ TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN,
	/* 60 */ TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN,
	/* 70 */ TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN,
	/* 80 */ TXC, TXC, TXC, TXC, TXC, TXC, TXC, TXC, TXC, TXC, TXC, TXC, TXC, TXC, TXC, TXC,
	/* 90 */ TXA, TXA, TXC, TXA, TXC, TXC, TXC, TXC, TXC, TXC, TXC, TXC, TXC, TXC, TXC, TXC,
	/* A0 */ TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN,
	/* B0 */ TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN,
	/* C0 */ TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN,
	/* D0 */ TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN,
	/* E0 */ TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN,
	/* F0 */ TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXN, TXI,
};
#undef TXN
#undef TXA
#undef TXC
#undef TXI

/*
 * The escape modes remain fixed throughout a data subpacket, so a data sender
 * computes this mask once and reuses it for every byte.
 */
static inline unsigned zmodem_tx_active(const zmodem_t* zm)
{
	unsigned escape_ctrl = !!zm->escape_ctrl_chars;

	return ZMODEM_TX_ESCAPE_ALWAYS
	    | (escape_ctrl * ZMODEM_TX_ESCAPE_CTRL)
	    | (!!zm->escape_telnet_iac * ZMODEM_TX_ESCAPE_IAC);
}

static int zmodem_tx_non_normal(zmodem_t* zm, unsigned char c, unsigned action)
{
	int result;

	if (action == ZMODEM_TX_ESCAPE_IAC) {
		if ((result = zmodem_send_raw_fast(zm, ZDLE)) != SEND_SUCCESS)
			return result;
		return zmodem_send_raw_fast(zm, ZRUB1);
	}
	return zmodem_send_esc(zm, c);
}


static inline int zmodem_tx_masked(zmodem_t* zm, unsigned char c, unsigned active)
{
	unsigned action = zmodem_tx_classes[c] & active;

	if (action != ZMODEM_TX_NORMAL)
		return zmodem_tx_non_normal(zm, c, action);
	return zmodem_send_raw_fast(zm, c);
}

int zmodem_tx(zmodem_t* zm, unsigned char c)
{
	return zmodem_tx_masked(zm, c, zmodem_tx_active(zm));
}

/**********************************************/
/* Output single byte as two hex ASCII digits */
/**********************************************/
int zmodem_send_hex(zmodem_t* zm, uint8_t val)
{
	char* xdigit = "0123456789abcdef";
	int   result;

//	lprintf(zm, LOG_DEBUG, __func__ " %02X",val);

	if ((result = zmodem_send_raw(zm, xdigit[val >> 4])) != SEND_SUCCESS)
		return result;
	return zmodem_send_raw(zm, xdigit[val & 0xf]);
}

int zmodem_send_padded_zdle(zmodem_t* zm)
{
	int result;

	if ((result = zmodem_send_raw(zm, ZPAD)) != SEND_SUCCESS)
		return result;
	if ((result = zmodem_send_raw(zm, ZPAD)) != SEND_SUCCESS)
		return result;
	return zmodem_send_raw(zm, ZDLE);
}

/*
 * transmit a hex header.
 * these routines use tx_raw because we're sure that all the
 * characters are not to be escaped.
 */
int zmodem_send_hex_header(zmodem_t* zm, unsigned char * p)
{
	int                i;
	int                result;
	uint8_t              type = *p;
	unsigned short int crc;

//	lprintf(zm, LOG_DEBUG, __func__ " %s", chr(type));

	if ((result = zmodem_send_padded_zdle(zm)) != SEND_SUCCESS)
		return result;

	if ((result = zmodem_send_raw(zm, ZHEX)) != SEND_SUCCESS)
		return result;

	/*
	 * initialise the crc
	 */

	crc = 0;

	/*
	 * transmit the header
	 */

	for (i = 0; i < HDRLEN; i++) {
		if ((result = zmodem_send_hex(zm, *p)) != SEND_SUCCESS)
			return result;
		crc = st_transfer_crc16_byte(*p, crc);
		p++;
	}

	/*
	 * update the crc as though it were zero
	 */

	/*
	 * transmit the crc
	 */

	if ((result = zmodem_send_hex(zm, (uint8_t)(crc >> 8))) != SEND_SUCCESS)
		return result;
	if ((result = zmodem_send_hex(zm, (uint8_t)(crc & 0xff))) != SEND_SUCCESS)
		return result;

	/*
	 * end of line sequence
	 */

	if ((result = zmodem_send_raw(zm, '\r')) != SEND_SUCCESS)
		return result;
	if ((result = zmodem_send_raw(zm, '\n')) != SEND_SUCCESS)  /* FDSZ sends 0x8a instead of 0x0a */
		return result;

	if (type != ZACK && type != ZFIN)
		result = zmodem_send_raw(zm, XON);

	zmodem_flush(zm);

	return result;
}

/*
 * Send ZMODEM binary header hdr
 */

int zmodem_send_bin32_header(zmodem_t* zm, unsigned char * p)
{
	int      i;
	int      result;
	uint32_t crc;

//	lprintf(zm, LOG_DEBUG, __func__ " %s", chr(*p));

	if ((result = zmodem_send_padded_zdle(zm)) != SEND_SUCCESS)
		return result;

	if ((result = zmodem_send_raw(zm, ZBIN32)) != SEND_SUCCESS)
		return result;

	crc = 0xffffffffL;

	for (i = 0; i < HDRLEN; i++) {
		crc = st_transfer_crc32_byte(*p, crc);
		if ((result = zmodem_tx(zm, *p++)) != SEND_SUCCESS)
			return result;
	}

	crc = ~crc;

	if ((result = zmodem_tx(zm, (uint8_t)((crc) & 0xff))) != SEND_SUCCESS)
		return result;
	if ((result = zmodem_tx(zm, (uint8_t)((crc >>  8) & 0xff))) != SEND_SUCCESS)
		return result;
	if ((result = zmodem_tx(zm, (uint8_t)((crc >> 16) & 0xff))) != SEND_SUCCESS)
		return result;
	return zmodem_tx(zm, (uint8_t)((crc >> 24) & 0xff));
}

int zmodem_send_bin16_header(zmodem_t* zm, unsigned char * p)
{
	int          i;
	int          result;
	uint16_t     crc;

//	lprintf(zm, LOG_DEBUG, __func__ " %s", chr(*p));

	if ((result = zmodem_send_padded_zdle(zm)) != SEND_SUCCESS)
		return result;

	if ((result = zmodem_send_raw(zm, ZBIN)) != SEND_SUCCESS)
		return result;

	crc = 0;

	for (i = 0; i < HDRLEN; i++) {
		crc = st_transfer_crc16_byte(*p, crc);
		if ((result = zmodem_tx(zm, *p++)) != SEND_SUCCESS)
			return result;
	}

	if ((result = zmodem_tx(zm, (uint8_t)(crc >> 8))) != SEND_SUCCESS)
		return result;
	return zmodem_tx(zm, (uint8_t)(crc & 0xff));
}


/*
 * transmit a header using either hex 16 bit crc or binary 32 bit crc
 * depending on the receivers capabilities
 * we dont bother with variable length headers. I dont really see their
 * advantage and they would clutter the code unneccesarily
 */

int zmodem_send_bin_header(zmodem_t* zm, unsigned char * p)
{
	if (zm->can_fcs_32 && !zm->want_fcs_16)
		return zmodem_send_bin32_header(zm, p);
	return zmodem_send_bin16_header(zm, p);
}

/*
 * data subpacket transmission
 */

int zmodem_send_data32(zmodem_t* zm, uint8_t subpkt_type, unsigned char * p, size_t l)
{
	int      result;
	uint32_t crc;
	unsigned active;
	unsigned i;

//	lprintf(zm, LOG_DEBUG, __func__ " %s (%u bytes)", chr(subpkt_type), l);

	active = zmodem_tx_active(zm);
	crc    = 0xffffffffl;

	while (l >= 4) {
		crc = st_transfer_crc32_four(p, crc);
		for (i = 0; i < 4; i++) {
			if ((result = zmodem_tx_masked(zm, p[i], active)) != SEND_SUCCESS)
				return result;
		}
		p += 4;
		l -= 4;
	}
	while (l > 0) {
		crc = st_transfer_crc32_byte(*p, crc);
		if ((result = zmodem_tx_masked(zm, *p++, active)) != SEND_SUCCESS)
			return result;
		l--;
	}

	crc = st_transfer_crc32_byte(subpkt_type, crc);

	if ((result = zmodem_send_raw(zm, ZDLE)) != SEND_SUCCESS)
		return result;
	if ((result = zmodem_send_raw(zm, subpkt_type)) != SEND_SUCCESS)
		return result;

	crc = ~crc;

	if ((result = zmodem_tx_masked(zm, (uint8_t) ((crc) & 0xff), active)) != SEND_SUCCESS)
		return result;
	if ((result = zmodem_tx_masked(zm, (uint8_t) ((crc >> 8) & 0xff), active)) != SEND_SUCCESS)
		return result;
	if ((result = zmodem_tx_masked(zm, (uint8_t) ((crc >> 16) & 0xff), active)) != SEND_SUCCESS)
		return result;
	return zmodem_tx_masked(zm, (uint8_t) ((crc >> 24) & 0xff), active);
}

int zmodem_send_data16(zmodem_t* zm, uint8_t subpkt_type, unsigned char * p, size_t l)
{
	int            result;
	unsigned short crc;
	unsigned       active;

//	lprintf(zm, LOG_DEBUG, __func__ " %s (%u bytes)", chr(subpkt_type), l);

	active = zmodem_tx_active(zm);
	crc    = 0;

	while (l > 0) {
		crc = st_transfer_crc16_byte(*p, crc);
		if ((result = zmodem_tx_masked(zm, *p++, active)) != SEND_SUCCESS)
			return result;
		l--;
	}

	crc = st_transfer_crc16_byte(subpkt_type, crc);

	if ((result = zmodem_send_raw(zm, ZDLE)) != SEND_SUCCESS)
		return result;
	if ((result = zmodem_send_raw(zm, subpkt_type)) != SEND_SUCCESS)
		return result;

	if ((result = zmodem_tx_masked(zm, (uint8_t)(crc >> 8), active)) != SEND_SUCCESS)
		return result;
	return zmodem_tx_masked(zm, (uint8_t)(crc & 0xff), active);
}

bool zmodem_end_of_frame(int subpkt_type)
{
	return subpkt_type == ZCRCW || subpkt_type == ZCRCE;
}

/*
 * send a data subpacket using crc 16 or crc 32 as desired by the receiver
 */

int zmodem_send_data_subpkt(zmodem_t* zm, uint8_t subpkt_type, unsigned char* data, size_t len)
{
	int result;

	if (zmodem_end_of_frame(subpkt_type))
		zm->frame_in_transit = false;
	else    /* other subpacket (mid-frame) */
		zm->frame_in_transit = true;

	if (!zm->want_fcs_16 && zm->can_fcs_32) {
		if ((result = zmodem_send_data32(zm, subpkt_type, data, len)) != SEND_SUCCESS)
			return result;
	}
	else {
		if ((result = zmodem_send_data16(zm, subpkt_type, data, len)) != SEND_SUCCESS)
			return result;
	}

	if (subpkt_type == ZCRCW)
		result = zmodem_send_raw(zm, XON);

	zmodem_flush(zm);

	return result;
}

int zmodem_send_data(zmodem_t* zm, uint8_t subpkt_type, unsigned char* data, size_t len)
{
	if (!zm->frame_in_transit)   { /* Start of frame, include ZDATA header */
		lprintf(zm, LOG_DEBUG, "%lu %s Start of frame: %s"
		        , (unsigned long)zm->current_file_pos, __func__, chr(subpkt_type));
		zmodem_send_pos_header(zm, ZDATA, (uint32_t)zm->current_file_pos, /* Hex? */ false);
	}

	return zmodem_send_data_subpkt(zm, subpkt_type, data, len);
}

int zmodem_send_pos_header(zmodem_t* zm, int type, uint32_t pos, bool hex)
{
	uint8_t header[5];

	lprintf(zm, LOG_DEBUG, "%lu %s %s", (unsigned long)pos, __func__, chr(type));
	header[0]   = (uint8_t)type;
	header[ZP0] = (uint8_t) (pos        & 0xff);
	header[ZP1] = (uint8_t)((pos >>  8) & 0xff);
	header[ZP2] = (uint8_t)((pos >> 16) & 0xff);
	header[ZP3] = (uint8_t)((pos >> 24) & 0xff);

	if (hex)
		return zmodem_send_hex_header(zm, header);
	else
		return zmodem_send_bin_header(zm, header);
}

int zmodem_send_ack(zmodem_t* zm, uint32_t pos)
{
	return zmodem_send_pos_header(zm, ZACK, pos, /* Hex? */ true);
}

int zmodem_send_zfin(zmodem_t* zm)
{
	unsigned char zfin_header[] = { ZFIN, 0, 0, 0, 0 };

	lprintf(zm, LOG_NOTICE, "Finishing Session (Sending ZFIN)");
	return zmodem_send_hex_header(zm, zfin_header);
}

int zmodem_send_zabort(zmodem_t* zm)
{
	lprintf(zm, LOG_WARNING, "%lu Aborting Transfer (Sending ZABORT)", (unsigned long)zm->current_file_pos);
	return zmodem_send_pos_header(zm, ZABORT, 0, /* Hex? */ true);
}

int zmodem_send_znak(zmodem_t* zm)
{
	lprintf(zm, LOG_INFO, "%lu Sending ZNAK", (unsigned long)zm->current_file_pos);
	return zmodem_send_pos_header(zm, ZNAK, 0, /* Hex? */ true);
}

int zmodem_send_zskip(zmodem_t* zm)
{
	lprintf(zm, LOG_INFO, "%lu Sending ZSKIP", (unsigned long)zm->current_file_pos);
	return zmodem_send_pos_header(zm, ZSKIP, 0L, /* Hex? */ true);
}

int zmodem_send_zeof(zmodem_t* zm)
{
	lprintf(zm, LOG_INFO, "%lu Sending End-of-File (ZEOF) frame", (unsigned long)zm->current_file_pos);
	return zmodem_send_pos_header(zm, ZEOF, (uint32_t)zm->current_file_pos, /* Hex? */ true);
}


/*
 * rx_raw ; receive a single byte from the line.
 * reads as many are available and then processes them one at a time
 * check the data stream for 5 consecutive CAN characters;
 * and if you see them abort. this saves a lot of clutter in
 * the rest of the code; even though it is a very strange place
 * for an exit. (but that was what session abort was all about.)
 */

int zmodem_recv_raw(zmodem_t* zm)
{
	int      c = NOINP;
	unsigned attempt;

	for (attempt = 0; attempt < zm->recv_timeout; attempt++) {
		if ((c = zm->recv_byte(zm->cbdata, 1 /* second timeout */)) >= 0)
			break;
		if (is_cancelled(zm))
			return ZCAN;
		if (!is_connected(zm))
			return ABORTED;
		lprintf(zm, LOG_DEBUG, "%s Received NO INPUT (attempt %u of %u)"
		        , __func__, attempt + 1, zm->recv_timeout);
	}
	if (attempt >= zm->recv_timeout) {
		lprintf(zm, LOG_WARNING, "%lu %s TIMEOUT (%u seconds)"
		        , (unsigned long)zm->current_file_pos, __func__, attempt);
		return TIMEOUT;
	}

	if (c == CAN) {
		zm->n_cans++;
		if (zm->n_cans == 5) {
			zm->cancelled = true;
			lprintf(zm, LOG_WARNING, "%lu Canceled remotely", (unsigned long)zm->current_file_pos);
/*			return(TIMEOUT);	removed June-12-2005 */
		}
	}
	else {
		zm->n_cans = 0;
	}

	return c;
}

/*
 * rx; receive a single byte undoing any escaping at the
 * sending site. this bit looks like a mess. sorry for that
 * but there seems to be no other way without incurring a lot
 * of overhead. at least like this the path for a normal character
 * is relatively short.
 */

int zmodem_rx(zmodem_t* zm)
{
	int c;

	/*
	 * outer loop for ever so for sure something valid
	 * will come in; a timeout will occur or a session abort
	 * will be received.
	 */

	while (is_connected(zm) && !is_cancelled(zm)) {

		do {
			c = zmodem_recv_raw(zm);
			if (c >= 0 && zm->rx_plain_tab[c])
				return c;           /* the common case, by a wide margin */
			switch (c) {
				case ZDLE:
					break;
				case XON:
				case XON | 0x80:
				case XOFF:
				case XOFF | 0x80:
					lprintf(zm, LOG_WARNING, "%lu Dropping flow ctrl char: %s"
					        , (unsigned long)zm->current_file_pos, chr(c));
					continue;
				default:
					if (c < 0)
						return c;
					/*
					 * if all control characters should be escaped and
					 * this one wasn't then its spurious and should be dropped.
					 */
					if (zm->escape_ctrl_chars && (c >= 0) && (c & 0x60) == 0) {
						lprintf(zm, LOG_WARNING, "%lu Dropping unescaped ctrl char: %s"
						        , (unsigned long)zm->current_file_pos, chr(c));
						continue;
					}
					/*
					 * normal character; return it.
					 */
					return c;
			}
			break;
		} while (!is_cancelled(zm));

		/*
		 * ZDLE encoded sequence or session abort.
		 * (or something illegal; then back to the top)
		 */

		while (!is_cancelled(zm)) {

			switch (c = zmodem_recv_raw(zm)) {
				case XON:
				case XON | 0x80:
				case XOFF:
				case XOFF | 0x80:
				case ZDLE:
					lprintf(zm, LOG_WARNING, "%lu Dropping escaped flow ctrl char: %s"
					        , (unsigned long)zm->current_file_pos, chr(c));
					continue;
				/*
				 * these four are really nasty.
				 * for convenience we just change them into
				 * special characters by setting a bit outside the
				 * first 8. that way they can be recognized and still
				 * be processed as characters by the rest of the code.
				 */
				case ZCRCE:
				case ZCRCG:
				case ZCRCQ:
				case ZCRCW:
//					lprintf(zm,LOG_DEBUG, "%lu Encoding data subpacket type: %s", (unsigned long)zm->current_file_pos, chr(c));
					return c | ZDLEESC;
				case ZRUB0:
					return 0x7f;
				case ZRUB1:
					return 0xff;
				default:
					if (c < 0)
						return c;

					if (zm->escape_ctrl_chars && (c & 0x60) == 0) {
						/*
						 * a not escaped control character; probably
						 * something from a network. just drop it.
						 */
						lprintf(zm, LOG_WARNING, "%lu Dropping unescaped ctrl char: %s"
						        , (unsigned long)zm->current_file_pos, chr(c));
						continue;
					}
					/*
					 * legitimate escape sequence.
					 * rebuild the original and return it.
					 */
					if ((c & 0x60) == 0x40) {
						return c ^ 0x40;
					}
					lprintf(zm, LOG_WARNING, "%lu Illegal sequence: ZDLE %s"
					        , (unsigned long)zm->current_file_pos, chr(c));
					break;
			}
			break;
		}
	}

	/*
	 * not reached (unless canceled).
	 */

	return ABORTED;
}

/* CRC16 is big-endian; CRC32 is little-endian. A timeout, disconnect or
 * escaped frame marker is not a CRC byte, even when narrowing it would
 * happen to match the expected checksum. */
static int recv_crc_bytes(zmodem_t *zm, bool crc32, uint32_t *result)
{
	uint32_t value = 0;
	for (unsigned i = 0; i < (crc32 ? 4U : 2U); ++i) {
		int byte = zmodem_rx(zm);
		if (byte < 0) return byte;
		if (byte > 0xff) return INVALIDSUBPKT;
		unsigned shift = crc32 ? i * 8 : (1 - i) * 8;
		value |= (uint32_t)byte << shift;
	}
	*result = value;
	return 0;
}

/*
 * receive a data subpacket as dictated by the last received header.
 * return 2 with correct packet and end of frame
 * return 1 with correct packet frame continues
 * return 0 with incorrect frame.
 * return TIMEOUT with a timeout
 * if an acknowledgment is requested it is generated automatically
 * here.
 */

/*
 * data subpacket reception
 */

int zmodem_recv_data32(zmodem_t* zm, unsigned char * p, unsigned maxlen, unsigned* len, int* type)
{
	int      c;
	uint32_t rxd_crc;
	uint32_t crc;
	int      subpkt_type;

//	lprintf(zm,LOG_DEBUG, __FUNCTION___);

	*type = INVALIDSUBPKT;
	crc = 0xffffffffl;

	do {
		/* Bulk fast path: pull a run of bytes that zmodem_rx() would return
		   verbatim, CRC it as a block, and leave the first byte needing real
		   handling for the per-byte path below.  Skipped entirely when no
		   recv_span callback was supplied. */
		if (zm->recv_span != NULL && *len < maxlen) {
			size_t got;

			while ((got = zm->recv_span(zm->cbdata, p, maxlen - *len,
			                            zm->rx_plain_tab)) != 0) {
				crc = st_transfer_crc32_update(crc, p, got);
				p += got;
				*len += got;
				if (*len >= maxlen)
					break;
			}
		}

		c = zmodem_rx(zm);

		if (c < 0)
			return c;

		if (c > 0xff)
			break;

		if (*len >= maxlen) {
			lprintf(zm, LOG_ERR, "%lu Subpacket OVERFLOW (%u >= %u)", (unsigned long)zm->ack_file_pos, *len, maxlen);
			return SUBPKTOVERFLOW;
		}
		crc = st_transfer_crc32_byte((uint8_t)c, crc);
		*p++ = (uint8_t)c;
		(*len)++;
	} while (1);

	subpkt_type = c & 0xff;
	*type = subpkt_type;

	crc = st_transfer_crc32_byte((uint8_t)subpkt_type, crc);

	crc = ~crc;

	int status = recv_crc_bytes(zm, true, &rxd_crc);
	if (status != 0) return status;

	if (rxd_crc != crc) {
		lprintf(zm, LOG_DEBUG, "%lu %s CRC ERROR (%08X, expected: %08X) Bytes=%u, subpacket type=%s"
		        , (unsigned long)zm->ack_file_pos, __func__, rxd_crc, crc, *len, chr(subpkt_type));
		return CRCFAILED;
	}
//	lprintf(zm,LOG_DEBUG, "%lu %s GOOD CRC: %08lX (Bytes=%u, subpacket type=%s)"
//		,(unsigned long)zm->ack_file_pos, __func__, crc, *len, chr(subpkt_type));

	zm->ack_file_pos += *len;

	return subpkt_type;
}

int zmodem_recv_data16(zmodem_t* zm, register unsigned char* p, unsigned maxlen, unsigned* len, int* type)
{
	int            c;
	int            subpkt_type;
	unsigned short crc;
	uint32_t       rxd_crc;

//	lprintf(zm, LOG_DEBUG, __func__);

	crc = 0;
	*type = INVALIDSUBPKT;

	do {
		c = zmodem_rx(zm);

		if (c < 0)
			return c;

		if (c > 0xff)
			break;

		if (*len >= maxlen)
			return SUBPKTOVERFLOW;
		crc = st_transfer_crc16_byte((uint8_t)c, crc);
		*p++ = (uint8_t)c;
		(*len)++;
	} while (1);

	subpkt_type = c & 0xff;
	*type = subpkt_type;

	crc = st_transfer_crc16_byte((uint8_t)subpkt_type, crc);

	int status = recv_crc_bytes(zm, false, &rxd_crc);
	if (status != 0) return status;

	if (rxd_crc != crc) {
		lprintf(zm, LOG_DEBUG, "%lu %s CRC ERROR (%04hX, expected: %04hX) Bytes=%u, subpacket type=%s"
		        , (unsigned long)zm->ack_file_pos, __func__, rxd_crc, crc, *len, chr(subpkt_type));
		return CRCFAILED;
	}
//	lprintf(zm,LOG_DEBUG, "%lu %s GOOD CRC: %04hX (Bytes=%d, subpacket type=%s)"
//		,(unsigned long)zm->ack_file_pos, __func__, crc, *len, chr(subpkt_type));

	zm->ack_file_pos += *len;

	return subpkt_type;
}

int zmodem_recv_data(zmodem_t* zm, unsigned char* buf, size_t maxlen, unsigned* len, bool ack, int* type)
{
	int      subpkt_type;
	unsigned n = 0;

	if (len == NULL)
		len = &n;

//	lprintf(zm,LOG_DEBUG, __func__ " (%u-bit)", zm->receive_32bit_data ? 32:16);

	/*
	 * receive the right type of frame
	 */

	*len = 0;

	if (zm->receive_32bit_data) {
		subpkt_type = zmodem_recv_data32(zm, buf, maxlen, len, type);
	}
	else {
		subpkt_type = zmodem_recv_data16(zm, buf, maxlen, len, type);
	}

	if (subpkt_type <= 0) {  /* e.g. TIMEOUT, SUBPKTOVERFLOW, CRCFAILED */
		lprintf(zm, LOG_WARNING, "%s data subpacket (%u bytes) %s"
		        , chr(*type), *len, chr(subpkt_type));
		return subpkt_type;
	}

	lprintf(zm, LOG_DEBUG, "%lu Successful receipt of subpacket type: %s (%u bytes)"
	        , (unsigned long)zm->ack_file_pos, chr(subpkt_type), *len);

	switch (subpkt_type)  {
		/*
		 * frame continues non-stop
		 */
		case ZCRCG:
			return FRAMEOK;
		/*
		 * frame ends
		 */
		case ZCRCE:
			return ENDOFFRAME;
		/*
		 * frame continues; ZACK expected
		 */
		case ZCRCQ:
			if (ack)
				zmodem_send_ack(zm, zm->ack_file_pos);
			return FRAMEOK;
		/*
		 * frame ends; ZACK expected
		 */
		case ZCRCW:
			if (ack)
				zmodem_send_ack(zm, zm->ack_file_pos);
			return ENDOFFRAME;
	}

	lprintf(zm, LOG_WARNING, "%lu INVALID subpacket type: %s (%u bytes)"
	        , (unsigned long)zm->ack_file_pos, chr(subpkt_type), *len);

	return INVALIDSUBPKT;
}

bool zmodem_recv_subpacket(zmodem_t* zm, bool ack, int* type)
{
	int      result;
	unsigned len = 0;

	result = zmodem_recv_data(zm, zm->rx_data_subpacket, sizeof(zm->rx_data_subpacket) - 2, &len, ack, type);
	zm->rx_data_subpacket[len] = 0;
	zm->rx_data_subpacket[len + 1] = 0;
	zm->rx_data_subpacket_size = len;
	if (result != FRAMEOK && result != ENDOFFRAME) {
		lprintf(zm, LOG_DEBUG, "%lu %s ERROR: %s (subpacket type: %s, %u bytes)"
		        , (unsigned long)zm->ack_file_pos, __func__, chr(result), chr(*type), len);
		zmodem_send_znak(zm);
		return false;
	}

	return true;
}

int zmodem_recv_nibble(zmodem_t* zm)
{
	int c;

	c = zmodem_rx(zm);

	if (c < 0)
		return c;

	c = STRIPPED_PARITY(c);
	if (c > '9') {
		if (c < 'a' || c > 'f') {
			/*
			 * illegal hex; different than expected.
			 * we might as well time out.
			 */
			return -1;
		}

		c -= 'a' - 10;
	}
	else {
		if (c < '0') {
			/*
			 * illegal hex; different than expected.
			 * we might as well time out.
			 */
			return -1;
		}
		c -= '0';
	}

	return c;
}

int zmodem_recv_hex(zmodem_t* zm)
{
	int n1;
	int n0;
	int ret;

	n1 = zmodem_recv_nibble(zm);
	if (n1 < 0)
		return n1;
	n0 = zmodem_recv_nibble(zm);
	if (n0 < 0)
		return n0;

	// coverity[overflow:SUPPRESS]
	ret = (n1 << 4) | n0;

//	lprintf(zm,LOG_DEBUG, __func__ " returning: 0x%02X", ret);

	return ret;
}

/*
 * receive routines for each of the six different styles of header.
 * each of these leaves zm->rxd_header_len set to 0 if the end result is
 * not a valid header.
 */

bool zmodem_recv_bin16_header(zmodem_t* zm)
{
	int                c;
	int                n;
	unsigned short int crc;
	uint32_t           rxd_crc;

//	lprintf(zm ,LOG_DEBUG, __func__);

	crc = 0;

	for (n = 0; n < HDRLEN; n++) {
		c = zmodem_rx(zm);
		if (c < 0 || c > 0xff) {
			lprintf(zm, LOG_WARNING, "%lu %s ERROR: %s"
			        , (unsigned long)zm->current_file_pos, __func__, chr(c));
			return false;
		}
		crc = st_transfer_crc16_byte((uint8_t)c, crc);
		zm->rxd_header[n] = (uint8_t)c;
	}

	if (recv_crc_bytes(zm, false, &rxd_crc) != 0) return false;

	if (rxd_crc != crc) {
		lprintf(zm, LOG_WARNING, "%lu %s CRC ERROR: 0x%hX, expected: 0x%hX"
		        , (unsigned long)zm->ack_file_pos, __func__, rxd_crc, crc);
		return false;
	}
//	lprintf(zm,LOG_DEBUG, "%lu %s GOOD CRC: %04hX", __func__
//		,(unsigned long)zm->ack_file_pos, __func__, crc);

	zm->rxd_header_len = n;

	return true;
}

/* Read one byte of the CR/LF sequence that terminates a hex header.  That
   terminator is framing, not data: it is sent unescaped even when ESCCTL has
   been negotiated -- a hex header is built from printable characters plus
   CR/LF precisely so it survives a control-character-hostile link.  So it must
   NOT go through zmodem_rx(), whose ESCCTL filter would drop it as an
   unescaped control character and leave the header unterminated (a receiver
   configured with EscapeCtrlChars would then time out on every hex header).
   Flow-control bytes are skipped here the way zmodem_rx() skips them. */
static int zmodem_recv_hex_eol(zmodem_t* zm)
{
	int c;

	do {
		c = zmodem_recv_raw(zm);
	} while (c == XON || c == (XON | 0x80) || c == XOFF || c == (XOFF | 0x80));
	return c;
}

bool zmodem_recv_hex_header(zmodem_t* zm)
{
	int                c;
	int                i;
	unsigned short int crc = 0;
	unsigned short int rxd_crc;

//	lprintf(zm, LOG_DEBUG, __func__);

	for (i = 0; i < HDRLEN; i++) {
		c = zmodem_recv_hex(zm);
		if (c < 0)
			return false;
		crc = st_transfer_crc16_byte((uint8_t)c, crc);

		zm->rxd_header[i] = (uint8_t)c;
	}

	/*
	 * receive the crc
	 */

	c = zmodem_recv_hex(zm);

	if (c < 0)
		return false;

	rxd_crc = (uint16_t)((unsigned)c << 8);

	c = zmodem_recv_hex(zm);

	if (c < 0)
		return false;

	rxd_crc |= c;

	if (rxd_crc == crc) {
//		lprintf(zm,LOG_DEBUG, "%s GOOD CRC: %04hX", __func__, crc);
		zm->rxd_header_len = i;
	}
	else {
		lprintf(zm, LOG_WARNING, "%s CRC ERROR: 0x%hX, expected: 0x%hX"
		        , __func__, rxd_crc, crc);
		return false;
	}

	/*
	 * drop the end of line sequence after a hex header
	 */
	c = zmodem_recv_hex_eol(zm);
	if (c == '\r' || c == SET_PARITY('\r')) { // CR with Even Parity (Tera Term's ZMODEM sends this)
		/*
		 * both bytes are expected when the first received is a CR
		 */
		c = zmodem_recv_hex_eol(zm);  /* drop LF */
	}
	if (c != '\n' && c != SET_PARITY('\n')) { // LF with Odd Parity
		lprintf(zm, LOG_ERR, "%s HEX header not terminated with LF: %s"
		        , __func__, chr(c));
		return false;
	}

	return true;
}

bool zmodem_recv_bin32_header(zmodem_t* zm)
{
	int      c;
	int      n;
	uint32_t crc;
	uint32_t rxd_crc;

//	lprintf(zm,LOG_DEBUG,"recv_bin32_header");

	crc = 0xffffffffL;

	for (n = 0; n < HDRLEN; n++) {
		c = zmodem_rx(zm);
		if (c < 0 || c > 0xff)
			return false;
		crc = st_transfer_crc32_byte((uint8_t)c, crc);
		zm->rxd_header[n] = (uint8_t)c;
	}

	crc = ~crc;

	if (recv_crc_bytes(zm, true, &rxd_crc) != 0) return false;

	if (rxd_crc != crc) {
		lprintf(zm, LOG_WARNING, "%lu %s CRC ERROR (%08X, expected: %08X)"
		        , (unsigned long)zm->ack_file_pos, __func__, rxd_crc, crc);
		return false;
	}
//	lprintf(zm,LOG_DEBUG, "%lu %s GOOD CRC: %08lX", (unsigned long)zm->ack_file_pos, __func__, crc);

	zm->rxd_header_len = n;
	return true;
}

/*
 * receive any style header
 * a flag (receive_32bit_data) will be set to indicate whether data
 * packets following this header will have 16 or 32 bit data attached.
 * variable headers are not implemented.
 */

int zmodem_recv_header_raw(zmodem_t* zm)
{
	int      c;
	int      type = INVALIDSUBPKT;
	int      frame_type;
	uint64_t freespace;

//	lprintf(zm,LOG_DEBUG, __func__);

	zm->rxd_header_len = 0;

	do {
		do {
			if ((c = zmodem_recv_raw(zm)) < 0)
				return c;
			if (is_cancelled(zm))
				return ZCAN;
		} while (STRIPPED_PARITY(c) != ZPAD);

		if ((c = zmodem_recv_raw(zm)) < 0)
			return c;

		if (STRIPPED_PARITY(c) == ZPAD) {
			if ((c = zmodem_recv_raw(zm)) < 0)
				return c;
		}

		/*
		 * spurious ZPAD check
		 */

		if (STRIPPED_PARITY(c) != ZDLE) {
			lprintf(zm, LOG_DEBUG, "%s Expected ZDLE, received: %s", __func__, chr(c));
			continue;
		}

		/*
		 * now read the header style
		 */

		c = zmodem_rx(zm);

		switch (STRIPPED_PARITY(c)) {
			case ZBIN:
				if (!zmodem_recv_bin16_header(zm))
					return INVHDR;
				zm->receive_32bit_data = false;
				break;
			case ZHEX:
				if (!zmodem_recv_hex_header(zm))
					return INVHDR;
				zm->receive_32bit_data = false;
				break;
			case ZBIN32:
				if (!zmodem_recv_bin32_header(zm))
					return INVHDR;
				zm->receive_32bit_data = true;
				break;
			default:
				if (c < 0) {
					lprintf(zm, LOG_WARNING, "%lu %s ERROR: %s"
					        , (unsigned long)zm->current_file_pos, __func__, chr(c));
					return c;
				}
				/*
				 * unrecognized header style
				 */
				lprintf(zm, LOG_ERR, "%lu %s UNRECOGNIZED header: %s"
				        , (unsigned long)zm->current_file_pos, __func__, chr(c));
				continue;
		}

	} while (zm->rxd_header_len == 0 && !is_cancelled(zm));

	if (is_cancelled(zm))
		return ZCAN;

	/*
	 * this appears to have been a valid header.
	 * return its type.
	 */

	frame_type = zm->rxd_header[0];

	zm->rxd_header_pos = (uint32_t)zm->rxd_header[ZP0] | ((uint32_t)zm->rxd_header[ZP1] << 8) |
	                     ((uint32_t)zm->rxd_header[ZP2] << 16) | ((uint32_t)zm->rxd_header[ZP3] << 24);

	switch (frame_type) {
		case ZCRC:
			zm->crc_request = zm->rxd_header_pos;
			break;
		case ZDATA:
			zm->ack_file_pos = zm->rxd_header_pos;
			break;
		case ZFILE:
			zm->ack_file_pos = 0l;
			if (!zmodem_recv_subpacket(zm, /* ack? */ false, &type)) {
				lprintf(zm, LOG_WARNING, "%s bad %s subpacket: %s", __func__, chr(frame_type), chr(type));
				frame_type |= BADSUBPKT;
			}
			break;
		case ZSINIT:
		case ZCOMMAND:
			if (!zmodem_recv_subpacket(zm, /* ack? */ true, &type)) {
				lprintf(zm, LOG_WARNING, "%s bad %s subpacket: %s", __func__, chr(frame_type), chr(type));
				frame_type |= BADSUBPKT;
			}
			break;
		case ZFREECNT:
			freespace = UINT32_MAX;
			zmodem_send_pos_header(zm, ZACK, (uint32_t)(freespace > UINT32_MAX ? UINT32_MAX : freespace), /* Hex? */ true);
			break;
	}

#if 0 /* def _DEBUG */
	lprintf(zm, LOG_DEBUG, "%s received header type: %s", __func__
	        , frame_desc(frame_type));
#endif

	return frame_type;
}

int zmodem_recv_header(zmodem_t* zm)
{
	int ret;

	switch (ret = zmodem_recv_header_raw(zm)) {
		case TIMEOUT:
			lprintf(zm, LOG_WARNING, "%s TIMEOUT", __func__);
			break;
		case INVHDR:
			lprintf(zm, LOG_WARNING, "%s detected an INVALID HEADER", __func__);
			break;
		default:
			lprintf(zm, LOG_DEBUG, "%lu %s frame: %s"
			        , (unsigned long)frame_pos(zm, ret), __func__, frame_desc(ret));
			if (ret == ZCAN)
				zm->cancelled = true;
			else if (ret == ZRINIT)
				zmodem_parse_zrinit(zm);
			break;
	}

	return ret;
}

bool zmodem_request_crc(zmodem_t* zm, int32_t length)
{
	zmodem_recv_purge(zm, /* timeout: */ 0);
	zmodem_send_pos_header(zm, ZCRC, length, /* HEX: */ true);
	return true;
}

bool zmodem_recv_crc(zmodem_t* zm, uint32_t* crc)
{
	int type;

	if (!is_data_waiting(zm, zm->crc_timeout)) {
		lprintf(zm, LOG_ERR, "%lu %s Timeout waiting for response (%u seconds)"
		        , (unsigned long)zm->current_file_pos, __func__, zm->crc_timeout);
		return false;
	}
	if ((type = zmodem_recv_header(zm)) != ZCRC) {
		lprintf(zm, LOG_ERR, "%lu %s Received %s instead of ZCRC"
		        , (unsigned long)zm->current_file_pos, __func__, frame_desc(type));
		return false;
	}
	if (crc != NULL)
		*crc = zm->crc_request;
	return true;
}

bool zmodem_get_crc(zmodem_t* zm, int32_t length, uint32_t* crc)
{
	if (zmodem_request_crc(zm, length))
		return zmodem_recv_crc(zm, crc);
	return false;
}

void zmodem_parse_zrinit(zmodem_t* zm)
{
	zm->can_full_duplex                 = ((zm->rxd_header[ZF0] & ZF0_CANFDX) != 0);
	zm->can_overlap_io                  = ((zm->rxd_header[ZF0] & ZF0_CANOVIO) != 0);
	zm->can_break                       = ((zm->rxd_header[ZF0] & ZF0_CANBRK) != 0);
	zm->can_fcs_32                      = ((zm->rxd_header[ZF0] & ZF0_CANFC32) != 0);
	zm->escape_ctrl_chars               = ((zm->rxd_header[ZF0] & ZF0_ESCCTL) != 0);
	zmodem_build_rx_plain_tab(zm);
	/* Recorded for the log only: we never escape on a peer's behalf either
	   (no agreed encoding -- see zmodem_send_zrinit()).  lrzsz ignores an
	   ESC8 request the same way. */
	zm->escape_8th_bit                  = ((zm->rxd_header[ZF0] & ZF0_ESC8) != 0);

	lprintf(zm, LOG_INFO, "Receiver requested mode (0x%02X):\r\n"
	        "%s-duplex, %s overlap I/O, CRC-%u, Escape: %s"
	        , zm->rxd_header[ZF0]
	        , zm->can_full_duplex ? "Full" : "Half"
	        , zm->can_overlap_io ? "Can" : "Cannot"
	        , zm->can_fcs_32 ? 32 : 16
	        , zm->escape_ctrl_chars ? "ALL" : "Normal"
	        );

	if ((zm->recv_bufsize = (zm->rxd_header[ZP0] | zm->rxd_header[ZP1] << 8)) != 0)
		lprintf(zm, LOG_INFO, "Receiver specified buffer size of: %u", zm->recv_bufsize);
}

int zmodem_get_zrinit(zmodem_t* zm)
{
	unsigned char zrqinit_header[] = { ZRQINIT, /* ZF3: */ 0, 0, 0, /* ZF0: */ 0 };
	/* Note: sz/dsz/fdsz sends 0x80 in ZF3 because it supports var-length headers. */
	/* We do not, so we send 0x00, resulting in a CRC-16 value of 0x0000 as well. */

	lprintf(zm, LOG_DEBUG, __func__);
	zmodem_send_raw(zm, 'r');
	zmodem_send_raw(zm, 'z');
	zmodem_send_raw(zm, '\r');
	zmodem_send_hex_header(zm, zrqinit_header);

	if (!is_data_waiting(zm, zm->init_timeout))
		return TIMEOUT;
	return zmodem_recv_header(zm);
}

int zmodem_send_zrinit(zmodem_t* zm)
{
	unsigned char zrinit_header[] = { ZRINIT, 0, 0, 0, 0 };

	/* A receiver never parses a ZRINIT, so this is its only chance to pick up
	   an escape_ctrl_chars a consumer set directly after zmodem_init() --
	   sexyz does, from sexyz.ini's EscapeCtrlChars.  The value is final here
	   by definition: the ZRINIT built below is what announces it to the peer. */
	zmodem_build_rx_plain_tab(zm);

	if (zm->can_full_duplex)
		zrinit_header[ZF0] = ZF0_CANFDX;

	if (!zm->no_streaming)
		zrinit_header[ZF0] |= ZF0_CANOVIO;

	if (zm->can_break)
		zrinit_header[ZF0] |= ZF0_CANBRK;

	if (!zm->want_fcs_16)
		zrinit_header[ZF0] |= ZF0_CANFC32;

	if (zm->escape_ctrl_chars)
		zrinit_header[ZF0] |= ZF0_ESCCTL;

	/* ZF0_ESC8 is deliberately never advertised.  We cannot decode an
	   8th-bit-escaped stream, and there is no encoding to implement: the
	   specification names ESC8 once and never says how a high-bit byte is
	   escaped, so the two implementations that exist disagree -- Omen's DSZ
	   prefixes with 0x0E and clears bit 7, zmtx/zmrx 2.04 sends ZDLE and
	   c ^ 0x40.  Asking for the mode commits us to whichever the peer chose.
	   See GitLab issue #1229. */

	if (zm->no_streaming && zm->recv_bufsize == 0)
		zm->recv_bufsize = zm->max_block_size;

	zrinit_header[ZP0] = zm->recv_bufsize & 0xff;
	zrinit_header[ZP1] = (uint8_t)(zm->recv_bufsize >> 8);

	return zmodem_send_hex_header(zm, zrinit_header);
}

/* Returns ZFIN on success */
int zmodem_get_zfin(zmodem_t* zm)
{
	int      result;
	int      type = ZCAN;
	unsigned attempts;

	lprintf(zm, LOG_DEBUG, __func__);

	for (attempts = 0; attempts < zm->max_errors && is_connected(zm) && !is_cancelled(zm); attempts++) {
		if (attempts & 1)  /* Alternate between ZABORT and ZFIN */
			result = zmodem_send_zabort(zm);
		else
			result = zmodem_send_zfin(zm);
		if (result != SEND_SUCCESS)
			return result;
		if ((type = zmodem_recv_header(zm)) == ZFIN)
			break;
	}

	lprintf(zm, LOG_DEBUG, "%s result: %s (attempts=%d)", __func__, frame_desc(type), attempts);

	/*
	 * these Os are formally required; but they don't do a thing
	 * unfortunately many programs require them to exit
	 * (both programs already sent a ZFIN so why bother ?)
	 */

	for (int i = 0; type == ZFIN && i < 2; ++i) {
		if (zmodem_send_raw(zm, 'O') == SEND_SUCCESS)
			lprintf(zm, LOG_DEBUG, "%s sent 'O'", __func__);
		else
			lprintf(zm, LOG_WARNING, "%s failed to send 'O'", __func__);
	}
	zmodem_flush(zm);

	return type;
}

bool zmodem_handle_zrpos(zmodem_t* zm, uint64_t* pos, unsigned purge_timeout)
{
	if (zm->rxd_header_pos <= zm->current_file_size) {
		if (*pos != zm->rxd_header_pos) {
			*pos = zm->rxd_header_pos;
			zm->ack_file_pos = (uint32_t)*pos;
			lprintf(zm, LOG_INFO, "%lu Resuming transfer from offset: %" PRIu64
			        , (unsigned long)zm->current_file_pos, *pos);
		}
		zmodem_recv_purge(zm, purge_timeout);
		return true;
	}
	lprintf(zm, LOG_WARNING, "%lu Received INVALID ZRPOS offset: %u"
	        , (unsigned long)zm->current_file_pos, zm->rxd_header_pos);
	return false;
}

bool zmodem_handle_zack(zmodem_t* zm, uint32_t min, uint32_t max)
{
	if (zm->rxd_header_pos >= min && zm->rxd_header_pos <= max) {
		lprintf(zm, LOG_DEBUG, "%lu Received valid ZACK", (unsigned long)zm->rxd_header_pos);
		zm->ack_file_pos = zm->rxd_header_pos;
		return true;
	}
	lprintf(zm, LOG_WARNING, "%lu Received INVALID ZACK, offset: %u"
	        , (unsigned long)zm->current_file_pos, zm->rxd_header_pos);
	return false;
}

static uint64_t new_window_size(zmodem_t* zm, time_t start, unsigned pos)
{
	time_t   elapsed = time(NULL) - start;
	if (elapsed < 1)
		elapsed = 1;
	uint64_t cps = (uint64_t)(pos / elapsed);
	if (cps < 1)
		cps = 1;
	if (cps > UINT_MAX)
		cps = UINT_MAX;
	if (cps * zm->target_window_size > UINT_MAX)
		cps = UINT_MAX / zm->target_window_size;
	return cps * zm->target_window_size;
}

/*
 * send from the current position in the file
 * all the way to end of file or until something goes wrong.
 * (ZNAK or ZRPOS received)
 * returns ZRINIT on success.
 */

int zmodem_send_from(zmodem_t* zm, FILE* fp, uint64_t pos, uint64_t* sent)
{
	size_t   len;
	uint8_t    tx_type;
	unsigned buf_sent = 0;
	unsigned subpkts_sent = 0;
	unsigned backchannel_wait = 0;
	time_t   start = time(NULL);

	lprintf(zm, LOG_DEBUG, "%lu %s", (unsigned long)pos, __func__);
	if (sent != NULL)
		*sent = 0;

	if (fs_stream_seek(fp, (int64_t)pos) != 0) {
		lprintf(zm, LOG_ERR, "%s ERROR %d seeking to file offset %" PRIu64
		        , __func__, errno, pos);
		zmodem_send_pos_header(zm, ZFERR, (uint32_t)pos, /* Hex? */ true);
		return ZFERR;
	}
	zm->current_file_pos = pos;

	/*
	 * send the data in the file
	 */

	while (is_connected(zm)) {

		/*
		 * characters from the other side
		 * check out that header
		 */
		if (zm->consecutive_errors && backchannel_wait == 0)
			backchannel_wait = 1;
		while (is_data_waiting(zm, backchannel_wait) && !is_cancelled(zm) && is_connected(zm)) {
			int rx_type;
			int c;
			lprintf(zm, LOG_DEBUG, "Back-channel traffic detected");
			if ((c = zmodem_recv_raw(zm)) < 0) {
				lprintf(zm, LOG_ERR, "Back-channel receive ERROR: %s", chr(c));
				return c;
			}
			if (c == ZPAD) {
				rx_type = zmodem_recv_header(zm);
				lprintf(zm, LOG_DEBUG, "Received back-channel data: %s", chr(rx_type));
				if (rx_type == ZACK && zmodem_handle_zack(zm, zm->ack_file_pos, (uint32_t)zm->current_file_pos)) {
					zm->current_window_size = zm->current_file_pos - zm->ack_file_pos;
					lprintf(zm, LOG_DEBUG, "%lu Asynchronous acknowledgment (ZACK) of %lu bytes, new window: %lu"
					        , (unsigned long)zm->current_file_pos, (unsigned long)zm->ack_file_pos, (unsigned long)zm->current_window_size);
					if (zm->max_window_size && zm->target_window_size) {
						zm->max_window_size = new_window_size(zm, start, zm->rxd_header_pos);
						lprintf(zm, LOG_DEBUG, "%lu New window size: %lu (%u seconds of data)"
						        , (unsigned long)zm->current_file_pos, (unsigned long)zm->max_window_size, zm->target_window_size);
					}
					continue;
				}
				else if (rx_type >= 0) {
					zmodem_send_data(zm, ZCRCE, /* data: */ NULL, /* len: */ 0);
					return rx_type;
				}
			} else
				lprintf(zm, LOG_INFO, "Unexpected back-channel data received: %s", chr(c));
		}
		if (is_cancelled(zm))
			return ZCAN;

		/*
		 * read a block from the file
		 */
		pos = zm->current_file_pos;
		if (zm->max_window_size && zm->current_window_size >= zm->max_window_size) {
			lprintf(zm, LOG_WARNING, "%lu Transmit-Window management: %lu >= %lu"
			        , (unsigned long)zm->current_file_pos, (unsigned long)zm->current_window_size, (unsigned long)zm->max_window_size);
			backchannel_wait = 1;
			continue;
		}

		len = fread(zm->tx_data_subpacket, sizeof(uint8_t), zm->block_size, fp);

		if (zm->progress != NULL)
			zm->progress(zm->cbdata, fs_stream_position(fp));

		tx_type = ZCRCW;

		/** ZMODEM.DOC:
			ZCRCW data subpackets expect a response before the next frame is sent.
			If the receiver does not indicate overlapped I/O capability with the
			CANOVIO bit, or sets a buffer size, the sender uses the ZCRCW to allow
			the receiver to write its buffer before sending more data.
		***/
		/* Note: we always use ZCRCW for the first frame */
		if (subpkts_sent || len < zm->block_size) {
			/*  ZMODEM.DOC:
				In the absence of fatal error, the sender eventually encounters end of
				file.  If the end of file is encountered within a frame, the frame is
				closed with a ZCRCE data subpacket which does not elicit a response
				except in case of error.
			*/
			if (len < zm->block_size || zm->consecutive_errors)
				tx_type = ZCRCE;
			else {
				if (zm->can_overlap_io && !zm->no_streaming && (zm->recv_bufsize == 0 || buf_sent + len < zm->recv_bufsize)) {
					if (zm->can_full_duplex && zm->max_window_size) {
						/* A window narrower than 4 blocks has no quarter-window
						   interval to ACK on; clamp to 1 (GitLab #1197). */
						int64_t interval = zm->max_window_size / zm->block_size / 4;
						if (interval < 1)
							interval = 1;
						tx_type = (subpkts_sent % interval) == 0 ? ZCRCQ : ZCRCG;
					}
					else
						tx_type = ZCRCG;
				}
				else    /* Send a ZCRCW frame */
					buf_sent = 0;
			}
		}

		lprintf(zm, LOG_DEBUG, "%lu Sending %s data subpacket (%lu bytes) window: %lu / %lu"
		        , (unsigned long)pos, chr(tx_type), len, (unsigned long)zm->current_window_size, (unsigned long)zm->max_window_size);
		if (zmodem_send_data(zm, tx_type, zm->tx_data_subpacket, len) != SEND_SUCCESS) {
			zm->consecutive_errors++;
			continue;
		}

		zm->current_window_size += len;
		zm->current_file_pos += len;
		if (zm->current_file_pos > zm->current_file_size)
			zm->current_file_size = zm->current_file_pos;
		subpkts_sent++;

		if (zmodem_end_of_frame(tx_type)) {
			lprintf(zm, LOG_DEBUG, "%lu Sent end-of-frame (%s subpacket)", (unsigned long)pos, chr(tx_type));
			if (tx_type == ZCRCW) {  /* ZACK expected */
				lprintf(zm, LOG_DEBUG, "%lu Waiting for ZACK", (unsigned long)pos);
				while (is_connected(zm)) {
					int ack;
					if ((ack = zmodem_recv_header(zm)) != ZACK)
						return ack;

					if (is_cancelled(zm))
						return ZCAN;

					if (zmodem_handle_zack(zm, (uint32_t)zm->current_file_pos, (uint32_t)zm->current_file_pos)) {
						zm->current_window_size = 0;
						break;
					}
				}
			}
		}

		if (sent != NULL)
			*sent += len;

		buf_sent += len;

		if (len < zm->block_size) {
			lprintf(zm, LOG_DEBUG, "%lu End of file (or read error) reached", (unsigned long)zm->current_file_pos);
			zmodem_send_zeof(zm);
			return zmodem_recv_header(zm);  /* If this is ZRINIT, Success */
		}
		backchannel_wait = 0;

		zm->consecutive_errors = 0;

		if (zm->block_size < zm->max_block_size) {
			zm->block_size *= 2;
			if (zm->block_size > zm->max_block_size)
				zm->block_size = zm->max_block_size;
		}
	}

	lprintf(zm, LOG_WARNING, "%s Returning unexpectedly!", __func__);

	/*
	 * end of file reached.
	 * should receive something... so fake ZACK
	 */

	return ZACK;
}

/*
 * send a file; returns true when session is successful. (or file is skipped)
 */

bool zmodem_send_file(zmodem_t* zm, char* fname, FILE* fp, bool request_init, time_t* start, uint64_t* sent)
{
	uint64_t        pos = 0;
	uint64_t        sent_bytes;
	struct fs_info s;
	unsigned char * p;
	uint8_t           zfile_frame[] = { ZFILE, 0, 0, 0, 0 };
	int             type;
	int             i;
	unsigned        attempts;

	if (zm->block_size == 0)
		zm->block_size = ZBLOCKLEN;

	if (zm->block_size < 128)
		zm->block_size = 128;

	if (zm->block_size > sizeof(zm->tx_data_subpacket))
		zm->block_size = sizeof(zm->tx_data_subpacket);

	if (zm->max_block_size < zm->block_size)
		zm->max_block_size = zm->block_size;

	if (zm->max_block_size > sizeof(zm->rx_data_subpacket))
		zm->max_block_size = sizeof(zm->rx_data_subpacket);

	if (sent != NULL)
		*sent = 0;

	if (start != NULL)
		*start = time(NULL);

	zm->file_skipped = false;

	if (zm->no_streaming)
		lprintf(zm, LOG_WARNING, "Streaming disabled");

	if (request_init) {
		for (zm->errors = 0; zm->errors <= zm->max_errors && !is_cancelled(zm) && is_connected(zm); zm->errors++) {
			if (zm->errors)
				lprintf(zm, LOG_NOTICE, "Sending ZRQINIT (%u of %u)"
				        , zm->errors + 1, zm->max_errors + 1);
			else
				lprintf(zm, LOG_INFO, "Sending ZRQINIT");
			i = zmodem_get_zrinit(zm);
			if (i == ZRINIT)
				break;
			lprintf(zm, LOG_WARNING, "%s UNEXPECTED %s received instead of ZRINIT"
			        , __func__, frame_desc(i));
		}
		if (zm->errors >= zm->max_errors || is_cancelled(zm) || !is_connected(zm))
			return false;
	}

	if (fs_get_info(&s, fname) != 0 || fs_stream_size(fp) < 0)
		return false;
	zm->current_file_size = fs_stream_size(fp);
	strlcpy(zm->current_file_name, path_untrusted_basename(fname), sizeof(zm->current_file_name));

	/*
	 * the file exists. now build the ZFILE frame
	 */

	/*
	 * set conversion option
	 * (not used; always binary)
	 */

	zfile_frame[ZF0] = ZF0_ZCBIN;

	/*
	 * management option
	 */

	if (zm->management_protect) {
		zfile_frame[ZF1] = ZF1_ZMPROT;
		lprintf(zm, LOG_DEBUG, "protecting destination");
	}
	else if (zm->management_clobber) {
		zfile_frame[ZF1] = ZF1_ZMCLOB;
		lprintf(zm, LOG_DEBUG, "overwriting destination");
	}
	else if (zm->management_newer) {
		zfile_frame[ZF1] = ZF1_ZMNEW;
		lprintf(zm, LOG_DEBUG, "overwriting destination if newer");
	}
	else
		zfile_frame[ZF1] = ZF1_ZMCRC;

	/*
	 * transport options
	 * (just plain normal transfer)
	 */

	zfile_frame[ZF2] = ZF2_ZTNOR;

	/*
	 * extended options
	 */

	zfile_frame[ZF3] = 0;

	/*
	 * now build the data subpacket with the file name and lots of other
	 * useful information.
	 */

	/*
	 * first enter the name and a 0
	 */

	p = zm->tx_data_subpacket;

	const char *name = path_untrusted_basename(fname);
	size_t name_size = strlen(name) + 1;
	if (name_size >= sizeof(zm->tx_data_subpacket)) return false;
	memcpy(p, name, name_size);
	p += name_size;
	size_t capacity = sizeof(zm->tx_data_subpacket) - name_size;
	int metadata = snprintf((char *)p, capacity, "%" PRId64 " %" PRIoMAX " 0 0 %u %" PRId64 " 0",
	    zm->current_file_size, (uintmax_t)s.modified_time,
	    zm->files_remaining, zm->bytes_remaining);
	if (metadata < 0 || (size_t)metadata >= capacity) return false;
	p += (size_t)metadata + 1;

	for (type = INVALIDSUBPKT, attempts = 0; type != ZRPOS; attempts++) {
		bool stale_zrinit = false;

		if (attempts >= zm->max_errors)
			return false;

		/*
		 * send the header and the data
		 */

		lprintf(zm, LOG_DEBUG, "Sending ZFILE frame: '%s'"
		        , zm->tx_data_subpacket + strlen((char*)zm->tx_data_subpacket) + 1);

		if ((i = zmodem_send_bin_header(zm, zfile_frame)) != SEND_SUCCESS) {
			lprintf(zm, LOG_DEBUG, "zmodem_send_bin_header returned %d", i);
			continue;
		}
		if ((i = zmodem_send_data_subpkt(zm, ZCRCW, zm->tx_data_subpacket, p - zm->tx_data_subpacket)) != SEND_SUCCESS) {
			lprintf(zm, LOG_DEBUG, "zmodem_send_data_subpkt returned %d", i);
			continue;
		}
		/*
		 * wait for anything but an ZACK packet
		 */

		while (is_connected(zm)) {
			type = zmodem_recv_header(zm);
			if (is_cancelled(zm))
				return false;
			if (type == ZACK)
				continue;
			/* A ZRINIT here answers our ZRQINIT, not our ZFILE */
			if (type == ZRINIT && !stale_zrinit) {
				stale_zrinit = true;
				continue;
			}
			break;
		}

		if (!is_connected(zm))
			return false;

#if 0
		lprintf(zm, LOG_INFO, "type : %d", type);
#endif

		if (type == ZCRC) {
			if (zm->crc_request == 0)
				lprintf(zm, LOG_NOTICE, "Receiver requested CRC of entire file");
			else
				lprintf(zm, LOG_NOTICE, "Receiver requested CRC of first %u bytes of file"
				        , zm->crc_request);
			zmodem_send_pos_header(zm, ZCRC, st_transfer_file_crc32(fp, zm->crc_request), /* Hex: */ true);
			type = zmodem_recv_header(zm);
		}

		if (type == ZSKIP) {
			zm->file_skipped = true;
			lprintf(zm, LOG_WARNING, "File skipped by receiver");
			return true;
		}
	}

	if (!zmodem_handle_zrpos(zm, &pos, /* timeout: */ 0))
		return false;

	zm->transfer_start_pos = pos;
	zm->transfer_start_time = time(NULL);

	if (start != NULL)
		*start = zm->transfer_start_time;

	rewind(fp);
	zm->errors = 0;
	zm->consecutive_errors = 0;

	lprintf(zm, LOG_DEBUG, "%lu Sending %s", (unsigned long)pos, fname);
	do {
		/*
		 * and start sending
		 */

		type = zmodem_send_from(zm, fp, pos, &sent_bytes);

		if (!is_connected(zm))
			return false;

		if (type == ZFERR || type == ZABORT || is_cancelled(zm)) {
			lprintf(zm, LOG_WARNING, "Aborting receive");
			break;
		}

		if (type == ZSKIP) {
			zm->file_skipped = true;
			lprintf(zm, LOG_WARNING, "File skipped by receiver at offset: %" PRIu64, pos + sent_bytes);
			/* ZOC sends a ZRINIT after mid-file ZSKIP, so consume the ZRINIT here */
			zmodem_recv_header(zm);
			return true;
		}

		if (sent != NULL)
			*sent += sent_bytes;

		if (type == ZRINIT)
			return true;  /* Success */

		pos = zm->ack_file_pos;

		if (type == ZACK && zmodem_handle_zack(zm, (uint32_t)pos, (uint32_t)zm->current_file_pos)) {
			continue;
		}

		/* Error of some kind */

		zm->errors++;
		lprintf(zm, LOG_ERR, "%lu ERROR #%d: %s", (unsigned long)zm->current_file_pos, zm->errors, chr(type));

		if (zm->block_size == zm->max_block_size && zm->max_block_size > ZBLOCKLEN)
			zm->max_block_size /= 2;

		if (zm->block_size > 128)
			zm->block_size /= 2;

		if (++zm->consecutive_errors > zm->max_errors) {
			lprintf(zm, LOG_WARNING, "Too many consecutive errors: %u (%u total)"
			        , zm->consecutive_errors, zm->errors);
			break;  /* failure */
		}

		if (type == ZRPOS) {
			if (!zmodem_handle_zrpos(zm, &pos, /* timeout: */ 1))
				break;
		}
	} while (true);

	lprintf(zm, LOG_WARNING, "Transfer failed on receipt of: %s", chr(type));

	return false;
}

/* Returns number of successfully-received files */
int zmodem_recv_init(zmodem_t* zm)
{
	int      type = CAN;
	unsigned errors;

	lprintf(zm, LOG_DEBUG, __func__);

#if 0
	while (is_connected(zm) && !is_cancelled(zm) && (ch = zm->recv_byte(zm, 0)) != NOINP)
		lprintf(zm, LOG_DEBUG, "%s Throwing out received: %s", __func__, chr((uint8_t)ch));
#endif

	for (errors = 0; errors <= zm->max_errors && !is_cancelled(zm) && is_connected(zm); errors++) {
		if (errors)
			lprintf(zm, LOG_NOTICE, "Sending ZRINIT (%u of %u)"
			        , errors + 1, zm->max_errors + 1);
		else
			lprintf(zm, LOG_INFO, "Sending ZRINIT");
		zmodem_send_zrinit(zm);

		type = zmodem_recv_header(zm);

		if (zm->local_abort)
			break;

		if (type == TIMEOUT)
			continue;

		lprintf(zm, LOG_DEBUG, "%s received %s", __func__, chr(type));

		if (type == ZFILE) {
			if (memchr(zm->rx_data_subpacket, 0, zm->rx_data_subpacket_size) == NULL) return BADSUBPKT;
			zmodem_parse_zfile_subpacket(zm);
			return type;
		}

		if (type == ZFIN) {
			zmodem_send_zfin(zm);   /* ACK */
			return type;
		}

		lprintf(zm, LOG_WARNING, "%s received %s instead of ZFILE or ZFIN"
		        , __func__, frame_desc(type));
		lprintf(zm, LOG_DEBUG, "ZF0=%02X ZF1=%02X ZF2=%02X ZF3=%02X"
		        , zm->rxd_header[ZF0], zm->rxd_header[ZF1], zm->rxd_header[ZF2], zm->rxd_header[ZF3]);
	}

	return type;
}

void zmodem_parse_zfile_subpacket(zmodem_t* zm)
{
	int   i;
	unsigned mode = 0;
	unsigned long serial = 0;
	uintmax_t tmptime = 0;

	/* Keep the original name until the destination policy has validated it. */
	if (strlcpy(zm->current_file_name, (char*)zm->rx_data_subpacket, sizeof(zm->current_file_name)) >= sizeof(zm->current_file_name))
		zm->current_file_name[0] = '\0';

	zm->current_file_size = 0;
	zm->current_file_time = 0;
	zm->files_remaining = 0;
	zm->bytes_remaining = 0;

	i = sscanf((char*)zm->rx_data_subpacket + strlen((char*)zm->rx_data_subpacket) + 1, "%" SCNd64 " %" SCNoMAX " %o %lo %u %" SCNd64
	           , &zm->current_file_size /* file size (decimal) */
	           , &tmptime       /* file time (octal unix format) */
	           , &mode          /* file mode */
	           , &serial        /* program serial number */
	           , &zm->files_remaining /* remaining files to be sent */
	           , &zm->bytes_remaining /* remaining bytes to be sent */
	           );
	zm->current_file_time = tmptime;

	lprintf(zm, LOG_DEBUG, "ZMODEM file (ZFILE) data (%u fields): %s"
	        , i, zm->rx_data_subpacket + strlen((char*)zm->rx_data_subpacket) + 1);

	if (!zm->files_remaining)
		zm->files_remaining = 1;
	if (!zm->bytes_remaining)
		zm->bytes_remaining = zm->current_file_size;

	if (!zm->total_files)
		zm->total_files = zm->files_remaining;
	if (!zm->total_bytes)
		zm->total_bytes = zm->bytes_remaining;
}

bool zmodem_waits_for_ack(int type)
{
	return type == ZCRCW;
}

/*
 * receive file data until the end of the file or until something goes wrong.
 * returns error count (non-zero does not mean failure).
 */

unsigned zmodem_recv_file_data(zmodem_t* zm, FILE* fp, int64_t offset)
{
	zm->file_complete = false;
	int      type = INVALIDSUBPKT;      // data subpacket type
	unsigned errors = 0;
	int64_t    pos = (int64_t)offset;
	int64_t    error_pos = -1;            /* file position of the last error */

	zm->current_file_pos = pos;
	zm->transfer_start_pos = offset;
	zm->transfer_start_time = time(NULL);

	if (fs_stream_seek(fp, pos) != 0) {
		lprintf(zm, LOG_ERR, "%s ERROR %d seeking to file offset %" PRId64
		        , __func__, errno, offset);
		zmodem_send_pos_header(zm, ZFERR, (uint32_t)pos, /* Hex? */ true);
		return 1; /* errors */
	}
	zmodem_send_pos_header(zm, ZRPOS, (uint32_t)pos, /* Hex? */ true);

	/*  zmodem.doc:

		The zmodem receiver uses the file length [from ZFILE data] as an estimate only.
		It may be used to display an estimate of the transmission time,
		and may be compared with the amount of free disk space.  The
		actual length of the received file is determined by the data
		transfer. A file may grow after transmission commences, and
		all the data will be sent.
	*/
	while (is_connected(zm) && !is_cancelled(zm)) {

		if (pos > zm->current_file_size)
			zm->current_file_size = pos;

		if (zm->max_file_size != 0 && pos >= zm->max_file_size) {
			lprintf(zm, LOG_ERR, "%lu Specified maximum file size (%" PRId64 " bytes) reached"
			        , (unsigned long)pos, zm->max_file_size);
			zmodem_send_pos_header(zm, ZFERR, (uint32_t)pos, /* Hex? */ true);
			++errors;
			break;
		}

		int result = zmodem_recv_file_frame(zm, fp, &type);
		pos = fs_stream_position(fp);
		if (pos > error_pos)
			errors = 0;                 /* progressed since the last error */
		if (result == ENDOFFRAME) {
			lprintf(zm, LOG_DEBUG, "%lu Complete data frame received (type: %s)", (unsigned long)pos, chr(type));
			continue;
		}
		if (result == ZEOF || result == ZFIN) {
			zm->file_complete = result == ZEOF;
			lprintf(zm, LOG_INFO, "%lu Received: %s", (unsigned long)pos, chr(result));
			break;
		}
		errors++;
		error_pos = pos;
		lprintf(zm, LOG_WARNING, "%lu ERROR #%d: %s (type: %s)", (unsigned long)pos, errors, chr(result), chr(type));
		if (errors > zm->max_errors) {
			lprintf(zm, LOG_ERR, "%lu Maximum errors (%u) exceeded", (unsigned long)pos, zm->max_errors);
			break;
		}
		if (zmodem_waits_for_ack(type))
			zmodem_recv_purge(zm, /* timeout: */ 1);
		lprintf(zm, LOG_NOTICE, "%lu Repositioning sender (sending ZRPOS)", (unsigned long)pos);
		zmodem_send_pos_header(zm, ZRPOS, (uint32_t)pos, /* Hex? */ true);
	}

	return errors;
}


int zmodem_recv_file_frame(zmodem_t* zm, FILE* fp, int* type)
{
	int      result;
	unsigned attempt;

	/*
	 * wait for a ZDATA header with the right file offset
	 * or a timeout or a ZFIN
	 */

	for (attempt = 0;; attempt++) {
		if (attempt >= zm->max_errors)
			return TIMEOUT;
		result = zmodem_recv_header(zm);
		switch (result) {
			case ZEOF:
				/* ZMODEM.DOC:
				   If the receiver has not received all the bytes of the file,
				   the receiver ignores the ZEOF because a new ZDATA is coming.
				*/
				if (zm->rxd_header_pos == (uint32_t)fs_stream_position(fp))
					return result;
				lprintf(zm, LOG_WARNING, "Ignoring ZEOF as all bytes (%u) have not been received"
				        , zm->rxd_header_pos);
				continue;
			case ZFIN:
			case TIMEOUT:
				return result;
		}
		if (is_cancelled(zm) || !is_connected(zm))
			return ZCAN;

		if (result == ZDATA)
			break;

		lprintf(zm, LOG_WARNING, "%lu Received %s instead of ZDATA frame", (unsigned long)fs_stream_position(fp), frame_desc(result));
	}

	if (zm->rxd_header_pos != (uint32_t)fs_stream_position(fp)) {
		lprintf(zm, LOG_WARNING, "%lu Received ZDATA frame with invalid position: %lu"
		        , (unsigned long)fs_stream_position(fp), (unsigned long)zm->rxd_header_pos);
		return false;
	}

	do {
		unsigned len = 0;
		result = zmodem_recv_data(zm, zm->rx_data_subpacket, sizeof(zm->rx_data_subpacket), &len, /* ack */ true, type);

/*		fprintf(stderr,"packet len %d type %d\n",n,type);
*/
		if (result == ENDOFFRAME || result == FRAMEOK) {
			if (fwrite(zm->rx_data_subpacket, sizeof(uint8_t), len, fp) != len) {
				lprintf(zm, LOG_ERR, "ERROR %d writing %u bytes at file offset %" PRIu64
				        , errno, len, (uint64_t)fs_stream_position(fp));
				zmodem_send_pos_header(zm, ZFERR, (uint32_t)fs_stream_position(fp), /* Hex? */ true);
				return false;
			}
			if (zm->received != NULL && zm->received(zm->cbdata, zm->rx_data_subpacket, len) != 0)
				return ZCAN;
		}

		if (result == FRAMEOK)
			zm->block_size = len;

		zm->current_file_pos = fs_stream_position(fp);

		if (zm->progress != NULL)
			zm->progress(zm->cbdata, fs_stream_position(fp));

		if (is_cancelled(zm))
			return ZCAN;

	} while (result == FRAMEOK);

	return result;
}

void zmodem_init(zmodem_t* zm, void* cbdata
                 , int (*lputs)(void*, int level, const char* str)
                 , void (*progress)(void* unused, int64_t)
                 , int (*send_byte)(void*, uint8_t ch, unsigned timeout /* seconds */)
                 , int (*recv_byte)(void*, unsigned timeout /* seconds */)
                 , bool (*is_connected)(void*)
                 , bool (*is_cancelled)(void*)
                 , bool (*data_waiting)(void*, unsigned timeout /* seconds */)
                 , void (*flush)(void*))
{
	memset(zm, 0, sizeof(zmodem_t));

	/* Use sane default values */
	zm->init_timeout = 10;        /* seconds */
	zm->send_timeout = 10;        /* seconds (reduced from 15) */
	zm->recv_timeout = 10;        /* seconds (reduced from 20) */
	zm->crc_timeout = 120;        /* seconds */
	zm->block_size = ZBLOCKLEN;
	zm->max_block_size = ZBLOCKLEN;
	zm->max_errors = 9;
	zm->can_full_duplex = true;
	zmodem_build_rx_plain_tab(zm);

	zm->cbdata = cbdata;
	zm->lputs = lputs;
	zm->progress = progress;
	zm->send_byte = send_byte;
	zm->recv_byte = recv_byte;
	zm->is_connected = is_connected;
	zm->is_cancelled = is_cancelled;
	zm->data_waiting = data_waiting;
	zm->flush = flush;
}
