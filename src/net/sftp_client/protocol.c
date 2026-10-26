/* Client-only extraction of Synchronet's SFTP packet and request engine.
 * Internal API: callbacks must not re-enter the client while its lock is held. */
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "st_threads.h"
#include "sftp.h"

/* Conversion by object bytes works on either byte order without platform tests. */
static uint32_t wire32(uint32_t value)
{
	const uint8_t *p = (const uint8_t *)&value;
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
	    | ((uint32_t)p[2] << 8) | p[3];
}
static uint64_t wire64(uint64_t value)
{
	const uint8_t *p = (const uint8_t *)&value;
	uint64_t result = 0;
	for (size_t i = 0; i < 8; ++i) result = (result << 8) | p[i];
	return result;
}
#include "sftp_str.c"
#include "sftp_pkt.c"
#include "sftp_attr.c"
#include "sftp_common.c"
#include "sftp_outcome.c"
#include "sftp_client.c"
