/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */
#include "client.h"
#include "sftp.h"
#include "io_error.h"
#include "st_threads.h"
#include "st_time.h"
#include "string_compat.h"
#include <limits.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct remote_handle {
	struct remote_handle *next;
	uint64_t id;
	sftp_str_t bytes;
};
struct outgoing {
	struct outgoing *next;
	size_t size, sent;
	uint8_t bytes[];
};
struct request {
	struct request *next;
	struct st_sftp_client *client;
	struct st_sftp_request input;
	struct st_sftp_response output;
	struct sftpc_pending *pending;
	struct st_sftp_entry *entries;
};
struct st_sftp_client {
	struct st_sftp_io io;
	struct st_sftp_provider provider;
	st_mutex_t mutex;
	st_condition_t changed;
	st_thread_t worker;
	bool started, stopping, ready, failed;
	bool init_done; /* worker only */
	uint64_t next_id;
	uint32_t extensions;
	char *pubdir;
	struct request *queued, *queued_tail, *completed, *completed_tail, *borrowed;
	/* Protocol and handle state below belong exclusively to worker. */
	sftpc_state_t protocol;
	struct remote_handle *handles;
	struct outgoing *tx_head, *tx_tail;
};
static atomic_uint_fast64_t next_handle;

const char *
st_sftp_error_name(int code, uint32_t server_status)
{
	static const char *const names[] = {"OK", "NULL_STATE", "NULL_PATH",
	    "NULL_HANDLE_OUT", "NULL_HANDLE", "HANDLE_NOT_NULL", "NULL_DATA",
	    "NULL_OUT", "OOM", "PACKET_BUILD_FAILED", "SEND_FAILED",
	    "CHANNEL_CLOSED", "ABORTED", "REPLY_NULL", "REPLY_WRONG_TYPE",
	    "REPLY_BAD_STRING", "PARSE_FAILED"};
	if (code == 0) return sftp_get_errcode_name(server_status);
	if (code > 0 && (size_t)code < sizeof(names) / sizeof(names[0])) return names[code];
	return code < 0 ? "HOST" : "UNKNOWN";
}

static void
free_request(struct request *r)
{
	if (r == NULL) return;
	sftpc_pending_free(r->pending);
	free(r->entries);
	free((void *)r->input.path);
	free((void *)r->input.second_path);
	free((void *)r->input.data);
	free(r);
}

static void
complete(struct request *r)
{
	struct st_sftp_client *c = r->client;
	r->next = NULL;
	(void)st_mutex_lock(&c->mutex);
	if (c->completed_tail != NULL) c->completed_tail->next = r;
	else c->completed = r;
	c->completed_tail = r;
	(void)st_condition_broadcast(&c->changed);
	(void)st_mutex_unlock(&c->mutex);
}

static void
fail(struct request *r, int code, const char *message)
{
	r->output.kind = ST_SFTP_RESULT_ERROR;
	r->output.error_code = code;
	r->output.message = message;
	r->output.transient = code == IO_ERROR_CONNECTION_RESET
	    || code == IO_ERROR_CANCELLED || code == IO_ERROR_TIMED_OUT;
	complete(r);
}

static struct st_sftp_stat
convert_stat(sftp_file_attr_t a)
{
	struct st_sftp_stat s = {0};
	uint32_t time;
	(void)sftp_fattr_get_size(a, &s.size);
	(void)sftp_fattr_get_permissions(a, &s.mode);
	(void)sftp_fattr_get_uid(a, &s.uid);
	(void)sftp_fattr_get_gid(a, &s.gid);
	if (sftp_fattr_get_mtime(a, &time)) s.modified_time = time;
	if (sftp_fattr_get_atime(a, &time)) s.accessed_time = time;
	return s;
}

static struct remote_handle *
find_handle(struct st_sftp_client *c, uint64_t id)
{
	for (struct remote_handle *h = c->handles; h != NULL; h = h->next)
		if (h->id == id) return h;
	return NULL;
}

static void
remove_handle(struct st_sftp_client *c, uint64_t id)
{
	struct remote_handle **p = &c->handles;
	while (*p != NULL) {
		struct remote_handle *h = *p;
		if (h->id == id) {
			*p = h->next;
			free_sftp_str(h->bytes);
			free(h);
			return;
		}
		p = &h->next;
	}
}

static void
reply(struct sftpc_pending *p)
{
	struct request *r = p->cbdata;
	struct st_sftp_client *c = r->client;
	r->pending = p;
	if (r->input.operation == ST_SFTP_START) c->init_done = true;
	if (p->err != SFTP_ERR_OK || (p->result != SSH_FX_OK && p->result != SSH_FX_EOF)) {
		r->output.kind = ST_SFTP_RESULT_ERROR;
		r->output.error_code = (int)p->err;
		r->output.server_status = p->result;
		r->output.message = p->estr;
		r->output.transient = sftp_err_is_transient(p->err) || p->err == SFTP_ERR_OOM;
		if (c->io.log != NULL) {
			char message[128];
			(void)snprintf(message, sizeof(message), "SFTP operation %u failed (library %d, status %u)",
			    (unsigned)r->input.operation, (int)p->err, (unsigned)p->result);
			c->io.log(c->io.context, message);
		}
		complete(r);
		return;
	}
	if (p->result == SSH_FX_EOF) {
		r->output.kind = ST_SFTP_RESULT_EOF;
		complete(r);
		return;
	}
	r->output.kind = ST_SFTP_RESULT_OK;
	switch (r->input.operation) {
	case ST_SFTP_START: {
		const char *path = sftpc_get_pubdir(c->protocol);
		char *copy = path == NULL ? NULL : strdup(path);
		if (path != NULL && copy == NULL) {
			fail(r, IO_ERROR_OUT_OF_MEMORY, "SFTP metadata allocation failed");
			return;
		}
		(void)st_mutex_lock(&c->mutex);
		c->extensions = sftpc_get_extensions(c->protocol);
		c->pubdir = copy;
		c->ready = true;
		(void)st_mutex_unlock(&c->mutex);
		r->output.kind = ST_SFTP_RESULT_STARTED;
		if (c->io.log != NULL) c->io.log(c->io.context, "SFTP v3 subsystem ready");
		break;
	}
	case ST_SFTP_OPEN:
	case ST_SFTP_OPENDIR: {
		struct sftpc_open_pending *o = (struct sftpc_open_pending *)p;
		if (o->handle == NULL) { fail(r, IO_ERROR_PROTOCOL, "SFTP reply is missing its handle"); return; }
		struct remote_handle *h = calloc(1, sizeof(*h));
		if (h == NULL) { fail(r, IO_ERROR_OUT_OF_MEMORY, "SFTP handle allocation failed"); return; }
		h->id = atomic_fetch_add(&next_handle, 1) + 1;
		h->bytes = o->handle;
		o->handle = NULL;
		h->next = c->handles;
		c->handles = h;
		r->output.kind = ST_SFTP_RESULT_HANDLE;
		r->output.handle = h->id;
		break;
	}
	case ST_SFTP_REALPATH:
	case ST_SFTP_READ:
	case ST_SFTP_DESCRIPTIONS: {
		sftp_str_t str = r->input.operation == ST_SFTP_REALPATH
		    ? ((struct sftpc_realpath_pending *)p)->ret
		    : r->input.operation == ST_SFTP_READ
		    ? ((struct sftpc_read_pending *)p)->data
		    : ((struct sftpc_descs_pending *)p)->desc;
		if (str == NULL) { fail(r, IO_ERROR_PROTOCOL, "SFTP reply is missing its string"); return; }
		r->output.kind = r->input.operation == ST_SFTP_READ
		    ? ST_SFTP_RESULT_BYTES : ST_SFTP_RESULT_STRING;
		r->output.data = str->c_str;
		r->output.size = str->len;
		break;
	}
	case ST_SFTP_STAT:
		if (((struct sftpc_stat_pending *)p)->attrs == NULL) {
			fail(r, IO_ERROR_PROTOCOL, "SFTP reply is missing its attributes"); return;
		}
		r->output.kind = ST_SFTP_RESULT_STAT;
		r->output.stat = convert_stat(((struct sftpc_stat_pending *)p)->attrs);
		break;
	case ST_SFTP_READDIR: {
		struct sftpc_readdir_pending *d = (struct sftpc_readdir_pending *)p;
		r->output.kind = ST_SFTP_RESULT_ENTRIES;
		r->entries = calloc(d->count, sizeof(*r->entries));
		if (d->count != 0 && r->entries == NULL) { fail(r, IO_ERROR_OUT_OF_MEMORY, "SFTP directory allocation failed"); return; }
		r->output.entries = r->entries;
		r->output.entry_count = d->count;
		for (uint32_t i = 0; i < d->count; ++i) {
			struct st_sftp_entry *e = &r->entries[i];
			struct sftpc_dir_entry *source = &d->entries[i];
			e->name = (const char *)source->filename->c_str;
			e->stat = convert_stat(source->attrs);
			uint32_t mode;
			e->directory = sftp_fattr_get_permissions(source->attrs, &mode)
			    ? (mode & ST_SFTP_MODE_TYPE_MASK) == ST_SFTP_MODE_DIRECTORY
			    : source->longname != NULL && source->longname->len != 0 && source->longname->c_str[0] == 'd';
			if ((c->extensions & SFTP_EXT_LNAME) != 0) {
				sftp_str_t label = sftp_fattr_get_ext_by_type(source->attrs, SFTP_EXT_NAME_LNAME);
				if (label != NULL && label->len != 0) e->long_name = (const char *)label->c_str;
			}
			sftp_str_t desc = sftp_fattr_get_ext_by_type(source->attrs, SFTP_EXT_NAME_DESCS);
			e->has_long_description = (c->extensions & SFTP_EXT_DESCS) != 0 && desc != NULL;
			sftp_str_t hash = (c->extensions & SFTP_EXT_SHA1S) == 0 ? NULL
			    : sftp_fattr_get_ext_by_type(source->attrs, "sha1@syncterm.net");
			if (hash == NULL && (c->extensions & SFTP_EXT_MD5S) != 0)
				hash = sftp_fattr_get_ext_by_type(source->attrs, "md5@syncterm.net");
			if (hash != NULL) { e->hash = hash->c_str; e->hash_size = hash->len; }
		}
		break;
	}
	case ST_SFTP_CLOSE: remove_handle(c, r->input.handle); break;
	default: break;
	}
	complete(r);
}

static void
dispatch(struct request *r)
{
	struct st_sftp_client *c = r->client;
	struct st_sftp_request *q = &r->input;
	struct remote_handle *h = find_handle(c, q->handle);
	struct sftpc_pending *p = NULL;
	struct sftp_string bytes;
	sftp_file_attr_t attr = NULL;
	if ((q->operation == ST_SFTP_READ || q->operation == ST_SFTP_WRITE
	    || q->operation == ST_SFTP_READDIR || q->operation == ST_SFTP_CLOSE) && h == NULL) {
		fail(r, IO_ERROR_INVALID_ARGUMENT, "SFTP handle is closed or belongs to another session");
		return;
	}
	/* WRITE body: type/id, handle string, offset, data string (21 + H + N).
	 * READ's count is only a wire uint32; a server may return a short read. */
	if (q->operation == ST_SFTP_WRITE && (h->bytes->len > SFTP_MAX_PACKET_SIZE - 21
	    || q->size > SFTP_MAX_PACKET_SIZE - 21 - h->bytes->len)) {
		fail(r, IO_ERROR_MESSAGE_TOO_LARGE, "SFTP write exceeds maximum packet size");
		return;
	}
	switch (q->operation) {
	case ST_SFTP_START:
		r->output.kind = ST_SFTP_RESULT_STARTED;
		complete(r);
		return;
	case ST_SFTP_REALPATH: p = (struct sftpc_pending *)sftpc_realpath(c->protocol, q->path, reply, r); break;
	case ST_SFTP_STAT: p = (struct sftpc_pending *)sftpc_stat(c->protocol, q->path, reply, r); break;
	case ST_SFTP_OPEN: p = (struct sftpc_pending *)sftpc_open(c->protocol, q->path, q->flags, reply, r); break;
	case ST_SFTP_OPENDIR: p = (struct sftpc_pending *)sftpc_opendir(c->protocol, q->path, reply, r); break;
	case ST_SFTP_READDIR: p = (struct sftpc_pending *)sftpc_readdir(c->protocol, h->bytes, reply, r); break;
	case ST_SFTP_READ: p = (struct sftpc_pending *)sftpc_read(c->protocol, h->bytes, q->offset, (uint32_t)q->size, reply, r); break;
	case ST_SFTP_WRITE:
		sftp_memstatic(&bytes, q->data, (uint32_t)q->size);
		p = sftpc_write(c->protocol, h->bytes, q->offset, &bytes, reply, r); break;
	case ST_SFTP_CLOSE: p = sftpc_close(c->protocol, h->bytes, reply, r); break;
	case ST_SFTP_REMOVE: p = sftpc_remove(c->protocol, q->path, reply, r); break;
	case ST_SFTP_RMDIR: p = sftpc_rmdir(c->protocol, q->path, reply, r); break;
	case ST_SFTP_RENAME: p = sftpc_rename(c->protocol, q->path, q->second_path, reply, r); break;
	case ST_SFTP_DESCRIPTIONS: p = (struct sftpc_pending *)sftpc_descs(c->protocol, q->path, reply, r); break;
	case ST_SFTP_MKDIR:
	case ST_SFTP_SET_MTIME:
		attr = sftp_fattr_alloc();
		if (attr == NULL) break;
		if (q->operation == ST_SFTP_SET_MTIME) {
			sftp_fattr_set_times(attr, (uint32_t)q->modified_time, (uint32_t)q->modified_time);
			p = sftpc_setstat(c->protocol, q->path, attr, reply, r);
		} else p = sftpc_mkdir(c->protocol, q->path, attr, reply, r);
		sftp_fattr_free(attr);
		break;
	}
	if (p == NULL) fail(r, IO_ERROR_OUT_OF_MEMORY, "SFTP request allocation failed");
}

static bool
send_packet(uint8_t *data, size_t size, void *context)
{
	struct st_sftp_client *c = context;
	if (size > SFTP_MAX_PACKET_SIZE + 4) return false;
	struct outgoing *packet = malloc(sizeof(*packet) + size);
	if (packet == NULL) return false;
	*packet = (struct outgoing){.size = size};
	memcpy(packet->bytes, data, size);
	if (c->tx_tail != NULL) c->tx_tail->next = packet;
	else c->tx_head = packet;
	c->tx_tail = packet;
	return true;
}

static bool
flush_packets(struct st_sftp_client *c)
{
	for (unsigned batch = 0; c->tx_head != NULL && batch < 64; ++batch) {
		struct outgoing *p = c->tx_head;
		ssize_t n = c->io.write(c->io.context, p->bytes + p->sent, p->size - p->sent);
		if (n == IO_ERROR_WOULD_BLOCK || n == IO_ERROR_TIMED_OUT || n == 0) return true;
		if (n < 0 || (size_t)n > p->size - p->sent) return false;
		p->sent += (size_t)n;
		if (p->sent == p->size) {
			c->tx_head = p->next;
			if (c->tx_head == NULL) c->tx_tail = NULL;
			free(p);
		}
	}
	return true;
}

static bool
stopped(struct st_sftp_client *c)
{
	bool result;
	(void)st_mutex_lock(&c->mutex);
	result = c->stopping;
	(void)st_mutex_unlock(&c->mutex);
	return result;
}

static struct request *
take_queued(struct st_sftp_client *c)
{
	struct request *r;
	(void)st_mutex_lock(&c->mutex);
	r = c->queued;
	if (r != NULL) {
		c->queued = r->next;
		if (c->queued == NULL) c->queued_tail = NULL;
		r->next = NULL;
	}
	(void)st_mutex_unlock(&c->mutex);
	return r;
}

static int
worker(void *context)
{
	struct st_sftp_client *c = context;
	uint8_t *data = malloc(SFTP_MAX_PACKET_SIZE);
	struct request *initial = take_queued(c);
	bool opened = false;
	uint64_t start_ms = 0, now_ms;
	if (data == NULL) goto done;
	if (stopped(c)) goto done;
	if (c->io.log != NULL) c->io.log(c->io.context, "Opening SFTP subsystem on demand");
	if (c->io.open(c->io.context) != 0) goto done;
	opened = true;
	c->protocol = sftpc_begin(send_packet, c);
	if (c->protocol == NULL) goto done;
	if (sftpc_init(c->protocol, reply, initial) == NULL) goto done;
	initial = NULL;
	(void)st_time_monotonic_ms(&start_ms);
	while (!stopped(c)) {
		if (!c->ready && st_time_monotonic_ms(&now_ms) == 0
		    && now_ms - start_ms >= 60000) break;
		/* INIT must complete before any other request is sent. */
		if (c->ready) {
			struct request *r;
			for (unsigned batch = 0; batch < 64 && !stopped(c)
			    && (r = take_queued(c)) != NULL; ++batch) dispatch(r);
		}
		if (!flush_packets(c)) break;
		ssize_t n = c->io.read(c->io.context, data, SFTP_MAX_PACKET_SIZE, 20);
		if (n == IO_ERROR_WOULD_BLOCK || n == IO_ERROR_TIMED_OUT) continue;
		if (n <= 0 || !sftpc_recv(c->protocol, data, (uint32_t)n)) break;
		/* A rejected VERSION cannot leave startup waiters parked forever. */
		if (c->init_done && !c->ready) break;
	}
done:
	(void)st_mutex_lock(&c->mutex);
	c->ready = false;
	c->failed = true;
	(void)st_mutex_unlock(&c->mutex);
	if (c->io.log != NULL) c->io.log(c->io.context, "SFTP subsystem closed or unavailable");
	if (initial != NULL) fail(initial, IO_ERROR_NOT_SUPPORTED, "SFTP subsystem could not be started");
	if (c->protocol != NULL) { sftpc_end(c->protocol); c->protocol = NULL; }
	struct request *r;
	while ((r = take_queued(c)) != NULL) fail(r, IO_ERROR_CONNECTION_RESET, "SFTP subsystem closed");
	while (c->handles != NULL) remove_handle(c, c->handles->id);
	while (c->tx_head != NULL) {
		struct outgoing *next = c->tx_head->next;
		free(c->tx_head);
		c->tx_head = next;
	}
	if (opened) c->io.close(c->io.context);
	free(data);
	return 0;
}

static bool
available(void *context)
{
	struct st_sftp_client *c = context;
	bool ready;
	(void)st_mutex_lock(&c->mutex);
	ready = c->ready && !c->stopping;
	(void)st_mutex_unlock(&c->mutex);
	return ready;
}
static const char *
pubdir(void *context)
{
	struct st_sftp_client *c = context;
	const char *path;
	(void)st_mutex_lock(&c->mutex);
	path = c->ready && !c->stopping ? c->pubdir : NULL;
	(void)st_mutex_unlock(&c->mutex);
	return path;
}
static bool
lname(void *context)
{
	struct st_sftp_client *c = context;
	bool enabled;
	(void)st_mutex_lock(&c->mutex);
	enabled = c->ready && !c->stopping && (c->extensions & SFTP_EXT_LNAME) != 0;
	(void)st_mutex_unlock(&c->mutex);
	return enabled;
}

static int
submit(void *context, const struct st_sftp_request *q, uint64_t *id)
{
	struct st_sftp_client *c = context;
	struct request *r;
	if (q == NULL || id == NULL || q->operation < ST_SFTP_START
	    || q->operation > ST_SFTP_DESCRIPTIONS
	    || (q->operation == ST_SFTP_READ && q->size > UINT32_MAX)
	    || (q->operation == ST_SFTP_WRITE && q->size > SFTP_MAX_PACKET_SIZE - 21)
	    || (q->operation == ST_SFTP_WRITE && q->size != 0 && q->data == NULL)
	    || (q->operation == ST_SFTP_SET_MTIME && q->modified_time > UINT32_MAX))
		return IO_ERROR_INVALID_ARGUMENT;
	r = calloc(1, sizeof(*r));
	if (r == NULL) return IO_ERROR_OUT_OF_MEMORY;
	r->client = c;
	r->input = *q;
	r->input.path = q->path == NULL ? NULL : strdup(q->path);
	r->input.second_path = q->second_path == NULL ? NULL : strdup(q->second_path);
	r->input.data = NULL;
	if (q->data != NULL && q->size != 0) {
		void *copy = malloc(q->size);
		if (copy != NULL) memcpy(copy, q->data, q->size);
		r->input.data = copy;
	}
	if ((q->path != NULL && r->input.path == NULL)
	    || (q->second_path != NULL && r->input.second_path == NULL)
	    || (q->data != NULL && q->size != 0 && r->input.data == NULL)) {
		free_request(r);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	(void)st_mutex_lock(&c->mutex);
	if (c->stopping || c->failed || (!c->ready && q->operation != ST_SFTP_START)) {
		(void)st_mutex_unlock(&c->mutex);
		free_request(r);
		return IO_ERROR_NOT_CONNECTED;
	}
	r->output.request_id = ++c->next_id;
	if (c->queued_tail != NULL) c->queued_tail->next = r;
	else c->queued = r;
	c->queued_tail = r;
	if (!c->started) {
		if (st_thread_create(&c->worker, worker, c) != ST_THREAD_SUCCESS) {
			c->queued = c->queued_tail = NULL;
			(void)st_mutex_unlock(&c->mutex);
			free_request(r);
			return IO_ERROR_IO;
		}
		c->started = true;
	}
	*id = r->output.request_id;
	(void)st_condition_broadcast(&c->changed);
	(void)st_mutex_unlock(&c->mutex);
	return 0;
}

static int
poll_response(void *context, struct st_sftp_response *output, uint32_t timeout)
{
	struct st_sftp_client *c = context;
	struct timespec deadline;
	if (output == NULL) return IO_ERROR_INVALID_ARGUMENT;
	(void)timespec_get(&deadline, TIME_UTC);
	deadline.tv_sec += timeout / 1000;
	deadline.tv_nsec += (long)(timeout % 1000) * 1000000L;
	if (deadline.tv_nsec >= 1000000000L) { deadline.tv_sec++; deadline.tv_nsec -= 1000000000L; }
	free_request(c->borrowed);
	c->borrowed = NULL;
	(void)st_mutex_lock(&c->mutex);
	while (c->completed == NULL && !c->stopping && timeout != 0) {
		if (st_condition_timedwait(&c->changed, &c->mutex, &deadline) != ST_THREAD_SUCCESS) break;
	}
	if (c->completed != NULL) {
		c->borrowed = c->completed;
		c->completed = c->completed->next;
		if (c->completed == NULL) c->completed_tail = NULL;
		*output = c->borrowed->output;
	}
	(void)st_mutex_unlock(&c->mutex);
	return c->borrowed != NULL;
}

static void
cancel_all(void *context)
{
	struct st_sftp_client *c = context;
	(void)st_mutex_lock(&c->mutex);
	c->stopping = true;
	(void)st_condition_broadcast(&c->changed);
	(void)st_mutex_unlock(&c->mutex);
}

int
st_sftp_client_create(struct st_sftp_client **result, const struct st_sftp_io *io)
{
	if (result == NULL || io == NULL || io->open == NULL || io->read == NULL
	    || io->write == NULL || io->close == NULL) return IO_ERROR_INVALID_ARGUMENT;
	struct st_sftp_client *c = calloc(1, sizeof(*c));
	if (c == NULL) return IO_ERROR_OUT_OF_MEMORY;
	if (st_mutex_init(&c->mutex, ST_MUTEX_PLAIN) != ST_THREAD_SUCCESS) { free(c); return IO_ERROR_IO; }
	if (st_condition_init(&c->changed) != ST_THREAD_SUCCESS) { st_mutex_destroy(&c->mutex); free(c); return IO_ERROR_IO; }
	c->io = *io;
	c->provider = (struct st_sftp_provider){.context = c, .available = available,
	    .public_directory = pubdir, .long_name_extension = lname,
	    .submit = submit, .poll = poll_response, .cancel_all = cancel_all};
	*result = c;
	return 0;
}
const struct st_sftp_provider *
st_sftp_client_provider(struct st_sftp_client *c)
{ return c == NULL ? NULL : &c->provider; }
void
st_sftp_client_destroy(struct st_sftp_client *c)
{
	int result;
	if (c == NULL) return;
	cancel_all(c);
	if (c->started) (void)st_thread_join(c->worker, &result);
	free_request(c->borrowed);
	while (c->completed != NULL) {
		struct request *next = c->completed->next;
		free_request(c->completed);
		c->completed = next;
	}
	free(c->pubdir);
	st_condition_destroy(&c->changed);
	st_mutex_destroy(&c->mutex);
	free(c);
}
