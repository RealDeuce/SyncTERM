/* Native socket handles with portable operations and error results. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef SYNCTERM_SOCKET_H_
#define SYNCTERM_SOCKET_H_

#include "syncterm_config.h"
#include "syncterm_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if SYNCTERM_PLATFORM_WINDOWS
	#include <winsock2.h>
	typedef SOCKET socket_handle_t;
#else
	typedef int socket_handle_t;
#endif

#define SOCKET_HANDLE_INVALID ((socket_handle_t)~(socket_handle_t)0)

#ifdef __cplusplus
extern "C" {
#endif

enum socket_wait_event {
	SOCKET_WAIT_READABLE = 1U << 0,
	SOCKET_WAIT_WRITABLE = 1U << 1,
	SOCKET_WAIT_URGENT = 1U << 2,
	SOCKET_WAIT_CLOSED = 1U << 3,
	SOCKET_WAIT_ERROR = 1U << 4,
};

enum socket_receive_flag {
	SOCKET_RECEIVE_NONE = 0,
	SOCKET_RECEIVE_PEEK = 1U << 0,
	SOCKET_RECEIVE_URGENT = 1U << 1,
};

enum socket_shutdown_direction {
	SOCKET_SHUTDOWN_READ,
	SOCKET_SHUTDOWN_WRITE,
	SOCKET_SHUTDOWN_BOTH,
};

/* Every successful initialization must have a matching cleanup call. */
int socket_runtime_init(void);
int socket_runtime_cleanup(void);

bool socket_handle_is_valid(socket_handle_t socket);
int socket_close(socket_handle_t socket);
int socket_shutdown(socket_handle_t socket,
    enum socket_shutdown_direction direction);
int socket_set_blocking(socket_handle_t socket, bool blocking);
int socket_set_keepalive(socket_handle_t socket, bool enabled);
int socket_set_no_delay(socket_handle_t socket, bool enabled);
int socket_set_send_buffer_size(socket_handle_t socket, size_t size);
int socket_set_receive_buffer_size(socket_handle_t socket, size_t size);

/* Returns a byte count, zero for an empty send or receive EOF, or IO_ERROR_*.
 * PEEK and URGENT are mutually exclusive; combining them is invalid on all
 * platforms. A native urgent peek need not preserve the TCP stream mark. */
ssize_t socket_send(socket_handle_t socket, const void *buffer, size_t size);
ssize_t socket_receive(socket_handle_t socket, void *buffer, size_t size,
    unsigned flags);

/* Reports whether the next normal byte follows the urgent-data mark. */
int socket_at_urgent_mark(socket_handle_t socket, bool *at_mark);

/* Non-consuming FIN query, including when ordinary data remains unread.
 * Does not mean the local write direction is closed. Returns IO_ERROR_*;
 * implementations without this capability return IO_ERROR_NOT_SUPPORTED. */
int socket_peer_write_closed(socket_handle_t socket, bool *closed);

/*
 * Interests contains at least one of READABLE, WRITABLE, and URGENT. CLOSED
 * may accompany a READABLE wait, and ERROR may accompany any wait. Timeout is
 * in milliseconds; -1 waits indefinitely. Events is cleared before waiting.
 * A timeout returns IO_ERROR_TIMED_OUT. When URGENT is reported, handle it
 * before normal reads; READABLE may then indicate data or EOF, whose CLOSED
 * classification is deferred to avoid disturbing an unread urgent mark.
 */
int socket_wait(socket_handle_t socket, unsigned interests,
    int64_t timeout_ms, unsigned *events);

#ifdef __cplusplus
}
#endif

#endif
