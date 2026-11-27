/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "filesystem.h"
#include "st_ddofx.h"
#include "string_util.h"
#include "value_convert_time.h"

static bool
verb(const struct st_ddofx *ddo, const char *name, size_t length)
{
	return ddo->length >= length && ddo->body[0] == (uint8_t)name[0]
	       && ascii_case_ncompare((const char *)ddo->body + 1, name + 1, length - 1) == 0;
}

static bool
matches(const struct st_ddofx *ddo, const char *name)
{
	size_t length = strlen(name);
	return ddo->length == length && verb(ddo, name, length);
}

static bool
argument(const struct st_ddofx *ddo, const char *name, long *value)
{
	size_t length = strlen(name);
	if (!verb(ddo, name, length) || ddo->body[length] != ',')
		return false;
	const char *start = (const char *)ddo->body + length + 1;
	char       *end;
	errno    = 0;
	long num = strtol(start, &end, 10);
	if (start == end || *end != 0 || errno == ERANGE)
		return false;
	*value = num;
	return true;
}

static void
query_reply(const struct st_ddofx_sink *sink, const char *text)
{
	uint8_t reply[ST_DDOFX_BODY_MAX + 1];
	size_t  n = strlen(text);
	memcpy(reply, text, n);
	reply[n] = 0xff;
	sink->reply(sink->context, reply, n + 1);
}

static void
file_query(const struct st_ddofx_sink *sink, const char *category, const char *client, const char *name)
{
	FILE          *file = sink->open_asset(sink->context, category, client, name);
	struct fs_info info;
	bool           found = file != NULL && fs_get_stream_info(&info, file) == 0 && info.kind == FS_ENTRY_FILE;
	if (file != NULL)
		fclose(file);
	struct tm local;
	if (found) {
		time_t stamp = (time_t)info.modified_time;
		found        = (int64_t)stamp == info.modified_time && value_convert_localtime(stamp, &local);
	}
	if (!found) {
		query_reply(sink, client == NULL ? "NO" : "N");
		return;
	}
	char record[100];
	/* The SDK's timestamp comes from DOS file time, in two-second units. */
	snprintf(record, sizeof(record), "%" PRIu64 ",%d,%d,%d,%d,%d,%d", info.size, local.tm_year + 1900,
	    local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec & ~1);
	query_reply(sink, record);
}

static void
music(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *filename)
{
	if (ascii_case_compare(filename, "NONE") == 0) {
		st_tracker_destroy(ddo->player);
		ddo->player        = NULL;
		ddo->music_name[0] = 0;
		return;
	}
	if (!st_audio_runtime_is_pull(sink->audio) && st_audio_runtime_allowed(sink->audio) == 0)
		return;
	FILE *file = sink->open_asset(sink->context, "MUSIC", NULL, filename);
	if (file == NULL)
		return;
	struct st_tracker *next = st_tracker_play(sink->audio, file, ddo->volume);
	fclose(file);
	if (next != NULL) {
		st_tracker_destroy(ddo->player);
		ddo->player = next;
		memcpy(ddo->music_name, filename, strlen(filename) + 1);
	}
}

void
st_ddofx_reset(struct st_ddofx *ddo)
{
	st_tracker_destroy(ddo->player);
	*ddo = (struct st_ddofx) {.volume = 64};
}

static void
execute(struct st_ddofx *ddo, const struct st_ddofx_sink *sink)
{
	if (matches(ddo, "~HEARCMD"))
		ddo->ignoring = false;
	else if (matches(ddo, "~IGNORCMD"))
		ddo->ignoring = true;
	else if (ddo->ignoring)
		return;
	else if (matches(ddo, "dLITERM"))
		query_reply(sink, "LITERM Ver. 2.00");
	else if (matches(ddo, "dGETMUSIC"))
		query_reply(sink, ddo->player == NULL ? "NONE" : ddo->music_name);
	else if (matches(ddo, "dMUSICON"))
		query_reply(sink,
		    st_audio_runtime_is_pull(sink->audio) || st_audio_runtime_allowed(sink->audio) ? "YES" : "NO");
	else if (ddo->body[0] == 'd' && ddo->length >= 2 && strchr("mftr", ddo->body[1]) != NULL) {
		const char *category = ddo->body[1] == 'm' ? "MUSIC" : ddo->body[1] == 'f' ? "FONTS" : "BBSTHEME";
		file_query(sink, category, NULL, (const char *)ddo->body + 2);
	}
	else if (verb(ddo, "8CDIR,", 6))
		query_reply(sink, sink->client_exists(sink->context, (const char *)ddo->body + 6) ? "Y" : "N");
	else if (verb(ddo, "8CFILE,", 7)) {
		char *client = (char *)ddo->body + 7;
		char *comma  = strchr(client, ',');
		if (comma != NULL) {
			*comma = 0;
			file_query(sink, "TERMS", client, comma + 1);
		}
		else
			query_reply(sink, "N");
	}
	else if (ddo->body[0] == 'm')
		music(ddo, sink, (const char *)ddo->body + 1);
	else if (matches(ddo, "MMPR"))
		st_tracker_toggle_pause(ddo->player);
	else if (matches(ddo, "MGVOL")) {
		char volume[4];
		snprintf(volume, sizeof(volume), "%u", ddo->volume);
		query_reply(sink, volume);
	}
	else {
		long value;
		if (argument(ddo, "MMPAT", &value) && (value == -1 || value == 1))
			st_tracker_step_order(ddo->player, (int)value);
		else if (argument(ddo, "MSVOL", &value) && value >= 0 && value <= 64)
			ddo->volume = (unsigned)value;
		else if (argument(ddo, "MMVOL", &value)) {
			if (value <= -(long)ddo->volume)
				ddo->volume = 0;
			else if (value >= 64 - (long)ddo->volume)
				ddo->volume = 64;
			else
				ddo->volume = (unsigned)((long)ddo->volume + value);
		}
		else
			return;
		st_tracker_set_volume(ddo->player, ddo->volume);
	}
}

int
st_ddofx_feed(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const uint8_t *bytes, size_t size,
    size_t *consumed)
{
	*consumed = 0;
	while (*consumed < size) {
		const uint8_t *here = bytes + *consumed;
		if (ddo->phase == ST_DDO_TEXT && *here != 0x1d) {
			size_t         remaining = size - *consumed;
			const uint8_t *gs        = memchr(here, 0x1d, remaining);
			size_t         length    = gs == NULL ? remaining : (size_t)(gs - here);
			size_t         used      = 0;
			int            status    = sink->text(sink->context, here, length, &used);
			*consumed += used;
			if (status != 0 || used < length)
				return status;
			continue;
		}
		uint8_t byte = *here;
		switch (ddo->phase) {
			case ST_DDO_TEXT:
				ddo->phase = ST_DDO_PREFIX;
				break;
			case ST_DDO_PREFIX:
				if (byte == '{') {
					ddo->phase  = ST_DDO_BODY;
					ddo->length = 0;
				}
				else if (byte != 0x1d && byte != 0) {
					ddo->phase = ST_DDO_TEXT;
					continue;
				}
				break;
			case ST_DDO_BODY:
				if (byte == '}') {
					ddo->phase             = ST_DDO_TEXT;
					ddo->body[ddo->length] = 0;
					execute(ddo, sink);
				}
				else if (byte == '\r' || byte == '\n') {
					ddo->phase = ST_DDO_TEXT;
					continue;
				}
				else if (byte != 0) {
					if (ddo->length == ST_DDOFX_BODY_MAX)
						ddo->phase = ST_DDO_DISCARD;
					else
						ddo->body[ddo->length++] = byte;
				}
				break;
			case ST_DDO_DISCARD:
				if (byte == '}')
					ddo->phase = ST_DDO_TEXT;
				else if (byte == '\r' || byte == '\n') {
					ddo->phase = ST_DDO_TEXT;
					continue;
				}
				else if (byte == 0x1d)
					ddo->phase = ST_DDO_PREFIX;
				break;
		}
		++*consumed;
	}
	return 0;
}
