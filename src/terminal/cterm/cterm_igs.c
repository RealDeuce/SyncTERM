/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_igs.h"
#include "cterm.h"
#include "cterm_internal.h"
#include "cterm_render.h"
#include "io_error.h"
#include "st_time.h"

#include <stdlib.h>
#include <string.h>

static const uint8_t attention[] = {'G','#'};
static const struct st_graphics_dialect dialect = {
	.attention = attention, .attention_size = sizeof(attention),
	.chain_indicator = true, .chain = '>', .number_skip_crlf = true,
	.number_digits = 4,
};

static int
text(void *context, const uint8_t *bytes, size_t size)
{
	struct cterm_igs *igs = context;
	size_t used;
	int status = cterm_input_write(igs->terminal, bytes, size, &used, CTERM_INPUT_WIRE);
	if (status == 0 && used != size)
		status = IO_ERROR_IO;
	/* A semantic hook can reset CTerm while text is being presented. Leave
	 * the parser callback before resetting its state or consuming more wire. */
	if (status == 0 && igs->reset_pending)
		status = IO_ERROR_INTERRUPTED;
	return status;
}

static int
boundary(void *context)
{
	struct cterm_igs *igs = context;
	int status = cterm_text_boundary(igs->terminal);
	if (status == 0 && igs->terminal->status_sub != NULL)
		status = cterm_text_boundary(igs->terminal->status_sub);
	if (status == 0 && igs->reset_pending)
		status = IO_ERROR_INTERRUPTED;
	return status;
}

static int
command(void *context, const struct st_graphics_command_view *view)
{
	/* No command descriptors are registered in this integration foundation.
	 * Each subsequent command step adds its complete grammar and effects. */
	(void)context;
	(void)view;
	return IO_ERROR_NOT_SUPPORTED;
}

static void
unknown(void *context, uint8_t code)
{
	struct cterm_igs *igs = context;
	unsigned mask = 1u << (code % 8);
	if ((igs->warned[code / 8] & mask) == 0) {
		igs->warned[code / 8] |= (uint8_t)mask;
		cterm_log(igs->terminal, ST_CTERM_LOG_WARNING,
		    "Unsupported IGS command 0x%02x", (unsigned)code);
	}
}

static struct st_graphics_parser_sink
sink(struct cterm_igs *igs)
{
	return (struct st_graphics_parser_sink){igs, text, boundary, command, unknown};
}

int
cterm_igs_create(struct cterminal *terminal)
{
	uint32_t width, height, cw, ch;
	unsigned resolution;
	if ((terminal->screen.capabilities & ST_CONSOLE_CAP_PIXELS) == 0)
		return IO_ERROR_NOT_SUPPORTED;
	int status = st_console_pixel_size(terminal->screen.console, &width, &height);
	if (status == 0)
		status = st_console_cell_pixel_size(terminal->screen.console, &cw, &ch);
	if (status != 0)
		return status;
	if (width == 320 && height == 200) resolution = 0;
	else if (width == 640 && height == 200) resolution = 1;
	else if (width == 640 && height == 400) resolution = 2;
	else return IO_ERROR_INVALID_ARGUMENT;
	if (cw != 8 || ch != (resolution == 2 ? 16u : 8u)
	    || (uint32_t)terminal->width != width / cw)
		return IO_ERROR_INVALID_ARGUMENT;
	struct cterm_igs *igs = calloc(1, sizeof(*igs));
	if (igs == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	igs->terminal = terminal;
	igs->resolution = resolution;
	igs->width = width;
	igs->height = height;
	igs->now_ms = st_time_monotonic_ms;
	const struct st_graphics_parser_config config = {
		.dialect = &dialect, .max_arguments = 4096, .max_payload = 65536,
	};
	status = st_graphics_parser_create(&igs->parser, &config);
	if (status != 0) {
		free(igs);
		return status;
	}
	/* Establish the base console before any graphics operation. Otherwise
	 * the first later ordinary byte would start CTerm and clear that scene. */
	cterm_start(terminal);
	status = cterm_render_take_error(terminal);
	if (status != 0) {
		cterm_igs_destroy(igs);
		return status;
	}
	terminal->igs_engine = igs;
	return 0;
}

void
cterm_igs_destroy(struct cterm_igs *igs)
{
	if (igs == NULL)
		return;
	st_graphics_parser_destroy(igs->parser);
	free(igs);
}

void
cterm_igs_reset(struct cterm_igs *igs)
{
	if (igs == NULL)
		return;
	if (igs->feeding) {
		igs->reset_pending = true;
		return;
	}
	st_graphics_parser_cancel(igs->parser);
	igs->input_deadline = 0;
	igs->reset_pending = false;
	memset(igs->warned, 0, sizeof(igs->warned));
}

int
cterm_igs_feed(struct cterm_igs *igs, const void *bytes, size_t size, size_t *used)
{
	const uint8_t *input = bytes;
	int status = 0;
	*used = 0;
	if (igs->feeding)
		return IO_ERROR_BUSY;
	do {
		size_t consumed = 0;
		struct st_graphics_parser_sink callbacks = sink(igs);
		igs->feeding = true;
		status = st_graphics_parser_feed(igs->parser, &callbacks,
		    input == NULL ? NULL : input + *used, size - *used, &consumed);
		igs->feeding = false;
		*used += consumed;
		if (!igs->reset_pending)
			break;
		cterm_igs_reset(igs);
		if (status == IO_ERROR_INTERRUPTED)
			status = 0;
	} while (status == 0 && *used < size);
	if (status == 0 && st_graphics_parser_in_command(igs->parser)) {
		uint64_t now;
		status = igs->now_ms(&now);
		if (status == 0 && (*used != 0 || igs->input_deadline == 0))
			igs->input_deadline = now > UINT64_MAX - 5000 ? UINT64_MAX : now + 5000;
	}
	else
		igs->input_deadline = 0;
	return status;
}

int
cterm_igs_finish(struct cterm_igs *igs)
{
	if (igs->feeding)
		return 0;
	struct st_graphics_parser_sink callbacks = sink(igs);
	igs->feeding = true;
	int status = st_graphics_parser_finish(igs->parser, &callbacks);
	igs->feeding = false;
	if (status == IO_ERROR_PROTOCOL) {
		cterm_log(igs->terminal, ST_CTERM_LOG_WARNING, "Discarded incomplete IGS command");
		status = 0;
	}
	if (igs->reset_pending) {
		cterm_igs_reset(igs);
		if (status == IO_ERROR_INTERRUPTED)
			status = 0;
	}
	igs->input_deadline = 0;
	return status;
}

int
cterm_igs_poll(struct cterm_igs *igs)
{
	uint64_t now;
	if (igs->input_deadline == 0)
		return 0;
	int status = igs->now_ms(&now);
	if (status == 0 && now >= igs->input_deadline) {
		cterm_igs_reset(igs);
		cterm_log(igs->terminal, ST_CTERM_LOG_WARNING, "IGS input timed out");
	}
	return status;
}
