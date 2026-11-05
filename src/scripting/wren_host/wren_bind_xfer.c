/* Per-VM transfer ownership; workers never touch Wren or CTerm. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "wren_host_internal.h"
#include "path.h"
#include "st_transfer.h"
#include "st_transfer_detect.h"
#include "st_term.h"
#include "st_connection.h"
#include "filesystem.h"
#include "io_error.h"
#include "string_compat.h"
#include "string_util.h"
#include "st_app_paths.h"
#include "cterm_rip_asset.h"
#include "st_bbs_list.h"
#include "st_unicode_text.h"
#include "st_utf8.h"
#include "st_time.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

struct transfer_error {
	int code;
	char message[160];
};

static void allocate_error(WrenVM *vm)
{
	struct transfer_error *error = wrenSetSlotNewForeign(vm, 0, 0, sizeof(*error));
	*error = (struct transfer_error){0};
}

WrenForeignClassMethods st_wren_transfer_bind_class(const char *class_name)
{
	return strcmp(class_name, "TransferError") == 0
	    ? (WrenForeignClassMethods){allocate_error, NULL}
	    : (WrenForeignClassMethods){NULL, NULL};
}

static struct transfer_error *error_value(WrenVM *vm)
{
	return st_wren_foreign_require(vm, 0, "syncterm", "TransferError");
}
static void error_code(WrenVM *vm)
{
	struct transfer_error *error = error_value(vm);
	if (error != NULL) wrenSetSlotDouble(vm, 0, error->code);
}
static void error_message(WrenVM *vm)
{
	struct transfer_error *error = error_value(vm);
	if (error != NULL) wrenSetSlotString(vm, 0, error->message);
}
static void error_string(WrenVM *vm)
{
	struct transfer_error *error = error_value(vm);
	char text[208];
	if (error == NULL) return;
	(void)snprintf(text, sizeof(text), "TransferError(%d): %s", error->code, error->message);
	wrenSetSlotString(vm, 0, text);
}

static void transfer_result(WrenVM *vm, int status, const char *operation)
{
	static const char *const reasons[] = {
		"Invalid argument", "Invalid encoding", "Not found", "Permission denied",
		"Already exists", "Not a directory", "Is a directory", "Directory not empty",
		"Out of memory", "No space available", "Name too long", "Out of range",
		"Busy", "Read-only", "Cross-device operation", "Too many links",
		"Interrupted", "Not supported", "I/O failure", "Timed out", "Cancelled",
		"Would block", "In progress", "Name resolution failed", "Connection refused",
		"Connection reset", "Connection aborted", "Not connected", "Network unreachable",
		"Host unreachable", "Address in use", "Address not available", "Message too large",
		"Protocol failure", "Password required", "Decryption failed", "Invalid format", "Conflict",
	};
	if (status == 0) { wrenSetSlotNull(vm, 0); return; }
	wrenEnsureSlots(vm, 2);
	wrenGetVariable(vm, "syncterm", "TransferError", 1);
	struct transfer_error *error = wrenSetSlotNewForeign(vm, 0, 1, sizeof(*error));
	error->code = status;
	(void)snprintf(error->message, sizeof(error->message), "%s: %s.", operation,
	    status <= -1 && status >= -38 ? reasons[-status - 1] : "Native transfer failure");
}

static bool transfer_text(WrenVM *vm, int slot, const char **text)
{
	int length;
	if (!st_wren_text_slot(vm, slot, text, &length)) return false;
	if (memchr(*text, '\0', (size_t)length) != NULL) {
		st_wren_abort(vm, "Transfer text must not contain NUL");
		return false;
	}
	return true;
}

struct st_wren_transfer {
	st_transfer_t *worker;
	struct st_transfer_detector detector;
	uint8_t prefix[33];
	size_t prefix_size;
	int automatic;
	bool upload, inline_upload;
	char *rip_directory, *rip_filename;
	char *skypix_directory, *skypix_filename, *skypix_partial;
	unsigned skypix_mode;
	int skypix_error;
	uint8_t *skypix_bytes;
	size_t skypix_size;
	unsigned rip_protocol, rip_file_type;
	bool rip_upload_pending, rip_scene;
	unsigned cancel_remaining;
	uint64_t cancel_next_ms;
};
static struct st_wren_transfer *state(struct st_wren_context *context)
{
	if (context->transfer == NULL) context->transfer = calloc(1, sizeof(*context->transfer));
	return context->transfer;
}
static const char *download_root(struct st_wren_context *context)
{
	const char *path = context->profile == NULL ? NULL : context->profile->download_directory;
	if ((path == NULL || !*path) && context->host->config.paths != NULL) path = context->host->config.paths->download_directory;
	return st_wren_download_directory_allowed(path) ? path : NULL;
}
static ssize_t send_bytes(void *context, const void *data, size_t size, int64_t timeout)
{
	return st_connection_send_bytes(context, data, size, timeout);
}
static void log_entry(struct st_wren_context *context, const struct st_transfer_log *entry)
{
	if (context->transfer_log != NULL && entry->level <= context->transfer_log_level)
		context->transfer_log(context->transfer_log_context, entry->level, entry->text);
}
static int start(struct st_wren_context *context, struct st_transfer_config *config, bool asset, bool inline_receive)
{
	if (st_term_session_wire_owned(context->session)) return IO_ERROR_BUSY;
	struct st_wren_transfer *s = state(context);
	if (s == NULL) return IO_ERROR_OUT_OF_MEMORY;
	if (s->worker != NULL) return IO_ERROR_BUSY;
	if (!asset && (s->rip_directory != NULL || s->skypix_directory != NULL || s->cancel_remaining != 0)) return IO_ERROR_BUSY;
	if (config->protocol != ST_TRANSFER_FAKE) {
		if (context->host->config.safe_mode || context->connection == NULL || context->session == NULL
		    || context->profile == NULL || context->profile->connection_type == ST_CONNECTION_MQTT)
			return IO_ERROR_PERMISSION_DENIED;
		config->send = send_bytes; config->send_context = context->connection;
		if (!asset) config->directory = download_root(context);
		if (!config->upload && config->directory == NULL) return IO_ERROR_PERMISSION_DENIED;
	}
	int status = st_transfer_create(&s->worker, config);
	if (status != 0) return status;
	if (context->session != NULL) {
		status = st_term_session_begin_transfer(context->session, s->worker);
		if (status != 0) { st_transfer_destroy(&s->worker); return status; }
	}
	status = st_transfer_launch(s->worker);
	if (status != 0) {
		(void)st_term_session_end_transfer(context->session);
		st_transfer_destroy(&s->worker);
		return status;
	}
	s->upload = config->upload;
	s->inline_upload = config->protocol == ST_TRANSFER_ASCII || config->protocol == ST_TRANSFER_RAW
	    || inline_receive;
	if (s->prefix_size != 0) {
		(void)st_transfer_feed(s->worker, s->prefix, s->prefix_size); s->prefix_size = 0;
	}
	if (config->protocol == ST_TRANSFER_CET && context->session != NULL) {
		size_t size;
		const uint8_t *frame = st_term_session_prestel_frame(context->session, &size);
		(void)st_transfer_feed(s->worker, frame, size);
	}
	return 0;
}

static bool rip_extension(const char *filename, const char *extension)
{
	size_t length = strlen(filename);
	if (length < 4) return false;
	for (size_t i = 0; i < 4; ++i)
		if (ascii_to_upper((unsigned char)filename[length - 4 + i])
		    != ascii_to_upper((unsigned char)extension[i])) return false;
	return true;
}

static int rip_destination(void *context, const char *directory, const char *name, char **path)
{
	struct st_wren_transfer *s = context;
	if (s->rip_file_type >= 4 && !rip_extension(name, ".rip") && !rip_extension(name, ".icn"))
		return IO_ERROR_INVALID_ARGUMENT;
	return cterm_rip_asset_transfer_path(path, directory, name, false);
}

static int start_rip(struct st_wren_context *context, bool upload)
{
	struct st_wren_transfer *s = context->transfer;
	static const enum st_transfer_protocol protocols[8] = {
		ST_TRANSFER_XMODEM_CHECKSUM, ST_TRANSFER_XMODEM_CRC, ST_TRANSFER_XMODEM_CRC,
		ST_TRANSFER_XMODEM_G, ST_TRANSFER_FAKE, ST_TRANSFER_YMODEM,
		ST_TRANSFER_YMODEM_G, ST_TRANSFER_ZMODEM
	};
	struct st_transfer_config config = {
		.protocol = protocols[s->rip_protocol], .upload = upload,
		.directory = s->rip_directory, .filename = s->rip_filename,
		.destination = rip_destination, .destination_context = s,
		.overwrite = true, .discard = !upload && s->rip_file_type == 0,
		.receive_events = !upload && (s->rip_file_type == 0 || s->rip_file_type == 5),
	};
	char *source = NULL;
	if (upload) {
		int status = cterm_rip_asset_transfer_path(&source, s->rip_directory, s->rip_filename, true);
		if (status != 0) return status;
		config.paths = (const char *const *)&source; config.path_count = 1;
		/* Sending XMODEM lets the peer select checksum or CRC. */
		if (s->rip_protocol < 3) config.protocol = s->rip_protocol == 2
		    ? ST_TRANSFER_XMODEM_1K : ST_TRANSFER_XMODEM_128;
	}
	int status = start(context, &config, true, !upload && (s->rip_file_type == 0 || s->rip_file_type == 5));
	free(source);
	return status;
}

static int skypix_destination(void *context, const char *directory, const char *name, char **out)
{
	struct st_wren_transfer *s = context;
	(void)directory;
	if (strcmp(name, s->skypix_filename) != 0 || s->skypix_partial == NULL)
		return IO_ERROR_PERMISSION_DENIED;
	struct fs_info info;
	int status = fs_get_link_info(&info, s->skypix_partial);
	if (status != IO_ERROR_NOT_FOUND && (status != 0 || info.kind != FS_ENTRY_FILE))
		return status == 0 ? IO_ERROR_PERMISSION_DENIED : status;
	*out = strdup(s->skypix_partial);
	return *out == NULL ? IO_ERROR_OUT_OF_MEMORY : 0;
}
static int skypix_cached(struct st_wren_context *context, const char *path, unsigned mode)
{
	int status = 0;
	if (mode == 1) {
		struct fs_info info;
		status = fs_get_link_info(&info, path);
		if (status == 0 && (info.kind != FS_ENTRY_FILE || info.size > 4 * 1024 * 1024))
			status = IO_ERROR_INVALID_FORMAT;
		FILE *file = NULL;
		uint8_t *bytes = NULL;
		if (status == 0)
			status = fs_open(&file, path, "rb");
		if (status == 0) {
			bytes = malloc(info.size ? (size_t)info.size : 1);
			if (bytes == NULL)
				status = IO_ERROR_OUT_OF_MEMORY;
		}
		if (status == 0 && fread(bytes, 1, (size_t)info.size, file) != (size_t)info.size)
			status = IO_ERROR_IO;
		if (file != NULL && fclose(file) != 0 && status == 0)
			status = IO_ERROR_IO;
		if (status == 0)
			status = st_cterm_skypix_receive(context->cterm, bytes, (size_t)info.size);
		free(bytes);
	}
	if (status == 0) {
		static const uint8_t cached[] = {24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 8,
										 8,	 8,	 8,	 8,	 8,	 8,	 8,	 8,	 8,	 13};
		ssize_t sent = st_connection_send_bytes(context->connection, cached, sizeof(cached), 100);
		if (sent != (ssize_t)sizeof(cached))
			status = sent < 0 ? (int)sent : IO_ERROR_IO;
	}
	return status;
}
int st_wren_transfer_skypix_request(struct st_wren_context *context, unsigned mode, unsigned width,
									unsigned height, const uint8_t *filename, size_t size)
{
	(void)width;
	(void)height;
	if (context == NULL || context->profile == NULL || context->host->config.paths == NULL ||
		!st_wren_transfer_detect_enabled(context) || (mode != 1 && mode != 20) || size == 0 ||
		size > 255)
		return IO_ERROR_NOT_SUPPORTED;
	struct st_wren_transfer *s = state(context);
	if (s == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	if (s->worker != NULL || s->automatic != 0 || s->rip_directory != NULL ||
		s->skypix_directory != NULL || s->cancel_remaining != 0)
		return IO_ERROR_BUSY;
	char text[511];
	size_t used = 0;
	for (size_t i = 0; i < size; ++i) {
		if (filename[i] < 0x20 || filename[i] == 0x7f || filename[i] == '/' ||
			filename[i] == '\\' || filename[i] == ':')
			return IO_ERROR_PERMISSION_DENIED;
		char bytes[4];
		ptrdiff_t n = st_utf8_encode_one(bytes, sizeof(bytes), filename[i]);
		if (n < 0 || (size_t)n >= sizeof(text) - used)
			return IO_ERROR_INVALID_ENCODING;
		memcpy(text + used, bytes, (size_t)n);
		used += (size_t)n;
	}
	text[used] = 0;
	char *normal = NULL;
	size_t normal_size = 0;
	int status = st_unicode_text_normalize(&normal, &normal_size, text, used, NULL);
	if (status != 0)
		return status;
	if (!path_untrusted_component_valid(normal) || normal[0] == '.') {
		free(normal);
		return IO_ERROR_PERMISSION_DENIED;
	}
	char *bbs = NULL, *root = NULL;
	status = st_bbs_cache_path_for_name(&bbs, context->host->config.paths->cache_directory,
										context->profile->name);
	if (status == 0)
		status = path_join_alloc(&root, bbs, "skypix");
	free(bbs);
	struct fs_info info;
	if (status == 0) {
		status = fs_get_link_info(&info, root);
		if (status == IO_ERROR_NOT_FOUND)
			status = fs_create_directories(root);
		else if (status == 0 && info.kind != FS_ENTRY_DIRECTORY)
			status = IO_ERROR_PERMISSION_DENIED;
	}
	if (status != 0) {
		free(root);
		free(normal);
		return status;
	}
	char *cached = NULL;
	status = path_join_alloc(&cached, root, normal);
	if (status == 0) {
		int found = fs_get_link_info(&info, cached);
		if (found == 0) {
			status = info.kind == FS_ENTRY_FILE ? skypix_cached(context, cached, mode)
												: IO_ERROR_PERMISSION_DENIED;
			free(cached);
			free(root);
			free(normal);
			return status;
		}
		if (found != IO_ERROR_NOT_FOUND)
			status = found;
	}
	free(cached);
	char partial_name[520];
	int count = snprintf(partial_name, sizeof(partial_name), ".%s.part", normal);
	if (status == 0 && (count < 0 || (size_t)count >= sizeof(partial_name)))
		status = IO_ERROR_NAME_TOO_LONG;
	char *partial = NULL;
	if (status == 0)
		status = path_join_alloc(&partial, root, partial_name);
	if (status != 0) {
		free(root);
		free(normal);
		return status;
	}
	s->skypix_directory = root;
	s->skypix_filename = normal;
	s->skypix_mode = mode;
	s->skypix_partial = partial;
	s->skypix_error = 0;
	const struct st_transfer_config config_template = {.protocol = ST_TRANSFER_XMODEM_CRC,
													   .directory = root,
													   .filename = normal,
													   .destination = skypix_destination,
													   .destination_context = s,
													   .overwrite = true,
													   .receive_events = true};
	struct st_transfer_config config = config_template;
	st_term_session_reserve_transfer(context->session, true);
	status = start(context, &config, true, false);
	if (status == 0) {
		s->automatic = 5;
		return IO_ERROR_IN_PROGRESS;
	}
	st_term_session_reserve_transfer(context->session, false);
	free(s->skypix_directory);
	s->skypix_directory = NULL;
	free(s->skypix_filename);
	s->skypix_filename = NULL;
	free(s->skypix_partial);
	s->skypix_partial = NULL;
	return status;
}

int st_wren_transfer_rip_request(struct st_wren_context *context,
	const struct st_cterm_rip_transfer_request *request)
{
	if (st_term_session_wire_owned(context->session)) return IO_ERROR_BUSY;
	if (!st_wren_transfer_detect_enabled(context) || request == NULL || request->directory == NULL
	    || !*request->directory || request->filename == NULL || request->protocol > 7
	    || request->protocol == 4 || (!request->upload && request->file_type > 5))
		return IO_ERROR_NOT_SUPPORTED;
	struct st_wren_transfer *s = state(context);
	if (s == NULL) return IO_ERROR_OUT_OF_MEMORY;
	if (s->worker != NULL || s->automatic != 0 || s->rip_directory != NULL || s->skypix_directory != NULL || s->cancel_remaining != 0)
		return IO_ERROR_BUSY;
	struct fs_info info;
	int status = fs_get_link_info(&info, request->directory);
	if (status == IO_ERROR_NOT_FOUND && !request->upload) status = fs_create_directories(request->directory);
	else if (status == 0 && info.kind != FS_ENTRY_DIRECTORY) status = IO_ERROR_PERMISSION_DENIED;
	if (status != 0) return status;
	s->rip_directory = strdup(request->directory); s->rip_filename = strdup(request->filename);
	if (s->rip_directory == NULL || s->rip_filename == NULL) { status = IO_ERROR_OUT_OF_MEMORY; goto fail; }
	s->rip_protocol = request->protocol; s->rip_file_type = request->file_type;
	st_term_session_reserve_rip_transfer(context->session);
	if (request->upload) {
		char *source = NULL;
		status = cterm_rip_asset_transfer_path(&source, s->rip_directory, s->rip_filename, true);
		free(source);
		if (status != 0) goto fail;
		s->rip_upload_pending = true; s->automatic = 4;
	} else {
		status = start_rip(context, false);
		if (status != 0) goto fail;
		s->automatic = 3;
	}
	return 0;
fail:
	st_term_session_reserve_transfer(context->session, false);
	free(s->rip_directory); s->rip_directory = NULL;
	free(s->rip_filename); s->rip_filename = NULL;
	return status;
}

void st_wren_transfer_pump(struct st_wren_context *context)
{
	struct st_wren_transfer *s = context->transfer;
	if (s == NULL) return;
	if (s->cancel_remaining != 0) {
		uint64_t now;
		if (st_time_monotonic_ms(&now) == 0 && now >= s->cancel_next_ms) {
			const uint8_t can = 24;
			ssize_t sent = context->connection == NULL ? IO_ERROR_NOT_CONNECTED
			    : st_connection_send(context->connection, &can, 1, 0);
			s->cancel_next_ms = now + 100;
			if (sent == 1) --s->cancel_remaining;
			else if (sent != IO_ERROR_WOULD_BLOCK && sent != IO_ERROR_TIMED_OUT) s->cancel_remaining = 0;
			if (s->cancel_remaining == 0) st_term_session_reserve_transfer(context->session, false);
		}
	}
	struct st_transfer_receive_event event;
	/* Bound renderer work so input/cancellation gets another dispatch turn
	 * even when a fast sender continuously replenishes the mailbox. */
	for (unsigned batch = 0; batch < 16 && st_transfer_take_received(s->worker, &event); ++batch) {
		int status = 0;
		if (s->skypix_directory != NULL) {
			if (event.kind == ST_TRANSFER_RECEIVE_BEGIN) s->skypix_size = 0;
			else if (event.kind == ST_TRANSFER_RECEIVE_DATA && s->skypix_mode == 1) {
				if (event.size > 4*1024*1024 - s->skypix_size) status = IO_ERROR_OUT_OF_RANGE;
				else {
					uint8_t *bytes = realloc(s->skypix_bytes,s->skypix_size+event.size);
					if (bytes == NULL) status = IO_ERROR_OUT_OF_MEMORY;
					else { s->skypix_bytes=bytes; memcpy(bytes+s->skypix_size,event.bytes,event.size); s->skypix_size+=event.size; }
				}
			}
			else if (event.kind == ST_TRANSFER_RECEIVE_END) {
				status = s->skypix_error!=0 ? s->skypix_error : st_cterm_skypix_receive(context->cterm,
				    event.success && s->skypix_mode==1?s->skypix_bytes:NULL,
				    event.success && s->skypix_mode==1?s->skypix_size:0);
				if(event.success && status==0) {
					char *live=NULL;
					status=path_join_alloc(&live,s->skypix_directory,s->skypix_filename);
					if(status==0) status=fs_rename(s->skypix_partial,live);
					free(live);
				}
			}
			if (status != 0) {
				struct st_transfer_log log={.level=ST_CTERM_LOG_WARNING};
				snprintf(log.text,sizeof(log.text),"SkyPix asset failed (%d)",status); log_entry(context,&log);
				s->skypix_error=status;
				if (event.kind != ST_TRANSFER_RECEIVE_END) (void)st_transfer_request_abort(s->worker);
				(void)st_cterm_skypix_receive(context->cterm,NULL,0);
			}
			continue;
		}
		switch (event.kind) {
		case ST_TRANSFER_RECEIVE_BEGIN:
			if (s->rip_file_type == 0 || rip_extension(event.filename, ".rip")) {
				status = st_cterm_rip_receive_begin(context->cterm);
				s->rip_scene = status == 0;
			}
			break;
		case ST_TRANSFER_RECEIVE_DATA:
			if (s->rip_scene) status = st_cterm_rip_receive_data(context->cterm, event.bytes, event.size);
			break;
		case ST_TRANSFER_RECEIVE_END:
			if (s->rip_scene) status = st_cterm_rip_receive_end(context->cterm, event.success);
			s->rip_scene = false;
			break;
		}
		if (status != 0) {
			struct st_transfer_log log = {.level = ST_CTERM_LOG_WARNING};
			snprintf(log.text, sizeof(log.text), "RIP received scene failed (%d)", status);
			log_entry(context, &log);
			(void)st_transfer_request_abort(s->worker);
			(void)st_cterm_rip_receive_end(context->cterm, false); s->rip_scene = false;
		}
	}
}
static void finish(struct st_wren_context *context)
{
	struct st_wren_transfer *s = context->transfer;
	if (s == NULL) return;
	if (s->worker != NULL) {
		(void)st_transfer_join(s->worker);
		st_wren_transfer_pump(context);
		if (s->rip_scene) (void)st_cterm_rip_receive_end(context->cterm, false);
		s->rip_scene = false;
		(void)st_term_session_end_transfer(context->session);
		if (s->skypix_directory != NULL) (void)st_cterm_skypix_receive(context->cterm,NULL,0);
		size_t replay_size;
		const uint8_t *replay = st_transfer_replay(s->worker, &replay_size);
		if (context->cterm != NULL && replay_size != 0) {
			size_t consumed;
			(void)st_cterm_write(context->cterm, replay, replay_size, &consumed);
		}
		struct st_transfer_log logs[32]; size_t count;
		do {
			count = st_transfer_drain_log(s->worker, logs, 32);
			for (size_t i = 0; i < count; ++i) log_entry(context, &logs[i]);
		} while (count != 0);
		st_transfer_destroy(&s->worker);
	}
	st_term_session_reserve_transfer(context->session, false);
	if (s->skypix_directory != NULL) (void)st_cterm_skypix_receive(context->cterm,NULL,0);
	free(s->skypix_directory); s->skypix_directory=NULL;
	free(s->skypix_filename); s->skypix_filename=NULL;
	if(s->skypix_partial!=NULL) (void)fs_remove_file(s->skypix_partial);
	free(s->skypix_partial); s->skypix_partial=NULL;
	free(s->skypix_bytes); s->skypix_bytes=NULL; s->skypix_size=0;
	free(s->rip_directory); s->rip_directory = NULL;
	free(s->rip_filename); s->rip_filename = NULL;
	s->rip_upload_pending = false; s->inline_upload = false; s->cancel_remaining = 0;
	s->automatic = 0; s->prefix_size = 0; s->detector.size = 0;
}
void st_wren_transfer_dispose(struct st_wren_context *context)
{
	finish(context); free(context->transfer); context->transfer = NULL;
}
bool st_wren_transfer_detect_enabled(const struct st_wren_context *context)
{
	return context != NULL && !context->host->config.safe_mode && context->connection != NULL
	    && context->session != NULL && context->profile != NULL
	    && context->profile->connection_type != ST_CONNECTION_MQTT;
}
int st_wren_transfer_filter(struct st_wren_context *context, uint8_t byte,
	bool *consumed, uint8_t *replacement, size_t capacity, size_t *size)
{
	if (!st_wren_transfer_detect_enabled(context)) return 0;
	struct st_wren_transfer *s = state(context);
	if (s == NULL) return IO_ERROR_OUT_OF_MEMORY;
	if (s->worker != NULL || s->automatic != 0 || s->rip_directory != NULL || s->skypix_directory != NULL || s->cancel_remaining != 0) return 0;
	uint8_t output[33]; size_t count;
	int event = st_transfer_detect(&s->detector, byte, output, &count);
	if (event != 0) {
		if (download_root(context) == NULL && event == 1) {
			/* Keep literal bytes when downloads cannot be offered. */
			event = 0;
		} else {
			memcpy(s->prefix, output, count); s->prefix_size = count; s->automatic = event;
			if (event == 1) {
				struct st_transfer_config config = {.protocol = ST_TRANSFER_ZMODEM};
				int status = start(context, &config, false, false);
				if (status != 0) { s->automatic = 0; s->prefix_size = 0; return status; }
			} else st_term_session_reserve_transfer(context->session, true);
			*consumed = true; *size = 0; return 0;
		}
	}
	if (count == 1 && output[0] == byte && s->detector.size == 0) return 0;
	if (count > capacity) return IO_ERROR_OUT_OF_RANGE;
	memcpy(replacement, output, count); *size = count; *consumed = true;
	return 0;
}
static st_transfer_t *get_worker(WrenVM *vm)
{
	struct st_wren_transfer *s = st_wren_context_from_vm(vm)->transfer;
	return s == NULL ? NULL : s->worker;
}
static void active(WrenVM *vm) { wrenSetSlotBool(vm, 0, get_worker(vm) != NULL); }
static void uploading(WrenVM *vm)
{
	struct st_wren_transfer *s = st_wren_context_from_vm(vm)->transfer;
	wrenSetSlotBool(vm, 0, s != NULL && s->worker != NULL && s->upload);
}
static void inline_upload(WrenVM *vm)
{
	struct st_wren_transfer *s = st_wren_context_from_vm(vm)->transfer;
	wrenSetSlotBool(vm, 0, s != NULL && s->inline_upload && s->upload);
}
static void inline_progress(WrenVM *vm)
{
	struct st_wren_transfer *s = st_wren_context_from_vm(vm)->transfer;
	wrenSetSlotBool(vm, 0, s != NULL && s->inline_upload);
}
static void begin(WrenVM *vm)
{
	const char *kind;
	if (!transfer_text(vm, 1, &kind)) return;
	if (strcmp(kind, "fake") != 0) { st_wren_abort(vm, "Transfer.beginSession expects fake"); return; }
	struct st_transfer_config config = {.protocol = ST_TRANSFER_FAKE};
	transfer_result(vm, start(st_wren_context_from_vm(vm), &config, false, false), "Transfer startup failed");
}
static void end(WrenVM *vm) { finish(st_wren_context_from_vm(vm)); wrenSetSlotNull(vm, 0); }
static void done(WrenVM *vm) { wrenSetSlotBool(vm, 0, st_transfer_done(get_worker(vm))); }
static void result(WrenVM *vm)
{
	st_transfer_t *worker = get_worker(vm);
	if (worker == NULL || !st_transfer_done(worker)) {
		st_wren_abort(vm, "Transfer.result requires an active, completed transfer");
		return;
	}
	transfer_result(vm, st_transfer_result(worker), "Transfer completion failed");
}
static void aborted(WrenVM *vm) { wrenSetSlotBool(vm, 0, st_transfer_aborted(get_worker(vm))); }
static void abort_transfer(WrenVM *vm) { wrenSetSlotBool(vm, 0, st_transfer_request_abort(get_worker(vm))); }
static void tick_dirty(WrenVM *vm)
{
	st_transfer_t *t = get_worker(vm); wrenSetSlotBool(vm, 0, t != NULL && st_transfer_tick_dirty(t));
}
static void snapshot(WrenVM *vm)
{
	struct st_transfer_snapshot value = {0}; st_transfer_t *t = get_worker(vm);
	if (t != NULL) st_transfer_snapshot(t, &value);
	if (!st_wren_i64_exact(value.bytes_current) || !st_wren_i64_exact(value.bytes_total)) {
		st_wren_abort(vm, "Transfer byte count exceeds Wren's exact Number range");
		return;
	}
	wrenEnsureSlots(vm, 2); wrenSetSlotNewList(vm, 0);
	const char *lines[] = {value.line1, value.line2, value.line3, value.line4};
	for (unsigned i = 0; i < 4; ++i) { wrenSetSlotString(vm, 1, lines[i]); wrenInsertInList(vm, 0, -1, 1); }
	wrenSetSlotDouble(vm, 1, (double)value.bytes_current); wrenInsertInList(vm, 0, -1, 1);
	wrenSetSlotDouble(vm, 1, (double)value.bytes_total); wrenInsertInList(vm, 0, -1, 1);
}
static void drain_log(WrenVM *vm)
{
	struct st_transfer_log logs[256]; st_transfer_t *t = get_worker(vm);
	size_t count = t == NULL ? 0 : st_transfer_drain_log(t, logs, 256);
	wrenEnsureSlots(vm, 4); wrenSetSlotNewList(vm, 0);
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	for (size_t i = 0; i < count; ++i) {
		log_entry(context, &logs[i]);
		wrenSetSlotNewList(vm, 1); wrenSetSlotDouble(vm, 2, logs[i].level); wrenInsertInList(vm, 1, -1, 2);
		wrenSetSlotString(vm, 2, logs[i].text); wrenInsertInList(vm, 1, -1, 2); wrenInsertInList(vm, 0, -1, 1);
	}
}
static void dialog_pending(WrenVM *vm)
{
	st_transfer_t *t = get_worker(vm);
	wrenSetSlotDouble(vm, 0, t == NULL ? 0 : st_transfer_dialog(t, NULL, 0));
}
static void dialog_filename(WrenVM *vm)
{
	char name[1024] = ""; st_transfer_t *t = get_worker(vm);
	if (t != NULL) (void)st_transfer_dialog(t, name, sizeof(name));
	wrenSetSlotString(vm, 0, name);
}
static void dialog_respond(WrenVM *vm)
{
	st_transfer_t *t = get_worker(vm); const char *name = NULL; int length = 0;
	if (t == NULL || wrenGetSlotType(vm, 1) != WREN_TYPE_NUM) { st_wren_abort(vm, "No transfer dialog"); return; }
	double response = wrenGetSlotDouble(vm, 1);
	if (!isfinite(response) || response < 0 || response > 2 || floor(response) != response) { st_wren_abort(vm, "Invalid transfer dialog response"); return; }
	if (wrenGetSlotType(vm, 2) != WREN_TYPE_NULL && !st_wren_text_slot(vm, 2, &name, &length)) return;
	if (st_transfer_respond(t, (int)response, name) != 0) st_wren_abort(vm, "No pending transfer dialog or invalid filename");
	else wrenSetSlotNull(vm, 0);
}
static bool parse_protocol(const char *name, bool upload, enum st_transfer_protocol *protocol)
{
	static const struct { const char *name; enum st_transfer_protocol protocol; } kinds[] = {
		{"zmodem", ST_TRANSFER_ZMODEM}, {"ymodem", ST_TRANSFER_YMODEM},
		{"ymodem-g", ST_TRANSFER_YMODEM_G}, {"xmodem-1k", ST_TRANSFER_XMODEM_1K},
		{"xmodem-128", ST_TRANSFER_XMODEM_128}, {"xmodem-crc", ST_TRANSFER_XMODEM_CRC},
		{"xmodem-chksum", ST_TRANSFER_XMODEM_CHECKSUM}, {"ascii", ST_TRANSFER_ASCII},
		{"raw", ST_TRANSFER_RAW}, {"cet", ST_TRANSFER_CET}, {"xmodem-g", ST_TRANSFER_XMODEM_G}
	};
	for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i) if (strcmp(name, kinds[i].name) == 0) {
		*protocol = kinds[i].protocol;
		if (upload) return *protocol != ST_TRANSFER_CET && *protocol != ST_TRANSFER_XMODEM_CRC && *protocol != ST_TRANSFER_XMODEM_CHECKSUM && *protocol != ST_TRANSFER_YMODEM_G && *protocol != ST_TRANSFER_XMODEM_G;
		return *protocol != ST_TRANSFER_RAW && *protocol != ST_TRANSFER_ASCII && *protocol != ST_TRANSFER_XMODEM_1K && *protocol != ST_TRANSFER_XMODEM_128;
	}
	return false;
}
static void dispatch(WrenVM *vm, bool upload, bool batch)
{
	const char *kind;
	struct st_transfer_config config = {.upload = upload};
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	if (!transfer_text(vm, 1, &kind)) return;
	if (!parse_protocol(kind, upload, &config.protocol)
	    || (batch && config.protocol != ST_TRANSFER_ZMODEM && config.protocol != ST_TRANSFER_YMODEM)) { st_wren_abort(vm, "Unsupported transfer protocol"); return; }
	struct st_cterm_state terminal_state = {0};
	if (context->cterm != NULL) (void)st_cterm_state(context->cterm, &terminal_state);
	if (config.protocol == ST_TRANSFER_CET && (context->cterm == NULL || terminal_state.emulation != ST_CTERM_PRESTEL)) { st_wren_abort(vm, "CET requires Prestel"); return; }
	char **paths = NULL;
	if (upload) {
		if (wrenGetSlotType(vm, 3) != WREN_TYPE_NUM) { st_wren_abort(vm, "Expected last wire byte"); return; }
		double last = wrenGetSlotDouble(vm, 3);
		if (!isfinite(last) || last < 0 || last > 255 || floor(last) != last) { st_wren_abort(vm, "Invalid last wire byte"); return; }
		config.last_ch = (int)last;
		if (batch && wrenGetSlotType(vm, 2) != WREN_TYPE_LIST) { st_wren_abort(vm, "Expected upload sources"); return; }
		int count = batch ? wrenGetListCount(vm, 2) : 1;
		if (count <= 0 || count > 65536) { st_wren_abort(vm, "Invalid upload file count"); return; }
		paths = calloc((size_t)count, sizeof(*paths));
		if (paths == NULL) { transfer_result(vm, IO_ERROR_OUT_OF_MEMORY, "Transfer startup failed"); return; }
		config.path_count = (size_t)count; config.paths = (const char *const *)paths;
		wrenEnsureSlots(vm, 5);
		for (int i = 0; i < count; ++i) {
			int slot = batch ? 4 : 2;
			if (batch) wrenGetListElement(vm, 2, i, slot);
			paths[i] = st_wren_file_upload_path(vm, slot);
			if (paths[i] == NULL) goto finished;
		}
	} else if (config.protocol == ST_TRANSFER_XMODEM_CRC || config.protocol == ST_TRANSFER_XMODEM_CHECKSUM
	    || config.protocol == ST_TRANSFER_XMODEM_G) {
		if (!transfer_text(vm, 2, &config.filename)) return;
	}
	transfer_result(vm, start(context, &config, false, false), "Transfer startup failed");
finished:
	for (size_t i = 0; i < config.path_count; ++i) free(paths[i]);
	free(paths);
}
static void upload(WrenVM *vm) { dispatch(vm, true, false); }
static void upload_batch(WrenVM *vm) { dispatch(vm, true, true); }
static void download(WrenVM *vm) { dispatch(vm, false, false); }
static void automatic(WrenVM *vm)
{
	struct st_wren_transfer *s = st_wren_context_from_vm(vm)->transfer;
	int event = s == NULL ? 0 : s->automatic;
	if (s != NULL) s->automatic = 0;
	static const char *const names[] = {"", "download", "upload", "rip-download", "rip-upload", "skypix-download"};
	if (event == 0) wrenSetSlotNull(vm, 0); else wrenSetSlotString(vm, 0, names[event]);
}
static void rip_upload_name(WrenVM *vm)
{
	struct st_wren_transfer *s = st_wren_context_from_vm(vm)->transfer;
	wrenSetSlotString(vm, 0, s == NULL || s->rip_filename == NULL ? "" : s->rip_filename);
}
static void accept_rip_upload(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	struct st_wren_transfer *s = context->transfer;
	if (s == NULL || !s->rip_upload_pending) {
		st_wren_abort(vm, "No pending RIP upload"); return;
	}
	int status = start_rip(context, true);
	if (status == 0) s->rip_upload_pending = false;
	transfer_result(vm, status, "RIP upload startup failed");
}
static void cancel_automatic(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	struct st_wren_transfer *s = context->transfer;
	if (s != NULL && s->worker == NULL && s->rip_upload_pending) {
		s->rip_upload_pending = false; s->automatic = 0;
		free(s->rip_directory); s->rip_directory = NULL;
		free(s->rip_filename); s->rip_filename = NULL;
		s->cancel_remaining = 10; s->cancel_next_ms = 0;
	} else if (s != NULL && s->worker == NULL && (s->automatic != 0 || s->prefix_size != 0)) {
		s->automatic = 0; s->prefix_size = 0;
		st_term_session_reserve_transfer(context->session, false);
		static const uint8_t cancel[] = {24,24,24,24,24,24,24,24};
		if (context->connection != NULL) (void)st_connection_send(context->connection, cancel, sizeof(cancel), 100);
	}
	wrenSetSlotNull(vm, 0);
}
WrenForeignMethodFn st_wren_transfer_bind_method(const char *class_name, bool is_static, const char *signature)
{
	if (!is_static && strcmp(class_name, "TransferError") == 0) {
		if (strcmp(signature, "code") == 0) return error_code;
		if (strcmp(signature, "message") == 0) return error_message;
		if (strcmp(signature, "toString") == 0) return error_string;
	}
	if (!is_static || strcmp(class_name, "Transfer") != 0) return NULL;
	static const struct { const char *signature; WrenForeignMethodFn method; } methods[] = {
		{"active", active}, {"uploading", uploading}, {"inlineUpload", inline_upload},
		{"inlineProgress", inline_progress},
		{"beginSession(_)", begin}, {"endSession()", end}, {"done", done}, {"result", result},
		{"aborted", aborted}, {"requestAbort()", abort_transfer}, {"tickDirty", tick_dirty},
		{"snapshot()", snapshot}, {"drainLog()", drain_log}, {"dialogPending", dialog_pending},
		{"dialogFilename", dialog_filename}, {"dialogRespond(_,_)", dialog_respond},
		{"upload(_,_,_)", upload}, {"uploadBatch(_,_,_)", upload_batch}, {"download(_,_)", download},
		{"autoPending_", automatic}, {"cancelAuto_()", cancel_automatic},
		{"ripUploadName_", rip_upload_name}, {"acceptRipUpload_()", accept_rip_upload}
	};
	for (size_t i = 0; i < sizeof(methods) / sizeof(methods[0]); ++i) if (strcmp(signature, methods[i].signature) == 0) return methods[i].method;
	return NULL;
}
