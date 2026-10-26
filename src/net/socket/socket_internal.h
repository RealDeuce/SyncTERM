/* Private mechanisms shared with the TCP client component. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef SYNCTERM_SOCKET_INTERNAL_H_
#define SYNCTERM_SOCKET_INTERNAL_H_

#include "socket.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct socket_wait_item {
	socket_handle_t socket;
	unsigned interests;
	unsigned events;
	int error;
};

int socket_internal_map_error(int native_error);
int socket_internal_now_ms(int64_t *result);
size_t socket_internal_wait_capacity(void);
/*
 * Clears each item's events and error before waiting. SOCKET_WAIT_ERROR
 * carries a nonzero portable error in the corresponding error field. That
 * value is authoritative because obtaining it may consume native error state.
 */
int socket_internal_wait_many(struct socket_wait_item *items, size_t count,
    int64_t timeout_ms);
int socket_internal_start_connect(const void *address, size_t address_length,
    int family, socket_handle_t *result, bool *complete);
int socket_internal_connect_result(socket_handle_t socket);

#endif
