/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_igs.h"
#include "cterm.h"
#include "cterm_internal.h"
#include "cterm_render.h"
#include "io_error.h"
#include "st_time.h"
#include "st_graphics_parser.h"
#include "st_igs_image.h"

#include <stdlib.h>
#include <string.h>

static const uint8_t attention[] = {'G','#'};
static const struct st_graphics_dialect dialect = {
	.attention = attention, .attention_size = sizeof(attention),
	.chain_indicator = true, .chain = '>', .number_skip_crlf = true,
	.number_digits = 4,
};
/* Fixed unscaled signatures share prefixes; counts remain per command. */
static const struct st_graphics_parameter unscaled_parameters[] = {
	{.kind = ST_GRAPHICS_NUMBER}, {.kind = ST_GRAPHICS_NUMBER},
	{.kind = ST_GRAPHICS_NUMBER}, {.kind = ST_GRAPHICS_NUMBER},
};
static const struct st_graphics_parameter point_parameters[] = {
	{.kind = ST_GRAPHICS_NUMBER, .axis = ST_GRAPHICS_X},
	{.kind = ST_GRAPHICS_NUMBER, .axis = ST_GRAPHICS_Y},
	{.kind = ST_GRAPHICS_NUMBER, .axis = ST_GRAPHICS_X},
	{.kind = ST_GRAPHICS_NUMBER, .axis = ST_GRAPHICS_Y},
};
/* Extended selectors reuse the same descriptor and parameter data as the
 * top-level byte grammar. X itself owns the unscaled selector field. */
static const struct st_graphics_command extended_commands[] = {
	{10, point_parameters, 2, NULL},
};

/* Dynamic arity inspects a private numeric-state copy. Real operands,
 * including a random selector/kind, advance once at execution. */
static int
preview_first(void *context, const struct st_graphics_command_view *view, int *value)
{
	if (view->count == 0)
		return IO_ERROR_PROTOCOL;
	struct cterm_igs preview = *(struct cterm_igs *)context;
	return cterm_igs_argument_value(&preview, &view->arguments[0], value);
}

static int
type_next(void *context, const struct st_graphics_command_view *view,
    struct st_graphics_parameter *result)
{
	*result = (struct st_graphics_parameter){.kind = ST_GRAPHICS_END};
	if (view->count >= 3)
		return 0;
	int kind;
	int status = preview_first(context, view, &kind);
	if (status == 0 && (kind == 1 || kind == 2))
		*result = unscaled_parameters[0];
	return status;
}

static int
box_next(void *context, const struct st_graphics_command_view *view,
    struct st_graphics_parameter *result)
{
	(void)context;
	*result = view->count == 4 ? unscaled_parameters[0]
	    : (struct st_graphics_parameter){.kind = ST_GRAPHICS_END};
	return 0;
}

static int
point_count(int count)
{
	return count < 2 ? 2 : count > CTERM_IGS_MAX_POINTS ? CTERM_IGS_MAX_POINTS : count;
}

static int
polyline_next(void *context, const struct st_graphics_command_view *view,
    struct st_graphics_parameter *result)
{
	*result = (struct st_graphics_parameter){.kind = ST_GRAPHICS_END};
	int count;
	int status = preview_first(context, view, &count);
	if (status == 0 && view->count-1 < (size_t)point_count(count)*2)
		*result = point_parameters[(view->count-1)%2];
	return status;
}

static int
extended_next(void *context, const struct st_graphics_command_view *view,
    struct st_graphics_parameter *result)
{
	*result = (struct st_graphics_parameter){.kind = ST_GRAPHICS_END};
	int selector;
	int status = preview_first(context, view, &selector);
	if (status != 0)
		return status;
	for (size_t i = 0; i < sizeof(extended_commands)/sizeof(*extended_commands); ++i) {
		const struct st_graphics_command *command = &extended_commands[i];
		if (selector == command->code) {
			if (view->count-1 < command->count)
				*result = command->parameters[view->count-1];
			break;
		}
	}
	return 0;
}

static const struct st_graphics_command commands[] = {
	{'I', unscaled_parameters, 1, NULL},
	{'R', unscaled_parameters, 2, NULL},
	{'g', unscaled_parameters, 1, NULL},
	{'M', unscaled_parameters, 1, NULL},
	{'C', unscaled_parameters, 2, NULL},
	{'S', unscaled_parameters, 4, NULL},
	{'H', unscaled_parameters, 1, NULL},
	{'A', unscaled_parameters, 3, NULL},
	{'T', unscaled_parameters, 1, type_next},
	{'P', point_parameters, 2, NULL},
	{'L', point_parameters, 4, NULL},
	{'D', point_parameters, 2, NULL},
	{'z', unscaled_parameters, 1, polyline_next},
	{'Z', point_parameters, 4, NULL},
	{'B', point_parameters, 4, box_next},
	{'U', point_parameters, 4, box_next},
	{'O', point_parameters, 3, NULL},
	{'X', unscaled_parameters, 1, extended_next},
};

static void
cancel_input(struct cterm_igs *igs)
{
	st_graphics_parser_cancel(igs->parser);
	igs->input_deadline = igs->wait_deadline = 0;
	igs->pending_resolution = -1;
	igs->pending_palette = 0;
	igs->reset_pending = false;
}

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
scale_number(const struct cterm_igs *igs, enum st_graphics_axis axis, unsigned value, int *result)
{
	if (value > 9999)
		return IO_ERROR_OUT_OF_RANGE;
	*result = (int)value;
	if (axis == ST_GRAPHICS_UNSCALED || igs->scaling_mode == 0 || value == 0)
		return 0;
	if (igs->scaling_resolution < 0 || igs->scaling_resolution > 2)
		return IO_ERROR_INVALID_ARGUMENT;
	if (igs->scaling_mode == 2) {
		if (axis == ST_GRAPHICS_Y && igs->scaling_resolution == 2)
			*result = (int)(value * 2);
	}
	else if (igs->scaling_mode == 1) {
		unsigned maximum = axis == ST_GRAPHICS_X
		    ? (igs->scaling_resolution == 0 ? 319 : 639)
		    : (igs->scaling_resolution == 2 ? 399 : 199);
		/* IG221.TXT's intended normalized canvas. Correct the original
		 * integer quotient before float conversion; no foreign FP model
		 * or accidental word overflow is required for these bounds. */
		*result = (int)((value * maximum + 4999) / 9999);
	}
	else
		return IO_ERROR_INVALID_ARGUMENT;
	return 0;
}

int
cterm_igs_argument_value(struct cterm_igs *igs, const struct st_graphics_argument *argument, int *result)
{
	if (igs == NULL || argument == NULL || result == NULL
	    || argument->kind != ST_GRAPHICS_NUMBER
	    || argument->axis < ST_GRAPHICS_UNSCALED || argument->axis > ST_GRAPHICS_Y)
		return IO_ERROR_INVALID_ARGUMENT;
	const struct st_graphics_operand *operand = &argument->operand;
	int value;
	if (operand->operation != ST_GRAPHICS_VALUE)
		return IO_ERROR_PROTOCOL;
	switch (operand->source) {
	case ST_GRAPHICS_CONSTANT:
	case ST_GRAPHICS_ARRAY: {
		int status = scale_number(igs, argument->axis, operand->value, &value);
		if (status != 0)
			return status;
		/* Scale the decimal index before lookup, never the loaded value. */
		if (operand->source == ST_GRAPHICS_ARRAY)
			value = igs->math[value > 127 ? 127 : value];
		break;
	}
	case ST_GRAPHICS_RANDOM_R:
	case ST_GRAPHICS_RANDOM_R2: {
		unsigned which = operand->source == ST_GRAPHICS_RANDOM_R ? 0 : 1;
		int lower = igs->random_range[which][0], upper = igs->random_range[which][1];
		if (lower > upper || upper < 0 || lower > 32767 || upper > 32767)
			return IO_ERROR_OUT_OF_RANGE;
		bool found = false;
		for (unsigned tries = 0; tries < 65536; ++tries) {
			unsigned raw = cterm_igs_random(igs);
			unsigned word = raw & 0xffff;
			value = upper < 256 ? (int)(raw & 255)
			    : word >= 32768 ? (int)word - 65536 : (int)word;
			if (value >= lower && value <= upper) { found = true; break; }
		}
		if (!found)
			return IO_ERROR_OUT_OF_RANGE;
		break;
	}
	default:
		return IO_ERROR_PROTOCOL;
	}
	*result = value;
	return 0;
}

static int
flow(struct cterm_igs *igs, bool on)
{
	if (igs->flow_mode == 0)
		return 0;
	/* The existing response transport queues output and owns readiness.
	 * No foreign BIOS polling or per-byte throughput delay belongs here. */
	for (int i = 0; i < (on ? igs->flow_on_count : igs->flow_off_count); ++i) {
		char byte = (char)(on ? igs->flow_on : igs->flow_off);
		cterm_respond(igs->terminal, &byte, 1);
		if (igs->reset_pending)
			return IO_ERROR_INTERRUPTED;
	}
	return 0;
}

static int
command(void *context, const struct st_graphics_command_view *view)
{
	struct cterm_igs *igs = context;
	int values[1+2*CTERM_IGS_MAX_POINTS];
	if (igs->reset_pending)
		return IO_ERROR_INTERRUPTED;
	if (view->count > sizeof(values) / sizeof(*values))
		return IO_ERROR_PROTOCOL;
	for (size_t i = 0; i < view->count; ++i) {
		int status = cterm_igs_argument_value(igs, &view->arguments[i], &values[i]);
		if (status != 0)
			return status;
	}
	switch (view->code) {
	case 'I': return cterm_igs_initialize(igs, values[0]);
	case 'R': return cterm_igs_resolution(igs, values[0], values[1]);
	case 'g': cterm_igs_set_scaling(igs, values[0]); return 0;
	case 'M':
		igs->attributes.writing_mode = values[0] >= 1 && values[0] <= 4 ? values[0] : 1;
		return 0;
	case 'C': cterm_igs_set_pen(igs, values[0], values[1]); return 0;
	case 'A': cterm_igs_set_fill(igs, values[0], values[1], values[2]); return 0;
	case 'P': return cterm_igs_marker(igs, values[0], values[1]);
	case 'L': return cterm_igs_line(igs, values[0], values[1], values[2], values[3]);
	case 'Z': return cterm_igs_rectangle(igs, values[0], values[1], values[2], values[3]);
	case 'O': return cterm_igs_circle(igs,values[0],values[1],values[2]);
	case 'B': return cterm_igs_box(igs, values[0], values[1], values[2], values[3], values[4] != 0);
	case 'U':
		if (values[4] < 0) return 0;
		if (values[4] > 0) return cterm_igs_box(igs,values[0],values[1],values[2],values[3],true);
		return cterm_igs_rounded_outline(igs,values[0],values[1],values[2],values[3]);
	case 'z': {
		int count = point_count(values[0]);
		if (view->count != 1+(size_t)count*2)
			return IO_ERROR_PROTOCOL;
		int status = igs->loop_mode == 1 ? 0 : flow(igs, false);
		if (status == 0)
			status = cterm_igs_polyline(igs, values+1, (size_t)count);
		if (status == 0 && igs->loop_mode != 1)
			status = flow(igs, true);
		return status;
	}
	case 'D': {
		int status = cterm_igs_line(igs, igs->draw_x, igs->draw_y, values[0], values[1]);
		if (status == 0) {
			/* Native D copies the original endpoint to both remembered
			 * pairs after drawing; arrow shortening stays raster-private. */
			igs->draw_start_x = igs->draw_x;
			igs->draw_start_y = igs->draw_y;
		}
		return status;
	}
	case 'X':
		if (values[0] == 10) {
			if (view->count != 3)
				return IO_ERROR_PROTOCOL;
			/* IG221.TXT's L/P-equivalent positioning uses X/Y scaling;
			 * correct the binary's off-by-one 0/1 reader classes. */
			igs->draw_x = values[1]; igs->draw_y = values[2];
			return 0;
		}
		if (values[0] >= 0 && values[0] <= 15) {
			cterm_log(igs->terminal, ST_CTERM_LOG_WARNING,
			    "Unsupported IGS X selector %d", values[0]);
			return IO_ERROR_NOT_SUPPORTED;
		}
		/* Out-of-range selectors have native one-argument no-op arity. */
		return 0;
	case 'T':
		if (values[0] < 1 || values[0] > 2)
			return 0;
		if (view->count != 3)
			return IO_ERROR_PROTOCOL;
		cterm_igs_set_type(igs, values[0], values[1], values[2]);
		return 0;
	case 'H': {
		if (values[0] < 0)
			return 0;
		bool hollow = values[0] != 0;
		igs->attributes.fill_interior = hollow ? 0 : 1;
		igs->attributes.writing_mode = hollow ? 2 : 1;
		igs->attributes.fill_perimeter = hollow;
		return 0;
	}
	case 'S': {
		static const int components[] = {0,142,285,428,571,714,857,1000};
		for (unsigned i = 1; i < 4; ++i)
			values[i] = values[i] >= 1 && values[i] <= 7 ? components[values[i]] : 0;
		return cterm_igs_palette_pen(igs, values[0] > 15 ? 0 : values[0], values[1], values[2], values[3]);
	}
	default: return IO_ERROR_NOT_SUPPORTED;
	}
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
	igs->sync_wait_hz = 60;
	igs->pending_resolution = -1;
	const struct st_graphics_parser_config config = {
		.dialect = &dialect, .max_arguments = 4096, .max_payload = 65536,
		.commands = commands, .command_count = sizeof(commands) / sizeof(*commands),
	};
	status = st_graphics_parser_create(&igs->parser, &config);
	if (status != 0) {
		free(igs);
		return status;
	}
	status = cterm_igs_state_init(igs);
	if (status != 0) {
		cterm_igs_destroy(igs);
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
	st_igs_image_destroy(igs->memory[0]);
	st_igs_image_destroy(igs->memory[1]);
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
	cancel_input(igs);
	memset(igs->warned, 0, sizeof(igs->warned));
	cterm_render_record_error(igs->terminal, cterm_igs_state_reset(igs));
}

bool
cterm_igs_paused(const struct cterm_igs *igs)
{
	return igs != NULL && igs->pending_resolution >= 0;
}

int
cterm_igs_feed(struct cterm_igs *igs, const void *bytes, size_t size, size_t *used)
{
	const uint8_t *input = bytes;
	int status = 0;
	*used = 0;
	if (igs->feeding)
		return IO_ERROR_BUSY;
	if (cterm_igs_paused(igs))
		return 0;
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
	if (status == IO_ERROR_IN_PROGRESS)
		status = 0;
	if (status == 0 && !cterm_igs_paused(igs) && st_graphics_parser_in_command(igs->parser)) {
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
	if (cterm_igs_paused(igs)) {
		cancel_input(igs);
		return 0;
	}
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
	if (cterm_igs_paused(igs)) {
		int status = igs->now_ms(&now);
		if (status != 0 || now < igs->wait_deadline)
			return status;
		igs->feeding = true;
		status = cterm_igs_set_resolution(igs->terminal, (unsigned)igs->pending_resolution);
		igs->feeding = false;
		if (igs->reset_pending) {
			cterm_igs_reset(igs);
			return status;
		}
		igs->pending_resolution = -1;
		igs->wait_deadline = 0;
		int palette = igs->pending_palette;
		igs->pending_palette = 0;
		if (status == 0)
			status = cterm_igs_resolution_complete(igs, palette);
		if (status != 0) {
			cancel_input(igs);
			return status;
		}
		return st_graphics_parser_resume(igs->parser);
	}
	if (igs->input_deadline == 0)
		return 0;
	int status = igs->now_ms(&now);
	if (status == 0 && now >= igs->input_deadline) {
		cancel_input(igs);
		cterm_log(igs->terminal, ST_CTERM_LOG_WARNING, "IGS input timed out");
	}
	return status;
}
