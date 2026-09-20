/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "tcp_client_fake.h"

#include "io_error.h"

#include <stdio.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, \
		    __LINE__, #expression); \
		return 1; \
	} \
} while (0)

static bool
cancel_after_120_ms(void *context)
{
	(void)context;
	return tcp_client_fake_now() >= 120;
}

static int
test_interleaved_stagger(void)
{
	const struct tcp_client_fake_address addresses[] = {
		{TCP_CLIENT_ADDRESS_IPV4, TCP_CLIENT_FAKE_NEVER, 0, 0},
		{TCP_CLIENT_ADDRESS_IPV4, TCP_CLIENT_FAKE_NEVER, 0, 0},
		{TCP_CLIENT_ADDRESS_IPV6, TCP_CLIENT_FAKE_PENDING_SUCCESS, 300, 0},
	};
	const struct tcp_client_options options = {
		TCP_CLIENT_ADDRESS_ANY, 2000, NULL, NULL
	};
	socket_handle_t result = SOCKET_HANDLE_INVALID;
	enum tcp_client_address_family family = TCP_CLIENT_ADDRESS_ANY;

	tcp_client_fake_reset(addresses, 3);
	CHECK(tcp_client_connect(&result, &family, "example", 23,
	    &options) == 0);
	CHECK(tcp_client_fake_start_count() == 2);
	CHECK(tcp_client_fake_started_address(0) == 0);
	CHECK(tcp_client_fake_started_address(1) == 2);
	CHECK(tcp_client_fake_start_time(0) == 0);
	CHECK(tcp_client_fake_start_time(1) == 250);
	CHECK(family == TCP_CLIENT_ADDRESS_IPV6);
	CHECK(tcp_client_fake_close_count() == 1);
	CHECK(socket_close(result) == 0);
	return 0;
}

static int
test_immediate_fallback(void)
{
	const struct tcp_client_fake_address addresses[] = {
		{TCP_CLIENT_ADDRESS_IPV4, TCP_CLIENT_FAKE_FAIL_START, 0,
		    IO_ERROR_CONNECTION_REFUSED},
		{TCP_CLIENT_ADDRESS_IPV4, TCP_CLIENT_FAKE_IMMEDIATE_SUCCESS, 0, 0},
	};
	const struct tcp_client_options options = {
		TCP_CLIENT_ADDRESS_IPV4, 1000, NULL, NULL
	};
	socket_handle_t result = SOCKET_HANDLE_INVALID;

	tcp_client_fake_reset(addresses, 2);
	CHECK(tcp_client_connect(&result, NULL, "example", 23, &options) == 0);
	CHECK(tcp_client_fake_start_count() == 2);
	CHECK(tcp_client_fake_start_time(0) == 0);
	CHECK(tcp_client_fake_start_time(1) == 0);
	CHECK(socket_close(result) == 0);
	return 0;
}

static int
test_cancel_poll_and_cleanup(void)
{
	const struct tcp_client_fake_address addresses[] = {
		{TCP_CLIENT_ADDRESS_IPV4, TCP_CLIENT_FAKE_NEVER, 0, 0},
	};
	const struct tcp_client_options options = {
		TCP_CLIENT_ADDRESS_IPV4, 1000, cancel_after_120_ms, NULL
	};
	socket_handle_t result = (socket_handle_t)77;

	tcp_client_fake_reset(addresses, 1);
	CHECK(tcp_client_connect(&result, NULL, "example", 23, &options) ==
	    IO_ERROR_CANCELLED);
	CHECK(result == (socket_handle_t)77);
	CHECK(tcp_client_fake_now() == 150);
	CHECK(tcp_client_fake_close_count() == 1);
	return 0;
}

static int
test_deadline_and_cleanup(void)
{
	const struct tcp_client_fake_address addresses[] = {
		{TCP_CLIENT_ADDRESS_IPV4, TCP_CLIENT_FAKE_NEVER, 0, 0},
	};
	const struct tcp_client_options options = {
		TCP_CLIENT_ADDRESS_IPV4, 125, NULL, NULL
	};
	socket_handle_t result = (socket_handle_t)88;

	tcp_client_fake_reset(addresses, 1);
	CHECK(tcp_client_connect(&result, NULL, "example", 23, &options) ==
	    IO_ERROR_TIMED_OUT);
	CHECK(result == (socket_handle_t)88);
	CHECK(tcp_client_fake_now() == 125);
	CHECK(tcp_client_fake_close_count() == 1);
	return 0;
}

static int
test_specific_error_and_unchanged_outputs(void)
{
	const struct tcp_client_fake_address addresses[] = {
		{TCP_CLIENT_ADDRESS_IPV4, TCP_CLIENT_FAKE_FAIL_START, 0,
		    IO_ERROR_IO},
		{TCP_CLIENT_ADDRESS_IPV6, TCP_CLIENT_FAKE_PENDING_FAILURE, 10,
		    IO_ERROR_HOST_UNREACHABLE},
	};
	const struct tcp_client_options options = {
		TCP_CLIENT_ADDRESS_ANY, 1000, NULL, NULL
	};
	socket_handle_t result = (socket_handle_t)66;
	enum tcp_client_address_family family = TCP_CLIENT_ADDRESS_IPV4;

	tcp_client_fake_reset(addresses, 2);
	CHECK(tcp_client_connect(&result, &family, "example", 23, &options) ==
	    IO_ERROR_HOST_UNREACHABLE);
	CHECK(result == (socket_handle_t)66);
	CHECK(family == TCP_CLIENT_ADDRESS_IPV4);
	CHECK(tcp_client_fake_start_count() == 2);
	CHECK(tcp_client_fake_start_time(0) == 0);
	CHECK(tcp_client_fake_start_time(1) == 0);
	CHECK(tcp_client_fake_close_count() == 1);
	return 0;
}

static int
test_recorded_failure_does_not_suppress_success(void)
{
	const struct tcp_client_fake_address addresses[] = {
		{TCP_CLIENT_ADDRESS_IPV4, TCP_CLIENT_FAKE_PENDING_FAILURE, 300,
		    IO_ERROR_HOST_UNREACHABLE},
		{TCP_CLIENT_ADDRESS_IPV6, TCP_CLIENT_FAKE_PENDING_SUCCESS, 300, 0},
	};
	const struct tcp_client_options options = {
		TCP_CLIENT_ADDRESS_ANY, 1000, NULL, NULL
	};
	socket_handle_t result = SOCKET_HANDLE_INVALID;
	enum tcp_client_address_family family = TCP_CLIENT_ADDRESS_ANY;

	tcp_client_fake_reset(addresses, 2);
	CHECK(tcp_client_connect(&result, &family, "example", 23,
	    &options) == 0);
	CHECK(tcp_client_fake_start_count() == 2);
	CHECK(tcp_client_fake_started_address(0) == 0);
	CHECK(tcp_client_fake_started_address(1) == 1);
	CHECK(tcp_client_fake_start_time(0) == 0);
	CHECK(tcp_client_fake_start_time(1) == 250);
	CHECK(tcp_client_fake_now() == 300);
	CHECK(family == TCP_CLIENT_ADDRESS_IPV6);
	CHECK(tcp_client_fake_close_count() == 1);
	CHECK(socket_close(result) == 0);
	CHECK(tcp_client_fake_close_count() == 2);
	return 0;
}

int
main(void)
{
	CHECK(test_interleaved_stagger() == 0);
	CHECK(test_immediate_fallback() == 0);
	CHECK(test_cancel_poll_and_cleanup() == 0);
	CHECK(test_deadline_and_cleanup() == 0);
	CHECK(test_specific_error_and_unchanged_outputs() == 0);
	CHECK(test_recorded_failure_does_not_suppress_success() == 0);
	return 0;
}
