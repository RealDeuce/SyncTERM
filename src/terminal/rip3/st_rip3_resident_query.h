/* Owned, unexpanded query definitions; storage never implies execution. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_RESIDENT_QUERY_H_
#define ST_RIP3_RESIDENT_QUERY_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct st_rip3_resident_query {uint8_t *bytes;size_t length;};
/* Zero-initialize before use. C-prefix, 1023 protocol-byte limit, exact
 * case-insensitive $OFF$ removal. Converted single-byte input counts original
 * units. Empty/oversized/allocation failures retain the old definition. */
int st_rip3_resident_query_set(struct st_rip3_resident_query *,const uint8_t *,size_t,bool converted);
/* Independently owned copy of accepted source, including absence. */
int st_rip3_resident_query_copy(struct st_rip3_resident_query *,const struct st_rip3_resident_query *);
void st_rip3_resident_query_clear(struct st_rip3_resident_query *);
#endif
