/* RIP !|1F response generation. */

/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#ifndef CTERM_RIP_FILE_QUERY_H_
#define CTERM_RIP_FILE_QUERY_H_

#include <stddef.h>

/* Response is allocated and may be NULL for an invalid command/name.
 * A valid query for an absent file returns the mode-specific "0" reply. */
int cterm_rip_file_query(char **response, size_t *response_size,
    const char *directory, const char *arguments);

#endif
