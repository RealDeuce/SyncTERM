/* TCP diagnostics shared by connection providers. */
/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_CONNECTION_TCP_H_
#define ST_CONNECTION_TCP_H_

#include "tcp_client.h"

void st_connection_tcp_diagnostic(void *context, enum tcp_client_event event,
	enum tcp_client_address_family family, int status);

#endif
