/* SyncTERM's synchronous conditional-cache downloader. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_WEBGET_H_
#define ST_WEBGET_H_

#include "st_tls.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ST_WEBGET_DEFAULT_MAX_SIZE (16U * 1024U * 1024U)

enum st_webget_phase {
	ST_WEBGET_RESOLVING,
	ST_WEBGET_CONNECTING,
	ST_WEBGET_TLS,
	ST_WEBGET_REQUESTING,
	ST_WEBGET_HEADERS,
	ST_WEBGET_BODY,
	ST_WEBGET_VALIDATING,
	ST_WEBGET_INSTALLING,
};

struct st_webget_progress {
	enum st_webget_phase phase;
	uint64_t received;
	uint64_t total;
	bool total_known;
};

typedef bool (*st_webget_progress_fn)(const struct st_webget_progress *progress,
    void *context);
typedef int (*st_webget_validate_fn)(const char *path, void *context);

struct st_webget_request {
	const char *url;
	const char *cache_directory;
	const char *cache_key;
	const char *output_name;
	int64_t connect_timeout_ms;
	int64_t io_timeout_ms;
	size_t maximum_size;
	st_webget_progress_fn progress;
	void *progress_context;
	st_webget_validate_fn validate;
	void *validate_context;
	const struct st_tls_client_options *tls_options;
};

enum st_webget_outcome {
	ST_WEBGET_DOWNLOADED,
	ST_WEBGET_NOT_MODIFIED,
	ST_WEBGET_FRESH_CACHE,
};

struct st_webget_result {
	enum st_webget_outcome outcome;
	uint64_t size;
};

int st_webget(struct st_webget_result *result,
    const struct st_webget_request *request);

#endif
