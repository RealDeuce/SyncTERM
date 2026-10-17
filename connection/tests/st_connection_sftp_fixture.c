/* Independent SFTP v3 wire oracle. No client codec or user files involved.
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */
#include <deucessh.h>
#include <deucessh-conn.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(e) do { if (!(e)) { fprintf(stderr, "SFTP fixture %d: %s\n", __LINE__, #e); return 1; } } while (0)
struct packet { uint8_t bytes[300000]; size_t used, pos; bool invalid; };
struct file { char path[256]; uint8_t data[1048576]; size_t size; uint32_t mode, mtime, flags; bool present; };
static struct file files[8];
static unsigned accepted;
static bool directory_read;
static uint32_t get32(struct packet *p)
{
	if (p->pos + 4 > p->used) { p->invalid = true; return 0; }
	const uint8_t *b = p->bytes + p->pos; p->pos += 4;
	return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
}
static uint64_t get64(struct packet *p)
{ uint64_t high = get32(p), low = get32(p); return (high << 32) | low; }
static const uint8_t *getstr(struct packet *p, uint32_t *size)
{
	*size = get32(p);
	if (p->invalid || *size > p->used - p->pos) { p->invalid = true; *size = 0; return NULL; }
	const uint8_t *s = p->bytes + p->pos; p->pos += *size; return s;
}
static void put32(struct packet *p, uint32_t n)
{ for (int s = 24; s >= 0; s -= 8) p->bytes[p->used++] = (uint8_t)(n >> s); }
static void put64(struct packet *p, uint64_t n)
{ put32(p, (uint32_t)(n >> 32)); put32(p, (uint32_t)n); }
static void putstr(struct packet *p, const void *bytes, size_t n)
{ put32(p, (uint32_t)n); if (n != 0) memcpy(p->bytes + p->used, bytes, n); p->used += n; }
static void text(struct packet *p, const char *s) { putstr(p, s, strlen(s)); }
static int send_bytes(dssh_channel ch, const void *data, size_t size)
{
	for (size_t sent = 0; sent < size;) {
		CHECK((dssh_chan_poll(ch, DSSH_POLL_WRITE, 10000) & DSSH_POLL_WRITE) != 0);
		int64_t n = dssh_chan_write(ch, 0, (const uint8_t *)data + sent, size - sent);
		CHECK(n > 0); sent += (size_t)n;
	}
	return 0;
}
static int read_bytes(dssh_channel ch, void *bytes, size_t size)
{
	for (size_t used = 0; used < size;) {
		if ((dssh_chan_poll(ch, DSSH_POLL_READ, 10000) & DSSH_POLL_READ) == 0) return -1;
		int64_t n = dssh_chan_read(ch, 0, (uint8_t *)bytes + used, size - used);
		if (n <= 0) return -1;
		used += (size_t)n;
	}
	return 0;
}
static int send_packet(dssh_channel ch, struct packet *p)
{
	struct packet length = {0}; put32(&length, (uint32_t)p->used);
	CHECK(send_bytes(ch, length.bytes, 2) == 0);
	CHECK(send_bytes(ch, length.bytes + 2, 2) == 0);
	CHECK(send_bytes(ch, p->bytes, 1) == 0);
	return send_bytes(ch, p->bytes + 1, p->used - 1);
}
static void status(struct packet *p, uint32_t id, uint32_t code)
{ p->used = 0; p->bytes[p->used++] = 101; put32(p, id); put32(p, code); text(p, code == 0 ? "" : "fixture failure"); text(p, "en"); }
static int subsystem(dssh_channel ch, const struct dssh_chan_params *params,
	struct dssh_chan_accept_result *result, void *context)
{
	(void)ch; (void)result; ++accepted;
	return params->subsystem != NULL && strcmp(params->subsystem, "sftp") == 0
	    && strcmp(context, "sftp-unsupported") != 0 ? 0 : -1;
}
static void attributes(struct packet *out, struct file *f, bool extensions)
{
	put32(out, 1 | 4 | 8 | (extensions ? UINT32_C(0x80000000) : 0));
	put64(out, f->size); put32(out, f->mode); put32(out, f->mtime); put32(out, f->mtime);
	if (extensions) {
		put32(out, 3);
		text(out, "lname@syncterm.net"); text(out, "Friendly fixture");
		text(out, "descs@syncterm.net"); text(out, "");
		text(out, "sha1@syncterm.net"); putstr(out, "01234567890123456789", 20);
	}
}
int st_connection_sftp_fixture(dssh_session session, dssh_channel shell, const char *mode)
{
	struct dssh_chan_accept_cbs callbacks = {.subsystem = subsystem, .cbdata = (void *)mode};
	CHECK(dssh_chan_accept(session, &callbacks, 150) == NULL && accepted == 0);
	CHECK(send_bytes(shell, "idle", 4) == 0);
	dssh_channel ch = dssh_chan_accept(session, &callbacks,
	    strcmp(mode, "sftp-unsupported") == 0 ? 1000 : 10000);
	if (strcmp(mode, "sftp-unsupported") == 0) {
		CHECK(ch == NULL && accepted == 1);
		CHECK(send_bytes(shell, "usable", 6) == 0);
		uint8_t done; (void)read_bytes(shell, &done, 1); return 0;
	}
	CHECK(ch != NULL && accepted == 1);
	strcpy(files[0].path, ".ssh/authorized_keys"); files[0].mode = 0100600; files[0].present = true;
	strcpy(files[1].path, "/remote.bin"); files[1].mode = 0100644; files[1].present = true;
	files[1].mtime = 1700000123;
	for (size_t i = 0; i < 90000; ++i) files[1].data[files[1].size++] = (uint8_t)(i * 13);
	for (;;) {
		struct packet in = {0}, out = {0};
		if (read_bytes(ch, in.bytes, 4) != 0) break;
		in.used = 4; uint32_t size = get32(&in);
		CHECK(size > 0 && size <= sizeof(in.bytes));
		CHECK(read_bytes(ch, in.bytes, size) == 0);
		in.used = size; in.pos = 1;
		uint8_t type = in.bytes[0]; uint32_t id = get32(&in);
		if (type == 1) {
			CHECK(id == 3); out.bytes[out.used++] = 2;
			if (strcmp(mode, "sftp-no-version") == 0) {
				CHECK(send_bytes(shell, "init", 4) == 0);
				uint8_t discarded;
				while (read_bytes(ch, &discarded, 1) == 0) {}
				break;
			}
			put32(&out, strcmp(mode, "sftp-bad-version") == 0 ? 2 : 3);
			const char *ext[] = {"lname@syncterm.net", "descs@syncterm.net", "sha1s@syncterm.net", "pubdir@syncterm.net"};
			for (size_t i = 0; i < 4; ++i) { text(&out, ext[i]); text(&out, i == 3 ? "/public" : "1"); }
			CHECK(send_packet(ch, &out) == 0);
			if (strcmp(mode, "sftp-shell-close") == 0) CHECK(dssh_chan_close(shell, 0) == 0);
			continue;
		}
		status(&out, id, 0);
		uint32_t len; const uint8_t *arg = getstr(&in, &len);
		CHECK(arg != NULL && !in.invalid && len < 256);
		char path[256]; memcpy(path, arg, len); path[len] = 0;
		int index = -1;
		for (int i = 0; i < 8; ++i) if (files[i].present && strcmp(files[i].path, path) == 0) index = i;
		if (type == 3) {
			uint32_t flags = get32(&in);
			if (strcmp(mode, "sftp-denied") == 0 || (strcmp(mode, "sftp-missing-dir") == 0 && path[0] == '.')) status(&out, id, strcmp(mode, "sftp-denied") == 0 ? 3 : 2);
			else {
				if (index < 0 && (flags & 8) != 0) {
					for (int i = 0; i < 8; ++i) if (!files[i].present) { index = i; break; }
					CHECK(index >= 0); strcpy(files[index].path, path); files[index].present = true;
					files[index].size = 0; files[index].mode = 0100644;
				}
				if (index < 0) status(&out, id, 2);
				else {
					if (flags & 16) files[index].size = 0;
					files[index].flags = flags;
					out.used = 0; out.bytes[out.used++] = 102; put32(&out, id);
					uint8_t handle[] = {0, (uint8_t)(index + 1)}; putstr(&out, handle, sizeof(handle));
				}
			}
		} else if (type == 5 || type == 6 || type == 4) {
			if (type == 5 && strcmp(mode, "sftp-drop") == 0) {
				(void)dssh_session_terminate(session); break;
			}
			CHECK(len == 2 && arg[0] == 0 && arg[1] <= 9); index = arg[1] - 1;
			if (type == 4) {
				if (strcmp(mode, "sftp-close-error") == 0) status(&out, id, 4);
			} else {
				CHECK(index >= 0 && index < 8); uint64_t offset = get64(&in);
				if (type == 5) {
					uint32_t count = get32(&in);
					if (strcmp(mode, "sftp-read-error") == 0) status(&out, id, 4);
					else if (offset >= files[index].size) status(&out, id, 1);
					else {
						if (count > 4093) count = 4093;
						if (count > files[index].size - offset) count = (uint32_t)(files[index].size - offset);
						out.used = 0; out.bytes[out.used++] = 103; put32(&out, id); putstr(&out, files[index].data + offset, count);
					}
				} else {
					uint32_t count; const uint8_t *bytes = getstr(&in, &count); CHECK(bytes != NULL);
					if (files[index].flags & 4) offset = files[index].size;
					CHECK(offset <= sizeof(files[index].data) && count <= sizeof(files[index].data) - offset);
					if (strcmp(mode, "sftp-write-error") == 0) status(&out, id, 4);
					else { memcpy(files[index].data + offset, bytes, count); if (offset + count > files[index].size) files[index].size = (size_t)offset + count; }
				}
			}
		} else if (type == 16) {
			out.used = 0; out.bytes[out.used++] = 104; put32(&out, id); put32(&out, 1); text(&out, "/"); text(&out, ""); put32(&out, 0);
		} else if (type == 17) {
			if (index < 0) status(&out, id, 2);
			else { out.used = 0; out.bytes[out.used++] = 105; put32(&out, id); attributes(&out, &files[index], false); }
		} else if (type == 11) {
			directory_read = false; out.used = 0; out.bytes[out.used++] = 102; put32(&out, id); const uint8_t handle[] = {0,9}; putstr(&out, handle, sizeof(handle));
		} else if (type == 12) {
			if (directory_read) status(&out, id, 1);
			else { directory_read = true; out.used = 0; out.bytes[out.used++] = 104; put32(&out, id); put32(&out, 1); text(&out, "remote.bin"); text(&out, "standard ls -l, NOT friendly name"); attributes(&out, &files[1], true); }
		} else if (type == 200) {
			CHECK(strcmp(path, "descs@syncterm.net") == 0); out.used = 0; out.bytes[out.used++] = 201; put32(&out, id); text(&out, "Long description");
		} else if (type == 18) {
			const uint8_t *new_path = getstr(&in, &len); CHECK(new_path != NULL && len < sizeof(path));
			if (index < 0) status(&out, id, 2); else { memcpy(files[index].path, new_path, len); files[index].path[len] = 0; }
		} else if (type == 13 || type == 15) {
			if (index < 0) status(&out, id, 2); else files[index].present = false;
		} else if (type == 14) {
			for (int i = 0; i < 8; ++i) if (!files[i].present) { index = i; break; }
			CHECK(index >= 0); strcpy(files[index].path, path); files[index].present = true; files[index].mode = 0040755;
		} else if (type == 9) {
			CHECK(get32(&in) == 8); (void)get32(&in); uint32_t mtime = get32(&in);
			if (index < 0) status(&out, id, 2); else files[index].mtime = mtime;
		} else status(&out, id, 8);
		CHECK(!in.invalid);
		if (send_packet(ch, &out) != 0) break;
	}
	CHECK(accepted == 1); return 0;
}
