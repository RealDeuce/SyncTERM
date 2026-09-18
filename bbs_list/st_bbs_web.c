/* Web-backed BBS directory refresh. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_bbs_list_internal.h"

#include "filesystem.h"
#include "ini.h"
#include "io_error.h"
#include "path.h"
#include "st_app_paths.h"
#include "st_settings.h"
#include "st_threads.h"
#include "st_time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
validate_list(const char *path, void *context)
{
	ini_document_t *document = NULL;
	FILE *stream;
	int status;
	(void)context;

	status = fs_open(&stream, path, "rb");
	if (status < 0)
		return status;
	status = ini_document_read(&document, stream);
	if (fclose(stream) != 0 && status == 0)
		status = IO_ERROR_IO;
	if (status == 0
	    && ini_document_section_count(document) > ST_BBS_LIST_MAX_ENTRIES)
		status = IO_ERROR_OUT_OF_RANGE;
	ini_document_free(&document);
	return status;
}

static char *
output_name(const char *name)
{
	size_t length = strlen(name);
	char *result = malloc(length + sizeof(".lst"));

	if (result != NULL) {
		memcpy(result, name, length);
		memcpy(result + length, ".lst", sizeof(".lst"));
	}
	return result;
}

static int
fetch_one(struct st_webget_result *result, const char *cache_directory,
    const char *name, const char *url, st_webget_progress_fn progress,
    void *progress_context)
{
	struct st_webget_result ignored;
	char *output;
	int status;

	if (!path_untrusted_component_valid(name))
		return IO_ERROR_INVALID_ARGUMENT;
	if (result == NULL)
		result = &ignored;
	output = output_name(name);
	if (output == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	{
		const struct st_webget_request request = {
			.url = url,
			.cache_directory = cache_directory,
			.cache_key = name,
			.output_name = output,
			.connect_timeout_ms = 30000,
			.io_timeout_ms = 5000,
			.maximum_size = ST_WEBGET_DEFAULT_MAX_SIZE,
			.progress = progress,
			.progress_context = progress_context,
			.validate = validate_list,
		};
		status = st_webget(result, &request);
	}
	free(output);
	return status;
}

struct refresh_batch;

struct refresh_worker {
	struct st_bbs_web_refresh_item public;
	const char *url;
	struct refresh_batch *batch;
	st_thread_t thread;
	bool started;
};

struct refresh_batch {
	struct refresh_worker *workers;
	size_t count;
	size_t complete;
	const char *cache_directory;
	st_mutex_t mutex;
	st_condition_t changed;
};

static bool
worker_progress(const struct st_webget_progress *progress, void *context)
{
	struct refresh_worker *worker = context;
	struct refresh_batch *batch = worker->batch;

	if (st_mutex_lock(&batch->mutex) != ST_THREAD_SUCCESS)
		return false;
	worker->public.state = ST_BBS_WEB_REFRESH_ACTIVE;
	worker->public.phase = progress->phase;
	worker->public.received = progress->received;
	worker->public.total = progress->total;
	worker->public.total_known = progress->total_known;
	(void)st_mutex_unlock(&batch->mutex);
	return true;
}

static int
refresh_worker(void *context)
{
	struct refresh_worker *worker = context;
	struct refresh_batch *batch = worker->batch;
	struct st_webget_result result = {0};
	int status = fetch_one(&result, batch->cache_directory,
	    worker->public.name, worker->url, worker_progress, worker);
	(void)st_mutex_lock(&batch->mutex);
	worker->public.status = status;
	worker->public.outcome = result.outcome;
	worker->public.state = status == 0 ? ST_BBS_WEB_REFRESH_COMPLETE
	    : ST_BBS_WEB_REFRESH_FAILED;
	batch->complete++;
	(void)st_condition_signal(&batch->changed);
	(void)st_mutex_unlock(&batch->mutex);
	return 0;
}

static void
report_batch(struct refresh_batch *batch,
	st_bbs_web_refresh_progress_fn progress, void *context,
	struct st_bbs_web_refresh_item *snapshot)
{
	if (progress == NULL)
		return;
	(void)st_mutex_lock(&batch->mutex);
	for (size_t i = 0; i < batch->count; ++i)
		snapshot[i] = batch->workers[i].public;
	(void)st_mutex_unlock(&batch->mutex);
	progress(snapshot, batch->count, context);
}

int
st_bbs_web_cache_refresh(const struct st_app_paths *paths,
	st_bbs_web_refresh_progress_fn progress, void *progress_context)
{
	struct refresh_batch batch = {0};
	struct st_bbs_web_refresh_item *snapshot = NULL;
	size_t named = st_settings_web_download_count();
	size_t system = st_settings.download_system_list
	    && st_settings.stored_list_path != NULL ? 1 : 0;
	size_t initialized = 0;
	int first_error = 0;
	int result = 0;

	if (paths == NULL || paths->system_cache_directory == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (named > SIZE_MAX - system)
		return IO_ERROR_OUT_OF_RANGE;
	batch.count = named + system;
	if (batch.count == 0)
		return 0;
	if (batch.count > SIZE_MAX / sizeof(batch.workers[0])
	    || batch.count > SIZE_MAX / sizeof(snapshot[0]))
		return IO_ERROR_OUT_OF_RANGE;
	batch.cache_directory = paths->system_cache_directory;
	batch.workers = calloc(batch.count, sizeof(batch.workers[0]));
	if (batch.workers == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	if (progress != NULL) {
		snapshot = malloc(batch.count * sizeof(snapshot[0]));
		if (snapshot == NULL) {
			free(batch.workers);
			return IO_ERROR_OUT_OF_MEMORY;
		}
	}
	if (st_mutex_init(&batch.mutex, ST_MUTEX_PLAIN) != ST_THREAD_SUCCESS) {
		result = IO_ERROR_IO;
		goto finish;
	}
	initialized = 1;
	if (st_condition_init(&batch.changed) != ST_THREAD_SUCCESS) {
		result = IO_ERROR_IO;
		goto finish;
	}
	initialized = 2;
	for (size_t i = 0; i < batch.count; ++i) {
		struct refresh_worker *worker = &batch.workers[i];
		const struct name_value *item;

		worker->batch = &batch;
		worker->public.state = ST_BBS_WEB_REFRESH_PENDING;
		worker->public.status = IO_ERROR_IN_PROGRESS;
		if (system != 0 && i == 0) {
			worker->public.name = "System List";
			worker->url = st_settings.stored_list_path;
		}
		else {
			item = st_settings_web_download_at(i - system);
			worker->public.name = item->name;
			worker->url = item->value;
		}
		if (st_thread_create(&worker->thread, refresh_worker, worker)
		    == ST_THREAD_SUCCESS) {
			worker->started = true;
		}
		else {
			(void)st_mutex_lock(&batch.mutex);
			worker->public.state = ST_BBS_WEB_REFRESH_FAILED;
			worker->public.status = IO_ERROR_IO;
			batch.complete++;
			(void)st_mutex_unlock(&batch.mutex);
		}
	}
	report_batch(&batch, progress, progress_context, snapshot);
	(void)st_mutex_lock(&batch.mutex);
	while (batch.complete < batch.count) {
		struct timespec deadline;
		int wait_status;

		if (st_time_utc_deadline_after_ms(&deadline, 200) != 0)
			wait_status = st_condition_wait(&batch.changed, &batch.mutex);
		else
			wait_status = st_condition_timedwait(&batch.changed,
			    &batch.mutex, &deadline);
		if (wait_status != ST_THREAD_SUCCESS
		    && wait_status != ST_THREAD_TIMED_OUT) {
			result = IO_ERROR_IO;
			break;
		}
		(void)st_mutex_unlock(&batch.mutex);
		report_batch(&batch, progress, progress_context, snapshot);
		(void)st_mutex_lock(&batch.mutex);
	}
	(void)st_mutex_unlock(&batch.mutex);
	for (size_t i = 0; i < batch.count; ++i) {
		int thread_result;

		if (batch.workers[i].started
		    && st_thread_join(batch.workers[i].thread, &thread_result)
		    != ST_THREAD_SUCCESS && result == 0)
			result = IO_ERROR_IO;
		if (first_error == 0 && batch.workers[i].public.status < 0)
			first_error = batch.workers[i].public.status;
	}
	report_batch(&batch, progress, progress_context, snapshot);
	if (result == 0)
		result = first_error;

finish:
	if (initialized >= 2)
		st_condition_destroy(&batch.changed);
	if (initialized >= 1)
		st_mutex_destroy(&batch.mutex);
	free(snapshot);
	free(batch.workers);
	return result;
}

static int
rebuild_after_refresh(st_bbs_list_t *list, int status)
{
	if (status != 0)
		return status;
	status = st_bbs_list_rebuild(list);
	if (status == 0 && list->sort_field_count != 0)
		status = st_bbs_list_sort(list, list->sort_fields,
		    list->sort_field_count);
	if (status == 0)
		list->generation++;
	return status;
}

int
st_bbs_list_refresh_web(st_bbs_list_t *list,
    st_webget_progress_fn progress, void *progress_context)
{
	int status = 0;

	if (list == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (st_settings.download_system_list
	    && st_settings.stored_list_path != NULL) {
		status = fetch_one(NULL, list->system_cache_path, "System List",
		    st_settings.stored_list_path, progress, progress_context);
	}
	for (size_t i = 0; status == 0
	    && i < st_settings_web_download_count(); ++i) {
		const struct name_value *item = st_settings_web_download_at(i);
		status = fetch_one(NULL, list->system_cache_path, item->name,
		    item->value, progress, progress_context);
	}
	return rebuild_after_refresh(list, status);
}

int
st_bbs_list_refresh_web_at(st_bbs_list_t *list, size_t index,
	st_webget_progress_fn progress, void *progress_context)
{
	const struct name_value *item;
	int status;

	if (list == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	item = st_settings_web_download_at(index);
	if (item == NULL)
		return IO_ERROR_OUT_OF_RANGE;
	status = fetch_one(NULL, list->system_cache_path, item->name, item->value,
	    progress, progress_context);
	return rebuild_after_refresh(list, status);
}
