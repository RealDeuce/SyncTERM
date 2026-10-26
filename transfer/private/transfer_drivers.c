/* Protocol orchestration and receive-file policy. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "transfer_internal.h"

static void xy_progress(void *context, unsigned block, int64_t offset, int64_t size, time_t start)
{
	(void)block; (void)start;
	st_transfer_progress(context, offset, size);
}
static void z_progress(void *context, int64_t offset)
{
	st_transfer_t *t = context;
	st_transfer_progress(t, offset, t->current_size);
}
static void init_xy(st_transfer_t *t, xmodem_t *xm, long *mode)
{
	xmodem_init(xm, t, mode, st_transfer_protocol_log, xy_progress,
	    st_transfer_send_byte, st_transfer_recv, st_transfer_connected,
	    st_transfer_cancelled, st_transfer_flush);
	if (t->config.protocol == ST_TRANSFER_XMODEM_128) xm->block_size = 128;
}
static int finish_file(FILE **file)
{
	int status = ferror(*file) ? IO_ERROR_IO : 0;
	if (fclose(*file) != 0) status = IO_ERROR_IO;
	*file = NULL;
	return status;
}
static int xy_send(st_transfer_t *t, xmodem_t *xm)
{
	for (size_t i = 0; i < t->config.path_count; ++i) {
		struct fs_info info;
		int status = fs_get_info(&info, t->paths[i]);
		if (status != 0 || info.kind != FS_ENTRY_FILE || info.size > INT64_MAX)
			return status != 0 ? status : IO_ERROR_INVALID_ARGUMENT;
		if (info.size > (uint64_t)(INT64_MAX - xm->total_bytes)) return IO_ERROR_OUT_OF_RANGE;
		xm->total_files++; xm->total_bytes += (int64_t)info.size;
	}
	for (size_t i = 0; i < t->config.path_count; ++i) {
		FILE *file;
		int status = fs_open(&file, t->paths[i], "rb");
		if (status != 0) return status;
		t->current_size = fs_stream_size(file);
		if (t->current_size < 0) { fclose(file); return IO_ERROR_IO; }
		xm->current_file_num = (unsigned)i + 1;
		t->file_number = i + 1;
		strlcpy(t->current_name, path_basename(t->paths[i]), sizeof(t->current_name));
		bool sent = xmodem_send_file(xm, t->paths[i], file, NULL, NULL);
		status = finish_file(&file);
		if (!sent) return IO_ERROR_PROTOCOL;
		if (status != 0) return status;
		xm->sent_files++; xm->sent_bytes += t->current_size;
		if (i + 1 < t->config.path_count && !(*xm->mode & YMODEM)) return IO_ERROR_PROTOCOL;
	}
	if (*xm->mode & YMODEM) {
		uint8_t block[128] = {0};
		if (!xmodem_get_mode(xm)) return IO_ERROR_PROTOCOL;
		for (unsigned attempt = 0; attempt <= xm->max_errors; ++attempt) {
			if (xmodem_put_block(xm, block, sizeof(block), 0) != 0) break;
			int ack = xmodem_get_ack(xm, 1, 0);
			if (ack == ACK) return 0;
			if (ack == CAN) return IO_ERROR_CANCELLED;
			if (!st_transfer_connected(t)) break;
		}
		st_transfer_log_message(t, LOG_ERR, "YMODEM batch terminator was not acknowledged");
		return IO_ERROR_PROTOCOL;
	}
	return 0;
}
static int xy_receive_file(st_transfer_t *t, xmodem_t *xm, long mode,
	const char *name, int64_t size, time_t modified, uint8_t *first_block, bool have_first)
{
	char *path = NULL;
	FILE *file = NULL;
	int status = st_transfer_destination(t, name, &path, false);
	if (status != 0) return status > 0 ? IO_ERROR_CANCELLED : status;
	if (t->config.discard) { file = tmpfile(); status = file == NULL ? IO_ERROR_IO : 0; }
	else status = fs_open(&file, path, "wb");
	if (status != 0) { free(path); return status; }
	strlcpy(t->current_name, path_basename(path), sizeof(t->current_name));
	int64_t received = 0;
	unsigned block_number = 1, errors = 0;
	uint8_t block[1025];
	status = st_transfer_received(t, ST_TRANSFER_RECEIVE_BEGIN, NULL, 0, false);
	if (status != 0) { fclose(file); free(path); return status; }
	if (!have_first) xmodem_put_nak(xm, 1);
	for (;;) {
		if (!st_transfer_connected(t)) { status = IO_ERROR_NOT_CONNECTED; break; }
		int result;
		if (have_first) { memcpy(block, first_block, xm->block_size); have_first = false; result = SUCCESS; }
		else result = xmodem_get_block(xm, block, block_number);
		if (result == EOT) {
			if (size >= 0 && received != size) { status = IO_ERROR_PROTOCOL; break; }
			status = finish_file(&file);
			if (status == 0) xmodem_put_ack(xm);
			break;
		}
		if (result == CAN) { status = IO_ERROR_CANCELLED; break; }
		if (result != SUCCESS) {
			if ((mode & GMODE) || ++errors > xm->max_errors) { status = IO_ERROR_PROTOCOL; break; }
			xmodem_put_nak(xm, block_number); continue;
		}
		size_t count = xm->block_size;
		if (size >= 0 && count > (uint64_t)(size - received)) count = (size_t)(size - received);
		if (size >= 0 && received == size) { status = IO_ERROR_PROTOCOL; break; }
		if (fwrite(block, 1, count, file) != count) { status = IO_ERROR_IO; break; }
		status = st_transfer_received(t, ST_TRANSFER_RECEIVE_DATA, block, count, false);
		if (status != 0) break;
		received += (int64_t)count; ++block_number; errors = 0;
		st_transfer_progress(t, received, size);
		if (!(mode & GMODE)) { st_transfer_send_byte(t, ACK, 10); st_transfer_flush(t); }
	}
	int closed = file == NULL ? 0 : finish_file(&file);
	if (status == 0) status = closed;
	if (status == 0 && !t->config.discard && modified != 0) (void)fs_set_modified_time(path, (int64_t)modified);
	if (status != 0 && received == 0 && !t->config.discard) (void)fs_remove_file(path);
	int event_status = st_transfer_received(t, ST_TRANSFER_RECEIVE_END, NULL, 0, status == 0);
	if (status == 0) status = event_status;
	free(path);
	return status;
}
int st_transfer_run_xymodem(st_transfer_t *t)
{
	long mode = t->config.protocol == ST_TRANSFER_YMODEM || t->config.protocol == ST_TRANSFER_YMODEM_G ? YMODEM : XMODEM;
	if (t->config.protocol != ST_TRANSFER_XMODEM_CHECKSUM) mode |= CRC;
	if (t->config.protocol == ST_TRANSFER_YMODEM_G || t->config.protocol == ST_TRANSFER_XMODEM_G) mode |= GMODE;
	mode |= t->config.upload ? SEND : RECV;
	xmodem_t xm;
	init_xy(t, &xm, &mode);
	if (t->config.upload) return xy_send(t, &xm);
	if (mode & XMODEM) {
		if (t->filename[0] == '\0') return IO_ERROR_INVALID_ARGUMENT;
		return xy_receive_file(t, &xm, mode, t->filename, -1, 0, NULL, false);
	}
	for (;;) {
		uint8_t header[1025] = {0};
		int result = FAILURE;
		for (unsigned error = 0; error <= xm.max_errors && st_transfer_connected(t); ++error) {
			xmodem_put_nak(&xm, 0);
			result = xmodem_get_block(&xm, header, 0);
			if (result == SUCCESS || result == NOT_YMODEM) break;
			if (result == CAN) return IO_ERROR_CANCELLED;
		}
		if (result == NOT_YMODEM) {
			char filename[1024];
			if (st_transfer_request_dialog(t, ST_TRANSFER_DIALOG_FILENAME,
			    "XMODEM Filename", filename, sizeof(filename)) != 0 || !filename[0]) return IO_ERROR_CANCELLED;
			mode = (mode & ~YMODEM) | XMODEM;
			return xy_receive_file(t, &xm, mode, filename, -1, 0, header, true);
		}
		if (result != SUCCESS) return IO_ERROR_PROTOCOL;
		if (!(mode & GMODE)) { st_transfer_send_byte(t, ACK, 10); st_transfer_flush(t); }
		if (header[0] == '\0') return 0;
		const uint8_t *end = memchr(header, 0, xm.block_size);
		if (end == NULL) return IO_ERROR_INVALID_FORMAT;
		int64_t size = -1;
		time_t modified = 0;
		if (end + 1 < header + xm.block_size && end[1] != 0) {
			char *next;
			errno = 0;
			unsigned long long value = strtoull((const char *)end + 1, &next, 10);
			if (errno != 0 || value > INT64_MAX || next == (const char *)end + 1) return IO_ERROR_INVALID_FORMAT;
			size = (int64_t)value;
			if (*next == ' ') { unsigned long long stamp = strtoull(next + 1, NULL, 8); modified = (time_t)stamp; }
		}
		int status = xy_receive_file(t, &xm, mode, (const char *)header, size, modified, NULL, false);
		if (status != 0) return status;
		++t->file_number;
	}
}
static void init_z(st_transfer_t *t, zmodem_t *zm)
{
	zmodem_init(zm, t, st_transfer_protocol_log, z_progress,
	    st_transfer_send_byte, st_transfer_recv, st_transfer_connected,
	    st_transfer_cancelled, st_transfer_waiting, st_transfer_flush);
	zm->recv_span = st_transfer_recv_span;
}
static int z_received(void *context, const uint8_t *bytes, size_t size)
{
	return st_transfer_received(context, ST_TRANSFER_RECEIVE_DATA, bytes, size, false);
}
static int z_send(st_transfer_t *t, zmodem_t *zm)
{
	zm->total_files = (unsigned)t->config.path_count;
	for (size_t i = 0; i < t->config.path_count; ++i) {
		FILE *file;
		int status = fs_open(&file, t->paths[i], "rb");
		if (status != 0) return status;
		t->current_size = fs_stream_size(file);
		if (t->current_size < 0 || (uint64_t)t->current_size > UINT32_MAX) { fclose(file); return IO_ERROR_OUT_OF_RANGE; }
		strlcpy(t->current_name, path_basename(t->paths[i]), sizeof(t->current_name));
		t->file_number = i + 1; zm->current_file_num = (unsigned)i + 1;
		st_transfer_progress(t, 0, t->current_size);
		bool success = zmodem_send_file(zm, t->paths[i], file, i == 0, NULL, NULL);
		status = finish_file(&file);
		if (!success) return IO_ERROR_PROTOCOL;
		if (status != 0) return status;
	}
	if (zmodem_get_zfin(zm) != ZFIN) return IO_ERROR_PROTOCOL;
	/* Some receivers exit immediately after their confirming ZFIN. They
	 * have already confirmed the batch, but can close before our final OO. */
	if (t->io_error == IO_ERROR_NOT_CONNECTED) {
		st_transfer_log_message(t, LOG_DEBUG, "Receiver closed after confirming ZFIN");
		t->io_error = 0;
	}
	return 0;
}
static void unread_byte(st_transfer_t *t, uint8_t byte)
{
	(void)st_mutex_lock(&t->mutex);
	if (t->input_size < sizeof(t->input)) {
		t->input_head = (t->input_head + sizeof(t->input) - 1) % sizeof(t->input);
		t->input[t->input_head] = byte; ++t->input_size;
	}
	(void)st_mutex_unlock(&t->mutex);
}
static int z_receive(st_transfer_t *t, zmodem_t *zm)
{
	for (;;) {
		int type = zmodem_recv_init(zm);
		st_transfer_flush(t);
		if (type == ZFIN) {
			/* Consume only the protocol's final OO, preserving BBS text. */
			for (unsigned i = 0; i < 2; ++i) {
				int byte = st_transfer_recv(t, 2);
				if (byte != 'O') { if (byte >= 0) unread_byte(t, (uint8_t)byte); break; }
			}
			return 0;
		}
		if (type != ZFILE) return IO_ERROR_PROTOCOL;
		if (zm->current_file_size < 0 || (uint64_t)zm->current_file_size > UINT32_MAX) return IO_ERROR_OUT_OF_RANGE;
		t->current_size = zm->current_file_size;
		strlcpy(t->current_name, zm->current_file_name, sizeof(t->current_name));
		st_transfer_progress(t, 0, t->current_size);
		char *path = NULL;
		int status = st_transfer_destination(t, t->current_name, &path, true);
		if (status != 0) return status;
		FILE *file = NULL;
		int64_t offset = 0;
		struct fs_info info;
		status = fs_get_info(&info, path);
		if (status == 0 && !t->config.overwrite && !t->config.discard) {
			bool matching = false;
			if (info.size <= (uint64_t)zm->current_file_size && info.size != 0) {
				status = fs_open(&file, path, "rb");
				if (status != 0) { free(path); return status; }
				uint32_t crc = st_transfer_file_crc32(file, (size_t)info.size), remote;
				if (ferror(file)) { fclose(file); free(path); return IO_ERROR_IO; }
				fclose(file); file = NULL;
				matching = zmodem_get_crc(zm, (int32_t)(uint32_t)info.size, &remote) && crc == remote;
			}
			if (matching && info.size == (uint64_t)zm->current_file_size) {
				free(path); zmodem_send_zskip(zm); continue;
			}
			if (matching) { offset = (int64_t)info.size; st_transfer_log_message(t, LOG_INFO, "Resuming %s at %" PRId64, t->current_name, offset); }
			else {
				free(path); path = NULL;
				status = st_transfer_destination(t, t->current_name, &path, false);
				if (status > 0) { zmodem_send_zskip(zm); continue; }
				if (status != 0) return status;
			}
		} else if (status != 0 && status != IO_ERROR_NOT_FOUND) { free(path); return status; }
		if (t->config.discard) { file = tmpfile(); status = file == NULL ? IO_ERROR_IO : 0; }
		else status = fs_open(&file, path, offset == 0 ? "wb" : "r+b");
		if (status != 0) { free(path); return status; }
		status = st_transfer_received(t, ST_TRANSFER_RECEIVE_BEGIN, NULL, 0, false);
		if (status != 0) { fclose(file); free(path); return status; }
		zm->received = t->config.receive_events ? z_received : NULL;
		(void)zmodem_recv_file_data(zm, file, offset);
		bool complete = zm->file_complete;
		int64_t actual = fs_stream_size(file);
		status = finish_file(&file);
		if (status == 0 && !complete) status = IO_ERROR_PROTOCOL;
		if (status == 0 && !t->config.discard && zm->current_file_time != 0) (void)fs_set_modified_time(path, zm->current_file_time);
		if (status != 0 && actual == 0 && !t->config.discard) (void)fs_remove_file(path);
		int event_status = st_transfer_received(t, ST_TRANSFER_RECEIVE_END, NULL, 0, status == 0);
		if (status == 0) status = event_status;
		free(path);
		if (status != 0) return status;
		++t->file_number;
	}
}
int st_transfer_run_zmodem(st_transfer_t *t)
{
	zmodem_t zm;
	init_z(t, &zm);
	return t->config.upload ? z_send(t, &zm) : z_receive(t, &zm);
}
int st_transfer_run_upload(st_transfer_t *t)
{
	if (!t->config.upload || t->config.path_count != 1) return IO_ERROR_INVALID_ARGUMENT;
	FILE *file;
	int status = fs_open(&file, t->paths[0], "rb");
	if (status != 0) return status;
	uint8_t input[8192], output[16384];
	int64_t offset = 0, total = fs_stream_size(file);
	bool cr = false;
	strlcpy(t->current_name, path_basename(t->paths[0]), sizeof(t->current_name));
	for (;;) {
		size_t count = fread(input, 1, sizeof(input), file), used = 0;
		if (count == 0) { if (ferror(file)) status = IO_ERROR_IO; break; }
		for (size_t i = 0; i < count; ++i) {
			if (t->config.protocol == ST_TRANSFER_ASCII && input[i] == '\n' && !cr) output[used++] = '\r';
			output[used++] = input[i]; cr = input[i] == '\r';
		}
		status = st_transfer_send_bytes(t, output, used, 10000);
		if (status != 0) break;
		offset += (int64_t)count; st_transfer_progress(t, offset, total);
	}
	int closed = finish_file(&file);
	return status != 0 ? status : closed;
}
