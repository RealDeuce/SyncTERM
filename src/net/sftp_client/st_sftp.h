/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_SFTP_H_
#define ST_SFTP_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum st_sftp_operation {
	ST_SFTP_START,
	ST_SFTP_REALPATH,
	ST_SFTP_STAT,
	ST_SFTP_OPENDIR,
	ST_SFTP_READDIR,
	ST_SFTP_CLOSE,
	ST_SFTP_OPEN,
	ST_SFTP_READ,
	ST_SFTP_WRITE,
	ST_SFTP_MKDIR,
	ST_SFTP_RMDIR,
	ST_SFTP_REMOVE,
	ST_SFTP_RENAME,
	ST_SFTP_SET_MTIME,
	ST_SFTP_DESCRIPTIONS,
};

struct st_sftp_request {
	/* START -> STARTED; REALPATH/DESCRIPTIONS -> STRING; STAT -> STAT;
	 * OPEN/OPENDIR -> HANDLE; READ -> BYTES/EOF; READDIR -> ENTRIES/EOF;
	 * CLOSE and mutations -> OK. Any operation can complete with ERROR.
	 * Unused fields must be zero/NULL. Paths are counted by their NUL;
	 * write data and read counts use size. Handles are provider-owned IDs. */
	enum st_sftp_operation operation;
	const char *path;
	const char *second_path;
	uint64_t handle;
	uint64_t offset;
	uint64_t modified_time;
	uint32_t flags;
	const void *data;
	size_t size;
};

struct st_sftp_stat {
	uint64_t size;
	int64_t modified_time;
	int64_t accessed_time;
	uint32_t mode;
	uint32_t uid;
	uint32_t gid;
};

struct st_sftp_entry {
	const char *name;
	const char *long_name;
	struct st_sftp_stat stat;
	bool directory;
	bool has_long_description;
	const void *hash;
	size_t hash_size;
};

enum st_sftp_result_kind {
	ST_SFTP_RESULT_STARTED,
	ST_SFTP_RESULT_OK,
	ST_SFTP_RESULT_EOF,
	ST_SFTP_RESULT_STRING,
	ST_SFTP_RESULT_BYTES,
	ST_SFTP_RESULT_HANDLE,
	ST_SFTP_RESULT_STAT,
	ST_SFTP_RESULT_ENTRIES,
	ST_SFTP_RESULT_ERROR,
};

/* Response pointers are borrowed until the next poll() or provider teardown. */
struct st_sftp_response {
	uint64_t request_id;
	enum st_sftp_result_kind kind;
	const void *data;
	size_t size;
	uint64_t handle;
	struct st_sftp_stat stat;
	const struct st_sftp_entry *entries;
	size_t entry_count;
	int error_code;
	uint32_t server_status;
	const char *message;
	bool transient;
};

struct st_sftp_provider {
	void *context;
	bool (*available)(void *context);
	const char *(*public_directory)(void *context);
	bool (*long_name_extension)(void *context);
	/* submit() must copy every borrowed request pointer before returning and
	 * return an ID unique among requests that have not yet been polled. */
	int (*submit)(void *context, const struct st_sftp_request *request,
		uint64_t *request_id);
	/* Returns one for a response, zero when none is ready, or an error. */
	int (*poll)(void *context, struct st_sftp_response *response,
		uint32_t timeout_ms);
	void (*cancel_all)(void *context);
};

/* Static symbolic name for a library/host error or SSH_FX_* server status. */
const char *st_sftp_error_name(int code, uint32_t server_status);

#endif
