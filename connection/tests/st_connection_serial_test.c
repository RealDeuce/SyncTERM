/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_connection.h"

#include "io_error.h"
#include "st_connection_serial_fake.h"
#include "st_settings.h"
#include "st_threads.h"
#include "string_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { \
	fprintf(stderr, "failed: %s:%d: %s\n", __FILE__, __LINE__, #e); \
	return 1; \
} } while (0)

struct setup_state {
	unsigned phases;
	enum st_connection_setup_phase cancel_phase;
};

static enum st_connection_setup_action
setup(void *context, enum st_connection_setup_phase phase)
{
	struct setup_state *state = context;

	state->phases |= 1U << (unsigned)phase;
	return phase == state->cancel_phase
	    ? ST_CONNECTION_SETUP_CANCEL : ST_CONNECTION_SETUP_CONTINUE;
}

static bool
wait_for_output(size_t size)
{
	unsigned attempts;
	uint8_t output[4096];

	for (attempts = 0; attempts < 1000; ++attempts) {
		if (st_connection_serial_fake_output(output, sizeof(output)) >= size)
			return true;
		(void)st_thread_sleep(&(struct timespec){0, 1000000L}, NULL);
	}
	return false;
}

static int
test_serial(void)
{
	struct st_profile profile;
	struct serial_config config;
	st_connection_t *connection = NULL;
	uint8_t byte;
	uint8_t output[16];

	st_connection_serial_fake_reset(SERIAL_MODEM_LINE_DSR);
	st_profile_init(&profile);
	profile.connection_type = ST_CONNECTION_SERIAL;
	profile.bps_rate = 19200;
	profile.data_bits = 7;
	profile.stop_bits = 1;
	profile.parity = ST_PROFILE_PARITY_EVEN;
	profile.flow_control = SERIAL_FLOW_CONTROL_XON_XOFF;
	strcpy(profile.address, "fake-serial");
	CHECK(st_connection_connect_serial(&connection, &profile, NULL, NULL) == 0);
	config = st_connection_serial_fake_config();
	CHECK(config.baud_rate == 19200);
	CHECK(config.data_bits == 7);
	CHECK(config.parity == SERIAL_PARITY_EVEN);
	CHECK(st_connection_serial_fake_dtr());
	byte = 0xff;
	st_connection_serial_fake_input(&byte, 1);
	CHECK(st_connection_receive(connection, &byte, 1, 1000) == 1);
	CHECK(byte == 0x7f);
	byte = 0xfe;
	CHECK(st_connection_send(connection, &byte, 1, 1000) == 1);
	CHECK(wait_for_output(1));
	CHECK(st_connection_serial_fake_output(output, sizeof(output)) == 1);
	CHECK(output[0] == 0x7e);
	CHECK(st_connection_update_terminal_size(connection,
	    &(struct st_screen_geometry){132, 50, 0, 0}) == 0);
	st_connection_serial_fake_set_lines(0);
	for (unsigned attempts = 0; attempts < 1000
	    && st_connection_is_connected(connection); ++attempts)
		(void)st_thread_sleep(&(struct timespec){0, 1000000L}, NULL);
	CHECK(!st_connection_is_connected(connection));
	CHECK(st_connection_receive(connection, &byte, 1, 0) == 0);
	CHECK(st_connection_close(connection) == 0);
	CHECK(!st_connection_serial_fake_open());
	CHECK(!st_connection_serial_fake_dtr());
	st_profile_dispose(&profile);
	return 0;
}

static int
test_three_wire(void)
{
	struct st_profile profile;
	struct serial_config config;
	st_connection_t *connection = NULL;

	st_connection_serial_fake_reset(0);
	st_profile_init(&profile);
	profile.connection_type = ST_CONNECTION_SERIAL_3WIRE;
	profile.data_bits = 8;
	profile.stop_bits = 1;
	profile.flow_control = SERIAL_FLOW_CONTROL_RTS_CTS
	    | SERIAL_FLOW_CONTROL_XON_XOFF;
	strcpy(profile.address, "fake-serial");
	CHECK(st_connection_connect_serial(&connection, &profile, NULL, NULL) == 0);
	config = st_connection_serial_fake_config();
	CHECK(config.flow_control == SERIAL_FLOW_CONTROL_XON_XOFF);
	CHECK(!st_connection_serial_fake_rts());
	CHECK(st_connection_close(connection) == 0);
	st_profile_dispose(&profile);
	return 0;
}

static int
test_modem(void)
{
	struct st_profile profile;
	struct st_connection_hooks hooks = {0};
	struct setup_state setup_state = {0, ST_CONNECTION_SETUP_NONE};
	st_connection_t *connection = NULL;
	uint8_t output[4096];
	size_t output_size;

	st_connection_serial_fake_reset(SERIAL_MODEM_LINE_DCD);
	st_settings_dispose(&st_settings);
	st_settings_init(&st_settings);
	st_settings.modem_device = strdup("fake-modem");
	CHECK(st_settings.modem_device != NULL);
	st_settings.modem_rate = 57600;
	strcpy(st_settings.modem_init_string, "ATZ");
	strcpy(st_settings.modem_dial_string, "ATDT");
	st_profile_init(&profile);
	profile.connection_type = ST_CONNECTION_MODEM;
	profile.data_bits = 8;
	profile.stop_bits = 1;
	strcpy(profile.address, "555");
	hooks.setup = setup;
	hooks.setup_context = &setup_state;
	CHECK(st_connection_connect_modem(&connection, &profile, &hooks, NULL) == 0);
	CHECK((setup_state.phases
	    & (1U << ST_CONNECTION_SETUP_OPENING_DEVICE)) != 0);
	CHECK((setup_state.phases
	    & (1U << ST_CONNECTION_SETUP_INITIALIZING_MODEM)) != 0);
	CHECK((setup_state.phases & (1U << ST_CONNECTION_SETUP_DIALING)) != 0);
	output_size = st_connection_serial_fake_output(output, sizeof(output));
	CHECK(output_size == 12);
	CHECK(memcmp(output, "ATZ\rATDT555\r", output_size) == 0);
	CHECK(st_connection_close(connection) == 0);
	CHECK(!st_connection_serial_fake_open());
	CHECK(!st_connection_serial_fake_dtr());

	st_connection_serial_fake_reset(SERIAL_MODEM_LINE_DCD);
	setup_state.phases = 0;
	setup_state.cancel_phase = ST_CONNECTION_SETUP_INITIALIZING_MODEM;
	connection = (st_connection_t *)(uintptr_t)1;
	CHECK(st_connection_connect_modem(&connection, &profile, &hooks, NULL)
	    == IO_ERROR_CANCELLED);
	CHECK(connection == (st_connection_t *)(uintptr_t)1);
	CHECK(!st_connection_serial_fake_open());
	CHECK(!st_connection_serial_fake_dtr());
	st_profile_dispose(&profile);
	st_settings_dispose(&st_settings);
	return 0;
}

static int
test_profile_flow_mapping(void)
{
	static const struct {
		int profile;
		unsigned serial;
	} cases[] = {
		{ST_PROFILE_FLOW_RTS_CTS, SERIAL_FLOW_CONTROL_RTS_CTS},
		{ST_PROFILE_FLOW_XON_XOFF, SERIAL_FLOW_CONTROL_XON_XOFF},
		{ST_PROFILE_FLOW_RTS_CTS_XON_XOFF,
		    SERIAL_FLOW_CONTROL_RTS_CTS | SERIAL_FLOW_CONTROL_XON_XOFF},
		{ST_PROFILE_FLOW_NONE, SERIAL_FLOW_CONTROL_NONE},
	};
	int failures = 0;

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
		struct st_profile profile;
		struct serial_config config;
		st_connection_t *connection = NULL;

		st_connection_serial_fake_reset(0);
		st_profile_init(&profile);
		profile.connection_type = ST_CONNECTION_SERIAL;
		profile.flow_control = cases[i].profile;
		strcpy(profile.address, "fake-serial");
		CHECK(st_connection_connect_serial(&connection, &profile, NULL, NULL) == 0);
		config = st_connection_serial_fake_config();
		CHECK(st_connection_close(connection) == 0);
		st_profile_dispose(&profile);
		if (config.flow_control != cases[i].serial) {
			fprintf(stderr, "profile flow %d: got serial flags %u, expected %u\n",
			    cases[i].profile, config.flow_control, cases[i].serial);
			++failures;
		}
	}
	return failures != 0;
}

int
main(void)
{
	CHECK(test_serial() == 0);
	CHECK(test_three_wire() == 0);
	CHECK(test_modem() == 0);
	int failure = test_profile_flow_mapping();
	st_connection_serial_fake_dispose();
	return failure;
}
