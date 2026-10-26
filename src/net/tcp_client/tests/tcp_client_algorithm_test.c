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

struct diagnostics {
	unsigned count;
	enum tcp_client_event events[16];
	enum tcp_client_address_family families[16];
	int statuses[16];
};

static void
capture_diagnostic(void *context, enum tcp_client_event event,
	enum tcp_client_address_family family, int status)
{
	struct diagnostics *log = context;
	if (log->count < 16) {
		log->events[log->count] = event;
		log->families[log->count] = family;
		log->statuses[log->count] = status;
	}
	++log->count;
}

static bool
cancel_after_120_ms(void *context)
{
	(void)context;
	return tcp_client_fake_now() >= 120;
}

static bool
cancel_after_370_ms(void *context)
{
	(void)context;
	return tcp_client_fake_now() >= 370;
}

static int
test_diagnostics(void)
{
	const struct tcp_client_fake_address addresses[] = {
		{TCP_CLIENT_ADDRESS_IPV4, TCP_CLIENT_FAKE_FAIL_START, 0, IO_ERROR_CONNECTION_REFUSED},
		{TCP_CLIENT_ADDRESS_IPV6, TCP_CLIENT_FAKE_PENDING_FAILURE, 10, IO_ERROR_HOST_UNREACHABLE},
		{TCP_CLIENT_ADDRESS_IPV4, TCP_CLIENT_FAKE_IMMEDIATE_SUCCESS, 0, 0},
	};
	struct diagnostics log = {0};
	struct tcp_client_options options = {
		.family = TCP_CLIENT_ADDRESS_ANY, .timeout_ms = 1000,
		.diagnostic = capture_diagnostic, .diagnostic_context = &log,
		.happy_eyeballs = true
	};
	socket_handle_t result = SOCKET_HANDLE_INVALID;
	const enum tcp_client_event expected[] = {
		TCP_CLIENT_RESOLVING, TCP_CLIENT_ATTEMPT, TCP_CLIENT_ATTEMPT_FAILED,
		TCP_CLIENT_ATTEMPT, TCP_CLIENT_ATTEMPT_FAILED, TCP_CLIENT_ATTEMPT, TCP_CLIENT_CONNECTED
	};
	tcp_client_fake_reset(addresses, 3);
	CHECK(tcp_client_connect(&result, NULL, "example", 23, &options) == 0);
	CHECK(log.count == sizeof(expected) / sizeof(expected[0]));
	for (unsigned i = 0; i < log.count; ++i) CHECK(log.events[i] == expected[i]);
	CHECK(log.statuses[2] == IO_ERROR_CONNECTION_REFUSED);
	CHECK(log.statuses[4] == IO_ERROR_HOST_UNREACHABLE);
	CHECK(log.families[3] == TCP_CLIENT_ADDRESS_IPV6);
	CHECK(log.families[6] == TCP_CLIENT_ADDRESS_IPV4 && log.statuses[6] == 0);
	CHECK(socket_close(result) == 0);
	log = (struct diagnostics){0};
	tcp_client_fake_reset(addresses, 2);
	CHECK(tcp_client_connect(&result, NULL, "example", 23, &options) == IO_ERROR_CONNECTION_REFUSED);
	CHECK(log.count == 6 && log.events[5] == TCP_CLIENT_FAILED);
	CHECK(log.statuses[5] == IO_ERROR_CONNECTION_REFUSED);
	log = (struct diagnostics){0};
	options.cancel = cancel_after_120_ms;
	const struct tcp_client_fake_address pending = {TCP_CLIENT_ADDRESS_IPV4, TCP_CLIENT_FAKE_NEVER, 0, 0};
	tcp_client_fake_reset(&pending, 1);
	CHECK(tcp_client_connect(&result, NULL, "example", 23, &options) == IO_ERROR_CANCELLED);
	CHECK(log.count == 3 && log.events[2] == TCP_CLIENT_FAILED);
	CHECK(log.statuses[2] == IO_ERROR_CANCELLED);
	return 0;
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
		.family = TCP_CLIENT_ADDRESS_ANY, .timeout_ms = 2000,
		.happy_eyeballs = true
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
		.family = TCP_CLIENT_ADDRESS_IPV4, .timeout_ms = 1000,
		.happy_eyeballs = true
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
		{TCP_CLIENT_ADDRESS_IPV6, TCP_CLIENT_FAKE_IMMEDIATE_SUCCESS, 0, 0},
	};
	const struct tcp_client_options options = {
		.family = TCP_CLIENT_ADDRESS_ANY, .timeout_ms = 1000,
		.cancel = cancel_after_120_ms
	};
	socket_handle_t result = (socket_handle_t)77;

	tcp_client_fake_reset(addresses, 2);
	CHECK(tcp_client_connect(&result, NULL, "example", 23, &options) ==
	    IO_ERROR_CANCELLED);
	CHECK(result == (socket_handle_t)77);
	CHECK(tcp_client_fake_now() == 150);
	CHECK(tcp_client_fake_close_count() == 1);
	CHECK(tcp_client_fake_start_count() == 1);
	return 0;
}

static int
test_deadline_and_cleanup(void)
{
	const struct tcp_client_fake_address addresses[] = {
		{TCP_CLIENT_ADDRESS_IPV4, TCP_CLIENT_FAKE_NEVER, 0, 0},
		{TCP_CLIENT_ADDRESS_IPV6, TCP_CLIENT_FAKE_IMMEDIATE_SUCCESS, 0, 0},
	};
	const struct tcp_client_options options = {
		.family = TCP_CLIENT_ADDRESS_ANY, .timeout_ms = 1000
	};
	socket_handle_t result = (socket_handle_t)88;

	tcp_client_fake_reset(addresses, 2);
	CHECK(tcp_client_connect(&result, NULL, "example", 23, &options) ==
	    IO_ERROR_TIMED_OUT);
	CHECK(result == (socket_handle_t)88);
	CHECK(tcp_client_fake_now() == 1000);
	CHECK(tcp_client_fake_close_count() == 1);
	CHECK(tcp_client_fake_start_count() == 1);
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
		.family = TCP_CLIENT_ADDRESS_ANY, .timeout_ms = 1000,
		.happy_eyeballs = true
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
		.family = TCP_CLIENT_ADDRESS_ANY, .timeout_ms = 1000,
		.happy_eyeballs = true
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

static int
test_preferred_address_only(void)
{
	const struct tcp_client_options options = {
		.family = TCP_CLIENT_ADDRESS_ANY, .timeout_ms = 2000
	};
	const struct tcp_client_fake_address first_addresses[] = {
		{TCP_CLIENT_ADDRESS_IPV6, TCP_CLIENT_FAKE_PENDING_SUCCESS, 600, 0},
		{TCP_CLIENT_ADDRESS_IPV6, TCP_CLIENT_FAKE_PENDING_FAILURE, 600,
		    IO_ERROR_HOST_UNREACHABLE},
		{TCP_CLIENT_ADDRESS_IPV6, TCP_CLIENT_FAKE_FAIL_START, 0,
		    IO_ERROR_CONNECTION_REFUSED},
	};

	for (size_t i = 0; i < sizeof(first_addresses) / sizeof(first_addresses[0]); ++i) {
		const struct tcp_client_fake_address addresses[] = {
			first_addresses[i],
			{TCP_CLIENT_ADDRESS_IPV4, TCP_CLIENT_FAKE_IMMEDIATE_SUCCESS, 0, 0},
		};
		socket_handle_t result = SOCKET_HANDLE_INVALID;
		enum tcp_client_address_family family = TCP_CLIENT_ADDRESS_ANY;

		tcp_client_fake_reset(addresses, 2);
		CHECK(tcp_client_connect(&result, &family, "example", 23, &options)
		    == first_addresses[i].error);
		CHECK(tcp_client_fake_start_count() == 1);
		CHECK(tcp_client_fake_started_address(0) == 0);
		if (first_addresses[i].error == 0) {
			CHECK(family == TCP_CLIENT_ADDRESS_IPV6);
			CHECK(tcp_client_fake_now() == 600);
			CHECK(socket_close(result) == 0);
		} else {
			CHECK(result == SOCKET_HANDLE_INVALID);
			CHECK(family == TCP_CLIENT_ADDRESS_ANY);
		}
		CHECK(tcp_client_fake_close_count()
		    == (first_addresses[i].behavior == TCP_CLIENT_FAKE_FAIL_START ? 0U : 1U));
	}
	return 0;
}

static int
test_racing_cleanup(void)
{
	const struct tcp_client_fake_address addresses[] = {
		{TCP_CLIENT_ADDRESS_IPV6, TCP_CLIENT_FAKE_NEVER, 0, 0},
		{TCP_CLIENT_ADDRESS_IPV4, TCP_CLIENT_FAKE_NEVER, 0, 0},
	};
	struct tcp_client_options options = {
		.family = TCP_CLIENT_ADDRESS_ANY, .timeout_ms = 500,
		.happy_eyeballs = true
	};

	for (unsigned cancel = 0; cancel < 2; ++cancel) {
		socket_handle_t result = SOCKET_HANDLE_INVALID;
		options.cancel = cancel ? cancel_after_370_ms : NULL;
		tcp_client_fake_reset(addresses, 2);
		CHECK(tcp_client_connect(&result, NULL, "example", 23, &options)
		    == (cancel ? IO_ERROR_CANCELLED : IO_ERROR_TIMED_OUT));
		CHECK(result == SOCKET_HANDLE_INVALID);
		CHECK(tcp_client_fake_start_count() == 2);
		CHECK(tcp_client_fake_close_count() == 2);
		CHECK(tcp_client_fake_now() == (cancel ? 400 : 500));
	}
	return 0;
}

int
main(void)
{
	CHECK(test_preferred_address_only() == 0);
	CHECK(test_racing_cleanup() == 0);
	CHECK(test_diagnostics() == 0);
	CHECK(test_interleaved_stagger() == 0);
	CHECK(test_immediate_fallback() == 0);
	CHECK(test_cancel_poll_and_cleanup() == 0);
	CHECK(test_deadline_and_cleanup() == 0);
	CHECK(test_specific_error_and_unchanged_outputs() == 0);
	CHECK(test_recorded_failure_does_not_suppress_success() == 0);
	return 0;
}
