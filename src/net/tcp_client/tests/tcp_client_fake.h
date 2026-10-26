/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef SYNCTERM_TCP_CLIENT_FAKE_H_
#define SYNCTERM_TCP_CLIENT_FAKE_H_

#include "tcp_client.h"

#include <stddef.h>
#include <stdint.h>

enum tcp_client_fake_behavior {
	TCP_CLIENT_FAKE_FAIL_START,
	TCP_CLIENT_FAKE_PENDING_SUCCESS,
	TCP_CLIENT_FAKE_PENDING_FAILURE,
	TCP_CLIENT_FAKE_NEVER,
	TCP_CLIENT_FAKE_IMMEDIATE_SUCCESS,
};

struct tcp_client_fake_address {
	enum tcp_client_address_family family;
	enum tcp_client_fake_behavior behavior;
	int64_t ready_at;
	int error;
};

void tcp_client_fake_reset(const struct tcp_client_fake_address *addresses,
    size_t count);
size_t tcp_client_fake_start_count(void);
size_t tcp_client_fake_started_address(size_t index);
int64_t tcp_client_fake_start_time(size_t index);
int64_t tcp_client_fake_now(void);
size_t tcp_client_fake_close_count(void);

#endif
