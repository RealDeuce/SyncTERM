/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <pipewire/pipewire.h>
#include <pipewire/impl-factory.h>
#include <stdio.h>

/* Load the pinned client's matching modules, create its event loop and stop
 * it again. No server connection, stream or physical audio device is opened. */
int main(void)
{
	int result = 1;
	pw_init(NULL, NULL);
	struct pw_thread_loop *loop = pw_thread_loop_new("syncterm-dependency-test", NULL);
	if (loop == NULL) {
		pw_deinit();
		return result;
	}
	struct pw_context *context = pw_context_new(pw_thread_loop_get_loop(loop), NULL, 0);
	if (context != NULL) {
		if (pw_context_find_factory(context, "client-node") != NULL
		    && pw_thread_loop_start(loop) == 0) {
			pw_thread_loop_lock(loop);
			pw_context_destroy(context);
			pw_thread_loop_unlock(loop);
			pw_thread_loop_stop(loop);
			context = NULL;
			result = 0;
		}
		if (context != NULL)
			pw_context_destroy(context);
	}
	if (result != 0)
		fprintf(stderr, "Pinned PipeWire client modules or loop unavailable\n");
	pw_thread_loop_destroy(loop);
	pw_deinit();
	return result;
}
