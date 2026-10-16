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
#include <stdatomic.h>
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

enum log_event {
	LOG_OPEN = 1, LOG_CONFIG = 2, LOG_INIT = 4, LOG_OK = 8,
	LOG_DIAL = 16, LOG_CONNECT = 32, LOG_CARRIER = 64, LOG_DROP = 128,
	LOG_DTR = 256, LOG_CLOSE = 512, LOG_CANCEL = 1024, LOG_OPEN_ERROR = 2048
};
struct log_state { atomic_uint events; atomic_bool invalid; bool expect_error; };

static void
capture_log(void *context, enum st_connection_log_level level,
	const char *source, const char *message)
{
	struct log_state *log = context;
	unsigned events = 0;
	(void)source;
	if ((level == ST_CONNECTION_LOG_ERROR && !log->expect_error) || strstr(message, "555") != NULL
	    || strstr(message, "ATZ") != NULL || strstr(message, "fake-") != NULL)
		atomic_store(&log->invalid, true);
	if (strstr(message, "opening serial device")) events |= LOG_OPEN;
	if (strstr(message, "configured baud=")) events |= LOG_CONFIG;
	if (strstr(message, "initialization command sent")) events |= LOG_INIT;
	if (strstr(message, "modem result: OK")) events |= LOG_OK;
	if (strstr(message, "dial command sent")) events |= LOG_DIAL;
	if (strstr(message, "modem result: CONNECT")) events |= LOG_CONNECT;
	if (strstr(message, "carrier established")) events |= LOG_CARRIER;
	if (strstr(message, "DSR dropped") || strstr(message, "DCD dropped")) events |= LOG_DROP;
	if (strstr(message, "lowering DTR")) events |= LOG_DTR;
	if (strstr(message, "closing serial interface")) events |= LOG_CLOSE;
	if (strstr(message, "setup cancelled")) events |= LOG_CANCEL;
	if (strstr(message, "serial open failed")) {
		if (level != ST_CONNECTION_LOG_ERROR) atomic_store(&log->invalid, true);
		events |= LOG_OPEN_ERROR;
	}
	atomic_fetch_or(&log->events, events);
}

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
configure_modem_settings(void)
{
	st_settings_dispose(&st_settings);
	st_settings_init(&st_settings);
	st_settings.modem_device = strdup("fake-modem");
	CHECK(st_settings.modem_device != NULL);
	st_settings.modem_rate = 57600;
	strcpy(st_settings.modem_init_string, "ATZ");
	strcpy(st_settings.modem_dial_string, "ATDT");
	return 0;
}

static int
test_serial(void)
{
	struct log_state log = {0};
	struct st_connection_hooks hooks = {.log = capture_log, .log_context = &log};
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
	profile.flow_control = ST_PROFILE_FLOW_XON_XOFF;
	strcpy(profile.address, "fake-serial");
	CHECK(st_connection_connect_serial(&connection, &profile, &hooks, NULL) == 0);
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
	CHECK(atomic_load(&log.events) == (LOG_OPEN | LOG_CONFIG | LOG_DROP | LOG_DTR | LOG_CLOSE));
	CHECK(!atomic_load(&log.invalid));
	st_profile_dispose(&profile);
	return 0;
}

static int
test_serial_open_failure(void)
{
	struct log_state log = {.expect_error = true};
	struct st_connection_hooks hooks = {.log = capture_log, .log_context = &log};
	struct st_connection_error error;
	struct st_profile profile;
	st_connection_t *connection = NULL;
	serial_port_t *occupied;
	st_connection_serial_fake_reset(0);
	CHECK(serial_port_open(&occupied, "fake-serial") == 0);
	st_profile_init(&profile);
	profile.connection_type = ST_CONNECTION_SERIAL;
	strcpy(profile.address, "fake-serial");
	CHECK(st_connection_connect_serial(&connection, &profile, &hooks, &error) == IO_ERROR_BUSY);
	CHECK(connection == NULL && error.phase == ST_CONNECTION_SETUP_OPENING_DEVICE);
	CHECK(atomic_load(&log.events) == (LOG_OPEN | LOG_OPEN_ERROR));
	CHECK(!atomic_load(&log.invalid));
	CHECK(st_connection_serial_fake_open());
	CHECK(serial_port_close(occupied) == 0);
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
	profile.flow_control = ST_PROFILE_FLOW_RTS_CTS_XON_XOFF;
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
	struct log_state log = {0};
	struct st_profile profile;
	struct st_connection_hooks hooks = {0};
	struct setup_state setup_state = {0, ST_CONNECTION_SETUP_NONE};
	st_connection_t *connection = NULL;
	uint8_t output[4096];
	size_t output_size;

	st_connection_serial_fake_reset(SERIAL_MODEM_LINE_DCD);
	CHECK(configure_modem_settings() == 0);
	st_profile_init(&profile);
	profile.connection_type = ST_CONNECTION_MODEM;
	profile.data_bits = 8;
	profile.stop_bits = 1;
	strcpy(profile.address, "555");
	hooks.setup = setup;
	hooks.log = capture_log;
	hooks.log_context = &log;
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
	st_connection_serial_fake_set_lines(0);
	for (unsigned attempts = 0; attempts < 1000
	    && st_connection_is_connected(connection); ++attempts)
		(void)st_thread_sleep(&(struct timespec){0, 1000000L}, NULL);
	CHECK(!st_connection_is_connected(connection));
	CHECK(st_connection_close(connection) == 0);
	CHECK(!st_connection_serial_fake_open());
	CHECK(!st_connection_serial_fake_dtr());
	CHECK(atomic_load(&log.events) == (LOG_OPEN | LOG_CONFIG | LOG_INIT | LOG_OK
	    | LOG_DIAL | LOG_CONNECT | LOG_CARRIER | LOG_DROP | LOG_DTR | LOG_CLOSE));
	CHECK(!atomic_load(&log.invalid));

	st_connection_serial_fake_reset(SERIAL_MODEM_LINE_DCD);
	atomic_store(&log.events, 0);
	setup_state.phases = 0;
	setup_state.cancel_phase = ST_CONNECTION_SETUP_INITIALIZING_MODEM;
	connection = (st_connection_t *)(uintptr_t)1;
	CHECK(st_connection_connect_modem(&connection, &profile, &hooks, NULL)
	    == IO_ERROR_CANCELLED);
	CHECK(connection == (st_connection_t *)(uintptr_t)1);
	CHECK(!st_connection_serial_fake_open());
	CHECK(!st_connection_serial_fake_dtr());
	CHECK(atomic_load(&log.events) == (LOG_OPEN | LOG_CONFIG | LOG_CANCEL));
	CHECK(!atomic_load(&log.invalid));
	st_profile_dispose(&profile);
	st_settings_dispose(&st_settings);
	return 0;
}

static int
test_flow_control_case(int connection_type, int profile_flow,
	unsigned serial_flow)
{
	struct st_profile profile;
	struct serial_config config;
	st_connection_t *connection = NULL;
	int status;

	st_connection_serial_fake_reset(connection_type == ST_CONNECTION_MODEM
	    ? SERIAL_MODEM_LINE_DCD : 0);
	st_profile_init(&profile);
	profile.connection_type = connection_type;
	profile.flow_control = profile_flow;
	strcpy(profile.address, connection_type == ST_CONNECTION_MODEM
	    ? "555" : "fake-serial");
	status = connection_type == ST_CONNECTION_MODEM
	    ? st_connection_connect_modem(&connection, &profile, NULL, NULL)
	    : st_connection_connect_serial(&connection, &profile, NULL, NULL);
	CHECK(status == 0);
	config = st_connection_serial_fake_config();
	CHECK(config.flow_control == serial_flow);
	if (connection_type == ST_CONNECTION_SERIAL_3WIRE)
		CHECK(!st_connection_serial_fake_rts());
	CHECK(st_connection_close(connection) == 0);
	st_profile_dispose(&profile);
	return 0;
}

static int
test_profile_flow_mapping(void)
{
	static const struct {
		int profile;
		unsigned serial;
		unsigned three_wire;
	} cases[] = {
		{ST_PROFILE_FLOW_RTS_CTS, SERIAL_FLOW_CONTROL_RTS_CTS,
		    SERIAL_FLOW_CONTROL_NONE},
		{ST_PROFILE_FLOW_XON_XOFF, SERIAL_FLOW_CONTROL_XON_XOFF,
		    SERIAL_FLOW_CONTROL_XON_XOFF},
		{ST_PROFILE_FLOW_RTS_CTS_XON_XOFF,
		    SERIAL_FLOW_CONTROL_RTS_CTS | SERIAL_FLOW_CONTROL_XON_XOFF,
		    SERIAL_FLOW_CONTROL_XON_XOFF},
		{ST_PROFILE_FLOW_NONE, SERIAL_FLOW_CONTROL_NONE,
		    SERIAL_FLOW_CONTROL_NONE},
	};

	CHECK(configure_modem_settings() == 0);
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
		CHECK(test_flow_control_case(ST_CONNECTION_SERIAL,
		    cases[i].profile, cases[i].serial) == 0);
		CHECK(test_flow_control_case(ST_CONNECTION_SERIAL_3WIRE,
		    cases[i].profile, cases[i].three_wire) == 0);
		CHECK(test_flow_control_case(ST_CONNECTION_MODEM,
		    cases[i].profile, cases[i].serial) == 0);
	}
	st_settings_dispose(&st_settings);
	return 0;
}

static int
test_invalid_flow_control(void)
{
	static const int connection_types[] = {
		ST_CONNECTION_SERIAL,
		ST_CONNECTION_SERIAL_3WIRE,
		ST_CONNECTION_MODEM,
	};
	static const int invalid_flows[] = {
		ST_PROFILE_FLOW_RTS_CTS - 1,
		ST_PROFILE_FLOW_NONE + 1,
	};

	CHECK(configure_modem_settings() == 0);
	for (size_t i = 0;
	    i < sizeof(connection_types) / sizeof(connection_types[0]); ++i) {
		for (size_t j = 0;
		    j < sizeof(invalid_flows) / sizeof(invalid_flows[0]); ++j) {
			struct st_profile profile;
			struct st_connection_error error;
			struct setup_state setup_state = {0,
			    ST_CONNECTION_SETUP_NONE};
			struct st_connection_hooks hooks = {
				.setup = setup,
				.setup_context = &setup_state,
			};
			st_connection_t *connection =
			    (st_connection_t *)(uintptr_t)1;
			int status;

			st_connection_serial_fake_reset(0);
			st_profile_init(&profile);
			profile.connection_type = connection_types[i];
			profile.flow_control = invalid_flows[j];
			strcpy(profile.address,
			    connection_types[i] == ST_CONNECTION_MODEM
			    ? "555" : "fake-serial");
			if (connection_types[i] == ST_CONNECTION_MODEM)
				status = st_connection_connect_modem(&connection,
				    &profile, &hooks, &error);
			else
				status = st_connection_connect_serial(&connection,
				    &profile, &hooks, &error);
			CHECK(status == IO_ERROR_INVALID_ARGUMENT);
			CHECK(connection == (st_connection_t *)(uintptr_t)1);
			CHECK(error.code == IO_ERROR_INVALID_ARGUMENT);
			CHECK(error.phase == ST_CONNECTION_SETUP_NONE);
			CHECK(error.detail[0] != '\0');
			CHECK(setup_state.phases == 0);
			CHECK(st_connection_serial_fake_open_attempts() == 0);
			CHECK(!st_connection_serial_fake_open());
			st_profile_dispose(&profile);
		}
	}
	st_settings_dispose(&st_settings);
	return 0;
}

int
main(void)
{
	CHECK(test_serial() == 0);
	CHECK(test_serial_open_failure() == 0);
	CHECK(test_three_wire() == 0);
	CHECK(test_modem() == 0);
	CHECK(test_profile_flow_mapping() == 0);
	CHECK(test_invalid_flow_control() == 0);
	st_connection_serial_fake_dispose();
	return 0;
}
