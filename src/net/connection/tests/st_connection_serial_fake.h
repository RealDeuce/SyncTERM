/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_CONNECTION_SERIAL_FAKE_H_
#define ST_CONNECTION_SERIAL_FAKE_H_

#include "serial.h"

#include <stdbool.h>
#include <stddef.h>

void st_connection_serial_fake_reset(unsigned lines);
void st_connection_serial_fake_dispose(void);
void st_connection_serial_fake_input(const void *data, size_t size);
void st_connection_serial_fake_set_lines(unsigned lines);
size_t st_connection_serial_fake_output(void *data, size_t capacity);
struct serial_config st_connection_serial_fake_config(void);
bool st_connection_serial_fake_open(void);
size_t st_connection_serial_fake_open_attempts(void);
bool st_connection_serial_fake_dtr(void);
bool st_connection_serial_fake_rts(void);

#endif
