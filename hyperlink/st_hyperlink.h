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

typedef int (*st_hyperlink_mark_fn)(void *context, uint16_t identifier);
typedef int (*st_hyperlink_root_fn)(void *context,
	st_hyperlink_mark_fn mark, void *mark_context);

struct st_hyperlink {
	char *uri;
	char *parameter;
};

int st_hyperlink_registry_create(st_hyperlink_registry_t **result);
void st_hyperlink_registry_destroy(st_hyperlink_registry_t *registry);

/*
 * IDs remain stable while a registered root reports them. When all 65535 IDs
 * have been allocated, unreachable entries are collected and their IDs may be
 * reused. Zero is reserved for no hyperlink. URI must be nonempty; neither
 * span may contain an embedded NUL. A NULL parameter is valid only when its
 * length is zero.
 */
int st_hyperlink_registry_add(st_hyperlink_registry_t *registry,
	const char *uri, size_t uri_length, const char *parameter,
	size_t parameter_length, uint16_t *identifier);

/* Result values are unchanged on failure and owned by the caller on success. */
int st_hyperlink_registry_copy(st_hyperlink_registry_t *registry,
	uint16_t identifier, struct st_hyperlink *result);
int st_hyperlink_registry_contains(st_hyperlink_registry_t *registry,
	uint16_t identifier, bool *result);

/* Roots are borrowed until unregistered and must not call registry APIs. */
int st_hyperlink_registry_register_root(st_hyperlink_registry_t *registry,
	st_hyperlink_root_fn root, void *context);
void st_hyperlink_registry_unregister_root(st_hyperlink_registry_t *registry,
	st_hyperlink_root_fn root, void *context);
/* Primarily useful at lifecycle boundaries and for deterministic tests. */
int st_hyperlink_registry_collect(st_hyperlink_registry_t *registry);
void st_hyperlink_dispose(struct st_hyperlink *hyperlink);

#endif
