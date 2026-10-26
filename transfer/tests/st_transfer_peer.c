/* Native interoperability driver: the owner feeds connection input to the worker. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_transfer.h"
#include "st_connection.h"
#include "st_threads.h"
#include "st_time.h"
#include "io_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static ssize_t send_bytes(void *context, const void *data, size_t size, int64_t timeout)
{
	return st_connection_send(context, data, size, timeout);
}
int main(int argc, char **argv)
{
	if (argc < 5) return 2;
	struct st_profile profile;
	st_profile_init(&profile);
	profile.connection_type = ST_CONNECTION_RAW;
	strcpy(profile.address, "127.0.0.1");
	profile.port = (uint16_t)strtoul(argv[1], NULL, 10);
	st_connection_t *connection = NULL;
	struct st_connection_error error;
	if (st_connection_connect_raw(&connection, &profile, NULL, &error) != 0) return 3;
	struct st_transfer_config config = {.upload = strcmp(argv[2], "send") == 0,
	    .protocol = (enum st_transfer_protocol)strtol(argv[3], NULL, 10),
	    .directory = argv[4], .send = send_bytes, .send_context = connection};
	if (config.upload) { config.paths = (const char *const *)(argv + 5); config.path_count = (size_t)argc - 5; }
	else config.filename = argc > 5 ? argv[5] : NULL;
	st_transfer_t *t = NULL;
	int status = st_transfer_start(&t, &config);
	uint64_t start = 0, now = 0; st_time_monotonic_ms(&start);
	while (status == 0 && !st_transfer_done(t)) {
		uint8_t bytes[8192]; size_t size = st_transfer_input_space(t);
		if (size > sizeof(bytes)) size = sizeof(bytes);
		if (size != 0) {
			ssize_t count = st_connection_receive(connection, bytes, size, 10);
			if (count > 0) st_transfer_feed(t, bytes, (size_t)count);
			else if (count == 0 || (count < 0 && count != IO_ERROR_TIMED_OUT && count != IO_ERROR_WOULD_BLOCK)) st_transfer_eof(t);
		}
		char filename[1024];
		if (st_transfer_dialog(t, filename, sizeof(filename)) != ST_TRANSFER_DIALOG_NONE)
			st_transfer_respond(t, ST_TRANSFER_OVERWRITE, "");
		struct st_transfer_log logs[32]; size_t count = st_transfer_drain_log(t, logs, 32);
		for (size_t i = 0; i < count; ++i) if (logs[i].level <= 6 || getenv("ST_TRANSFER_DEBUG")) fprintf(stderr, "%d %s\n", logs[i].level, logs[i].text);
		st_time_monotonic_ms(&now);
		if (now - start > 30000) { st_transfer_request_abort(t); status = IO_ERROR_TIMED_OUT; }
	}
	if (status == 0) status = st_transfer_result(t);
	fprintf(stderr, "transfer result=%d\n", status);
	st_transfer_destroy(&t); st_connection_close(connection); st_profile_dispose(&profile);
	return status == 0 ? 0 : 1;
}
