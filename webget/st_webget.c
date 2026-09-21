/* SyncTERM's synchronous HTTP conditional-cache downloader. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_webget.h"

#include "filesystem.h"
#include "ini.h"
#include "io_error.h"
#include "path.h"
#include "socket.h"
#include "st_file_lock.h"
#include "string_compat.h"
#include "string_util.h"
#include "tcp_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define HEADER_LIMIT (64U * 1024U)
#define LINE_LIMIT 8192U
#define DEFAULT_FRESHNESS_LIFETIME UINT64_C(86400)

struct parsed_url {
	bool tls;
	char *host;
	char *host_header;
	char *target;
	uint16_t port;
};

struct cache_info {
	char *url;
	char *etag;
	char *last_modified;
	int64_t response_time;
	int64_t date;
	int64_t expires;
	uint64_t max_age;
	uint64_t age;
	bool have_date;
	bool have_expires;
	bool have_max_age;
	bool no_cache;
	bool must_revalidate;
	bool immutable;
};

struct transport {
	socket_handle_t socket;
	st_tls_session_t *tls;
	int64_t timeout_ms;
};

struct reader {
	struct transport *transport;
	unsigned char buffer[16384];
	size_t begin;
	size_t end;
};

struct response_info {
	int status;
	bool chunked;
	bool have_length;
	uint64_t length;
	char *etag;
	char *last_modified;
	int64_t date;
	int64_t expires;
	uint64_t max_age;
	uint64_t age;
	bool have_date;
	bool have_expires;
	bool have_max_age;
	bool have_age;
	bool have_cache_control;
	bool no_cache;
	bool must_revalidate;
	bool immutable;
};

static char *
join_path(const char *directory, const char *name)
{
	char *result = NULL;

	return path_join_alloc(&result, directory, name) == 0 ? result : NULL;
}

static char *
suffix_name(const char *name, const char *suffix)
{
	size_t a = strlen(name);
	size_t b = strlen(suffix);
	char *result = malloc(a + b + 1);

	if (result != NULL) {
		memcpy(result, name, a);
		memcpy(result + a, suffix, b + 1);
	}
	return result;
}

static int
parse_port(const char *text, size_t size, uint16_t *result)
{
	unsigned value = 0;

	if (size == 0)
		return IO_ERROR_INVALID_FORMAT;
	for (size_t i = 0; i < size; ++i) {
		unsigned digit;
		if (text[i] < '0' || text[i] > '9')
			return IO_ERROR_INVALID_FORMAT;
		digit = (unsigned)(text[i] - '0');
		if (value > (UINT16_MAX - digit) / 10)
			return IO_ERROR_INVALID_FORMAT;
		value = value * 10 + digit;
	}
	if (value == 0)
		return IO_ERROR_INVALID_FORMAT;
	*result = (uint16_t)value;
	return 0;
}

static int
parse_url(struct parsed_url *result, const char *url)
{
	const char *authority;
	const char *path;
	const char *host_begin;
	const char *host_end;
	const char *port = NULL;
	size_t host_size;
	size_t authority_size;

	memset(result, 0, sizeof(*result));
	if (strlen(url) >= 7 && ascii_case_ncompare(url, "http://", 7) == 0) {
		result->port = 80;
		authority = url + 7;
	} else if (strlen(url) >= 8 && ascii_case_ncompare(url, "https://", 8) == 0) {
		result->tls = true;
		result->port = 443;
		authority = url + 8;
	} else
		return IO_ERROR_INVALID_ARGUMENT;
	path = authority + strcspn(authority, "/?#");
	if (*path == '#')
		return IO_ERROR_INVALID_FORMAT;
	authority_size = (size_t)(path - authority);
	if (authority_size == 0 || memchr(authority, '@', authority_size) != NULL
	    || memchr(authority, '#', authority_size) != NULL)
		return IO_ERROR_INVALID_FORMAT;
	for (size_t i = 0; i < authority_size; ++i) {
		unsigned char value = (unsigned char)authority[i];
		if (value <= ' ' || value == 0x7f)
			return IO_ERROR_INVALID_FORMAT;
	}
	host_begin = authority;
	if (*host_begin == '[') {
		host_begin++;
		host_end = memchr(host_begin, ']', authority_size - 1);
		if (host_end == NULL)
			return IO_ERROR_INVALID_FORMAT;
		if ((size_t)(host_end - authority + 1) < authority_size) {
			if (host_end[1] != ':')
				return IO_ERROR_INVALID_FORMAT;
			port = host_end + 2;
		}
	} else {
		host_end = memchr(authority, ':', authority_size);
		if (host_end == NULL)
			host_end = authority + authority_size;
		else
			port = host_end + 1;
	}
	host_size = (size_t)(host_end - host_begin);
	if (host_size == 0)
		return IO_ERROR_INVALID_FORMAT;
	if (port != NULL) {
		int status = parse_port(port,
		    authority_size - (size_t)(port - authority), &result->port);
		if (status < 0)
			return status;
	}
	result->host = strndup(host_begin, host_size);
	result->host_header = strndup(authority, authority_size);
	if (strchr(path, '#') != NULL)
		return IO_ERROR_INVALID_FORMAT;
	for (const char *scan = path; *scan != '\0'; ++scan) {
		unsigned char value = (unsigned char)*scan;
		if (value <= ' ' || value == 0x7f)
			return IO_ERROR_INVALID_FORMAT;
	}
	if (*path == '?') {
		size_t length = strlen(path);
		result->target = malloc(length + 2);
		if (result->target != NULL) {
			result->target[0] = '/';
			memcpy(result->target + 1, path, length + 1);
		}
	} else
		result->target = *path == '\0' ? strdup("/") : strdup(path);
	if (result->host == NULL || result->host_header == NULL
	    || result->target == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	return 0;
}

static void
free_url(struct parsed_url *url)
{
	free(url->host);
	free(url->host_header);
	free(url->target);
}

static void
free_cache_info(struct cache_info *info)
{
	free(info->url);
	free(info->etag);
	free(info->last_modified);
	memset(info, 0, sizeof(*info));
}

static int
load_cache_info(struct cache_info *info, const char *path)
{
	ini_document_t *document = NULL;
	ini_section_t *root;
	FILE *stream;
	int status = fs_open(&stream, path, "rb");

	memset(info, 0, sizeof(*info));
	if (status == IO_ERROR_NOT_FOUND)
		return 0;
	if (status < 0)
		return status;
	status = ini_document_read(&document, stream);
	(void)fclose(stream);
	if (status < 0)
		return status;
	root = ini_document_root(document);
	{
		const char *value = ini_section_find(root, "URL");
		if (value != NULL && (info->url = strdup(value)) == NULL)
			status = IO_ERROR_OUT_OF_MEMORY;
		value = ini_section_find(root, "ETag");
		if (status == 0 && value != NULL
		    && (info->etag = strdup(value)) == NULL)
			status = IO_ERROR_OUT_OF_MEMORY;
		value = ini_section_find(root, "LastModified");
		if (status == 0 && value != NULL
		    && (info->last_modified = strdup(value)) == NULL)
			status = IO_ERROR_OUT_OF_MEMORY;
	}
	if (status == 0) {
		info->response_time = ini_section_get_int64(root, "ResponseTime", 0);
		info->date = ini_section_get_int64(root, "Date", 0);
		info->expires = ini_section_get_int64(root, "Expires", 0);
		info->max_age = ini_section_get_uint64(root, "MaxAge", 0);
		info->age = ini_section_get_uint64(root, "Age", 0);
		info->have_date = ini_section_get_bool(root, "HaveDate", false);
		info->have_expires = ini_section_get_bool(root, "HaveExpires", false);
		info->have_max_age = ini_section_get_bool(root, "HaveMaxAge", false);
		info->no_cache = ini_section_get_bool(root, "NoCache", false);
		info->must_revalidate = ini_section_get_bool(root, "MustRevalidate", false);
		info->immutable = ini_section_get_bool(root, "Immutable", false);
	}
	ini_document_free(&document);
	if (status < 0)
		free_cache_info(info);
	return status;
}

static int
write_cache_info(const struct cache_info *info, const char *path)
{
	ini_document_t *document = NULL;
	ini_section_t *root;
	FILE *stream;
	int status = ini_document_create(&document);

	if (status < 0)
		return status;
	root = ini_document_root(document);
#define SET_INFO(call) do { status = (call); if (status < 0) goto finish; } while (0)
	if (info->url != NULL)
		SET_INFO(ini_section_set_string(root, "URL", info->url,
		    &ini_default_style));
	if (info->etag != NULL)
		SET_INFO(ini_section_set_string(root, "ETag", info->etag,
		    &ini_default_style));
	if (info->last_modified != NULL)
		SET_INFO(ini_section_set_string(root, "LastModified",
		    info->last_modified, &ini_default_style));
	SET_INFO(ini_section_set_int64(root, "ResponseTime", info->response_time,
	    &ini_default_style));
	SET_INFO(ini_section_set_int64(root, "Date", info->date,
	    &ini_default_style));
	SET_INFO(ini_section_set_int64(root, "Expires", info->expires,
	    &ini_default_style));
	SET_INFO(ini_section_set_uint64(root, "MaxAge", info->max_age,
	    &ini_default_style));
	SET_INFO(ini_section_set_uint64(root, "Age", info->age,
	    &ini_default_style));
	SET_INFO(ini_section_set_bool(root, "HaveDate", info->have_date,
	    &ini_default_style));
	SET_INFO(ini_section_set_bool(root, "HaveExpires", info->have_expires,
	    &ini_default_style));
	SET_INFO(ini_section_set_bool(root, "HaveMaxAge", info->have_max_age,
	    &ini_default_style));
	SET_INFO(ini_section_set_bool(root, "NoCache", info->no_cache,
	    &ini_default_style));
	SET_INFO(ini_section_set_bool(root, "MustRevalidate", info->must_revalidate,
	    &ini_default_style));
	SET_INFO(ini_section_set_bool(root, "Immutable", info->immutable,
	    &ini_default_style));
	status = fs_open(&stream, path, "wb");
	if (status == 0) {
		status = ini_document_write(document, stream);
		if (fclose(stream) != 0 && status == 0)
			status = IO_ERROR_IO;
	}
finish:
	ini_document_free(&document);
	return status;
#undef SET_INFO
}

static bool
report_progress(const struct st_webget_request *request,
    enum st_webget_phase phase, uint64_t received, uint64_t total, bool known)
{
	const struct st_webget_progress value = { phase, received, total, known };

	return request->progress == NULL
	    || request->progress(&value, request->progress_context);
}

static int
transport_send(struct transport *transport, const void *data, size_t size)
{
	size_t offset = 0;

	while (offset < size) {
		if (transport->tls != NULL) {
			size_t sent = 0;
			int status = st_tls_send(transport->tls,
			    (const unsigned char *)data + offset, size - offset,
			    transport->timeout_ms, &sent);
			if (status < 0)
				return status;
			offset += sent;
		} else {
			unsigned events;
			int status = socket_wait(transport->socket, SOCKET_WAIT_WRITABLE,
			    transport->timeout_ms, &events);
			ssize_t sent;
			if (status < 0)
				return status;
			sent = socket_send(transport->socket,
			    (const unsigned char *)data + offset, size - offset);
			if (sent <= 0)
				return sent == 0 ? IO_ERROR_CONNECTION_RESET : (int)sent;
			offset += (size_t)sent;
		}
	}
	return 0;
}

static int
transport_receive(struct transport *transport, void *data, size_t size,
    size_t *received)
{
	if (transport->tls != NULL)
		return st_tls_receive(transport->tls, data, size,
		    transport->timeout_ms, received);
	{
		unsigned events;
		int status = socket_wait(transport->socket, SOCKET_WAIT_READABLE,
		    transport->timeout_ms, &events);
		ssize_t count;
		if (status < 0)
			return status;
		count = socket_receive(transport->socket, data, size,
		    SOCKET_RECEIVE_NONE);
		if (count < 0)
			return (int)count;
		*received = (size_t)count;
		return 0;
	}
}

static int
reader_fill(struct reader *reader)
{
	size_t received = 0;
	int status = transport_receive(reader->transport, reader->buffer,
	    sizeof(reader->buffer), &received);

	if (status < 0)
		return status;
	reader->begin = 0;
	reader->end = received;
	return received == 0 ? IO_ERROR_NOT_CONNECTED : 0;
}

static int
reader_byte(struct reader *reader, unsigned char *value)
{
	int status;
	if (reader->begin == reader->end) {
		status = reader_fill(reader);
		if (status < 0)
			return status;
	}
	*value = reader->buffer[reader->begin++];
	return 0;
}

static int
reader_line(struct reader *reader, char *line, size_t capacity,
    size_t *header_bytes)
{
	size_t length = 0;
	unsigned char value;
	int status;

	for (;;) {
		status = reader_byte(reader, &value);
		if (status < 0)
			return status;
		if (++*header_bytes > HEADER_LIMIT || length + 1 >= capacity)
			return IO_ERROR_MESSAGE_TOO_LARGE;
		if (value == '\n')
			break;
		if (value != '\r')
			line[length++] = (char)value;
	}
	line[length] = '\0';
	return 0;
}

static char *
trim_value(char *value)
{
	char *end;
	while (*value == ' ' || *value == '\t')
		value++;
	end = value + strlen(value);
	while (end != value && (end[-1] == ' ' || end[-1] == '\t'))
		*--end = '\0';
	return value;
}

static bool
parse_uint64(const char *text, uint64_t *result)
{
	uint64_t value = 0;
	if (*text == '\0')
		return false;
	while (*text != '\0') {
		unsigned digit;
		if (*text < '0' || *text > '9')
			return false;
		digit = (unsigned)(*text++ - '0');
		if (value > (UINT64_MAX - digit) / 10)
			return false;
		value = value * 10 + digit;
	}
	*result = value;
	return true;
}

static int
ascii_month(const char *text)
{
	static const char names[] = "JanFebMarAprMayJunJulAugSepOctNovDec";

	for (int month = 0; month < 12; ++month) {
		if (ascii_case_ncompare(text, names + month * 3, 3) == 0)
			return month + 1;
	}
	return 0;
}

static bool
fixed_decimal(const char *text, size_t count, unsigned *result)
{
	unsigned value = 0;

	for (size_t i = 0; i < count; ++i) {
		if (text[i] < '0' || text[i] > '9')
			return false;
		value = value * 10 + (unsigned)(text[i] - '0');
	}
	*result = value;
	return true;
}

static bool
leap_year(unsigned year)
{
	return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static bool
parse_http_date(const char *text, int64_t *result)
{
	static const unsigned days_in_month[] = {
		31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
	};
	unsigned day;
	unsigned year;
	unsigned hour;
	unsigned minute;
	unsigned second;
	int month;
	int adjusted_year;
	unsigned adjusted_month;
	int era;
	unsigned year_of_era;
	unsigned day_of_year;
	unsigned day_of_era;
	int64_t days;

	/* RFC 9110's preferred IMF-fixdate form, interpreted as US-ASCII. */
	if (strlen(text) != 29 || text[3] != ',' || text[4] != ' '
	    || text[7] != ' ' || text[11] != ' ' || text[16] != ' '
	    || text[19] != ':' || text[22] != ':' || text[25] != ' '
	    || ascii_case_ncompare(text + 26, "GMT", 3) != 0
	    || !fixed_decimal(text + 5, 2, &day)
	    || !fixed_decimal(text + 12, 4, &year)
	    || !fixed_decimal(text + 17, 2, &hour)
	    || !fixed_decimal(text + 20, 2, &minute)
	    || !fixed_decimal(text + 23, 2, &second))
		return false;
	month = ascii_month(text + 8);
	if (month == 0 || year < 1601 || day == 0
	    || day > days_in_month[month - 1]
		+ (month == 2 && leap_year(year) ? 1U : 0U)
	    || hour > 23 || minute > 59 || second > 59)
		return false;
	/* Gregorian civil date to days relative to 1970-01-01. */
	adjusted_year = (int)year - (month <= 2 ? 1 : 0);
	era = adjusted_year / 400;
	year_of_era = (unsigned)(adjusted_year - era * 400);
	adjusted_month = (unsigned)(month + (month > 2 ? -3 : 9));
	day_of_year = (153 * adjusted_month + 2) / 5 + day - 1;
	day_of_era = year_of_era * 365 + year_of_era / 4
	    - year_of_era / 100 + day_of_year;
	days = (int64_t)era * 146097 + day_of_era - 719468;
	*result = days * 86400 + (int64_t)hour * 3600
	    + (int64_t)minute * 60 + second;
	return true;
}

static void
parse_cache_control(struct response_info *response, char *value)
{
	char *item = value;
	while (item != NULL) {
		char *next = strchr(item, ',');
		char *token;
		if (next != NULL)
			*next++ = '\0';
		token = trim_value(item);
		if (strlen(token) == 8 && ascii_case_ncompare(token, "no-cache", 8) == 0)
			response->no_cache = true;
		else if (strlen(token) == 15
		    && ascii_case_ncompare(token, "must-revalidate", 15) == 0)
			response->must_revalidate = true;
		else if (strlen(token) == 9 && ascii_case_ncompare(token, "immutable", 9) == 0)
			response->immutable = true;
		else if (strlen(token) >= 8 && ascii_case_ncompare(token, "max-age=", 8) == 0
		    && parse_uint64(token + 8, &response->max_age))
			response->have_max_age = true;
		item = next;
	}
}

static int
parse_headers(struct reader *reader, struct response_info *response)
{
	char line[LINE_LIMIT];
	size_t bytes = 0;
	int status = reader_line(reader, line, sizeof(line), &bytes);
	char *code;

	if (status < 0)
		return status;
	if (strlen(line) < 12 || (ascii_case_ncompare(line, "HTTP/1.0 ", 9) != 0
	    && ascii_case_ncompare(line, "HTTP/1.1 ", 9) != 0))
		return IO_ERROR_PROTOCOL;
	code = line + 9;
	if (code[0] < '0' || code[0] > '9' || code[1] < '0' || code[1] > '9'
	    || code[2] < '0' || code[2] > '9')
		return IO_ERROR_PROTOCOL;
	response->status = (code[0] - '0') * 100 + (code[1] - '0') * 10
	    + (code[2] - '0');
	for (;;) {
		char *colon;
		char *value;
		status = reader_line(reader, line, sizeof(line), &bytes);
		if (status < 0)
			return status;
		if (line[0] == '\0')
			break;
		colon = strchr(line, ':');
		if (colon == NULL)
			return IO_ERROR_PROTOCOL;
		*colon = '\0';
		value = trim_value(colon + 1);
		if (strlen(line) == 14 && ascii_case_ncompare(line, "Content-Length", 14) == 0) {
			if (!parse_uint64(value, &response->length))
				return IO_ERROR_PROTOCOL;
			response->have_length = true;
		} else if (strlen(line) == 17
		    && ascii_case_ncompare(line, "Transfer-Encoding", 17) == 0) {
			if (strlen(value) != 7 || ascii_case_ncompare(value, "chunked", 7) != 0)
				return IO_ERROR_NOT_SUPPORTED;
			response->chunked = true;
		} else if (strlen(line) == 4 && ascii_case_ncompare(line, "ETag", 4) == 0) {
			free(response->etag);
			response->etag = strdup(value);
			if (response->etag == NULL)
				return IO_ERROR_OUT_OF_MEMORY;
		} else if (strlen(line) == 13
		    && ascii_case_ncompare(line, "Last-Modified", 13) == 0) {
			free(response->last_modified);
			response->last_modified = strdup(value);
			if (response->last_modified == NULL)
				return IO_ERROR_OUT_OF_MEMORY;
		} else if (strlen(line) == 13
		    && ascii_case_ncompare(line, "Cache-Control", 13) == 0) {
			response->have_cache_control = true;
			parse_cache_control(response, value);
		}
		else if (strlen(line) == 4 && ascii_case_ncompare(line, "Date", 4) == 0)
			response->have_date = parse_http_date(value, &response->date);
		else if (strlen(line) == 7 && ascii_case_ncompare(line, "Expires", 7) == 0)
			response->have_expires = parse_http_date(value,
			    &response->expires);
		else if (strlen(line) == 3 && ascii_case_ncompare(line, "Age", 3) == 0)
			response->have_age = parse_uint64(value, &response->age);
	}
	if (response->chunked)
		response->have_length = false;
	return 0;
}

static bool
cache_is_fresh(const struct cache_info *cache, int64_t now)
{
	uint64_t resident;
	uint64_t apparent = 0;
	uint64_t current_age;
	uint64_t lifetime;

	if (cache->no_cache || cache->response_time <= 0
	    || now < cache->response_time)
		return false;
	resident = (uint64_t)(now - cache->response_time);
	if (cache->have_date && cache->response_time > cache->date)
		apparent = (uint64_t)(cache->response_time - cache->date);
	current_age = apparent > cache->age ? apparent : cache->age;
	if (current_age > UINT64_MAX - resident)
		return false;
	current_age += resident;
	if (cache->have_max_age)
		lifetime = cache->max_age;
	else if (cache->have_expires && cache->have_date
	    && cache->expires > cache->date)
		lifetime = (uint64_t)(cache->expires - cache->date);
	else
		lifetime = DEFAULT_FRESHNESS_LIFETIME;
	return current_age < lifetime;
}

static int
write_bytes(FILE *output, const unsigned char *data, size_t size,
    uint64_t *total, size_t maximum, const struct st_webget_request *request,
    uint64_t expected, bool known)
{
	if (*total > maximum || size > maximum - (size_t)*total)
		return IO_ERROR_MESSAGE_TOO_LARGE;
	if (size != 0 && fwrite(data, 1, size, output) != size)
		return IO_ERROR_IO;
	*total += size;
	return report_progress(request, ST_WEBGET_BODY, *total, expected, known)
	    ? 0 : IO_ERROR_CANCELLED;
}

static int
read_fixed_body(struct reader *reader, FILE *output, uint64_t target,
    size_t maximum, const struct st_webget_request *request, uint64_t *total,
    uint64_t progress_total, bool progress_known)
{
	while (*total < target) {
		size_t available;
		int status;
		if (reader->begin == reader->end) {
			status = reader_fill(reader);
			if (status < 0)
				return status;
		}
		available = reader->end - reader->begin;
		if (available > target - *total)
			available = (size_t)(target - *total);
		status = write_bytes(output, reader->buffer + reader->begin, available,
		    total, maximum, request, progress_total, progress_known);
		if (status < 0)
			return status;
		reader->begin += available;
	}
	return 0;
}

static int
read_chunked_body(struct reader *reader, FILE *output, size_t maximum,
    const struct st_webget_request *request, uint64_t *total)
{
	char line[LINE_LIMIT];
	size_t header_bytes = 0;

	for (;;) {
		uint64_t chunk = 0;
		char *extension;
		int status = reader_line(reader, line, sizeof(line), &header_bytes);
		if (status < 0)
			return status;
		extension = strchr(line, ';');
		if (extension != NULL)
			*extension = '\0';
		if (line[0] == '\0')
			return IO_ERROR_PROTOCOL;
		for (char *digit = line; *digit != '\0'; ++digit) {
			unsigned value;
			if (*digit >= '0' && *digit <= '9') value = (unsigned)(*digit - '0');
			else if (*digit >= 'a' && *digit <= 'f') value = (unsigned)(*digit - 'a' + 10);
			else if (*digit >= 'A' && *digit <= 'F') value = (unsigned)(*digit - 'A' + 10);
			else return IO_ERROR_PROTOCOL;
			if (chunk > (UINT64_MAX - value) / 16)
				return IO_ERROR_OUT_OF_RANGE;
			chunk = chunk * 16 + value;
		}
		if (chunk == 0) {
			do {
				status = reader_line(reader, line, sizeof(line), &header_bytes);
				if (status < 0)
					return status;
			} while (line[0] != '\0');
			return 0;
		}
		if (*total > UINT64_MAX - chunk)
			return IO_ERROR_OUT_OF_RANGE;
		status = read_fixed_body(reader, output, *total + chunk, maximum,
		    request, total, 0, false);
		if (status < 0)
			return status;
		{
			unsigned char cr;
			unsigned char lf;
			if (reader_byte(reader, &cr) < 0 || reader_byte(reader, &lf) < 0
			    || cr != '\r' || lf != '\n')
				return IO_ERROR_PROTOCOL;
		}
	}
}

static int
read_close_body(struct reader *reader, FILE *output, size_t maximum,
    const struct st_webget_request *request, uint64_t *total)
{
	for (;;) {
		int status;
		if (reader->begin == reader->end) {
			status = reader_fill(reader);
			if (status == IO_ERROR_NOT_CONNECTED)
				return 0;
			if (status < 0)
				return status;
		}
		status = write_bytes(output, reader->buffer + reader->begin,
		    reader->end - reader->begin, total, maximum, request, 0, false);
		if (status < 0)
			return status;
		reader->begin = reader->end;
	}
}

static bool
tcp_cancel(void *context)
{
	const struct st_webget_request *request = context;
	return !report_progress(request, ST_WEBGET_CONNECTING, 0, 0, false);
}

static int
connect_transport(struct transport *transport, const struct parsed_url *url,
    const struct st_webget_request *request)
{
	struct tcp_client_options options = {
		TCP_CLIENT_ADDRESS_ANY, request->connect_timeout_ms,
		tcp_cancel, (void *)request
	};
	int status;

	if (!report_progress(request, ST_WEBGET_RESOLVING, 0, 0, false))
		return IO_ERROR_CANCELLED;
	status = tcp_client_connect(&transport->socket, NULL, url->host, url->port,
	    &options);
	if (status < 0)
		return status;
	transport->timeout_ms = request->io_timeout_ms;
	if (url->tls) {
		struct st_tls_client_options tls = {
			.server_name = url->host,
			.server_auth = ST_TLS_SERVER_AUTH_WEB_PKI,
			.handshake_timeout_ms = request->io_timeout_ms
		};
		if (request->tls_options != NULL)
			tls = *request->tls_options;
		if (tls.server_name == NULL)
			tls.server_name = url->host;
		if (!report_progress(request, ST_WEBGET_TLS, 0, 0, false))
			return IO_ERROR_CANCELLED;
		status = st_tls_client_open(&transport->tls, transport->socket, &tls);
		if (status < 0)
			return status;
	}
	return 0;
}

static void
close_transport(struct transport *transport)
{
	if (transport->tls != NULL)
		st_tls_close(transport->tls);
	else if (socket_handle_is_valid(transport->socket))
		(void)socket_close(transport->socket);
	transport->tls = NULL;
	transport->socket = SOCKET_HANDLE_INVALID;
}

static int
make_request(char **result, const struct parsed_url *url,
    const struct cache_info *cache)
{
	const char *eh = cache->etag == NULL ? "" : "If-None-Match: ";
	const char *etag = cache->etag == NULL ? "" : cache->etag;
	const char *ee = cache->etag == NULL ? "" : "\r\n";
	const char *mh = cache->last_modified == NULL ? ""
	    : "If-Modified-Since: ";
	const char *modified = cache->last_modified == NULL ? ""
	    : cache->last_modified;
	const char *me = cache->last_modified == NULL ? "" : "\r\n";
	const char *format = "GET %s HTTP/1.1\r\nHost: %s\r\n"
	    "User-Agent: SyncTERM\r\nAccept: */*\r\nConnection: close\r\n"
	    "%s%s%s%s%s%s\r\n";
	int needed = snprintf(NULL, 0, format, url->target, url->host_header,
	    eh, etag, ee, mh, modified, me);

	if (needed < 0)
		return IO_ERROR_IO;
	*result = malloc((size_t)needed + 1);
	if (*result == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	(void)snprintf(*result, (size_t)needed + 1, format, url->target,
	    url->host_header, eh, etag, ee, mh, modified, me);
	return 0;
}

int
st_webget(struct st_webget_result *result,
    const struct st_webget_request *request)
{
	struct parsed_url url = {0};
	struct cache_info cache = {0};
	struct response_info response = {0};
	struct transport transport = { SOCKET_HANDLE_INVALID, NULL, 0 };
	struct reader reader = {0};
	struct fs_info existing;
	st_file_lock_t *lock = NULL;
	char *output_path = NULL;
	char *metadata_name = NULL;
	char *metadata_path = NULL;
	char *temporary_name = NULL;
	char *temporary_path = NULL;
	char *metadata_new_name = NULL;
	char *metadata_new_path = NULL;
	char *wire_request = NULL;
	FILE *output = NULL;
	uint64_t total = 0;
	size_t maximum;
	int status;
	bool runtime = false;
	bool conditional = false;

	if (result == NULL || request == NULL || request->url == NULL
	    || request->cache_directory == NULL
	    || !path_untrusted_component_valid(request->cache_key)
	    || !path_untrusted_component_valid(request->output_name))
		return IO_ERROR_INVALID_ARGUMENT;
	maximum = request->maximum_size == 0 ? ST_WEBGET_DEFAULT_MAX_SIZE
	    : request->maximum_size;
	status = parse_url(&url, request->url);
	if (status < 0)
		goto cleanup;
	metadata_name = suffix_name(request->cache_key, ".cacheinfo");
	temporary_name = suffix_name(request->cache_key, ".new");
	metadata_new_name = suffix_name(request->cache_key, ".cacheinfo.new");
	output_path = join_path(request->cache_directory, request->output_name);
	metadata_path = metadata_name == NULL ? NULL
	    : join_path(request->cache_directory, metadata_name);
	temporary_path = temporary_name == NULL ? NULL
	    : join_path(request->cache_directory, temporary_name);
	metadata_new_path = metadata_new_name == NULL ? NULL
	    : join_path(request->cache_directory, metadata_new_name);
	if (output_path == NULL || metadata_path == NULL || temporary_path == NULL
	    || metadata_new_path == NULL) {
		status = IO_ERROR_OUT_OF_MEMORY;
		goto cleanup;
	}
	status = fs_create_directories(request->cache_directory);
	if (status < 0)
		goto cleanup;
	status = st_file_lock_acquire(&lock, metadata_path, true);
	if (status < 0)
		goto cleanup;
	status = load_cache_info(&cache, metadata_path);
	if (status < 0)
		goto cleanup;
	if (cache.url == NULL || strcmp(cache.url, request->url) != 0) {
		free_cache_info(&cache);
		cache.url = strdup(request->url);
		if (cache.url == NULL) {
			status = IO_ERROR_OUT_OF_MEMORY;
			goto cleanup;
		}
	}
	status = fs_get_info(&existing, output_path);
	if (status == 0 && existing.kind == FS_ENTRY_FILE
	    && cache_is_fresh(&cache, time(NULL))) {
		result->outcome = ST_WEBGET_FRESH_CACHE;
		result->size = existing.size;
		status = 0;
		goto cleanup;
	}
	if (status != 0 && status != IO_ERROR_NOT_FOUND)
		goto cleanup;
	status = socket_runtime_init();
	if (status < 0)
		goto cleanup;
	runtime = true;
	status = connect_transport(&transport, &url, request);
	if (status < 0)
		goto cleanup;
	conditional = cache.etag != NULL || cache.last_modified != NULL;
	status = make_request(&wire_request, &url, &cache);
	if (status < 0)
		goto cleanup;
	if (!report_progress(request, ST_WEBGET_REQUESTING, 0, 0, false)) {
		status = IO_ERROR_CANCELLED;
		goto cleanup;
	}
	status = transport_send(&transport, wire_request, strlen(wire_request));
	if (status < 0)
		goto cleanup;
	reader.transport = &transport;
	if (!report_progress(request, ST_WEBGET_HEADERS, 0, 0, false)) {
		status = IO_ERROR_CANCELLED;
		goto cleanup;
	}
	status = parse_headers(&reader, &response);
	if (status < 0)
		goto cleanup;
	if (response.status == 304) {
		if (!conditional) {
			status = IO_ERROR_PROTOCOL;
			goto cleanup;
		}
		status = fs_get_info(&existing, output_path);
		if (status < 0 || existing.kind != FS_ENTRY_FILE) {
			status = IO_ERROR_PROTOCOL;
			goto cleanup;
		}
		cache.response_time = time(NULL);
		if (response.etag != NULL) {
			free(cache.etag);
			cache.etag = strdup(response.etag);
			if (cache.etag == NULL) {
				status = IO_ERROR_OUT_OF_MEMORY;
				goto cleanup;
			}
		}
		if (response.last_modified != NULL) {
			free(cache.last_modified);
			cache.last_modified = strdup(response.last_modified);
			if (cache.last_modified == NULL) {
				status = IO_ERROR_OUT_OF_MEMORY;
				goto cleanup;
			}
		}
		if (response.have_date) {
			cache.date = response.date;
			cache.have_date = true;
		}
		if (response.have_expires) {
			cache.expires = response.expires;
			cache.have_expires = true;
		}
		if (response.have_max_age) {
			cache.max_age = response.max_age;
			cache.have_max_age = true;
		}
		if (response.have_age)
			cache.age = response.age;
		if (response.have_cache_control) {
			cache.no_cache = response.no_cache;
			cache.must_revalidate = response.must_revalidate;
			cache.immutable = response.immutable;
		}
		status = write_cache_info(&cache, metadata_new_path);
		if (status == 0)
			status = fs_rename(metadata_new_path, metadata_path);
		if (status == 0) {
			result->outcome = ST_WEBGET_NOT_MODIFIED;
			result->size = existing.size;
		}
		goto cleanup;
	}
	if (response.status != 200) {
		status = IO_ERROR_PROTOCOL;
		goto cleanup;
	}
	if (response.have_length && response.length > maximum) {
		status = IO_ERROR_MESSAGE_TOO_LARGE;
		goto cleanup;
	}
	status = fs_open(&output, temporary_path, "wb");
	if (status < 0)
		goto cleanup;
	if (response.chunked)
		status = read_chunked_body(&reader, output, maximum, request, &total);
	else if (response.have_length)
		status = read_fixed_body(&reader, output, response.length, maximum,
		    request, &total, response.length, true);
	else
		status = read_close_body(&reader, output, maximum, request, &total);
	if (fclose(output) != 0 && status == 0)
		status = IO_ERROR_IO;
	output = NULL;
	if (status < 0)
		goto cleanup;
	if (!report_progress(request, ST_WEBGET_VALIDATING, total, total, true)) {
		status = IO_ERROR_CANCELLED;
		goto cleanup;
	}
	if (request->validate != NULL) {
		status = request->validate(temporary_path, request->validate_context);
		if (status < 0)
			goto cleanup;
	}
	if (!report_progress(request, ST_WEBGET_INSTALLING, total, total, true)) {
		status = IO_ERROR_CANCELLED;
		goto cleanup;
	}
	status = fs_rename(temporary_path, output_path);
	if (status < 0)
		goto cleanup;
	free(cache.etag);
	free(cache.last_modified);
	cache.etag = response.etag == NULL ? NULL : strdup(response.etag);
	cache.last_modified = response.last_modified == NULL ? NULL
	    : strdup(response.last_modified);
	if ((response.etag != NULL && cache.etag == NULL)
	    || (response.last_modified != NULL && cache.last_modified == NULL)) {
		status = IO_ERROR_OUT_OF_MEMORY;
		goto cleanup;
	}
	cache.response_time = time(NULL);
	cache.date = response.date;
	cache.expires = response.expires;
	cache.max_age = response.have_max_age ? response.max_age : 0;
	cache.age = response.age;
	cache.have_date = response.have_date;
	cache.have_expires = response.have_expires;
	cache.have_max_age = response.have_max_age;
	cache.no_cache = response.no_cache;
	cache.must_revalidate = response.must_revalidate;
	cache.immutable = response.immutable;
	status = write_cache_info(&cache, metadata_new_path);
	if (status == 0)
		status = fs_rename(metadata_new_path, metadata_path);
	if (status == 0) {
		result->outcome = ST_WEBGET_DOWNLOADED;
		result->size = total;
	}

cleanup:
	if (output != NULL)
		(void)fclose(output);
	close_transport(&transport);
	if (runtime)
		(void)socket_runtime_cleanup();
	if (status < 0) {
		if (temporary_path != NULL)
			(void)fs_remove_file(temporary_path);
		if (metadata_new_path != NULL)
			(void)fs_remove_file(metadata_new_path);
	}
	st_file_lock_release(lock);
	free(wire_request);
	free(response.etag);
	free(response.last_modified);
	free_cache_info(&cache);
	free_url(&url);
	free(output_path);
	free(metadata_name);
	free(metadata_path);
	free(temporary_name);
	free(temporary_path);
	free(metadata_new_name);
	free(metadata_new_path);
	return status;
}
