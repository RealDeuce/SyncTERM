/* Stateless byte digests and their native failure values. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "wren_host_internal.h"
#include "st_crypto.h"
#include "st_digest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct digest_error {
	int code;
	char message[160];
};

int
st_wren_digest_algorithm(WrenVM *vm, int slot)
{
	int64_t value;
	if (st_wren_number_i64(vm, slot, &value)) {
		switch (value) {
		case ST_DIGEST_SHA256: case ST_DIGEST_SHA384: case ST_DIGEST_SHA512:
		case ST_DIGEST_MD5: case ST_DIGEST_SHA1:
			return (int)value;
		}
	}
	st_wren_abort(vm, "expected a Digest algorithm constant");
	return 0;
}

void
st_wren_digest_push(WrenVM *vm, int algorithm, const void *bytes,
	size_t size, const char *operation)
{
	uint8_t result[64];
	size_t result_size = st_digest_size((enum st_digest_algorithm)algorithm);
	int status = st_digest((enum st_digest_algorithm)algorithm, bytes, size,
	    result, result_size);
	if (status == ST_CRYPTO_OK) {
		wrenSetSlotBytes(vm, 0, (const char *)result, result_size);
		return;
	}
	wrenEnsureSlots(vm, 2);
	wrenGetVariable(vm, "syncterm", "DigestError", 1);
	struct digest_error *error = wrenSetSlotNewForeign(vm, 0, 1, sizeof(*error));
	error->code = status;
	(void)snprintf(error->message, sizeof(error->message), "%s failed: %s",
	    operation, st_crypto_status_string(status));
}

static void
fn_digest_hash(WrenVM *vm)
{
	int algorithm = st_wren_digest_algorithm(vm, 1);
	if (algorithm == 0) return;
	if (wrenGetSlotType(vm, 2) != WREN_TYPE_STRING) {
		st_wren_abort(vm, "Digest.hash requires a byte String");
		return;
	}
	int size;
	const char *bytes = wrenGetSlotBytes(vm, 2, &size);
	if (size < 0) {
		st_wren_abort(vm, "Digest.hash input exceeds the Wren byte API length range");
		return;
	}
	st_wren_digest_push(vm, algorithm, bytes, (size_t)size, "Digest.hash");
}

static void
fn_digest_size(WrenVM *vm)
{
	int algorithm = st_wren_digest_algorithm(vm, 1);
	if (algorithm != 0)
		wrenSetSlotDouble(vm, 0, (double)st_digest_size((enum st_digest_algorithm)algorithm));
}

static void
allocate_digest_error(WrenVM *vm)
{
	st_wren_abort(vm, "DigestError values are created by the host");
}

static void
fn_digest_error_code(WrenVM *vm)
{
	struct digest_error *error = st_wren_foreign_require(vm, 0, "syncterm", "DigestError");
	if (error != NULL) wrenSetSlotDouble(vm, 0, error->code);
}

static void
fn_digest_error_message(WrenVM *vm)
{
	struct digest_error *error = st_wren_foreign_require(vm, 0, "syncterm", "DigestError");
	if (error != NULL) wrenSetSlotString(vm, 0, error->message);
}

static void
fn_digest_error_string(WrenVM *vm)
{
	struct digest_error *error = st_wren_foreign_require(vm, 0, "syncterm", "DigestError");
	char message[208];
	if (error == NULL) return;
	(void)snprintf(message, sizeof(message), "DigestError(%d): %s", error->code, error->message);
	wrenSetSlotString(vm, 0, message);
}

WrenForeignMethodFn
st_wren_digest_bind_method(const char *class_name, bool is_static,
	const char *signature)
{
	if (is_static && strcmp(class_name, "Digest") == 0) {
		if (strcmp(signature, "hash(_,_)") == 0) return fn_digest_hash;
		if (strcmp(signature, "size(_)") == 0) return fn_digest_size;
	}
	if (!is_static && strcmp(class_name, "DigestError") == 0) {
		if (strcmp(signature, "code") == 0) return fn_digest_error_code;
		if (strcmp(signature, "message") == 0) return fn_digest_error_message;
		if (strcmp(signature, "toString") == 0) return fn_digest_error_string;
	}
	return NULL;
}

WrenForeignClassMethods
st_wren_digest_bind_class(const char *class_name)
{
	WrenForeignClassMethods methods = {NULL, NULL};
	if (strcmp(class_name, "DigestError") == 0) methods.allocate = allocate_digest_error;
	return methods;
}
