/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "serial.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if ST_SERIAL_TEST_HAVE_PTY
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#endif

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, \
		    __LINE__, #expression); \
		return 1; \
	} \
} while (0)

static int
test_arguments(void)
{
	serial_port_t *port = (serial_port_t *)(uintptr_t)1;
	struct serial_config config = {
		9600, 8, 1, SERIAL_PARITY_NONE, SERIAL_FLOW_CONTROL_NONE
	};
	unsigned lines;
	size_t count;
	char byte;

	CHECK(serial_port_open(NULL, "device") == IO_ERROR_INVALID_ARGUMENT);
	CHECK(serial_port_open(&port, NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(port == (serial_port_t *)(uintptr_t)1);
	CHECK(serial_port_open(&port, "") == IO_ERROR_INVALID_ARGUMENT);
	CHECK(port == (serial_port_t *)(uintptr_t)1);
	CHECK(serial_port_open(&port, "st-serial-device-that-does-not-exist") != 0);
	CHECK(port == (serial_port_t *)(uintptr_t)1);
	CHECK(serial_port_configure(NULL, &config) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(serial_port_read(NULL, &byte, 1, 0) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(serial_port_write(NULL, &byte, 1, 0) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(serial_port_get_modem_lines(NULL, &lines) ==
	    IO_ERROR_INVALID_ARGUMENT);
	CHECK(serial_port_set_dtr(NULL, true) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(serial_port_set_rts(NULL, true) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(serial_port_get_baud_rates(NULL, NULL, 0, &count) ==
	    IO_ERROR_INVALID_ARGUMENT);
	CHECK(serial_port_close(NULL) == 0);
	return 0;
}

#if ST_SERIAL_TEST_HAVE_PTY
static int
test_pseudo_terminal(void)
{
	static const char inbound[] = "from-master";
	static const char outbound[] = "from-serial";
	struct serial_config config = {
		9600, 8, 1, SERIAL_PARITY_NONE, SERIAL_FLOW_CONTROL_NONE
	};
	serial_port_t *port = NULL;
	uint32_t rates[4];
	char buffer[32];
	size_t rate_count = 0;
	ssize_t amount;
	char *name;
	int master;
	bool found_9600 = false;

	master = posix_openpt(O_RDWR | O_NOCTTY);
	CHECK(master >= 0);
	CHECK(grantpt(master) == 0);
	CHECK(unlockpt(master) == 0);
	name = ptsname(master);
	CHECK(name != NULL);
	CHECK(serial_port_open(&port, name) == 0);
	CHECK(port != NULL);
	CHECK(serial_port_configure(port, &config) == 0);
	CHECK(serial_port_get_baud_rates(port, rates,
	    sizeof(rates) / sizeof(rates[0]), &rate_count) == 0);
	CHECK(rate_count >= sizeof(rates) / sizeof(rates[0]));
	CHECK(serial_port_get_baud_rates(port, NULL, 0, &rate_count) == 0);
	/* Verify the known rate through a buffer large enough for this contract. */
	{
		uint32_t all_rates[64];
		CHECK(rate_count <= sizeof(all_rates) / sizeof(all_rates[0]));
		CHECK(serial_port_get_baud_rates(port, all_rates,
		    sizeof(all_rates) / sizeof(all_rates[0]), &rate_count) == 0);
		for (size_t i = 0; i < rate_count; i++) {
			if (all_rates[i] == 9600)
				found_9600 = true;
		}
	}
	CHECK(found_9600);
	config.baud_rate = UINT32_MAX;
	CHECK(serial_port_configure(port, &config) == IO_ERROR_NOT_SUPPORTED);
	config.baud_rate = 9600;
	config.data_bits = 4;
	CHECK(serial_port_configure(port, &config) == IO_ERROR_INVALID_ARGUMENT);
	config.data_bits = 8;
	config.flow_control = SERIAL_FLOW_CONTROL_DTR_DSR;
	CHECK(serial_port_configure(port, &config) == IO_ERROR_NOT_SUPPORTED);
	config.flow_control = SERIAL_FLOW_CONTROL_NONE;
	CHECK(serial_port_get_baud_rates(port, NULL, 1, &rate_count) ==
	    IO_ERROR_INVALID_ARGUMENT);
	CHECK(serial_port_get_baud_rates(port, rates, 1, NULL) ==
	    IO_ERROR_INVALID_ARGUMENT);
	CHECK(serial_port_get_modem_lines(port, NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(serial_port_read(port, NULL, 1, 0) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(serial_port_write(port, NULL, 1, 0) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(serial_port_read(port, buffer, sizeof(buffer), -2) ==
	    IO_ERROR_INVALID_ARGUMENT);
	CHECK(serial_port_write(port, buffer, sizeof(buffer), -2) ==
	    IO_ERROR_INVALID_ARGUMENT);
	CHECK(serial_port_read(port, buffer, sizeof(buffer), 0) ==
	    IO_ERROR_TIMED_OUT);
	CHECK(serial_port_read(port, buffer, sizeof(buffer), 10) ==
	    IO_ERROR_TIMED_OUT);
	CHECK(write(master, inbound, sizeof(inbound) - 1) ==
	    (ssize_t)(sizeof(inbound) - 1));
	amount = serial_port_read(port, buffer, sizeof(buffer), 1000);
	CHECK(amount == (ssize_t)(sizeof(inbound) - 1));
	CHECK(memcmp(buffer, inbound, sizeof(inbound) - 1) == 0);
	amount = serial_port_write(port, outbound, sizeof(outbound) - 1, 1000);
	CHECK(amount == (ssize_t)(sizeof(outbound) - 1));
	CHECK(read(master, buffer, sizeof(buffer)) ==
	    (ssize_t)(sizeof(outbound) - 1));
	CHECK(memcmp(buffer, outbound, sizeof(outbound) - 1) == 0);
	CHECK(serial_port_read(port, NULL, 0, -1) == 0);
	CHECK(serial_port_write(port, NULL, 0, -1) == 0);
	CHECK(serial_port_close(port) == 0);
	CHECK(close(master) == 0);
	return 0;
}
#endif

int
main(void)
{
	CHECK(test_arguments() == 0);
#if ST_SERIAL_TEST_HAVE_PTY
	CHECK(test_pseudo_terminal() == 0);
#endif
	return 0;
}
