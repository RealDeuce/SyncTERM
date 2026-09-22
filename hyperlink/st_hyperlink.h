/* Application-lifetime hyperlink registry. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_HYPERLINK_H_
#define ST_HYPERLINK_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct st_hyperlink_registry st_hyperlink_registry_t;

struct st_hyperlink {
	char *uri;
	char *parameter;
};

int st_hyperlink_registry_create(st_hyperlink_registry_t **result);
void st_hyperlink_registry_destroy(st_hyperlink_registry_t *registry);

/*
 * IDs are stable until registry destruction and are never reused. Zero is
 * reserved for no hyperlink. URI must be nonempty; neither span may contain
 * an embedded NUL. A NULL parameter is valid only when its length is zero.
 */
int st_hyperlink_registry_add(st_hyperlink_registry_t *registry,
	const char *uri, size_t uri_length, const char *parameter,
	size_t parameter_length, uint16_t *identifier);

/* Result values are unchanged on failure and owned by the caller on success. */
int st_hyperlink_registry_copy(st_hyperlink_registry_t *registry,
	uint16_t identifier, struct st_hyperlink *result);
int st_hyperlink_registry_contains(st_hyperlink_registry_t *registry,
	uint16_t identifier, bool *result);
void st_hyperlink_dispose(struct st_hyperlink *hyperlink);

#endif
