/* Runtime Wayland client loader. */

/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#include "console_wayland_loader.h"
#include "console_config.h"

#include "dynamic_library.h"
#include "io_error.h"
#include "st_threads.h"

#include <stdbool.h>
#include <string.h>

struct st_wayland_api st_wayland_api;
#if !ST_DEP_WAYLAND_LINKED
static dynamic_library_t *library;
#endif
static st_once_flag_t load_once = ST_ONCE_FLAG_INIT;
static int load_status = IO_ERROR_NOT_FOUND;

#define LOAD(member) ((st_wayland_api.member = \
	(st_wl_ ## member ## _fn)dynamic_library_symbol(library, \
	"wl_" #member)) != NULL)

static void
load_client(void)
{
#if ST_DEP_WAYLAND_LINKED
#undef wl_proxy_marshal_flags
#undef wl_proxy_add_listener
#undef wl_proxy_destroy
#undef wl_proxy_get_version
#undef wl_display_connect
#undef wl_display_disconnect
#undef wl_display_get_fd
#undef wl_display_dispatch
#undef wl_display_dispatch_pending
#undef wl_display_roundtrip
#undef wl_display_flush
	st_wayland_api.proxy_marshal_flags = wl_proxy_marshal_flags;
	st_wayland_api.proxy_add_listener = wl_proxy_add_listener;
	st_wayland_api.proxy_destroy = wl_proxy_destroy;
	st_wayland_api.proxy_get_version = wl_proxy_get_version;
	st_wayland_api.display_connect = wl_display_connect;
	st_wayland_api.display_disconnect = wl_display_disconnect;
	st_wayland_api.display_get_fd = wl_display_get_fd;
	st_wayland_api.display_dispatch = wl_display_dispatch;
	st_wayland_api.display_dispatch_pending = wl_display_dispatch_pending;
	st_wayland_api.display_roundtrip = wl_display_roundtrip;
	st_wayland_api.display_flush = wl_display_flush;
	load_status = 0;
#else
	static const char *const names[] = {"wayland-client"};

	if (dynamic_library_open(&library, names, 1, 0,
	    DYNAMIC_LIBRARY_LOCAL) != 0)
		return;
	if (!LOAD(proxy_marshal_flags) || !LOAD(proxy_add_listener)
	    || !LOAD(proxy_destroy) || !LOAD(proxy_get_version)
	    || !LOAD(display_connect) || !LOAD(display_disconnect)
	    || !LOAD(display_get_fd) || !LOAD(display_dispatch)
	    || !LOAD(display_dispatch_pending) || !LOAD(display_roundtrip)
	    || !LOAD(display_flush)) {
		(void)dynamic_library_close(library);
		library = NULL;
		memset(&st_wayland_api, 0, sizeof(st_wayland_api));
		load_status = IO_ERROR_NOT_SUPPORTED;
		return;
	}
	load_status = 0;
#endif
}

int
st_wayland_loader_init(void)
{
	st_call_once(&load_once, load_client);
	return load_status;
}
