/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_VARIABLES_FILE_H_
#define ST_RIP3_VARIABLES_FILE_H_

#include "st_rip3_variables.h"

/* Optional client filesystem adapter, linked through st_rip3_storage. The
 * caller chooses a trusted UTF-8 client-owned path and coordinates its writers;
 * host text/variable names are never filenames. Parent setup, session versus
 * persistent table selection and host approval policy belong to the client.
 *
 * Private client format (not RIPscrip wire data or a legacy variable database):
 *   8 bytes: 'S','T','R','I','P','3','V',1 (format version)
 *   u32 LE: record count
 *   records: u8 name length, u8 value length, ASCII name bytes, value bytes
 * Writer records are sorted by canonical name; reader rejects duplicates,
 * malformed names/lengths, truncated records and trailing bytes. No C structs
 * are serialized. Values retain their explicit byte lengths, including NULs.
 *
 * Save atomically replaces the file after encoding/writing succeeds. Load
 * prepares a new owned table and publishes it only after validation and close.
 * Missing files return NOT_FOUND; failures preserve caller results/live data.
 * No legacy 1.54 file is accessed implicitly. */
int st_rip3_variables_save_file(const struct st_rip3_variables *variables,
	const char *path);
int st_rip3_variables_load_file(struct st_rip3_variables **result,
	const char *path);

#endif
