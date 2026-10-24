/* Application lifetime shared by desktop launchers and the libretro core. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_APPLICATION_H_
#define ST_APPLICATION_H_

#include "st_console.h"
#include "st_audio_runtime.h"
#include <stdbool.h>

struct st_application_host {
	bool console_mode;
	bool libretro;
	/* Optional frontend-owned storage root, including scripts/cache/keys. */
	const char *directory;
	/* Called on the application worker. ready publishes borrowed handles;
	 * release must revoke them before application cleanup can destroy either. */
	void (*ready)(void *context, st_console_display_t *display,
	    st_audio_runtime_t *audio);
	void (*release)(void *context);
	bool (*stopping)(void *context);
	void *context;
};

/* One active application per process; the caller owns argv through return. */
int st_application_run(int argc, char **argv,
	const struct st_application_host *host);

#endif
