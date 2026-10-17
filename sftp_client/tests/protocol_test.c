/* Literal wire fixtures, independent of the encoder under test.
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */
#include "sftp.h"
#include <stdio.h>
#include <string.h>
#define CHECK(e) do { if (!(e)) { fprintf(stderr, "SFTP codec %d: %s\n", __LINE__, #e); return 1; } } while (0)
struct capture { unsigned calls; int error; uint32_t result; uint8_t tx[1024]; size_t size; };
static bool send_packet(uint8_t *bytes, size_t size, void *data)
{
	struct capture *c = data;
	if (size > sizeof(c->tx)) return false;
	memcpy(c->tx, bytes, size); c->size = size; return true;
}
static void reply(struct sftpc_pending *p)
{
	struct capture *c = p->cbdata;
	c->calls++; c->error = p->err; c->result = p->result;
	/* The initiating function can still be returning; its caller frees p. */
}
int main(void)
{
	struct capture transport = {0}, a = {0}, b = {0};
	sftpc_state_t client = sftpc_begin(send_packet, &transport); CHECK(client != NULL);
	struct sftpc_pending *init = sftpc_init(client, reply, &a); CHECK(init != NULL);
	CHECK(transport.size > 9 && memcmp(transport.tx + 4, "\1\0\0\0\3", 5) == 0);
	uint8_t version[] = {0,0,0,5,2,0,0,0,3};
	for (size_t i = 0; i < sizeof(version); ++i) CHECK(sftpc_recv(client, version + i, 1));
	CHECK(a.calls == 1 && a.error == 0); sftpc_pending_free(init);
	a.calls = 0;
	struct sftpc_stat_pending *first = sftpc_stat(client, "/a", reply, &a);
	struct sftpc_stat_pending *second = sftpc_stat(client, "/b", reply, &b);
	CHECK(first != NULL && second != NULL);
	/* Two coalesced replies in reverse request order, ATTRS with size=42. */
	uint8_t replies[] = {
		0,0,0,17,105,0,0,0,2,0,0,0,1,0,0,0,0,0,0,0,42,
		0,0,0,17,105,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,42};
	CHECK(sftpc_recv(client, replies, sizeof(replies)));
	uint64_t size;
	CHECK(a.calls == 1 && b.calls == 1 && a.error == 0 && b.error == 0);
	CHECK(sftp_fattr_get_size(first->attrs, &size) && size == 42);
	sftpc_pending_free(&first->base); sftpc_pending_free(&second->base);
	a.calls = 0;
	first = sftpc_stat(client, "/truncated", reply, &a); CHECK(first != NULL);
	uint8_t truncated[] = {0,0,0,9,105,0,0,0,3,0,0,0,1};
	CHECK(sftpc_recv(client, truncated, sizeof(truncated)));
	CHECK(a.calls == 1 && a.error == SFTP_ERR_PARSE_FAILED); sftpc_pending_free(&first->base);
	a.calls = 0;
	first = sftpc_stat(client, "/pending", reply, &a); CHECK(first != NULL);
	uint8_t oversized[] = {255,255,255,255};
	CHECK(!sftpc_recv(client, oversized, sizeof(oversized)));
	sftpc_end(client);
	CHECK(a.calls == 1 && a.error == SFTP_ERR_ABORTED);
	sftpc_pending_free(&first->base);
	return 0;
}
