/* Runtime Wayland entry points used by generated protocol stubs. */

/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#ifndef ST_CONSOLE_WAYLAND_LOADER_H_
#define ST_CONSOLE_WAYLAND_LOADER_H_

#include <wayland-client-core.h>

typedef struct wl_proxy *(*st_wl_proxy_marshal_flags_fn)(struct wl_proxy *,
	uint32_t, const struct wl_interface *, uint32_t, uint32_t, ...);
typedef int (*st_wl_proxy_add_listener_fn)(struct wl_proxy *,
	void (**)(void), void *);
typedef void (*st_wl_proxy_destroy_fn)(struct wl_proxy *);
typedef uint32_t (*st_wl_proxy_get_version_fn)(struct wl_proxy *);
typedef struct wl_display *(*st_wl_display_connect_fn)(const char *);
typedef void (*st_wl_display_disconnect_fn)(struct wl_display *);
typedef int (*st_wl_display_get_fd_fn)(struct wl_display *);
typedef int (*st_wl_display_dispatch_fn)(struct wl_display *);
typedef int (*st_wl_display_dispatch_pending_fn)(struct wl_display *);
typedef int (*st_wl_display_roundtrip_fn)(struct wl_display *);
typedef int (*st_wl_display_flush_fn)(struct wl_display *);

struct st_wayland_api {
	st_wl_proxy_marshal_flags_fn proxy_marshal_flags;
	st_wl_proxy_add_listener_fn proxy_add_listener;
	st_wl_proxy_destroy_fn proxy_destroy;
	st_wl_proxy_get_version_fn proxy_get_version;
	st_wl_display_connect_fn display_connect;
	st_wl_display_disconnect_fn display_disconnect;
	st_wl_display_get_fd_fn display_get_fd;
	st_wl_display_dispatch_fn display_dispatch;
	st_wl_display_dispatch_pending_fn display_dispatch_pending;
	st_wl_display_roundtrip_fn display_roundtrip;
	st_wl_display_flush_fn display_flush;
};

extern struct st_wayland_api st_wayland_api;
int st_wayland_loader_init(void);

#define wl_proxy_marshal_flags st_wayland_api.proxy_marshal_flags
#define wl_proxy_add_listener st_wayland_api.proxy_add_listener
#define wl_proxy_destroy st_wayland_api.proxy_destroy
#define wl_proxy_get_version st_wayland_api.proxy_get_version
#define wl_display_connect st_wayland_api.display_connect
#define wl_display_disconnect st_wayland_api.display_disconnect
#define wl_display_get_fd st_wayland_api.display_get_fd
#define wl_display_dispatch st_wayland_api.display_dispatch
#define wl_display_dispatch_pending st_wayland_api.display_dispatch_pending
#define wl_display_roundtrip st_wayland_api.display_roundtrip
#define wl_display_flush st_wayland_api.display_flush

#endif
