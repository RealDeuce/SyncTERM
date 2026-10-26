/* CET telesoftware parser extracted from SyncTERM term.c. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "transfer_internal.h"
#include <stddef.h>

struct cet_ts_state {
	int (*recv_byte)(void *, unsigned);
	st_transfer_t *transfer;
	uint8_t *frame_buffer;
	size_t fb_pos;
	size_t fb_sz;
	int16_t shift;
	bool aborted;
};

enum { CET_TS_BLOCK_MAX = 4096 };
struct cet_ts_block {
	size_t  sz;
	size_t  length;
	uint8_t frame;
	uint8_t block_num;
	uint8_t block_cnt;
	uint8_t ends_file;
	char    data[CET_TS_BLOCK_MAX + 1];
};

static int
cet_frame_recv_byte(void *context, unsigned timeout)
{
	struct cet_ts_state *sp = context;
	return st_transfer_recv_ms(sp->transfer, timeout);
}

#define CET_TS_TIMEOUT_SEC 5
#define CET_TS_TIMEOUT_MS (CET_TS_TIMEOUT_SEC * 1000)
#define CET_TS_RETRIES 3

static bool
cet_send_string(struct cet_ts_state *sp, const char *str)
{
	for (;*str;str++) {
		if (st_transfer_send_byte(sp->transfer, (uint8_t)*str, CET_TS_TIMEOUT_SEC))
			return false;
	}
	st_transfer_flush(sp->transfer);
	return true;
}

static inline void
cet_append_fb(struct cet_ts_state *sp, int byte)
{
	if (byte < 0 || byte > 255) return;
	if (byte == 12) {	// Clear screen
		sp->fb_pos = 0;
	}
	if (sp->fb_pos == 65536) { sp->aborted = true; return; }
	if (sp->fb_pos == sp->fb_sz) {
		size_t newsz = sp->fb_sz ? sp->fb_sz * 2 : 1000;
		void *ra = realloc(sp->frame_buffer, newsz);
		if (!ra) { sp->aborted = true; return; }
		sp->frame_buffer = ra;
		sp->fb_sz = newsz;
	}
	sp->frame_buffer[sp->fb_pos++] = (uint8_t)byte;
}

static struct cet_ts_block *
cet_telesoftware_try_get_block(struct cet_ts_state *sp)
{
	size_t max_len = CET_TS_BLOCK_MAX;
	size_t sz = sizeof(struct cet_ts_block);
	struct cet_ts_block *ret = malloc(sz);
	if (ret == NULL)
		return NULL;
	ret->sz = sz;
	ret->length = 0;
	ret->frame = 'A';
	ret->block_num = 0;
	ret->block_cnt = 0;
	ret->ends_file = false;
	uint8_t checkxor = 0;
	bool got_start = false;
	bool got_escape = false;
	bool got_block_end = false;
	bool need_term = false;
	int16_t shift = sp->shift;

	while ((!sp->aborted) && (ret->length < max_len) && (!got_block_end)) {
		bool xor = got_start;
		sp->aborted = sp->aborted || st_transfer_cancelled(sp->transfer);
		if (sp->aborted) {
			free(ret);
			st_transfer_protocol_log(sp->transfer, LOG_INFO, "Aborted by user");
			return NULL;
		}
		int b = sp->recv_byte(sp, CET_TS_TIMEOUT_MS);
		if (b < 0 || b > 255) {
			free(ret);
			st_transfer_log_message(sp->transfer, LOG_ERR, "Failed to receive byte (timeout %d)", b);
			return NULL;
		}
		cet_append_fb(sp, b);
		if (got_start && (b > 127 || b < 32)) {
			free(ret);
			st_transfer_log_message(sp->transfer, LOG_ERR, "Received illegal byte 0x%02x (%s)", b, b < 32 ? "control character" : "high bit set");
			return NULL;
		}
		if (got_escape) {
			if (!got_start && b != 'A') {
				continue;
			}
			bool allowed_after_end = false;
			got_escape = false;
			if (need_term) {
				if (b == 'I') {
					allowed_after_end = true;
					need_term = false;
				}
			}
			else {
				int b1, b2, b3;
				switch (b) {
					case '0':
						shift = 0;
						break;
					case '1':
						shift = -64;
						break;
					case '2':
						shift = 64;
						break;
					case '3':
						shift = 96;
						break;
					case '4':
						shift = 128;
						break;
					case '5':
						shift = 160;
						break;
					case 'A':
						if (got_start) {
							free(ret);
							st_transfer_protocol_log(sp->transfer, LOG_ERR, "|A Inside block");
							return NULL;
						}
						got_start = true;
						break;
					case 'E':
						ret->data[ret->length++] = '|';
						break;
					case 'F':
						if (ret->ends_file) {
							free(ret);
							st_transfer_protocol_log(sp->transfer, LOG_ERR, "Multiple |F in file");
							return NULL;
						}
						ret->ends_file = true;
						allowed_after_end = true;
						break;
					case 'G':
						if (ret->frame != 'A') {
							free(ret);
							st_transfer_protocol_log(sp->transfer, LOG_ERR, "Multiple |G in block");
							return NULL;
						}
						// Read extra bytes up to | or four, then set got_escape
						need_term = true;
						b1 = sp->recv_byte(sp, CET_TS_TIMEOUT_MS);
						cet_append_fb(sp, b1);
						if (b1 < 'a' || b1 > 'z') {
							free(ret);
							st_transfer_log_message(sp->transfer, LOG_ERR, "Invalid |G frame byte 0x%02x%s", b1, b1 == -1 ? " (timeout)" : "");
							return NULL;
						}
						checkxor ^= b1;
						ret->frame = (uint8_t)b1;
						b1 = sp->recv_byte(sp, CET_TS_TIMEOUT_MS);
						cet_append_fb(sp, b1);
						if (((b1 < '0') || (b1 > '9')) && (b1 != '|')) {
							free(ret);
							st_transfer_log_message(sp->transfer, LOG_ERR, "Invalid |G block id byte 0x%02x%s", b1, b1 == -1 ? " (timeout)" : "");
							return NULL;
						}
						if (b1 == '|') {
							got_escape = true;
						}
						else {
							checkxor ^= b1;
							ret->block_num = (uint8_t)(b1 - '0');
							b1 = sp->recv_byte(sp, CET_TS_TIMEOUT_MS);
							cet_append_fb(sp, b1);
							if ((b1 < '0') || (b1 > '9')) {
								free(ret);
								st_transfer_log_message(sp->transfer, LOG_ERR, "Invalid |G block id byte 0x%02x%s", b1, b1 == -1 ? " (timeout)" : "");
								return NULL;
							}
							ret->block_cnt = (uint8_t)(b1 - '0');
							checkxor ^= b1;
						}
						break;
					case 'I':
						free(ret);
						st_transfer_protocol_log(sp->transfer, LOG_ERR, "Got unneeded |I");
						return NULL;
					case 'L':
#if SYNCTERM_PLATFORM_WINDOWS
						ret->data[ret->length++] = '\r';
#endif
						ret->data[ret->length++] = '\n';
						break;
					case 'Z':
						allowed_after_end = true;
						b1 = sp->recv_byte(sp, CET_TS_TIMEOUT_MS);
						cet_append_fb(sp, b1);
						if (b1 < '0' || b1 > '9') {
							free(ret);
							st_transfer_log_message(sp->transfer, LOG_ERR, "Invalid checkxor byte 0x%02x%s", b1, b1 == -1 ? " (timeout)" : "");
							return NULL;
						}
						b2 = sp->recv_byte(sp, CET_TS_TIMEOUT_MS);
						cet_append_fb(sp, b2);
						if (b2 < '0' || b2 > '9') {
							free(ret);
							st_transfer_log_message(sp->transfer, LOG_ERR, "Invalid checkxor byte 0x%02x%s", b2, b2 == -1 ? " (timeout)" : "");
							return NULL;
						}
						b3 = sp->recv_byte(sp, CET_TS_TIMEOUT_MS);
						cet_append_fb(sp, b3);
						if (b3 < '0' || b3 > '9') {
							free(ret);
							st_transfer_log_message(sp->transfer, LOG_ERR, "Invalid checkxor byte 0x%02x%s", b3, b3 == -1 ? " (timeout)" : "");
							return NULL;
						}
						unsigned checked = (unsigned)(b1 - '0') * 100 + (b2 - '0') * 10 + (b3 - '0');
						if (checked != checkxor) {
							free(ret);
							st_transfer_log_message(sp->transfer, LOG_ERR, "Incorrect checkxor. Calculated 0x%02x, Remote indicated 0x%02x", checkxor, checked);
							return NULL;
						}
						got_block_end = true;
						break;
					case '}': // ie: ¾
						ret->data[ret->length++] = '}';
						break;
					// We don't really handle these for now...
					default:
						allowed_after_end = true;
						// Fallthrough
					case 'D':
					case 'T':
						st_transfer_log_message(sp->transfer, LOG_WARNING, "Unhandled escape |%c", b);
						need_term = true;
						break;
				}
			}
			if (ret->ends_file && !allowed_after_end) {
				free(ret);
				st_transfer_log_message(sp->transfer, LOG_ERR, "Received |%c after |F", b);
				return NULL;
			}
			if (xor) {
				checkxor ^= '|';
				checkxor ^= (b & 0x7F);
			}
		}
		else {
			if (!got_start && b != '|') {
				continue;
			}
			switch (b) {
				case '|':
					got_escape = true;
					break;
				default: {
					uint8_t nb = (uint8_t)(b + shift);
					if (shift && b < 64) {
						free(ret);
						st_transfer_log_message(sp->transfer, LOG_ERR, "Illegal encoding, shift of %d with byte of %d", shift, b);
						return NULL;
					}
					if (b == '}')
						ret->data[ret->length++] = ' ';
					else
						ret->data[ret->length++] = nb;
					if (xor) {
						checkxor ^= (b & 0x7F);
					}
				}
			}
		}
	}
	if (!got_block_end || need_term || sp->aborted) {
		free(ret);
		st_transfer_protocol_log(sp->transfer, LOG_ERR, "No terminating |I found");
		return NULL;
	}
	if (ret->length == max_len) {
		free(ret);
		st_transfer_protocol_log(sp->transfer, LOG_ERR, "No terminating |Z found");
		return NULL;
	}
	sp->shift = shift;
	return ret;
}

static struct cet_ts_block *
cet_telesoftware_get_block(struct cet_ts_state *sp)
{
	int retries = CET_TS_RETRIES;
	struct cet_ts_block *ret = NULL;
	while ((!sp->aborted) && ret == NULL) {
		ret = cet_telesoftware_try_get_block(sp);
		if (ret)
			return ret;
		if (retries == 0)
			break;
		retries--;
		cet_send_string(sp, "*00");
	}
	if (!sp->aborted)
		st_transfer_protocol_log(sp->transfer, LOG_ERR, "Too many retries, aborting");
	return NULL;
}

int st_transfer_run_cet(st_transfer_t *t)
{
	struct cet_ts_state state = {.transfer = t, .recv_byte = cet_frame_recv_byte};
	struct cet_ts_block *header = cet_telesoftware_get_block(&state), *block = NULL;
	FILE *file = NULL;
	char *path = NULL;
	int status = IO_ERROR_PROTOCOL;
	if (header == NULL) goto finished;
	header->data[header->length] = 0;
	char *separator = strchr(header->data, '\n');
	if (separator == NULL || separator == header->data) goto finished;
	*separator++ = 0;
	size_t length = strlen(header->data);
	if (length != 0 && header->data[length - 1] == '\r') header->data[length - 1] = 0;
	char *end;
	long frames = strtol(separator, &end, 10);
	if (end == separator || frames < 1 || frames > 999) goto finished;
	status = st_transfer_destination(t, header->data, &path, false);
	if (status != 0) { if (status > 0) status = IO_ERROR_CANCELLED; goto finished; }
	status = fs_open(&file, path, "wb");
	if (status != 0) goto finished;
	strlcpy(t->current_name, path_basename(path), sizeof(t->current_name));
	uint8_t next_frame = 'A', next_block = 0;
	unsigned retries = 0;
	int64_t received = 0;
	status = IO_ERROR_PROTOCOL;
	while (!st_transfer_cancelled(t) && !state.aborted) {
		if (next_block == 0 && next_frame == 'z' + 1) {
			next_frame = 'a';
			if (!cet_send_string(&state, "0")) break;
		} else if (next_block == 0 && !cet_send_string(&state, "_")) break;
		block = cet_telesoftware_get_block(&state);
		if (block == NULL) break;
		if (block->frame != 'A') {
			if (next_frame == 'A') next_frame = block->frame;
			if (block->frame != next_frame) {
				if (++retries > 3) break;
				bool retry = block->frame < next_frame || next_frame == 'a';
				if (!retry || !cet_send_string(&state, next_frame == 'a' ? "0" : "_")) break;
				free(block); block = NULL; continue;
			}
			if (block->block_num != next_block || block->block_cnt < block->block_num) break;
		}
		retries = 0;
		if (fwrite(block->data, 1, block->length, file) != block->length) { status = IO_ERROR_IO; break; }
		received += (int64_t)block->length;
		st_transfer_progress(t, received, 0);
		if (block->block_num == block->block_cnt && frames != 999 && frames > 0) --frames;
		(void)st_mutex_lock(&t->mutex);
		snprintf(t->snapshot.line4, sizeof(t->snapshot.line4), "Frames remaining: %ld", frames);
		(void)st_mutex_unlock(&t->mutex);
		if (block->ends_file) { status = frames == 0 || frames == 999 ? 0 : IO_ERROR_PROTOCOL; break; }
		if (frames == 0) { st_transfer_log_message(t, LOG_WARNING, "Frame count exhausted without end-of-file marker"); frames = 999; }
		if (block->frame == 'A') { next_frame = 'a'; next_block = 0; }
		else if (block->block_cnt == block->block_num) { ++next_frame; next_block = 0; }
		else ++next_block;
		free(block); block = NULL;
	}
finished:
	if (file != NULL && fclose(file) != 0) status = IO_ERROR_IO;
	free(header); free(block); free(path);
	t->replay = state.frame_buffer; t->replay_size = state.fb_pos;
	return status;
}
