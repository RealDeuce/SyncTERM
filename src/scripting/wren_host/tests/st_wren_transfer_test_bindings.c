/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

/* Compile the real binding with controlled startup failures. The application
 * links the ordinary implementation without these test controls. */
#include "wren_host_internal.h"
#include "st_transfer.h"
#include "st_term.h"

int st_wren_test_transfer_create_status;
int st_wren_test_transfer_claim_status;
int st_wren_test_transfer_launch_status;
unsigned st_wren_test_transfer_create_calls;

static int test_transfer_create(st_transfer_t **result, const struct st_transfer_config *config)
{
	++st_wren_test_transfer_create_calls;
	return st_wren_test_transfer_create_status != 0 ? st_wren_test_transfer_create_status
	    : st_transfer_create(result, config);
}
static int test_transfer_claim(st_term_session_t *session, st_transfer_t *transfer)
{
	return st_wren_test_transfer_claim_status != 0 ? st_wren_test_transfer_claim_status
	    : st_term_session_begin_transfer(session, transfer);
}
static int test_transfer_launch(st_transfer_t *transfer)
{
	return st_wren_test_transfer_launch_status != 0 ? st_wren_test_transfer_launch_status
	    : st_transfer_launch(transfer);
}

#define st_transfer_create test_transfer_create
#define st_term_session_begin_transfer test_transfer_claim
#define st_transfer_launch test_transfer_launch
#include "../wren_bind_xfer.c"
