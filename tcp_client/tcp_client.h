/* Blocking TCP client connection establishment with address racing. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef SYNCTERM_TCP_CLIENT_H_
#define SYNCTERM_TCP_CLIENT_H_

#include "socket.h"

#include <stdbool.h>
#include <stdint.h>

enum tcp_client_address_family {
	TCP_CLIENT_ADDRESS_ANY,
	TCP_CLIENT_ADDRESS_IPV4,
	TCP_CLIENT_ADDRESS_IPV6,
};

typedef bool (*tcp_client_cancel_fn)(void *context);

struct tcp_client_options {
	enum tcp_client_address_family family;
	/* Overall connection-racing deadline; -1 waits indefinitely. */
	int64_t timeout_ms;
	tcp_client_cancel_fn cancel;
	void *cancel_context;
};

/*
 * The caller owns socket runtime initialization. Resolution is synchronous
 * and occurs before both the deadline and cancellation checks start. The
 * cancellation callback runs on the calling thread. On success, result
 * receives a blocking socket and connected_family, when non-NULL, receives
 * IPV4 or IPV6. Outputs are unchanged on failure.
 */
int tcp_client_connect(socket_handle_t *result,
    enum tcp_client_address_family *connected_family, const char *host,
    uint16_t port, const struct tcp_client_options *options);

#endif
