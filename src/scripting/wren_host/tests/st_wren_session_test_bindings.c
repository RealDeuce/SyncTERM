/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

/* Exercise the real binding at controlled native return boundaries.
 * The application contains no fault controls. */
#include "wren_host_internal.h"
#include "st_cterm.h"
#include "st_term.h"

bool st_wren_test_cterm_write_override;
int st_wren_test_cterm_write_status;
size_t st_wren_test_cterm_write_consumed;
size_t st_wren_test_cterm_write_size;
unsigned st_wren_test_cterm_write_calls;
bool st_wren_test_cterm_write_prefix;
int st_wren_test_cterm_write_last_status;
size_t st_wren_test_cterm_write_last_consumed;
bool st_wren_test_cterm_reset_override;
int st_wren_test_cterm_reset_status;
unsigned st_wren_test_cterm_reset_calls;
bool st_wren_test_cterm_key_override;
int st_wren_test_cterm_key_status;
bool st_wren_test_cterm_key_handled;
unsigned st_wren_test_cterm_key_calls;

static int
test_cterm_write(st_cterm_t *terminal, const void *bytes, size_t size,
	size_t *consumed)
{
	++st_wren_test_cterm_write_calls;
	st_wren_test_cterm_write_size = size;
	if (st_wren_test_cterm_write_override) {
		if (st_wren_test_cterm_write_prefix && st_wren_test_cterm_write_consumed <= size) {
			int status = st_cterm_write_utf8(terminal, bytes,
			    st_wren_test_cterm_write_consumed, consumed);
			if (status != 0) return status;
		}
		*consumed = st_wren_test_cterm_write_consumed;
		return st_wren_test_cterm_write_status;
	}
	int status = st_cterm_write_utf8(terminal, bytes, size, consumed);
	st_wren_test_cterm_write_last_status = status;
	st_wren_test_cterm_write_last_consumed = *consumed;
	return status;
}

static int
test_cterm_key(st_term_session_t *session, const struct st_console_event *event,
	bool *handled)
{
	++st_wren_test_cterm_key_calls;
	if (st_wren_test_cterm_key_override) {
		*handled = st_wren_test_cterm_key_handled;
		return st_wren_test_cterm_key_status;
	}
	return st_term_session_send_key(session, event, handled);
}

static int
test_cterm_reset(st_cterm_t *terminal)
{
	++st_wren_test_cterm_reset_calls;
	return st_wren_test_cterm_reset_override ? st_wren_test_cterm_reset_status
	    : st_cterm_reset_terminal(terminal);
}

#define st_cterm_reset_terminal test_cterm_reset
#define st_cterm_write_utf8 test_cterm_write
#define st_term_session_send_key test_cterm_key
#include "../wren_bind_session.c"
