/* Functions for owning NULL-terminated string lists. */

/****************************************************************************
 * Copyright Rob Swindell - http://www.synchro.net/copyright.html           *
 * Copyright (c) 2026 Stephen Hurd                                          *
 *                                                                          *
 * This library is free software; you can redistribute it and/or            *
 * modify it under the terms of the GNU Lesser General Public License       *
 * as published by the Free Software Foundation; either version 2          *
 * of the License, or (at your option) any later version.                    *
 ****************************************************************************/

#ifndef SYNCTERM_STR_LIST_H_
#define SYNCTERM_STR_LIST_H_

#include <stdbool.h>
#include <stddef.h>

/*
 * A non-empty list owns its strings and is terminated by a NULL item. NULL is
 * a valid empty list; removing the last item may also leave allocated empty
 * storage for reuse. The array and its item slots must not be freed, replaced,
 * or resized by callers. An item's characters may be edited in place without
 * extending the string. Mutating operations may change the list pointer, so
 * callers must not retain copied handles across a mutation.
 */
typedef char **str_list_t;

/* Frees a list and its items, then stores NULL in the caller's handle. */
void str_list_free(str_list_t *list);
/* Frees a string returned by a take or join operation; NULL is permitted. */
void str_list_free_item(char *item);

/* All non-NULL lists must have been allocated by this library. */
size_t str_list_count(str_list_t list);
/* These operations leave the destination unchanged on failure. */
bool str_list_append(str_list_t *list, const char *string);
bool str_list_append_list(str_list_t *list, str_list_t source);
/*
 * Replaces result with a complete deep copy; NULL source is an empty list.
 * Failure leaves result unchanged.
 */
bool str_list_clone(str_list_t *result, str_list_t source);
/* Performs an exact, case-sensitive comparison. */
bool str_list_contains(str_list_t list, const char *string);

/* Frees the indexed string and compacts the list without reallocating. */
bool str_list_remove(str_list_t list, size_t index);

/* The caller owns and must free a taken string with str_list_free_item(). */
char *str_list_take(str_list_t list, size_t index);
char *str_list_take_last(str_list_t list);

/*
 * Replaces result with fields from string. Delimiters is a set of delimiter
 * characters and empty fields are skipped. Empty input produces NULL. The
 * result pointer, string, and delimiters must be non-NULL. Failure leaves
 * result unchanged.
 */
bool str_list_split(str_list_t *result, const char *string,
    const char *delimiters);

/*
 * Returns an exactly sized allocated string owned by the caller, which must
 * release it with str_list_free_item(). NULL list is empty; separator must be
 * non-NULL.
 */
char *str_list_join(str_list_t list, const char *separator);

#endif
