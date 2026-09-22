/* Wren access to the application hyperlink registry. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "wren_host_internal.h"

#include "st_hyperlink.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static int
identifier_value(WrenVM *vm, int slot, uint16_t *identifier)
{
	double value;

	if (wrenGetSlotType(vm, slot) != WREN_TYPE_NUM) {
		st_wren_abort(vm, "Hyperlinks: identifier must be a number");
		return -1;
	}
	value = wrenGetSlotDouble(vm, slot);
	if (!isfinite(value) || value < 1 || value > UINT16_MAX
	    || floor(value) != value) {
		*identifier = 0;
		return 0;
	}
	*identifier = (uint16_t)value;
	return 1;
}

static void
fn_hyperlinks_subscript(WrenVM *vm)
{
	struct st_hyperlink hyperlink = {0};
	uint16_t identifier;
	int valid;

	valid = identifier_value(vm, 1, &identifier);
	if (valid < 0)
		return;
	if (valid == 0 || st_hyperlink_registry_copy(
	    st_wren_host_from_vm(vm)->config.hyperlinks, identifier,
	    &hyperlink) != 0) {
		wrenSetSlotNull(vm, 0);
		return;
	}
	wrenSetSlotString(vm, 0, hyperlink.uri);
	st_hyperlink_dispose(&hyperlink);
}

static void
fn_hyperlinks_contains(WrenVM *vm)
{
	uint16_t identifier;
	bool contains = false;
	int valid;

	valid = identifier_value(vm, 1, &identifier);
	if (valid < 0)
		return;
	if (valid > 0)
		(void)st_hyperlink_registry_contains(
		    st_wren_host_from_vm(vm)->config.hyperlinks, identifier,
		    &contains);
	wrenSetSlotBool(vm, 0, contains);
}

static void
fn_hyperlinks_add(WrenVM *vm)
{
	const char *uri;
	const char *parameter = NULL;
	int uri_length;
	int parameter_length = 0;
	uint16_t identifier = 0;

	if (wrenGetSlotType(vm, 1) != WREN_TYPE_STRING) {
		st_wren_abort(vm, "Hyperlinks.add: URI must be a String");
		return;
	}
	if (wrenGetSlotType(vm, 2) != WREN_TYPE_NULL
	    && wrenGetSlotType(vm, 2) != WREN_TYPE_STRING) {
		st_wren_abort(vm,
		    "Hyperlinks.add: parameter must be a String or null");
		return;
	}
	uri = wrenGetSlotBytes(vm, 1, &uri_length);
	if (wrenGetSlotType(vm, 2) == WREN_TYPE_STRING)
		parameter = wrenGetSlotBytes(vm, 2, &parameter_length);
	(void)st_hyperlink_registry_add(
	    st_wren_host_from_vm(vm)->config.hyperlinks,
	    uri, (size_t)uri_length, parameter, (size_t)parameter_length,
	    &identifier);
	wrenSetSlotDouble(vm, 0, identifier);
}

static void
fn_hyperlinks_params(WrenVM *vm)
{
	struct st_hyperlink hyperlink = {0};
	uint16_t identifier;
	int valid;

	valid = identifier_value(vm, 1, &identifier);
	if (valid < 0)
		return;
	if (valid == 0 || st_hyperlink_registry_copy(
	    st_wren_host_from_vm(vm)->config.hyperlinks, identifier,
	    &hyperlink) != 0) {
		wrenSetSlotNull(vm, 0);
		return;
	}
	if (hyperlink.parameter == NULL)
		wrenSetSlotNull(vm, 0);
	else
		wrenSetSlotString(vm, 0, hyperlink.parameter);
	st_hyperlink_dispose(&hyperlink);
}

struct hyperlink_binding {
	const char *signature;
	WrenForeignMethodFn method;
};

WrenForeignMethodFn
st_wren_hyperlink_bind_method(const char *class_name, bool is_static,
	const char *signature)
{
	static const struct hyperlink_binding bindings[] = {
		{"[_]", fn_hyperlinks_subscript},
		{"containsKey(_)", fn_hyperlinks_contains},
		{"add(_,_)", fn_hyperlinks_add},
		{"params(_)", fn_hyperlinks_params},
	};

	if (!is_static || strcmp(class_name, "Hyperlinks") != 0)
		return NULL;
	for (size_t i = 0; i < sizeof(bindings) / sizeof(bindings[0]); ++i)
		if (strcmp(bindings[i].signature, signature) == 0)
			return bindings[i].method;
	return NULL;
}
