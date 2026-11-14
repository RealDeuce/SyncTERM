/* Per-terminal RIP parser and graphics command dispatch. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "cterm_rip_engine.h"

#include "cterm_rip_asset.h"
#include "io_error.h"
#include "st_codepage.h"
#include "st_utf8.h"
#include "string_compat.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void
free_mouse_fields(struct cterm_rip_mouse_field **head)
{
	struct cterm_rip_mouse_field *field;

	while ((field = *head) != NULL) {
		*head = field->next;
		free(field->icon);
		free(field->label);
		free(field->command);
		free(field->template_command);
		free(field);
	}
}

static void
draw_tab_highlight(struct cterm_rip_engine *engine,
    const struct cterm_rip_mouse_field *field)
{
	struct cterm_rip_raster *raster = &engine->raster;
	bool old_xor;
	uint16_t old_pattern;
	int old_width, old_colour;
	int x1, y1, x2, y2;

	if (field == NULL)
		return;
	old_xor = raster->xor_mode;
	old_pattern = raster->line_pattern;
	old_width = raster->line_width;
	old_colour = raster->colour;
	raster->xor_mode = true;
	raster->line_pattern = 0xf8f8;
	raster->line_width = 3;
	raster->colour = 5;
	x1 = field->x1 - 1;
	y1 = field->y1 - 1;
	x2 = field->x2;
	y2 = field->y2;
	cterm_rip_raster_line(raster, x1, y1, x2, y1);
	cterm_rip_raster_line(raster, x2, y1, x2, y2);
	cterm_rip_raster_line(raster, x2, y2, x1, y2);
	cterm_rip_raster_line(raster, x1, y2, x1, y1);
	raster->xor_mode = old_xor;
	raster->line_pattern = old_pattern;
	raster->line_width = old_width;
	raster->colour = old_colour;
}

void
cterm_rip_engine_clear_tab(struct cterm_rip_engine *engine)
{
	if (engine == NULL || engine->tab_highlight == NULL)
		return;
	draw_tab_highlight(engine, engine->tab_highlight);
	engine->tab_highlight = NULL;
}

void
cterm_rip_engine_next_tab(struct cterm_rip_engine *engine, bool forward)
{
	struct cterm_rip_mouse_field *first, *last = NULL, *previous = NULL;
	struct cterm_rip_mouse_field *next = NULL;
	struct cterm_rip_mouse_field *current;
	bool found = false;

	if (engine == NULL)
		return;
	current = engine->tab_highlight;
	first = engine->mouse_fields;
	for (struct cterm_rip_mouse_field *field = first;
	    field != NULL; field = field->next) {
		if (found && next == NULL)
			next = field;
		if (field == current)
			found = true;
		last = field;
	}
	if (found && !forward) {
		for (struct cterm_rip_mouse_field *field = first;
		    field != current; field = field->next)
			previous = field;
		next = previous == NULL ? last : previous;
	}
	if (!found || next == NULL)
		next = forward ? first : last;
	cterm_rip_engine_clear_tab(engine);
	engine->tab_highlight = next;
	draw_tab_highlight(engine, next);
}

void
cterm_rip_engine_clear_mouse(struct cterm_rip_engine *engine)
{
	if (engine == NULL)
		return;
	/* Legacy MKILL drops the selection without erasing its pixels. */
	engine->tab_highlight = NULL;
	engine->pressed_field = NULL;
	engine->mouse_generation++;
	free_mouse_fields(&engine->mouse_fields);
}

static int
clone_mouse_fields(struct cterm_rip_mouse_field **result,
	const struct cterm_rip_mouse_field *source)
{
	struct cterm_rip_mouse_field *head = NULL;
	struct cterm_rip_mouse_field **tail = &head;

	for (const struct cterm_rip_mouse_field *src = source;
	    src != NULL; src = src->next) {
		struct cterm_rip_mouse_field *copy = calloc(1, sizeof(*copy));

		if (copy == NULL)
			goto out_of_memory;
		*copy = *src;
		copy->next = NULL;
		copy->icon = src->icon == NULL ? NULL : strdup(src->icon);
		copy->label = src->label == NULL ? NULL : strdup(src->label);
		copy->command = src->command == NULL
		    ? NULL : strdup(src->command);
		copy->template_command = src->template_command == NULL
		    ? NULL : strdup(src->template_command);
		copy->drawing.icon = copy->icon;
		copy->drawing.label = copy->label;
		if ((src->icon != NULL && copy->icon == NULL)
		    || (src->label != NULL && copy->label == NULL)
		    || (src->command != NULL && copy->command == NULL)
		    || (src->template_command != NULL && copy->template_command == NULL)) {
			free(copy->icon);
			free(copy->label);
			free(copy->command);
			free(copy->template_command);
			free(copy);
			goto out_of_memory;
		}
		*tail = copy;
		tail = &copy->next;
	}
	*result = head;
	return 0;
out_of_memory:
	free_mouse_fields(&head);
	return IO_ERROR_OUT_OF_MEMORY;
}

int
cterm_rip_engine_save_mouse(struct cterm_rip_engine *engine)
{
	struct cterm_rip_mouse_field *copy = NULL;
	int status;

	if (engine == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = clone_mouse_fields(&copy, engine->mouse_fields);
	if (status != 0)
		return status;
	free_mouse_fields(&engine->saved_mouse_fields);
	engine->saved_mouse_fields = copy;
	return 0;
}

int
cterm_rip_engine_restore_mouse(struct cterm_rip_engine *engine)
{
	struct cterm_rip_mouse_field *copy = NULL;
	int status;

	if (engine == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = clone_mouse_fields(&copy, engine->saved_mouse_fields);
	if (status != 0)
		return status;
	cterm_rip_engine_clear_mouse(engine);
	engine->mouse_fields = copy;
	return 0;
}

int
cterm_rip_engine_add_button_mouse(struct cterm_rip_engine *engine,
    const struct cterm_rip_button_draw *drawing, bool selected,
    const char *command)
{
	struct cterm_rip_mouse_field *field;

	if (engine == NULL || drawing == NULL || command == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	field = calloc(1, sizeof(*field));
	if (field == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	field->command = strdup(command);
	if (drawing->icon != NULL)
		field->icon = strdup(drawing->icon);
	if (drawing->label != NULL)
		field->label = strdup(drawing->label);
	if (field->command == NULL
	    || (drawing->icon != NULL && field->icon == NULL)
	    || (drawing->label != NULL && field->label == NULL)) {
		free(field->icon);
		free(field->label);
		free(field->command);
		free(field);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	field->drawing = *drawing;
	field->drawing.icon = field->icon;
	field->drawing.label = field->label;
	field->is_button = true;
	field->selected = selected;
	field->x1 = drawing->x1;
	field->y1 = drawing->y1;
	field->x2 = drawing->x2;
	field->y2 = drawing->y2;
	field->invert = (drawing->style.flags & 2) != 0;
	field->reset_after = (drawing->style.flags & 4) != 0;
	field->next = engine->mouse_fields;
	engine->mouse_fields = field;
	return 0;
}

int
cterm_rip_engine_add_mouse(struct cterm_rip_engine *engine,
    int x1, int y1, int x2, int y2, bool invert,
    bool reset_after, const char *command)
{
	struct cterm_rip_mouse_field *field;

	if (engine == NULL || command == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	field = calloc(1, sizeof(*field));
	if (field == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	field->command = strdup(command);
	if (field->command == NULL) {
		free(field);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	field->x1 = x1;
	field->y1 = y1;
	field->x2 = x2;
	field->y2 = y2;
	field->invert = invert;
	field->reset_after = reset_after;
	field->next = engine->mouse_fields;
	engine->mouse_fields = field;
	return 0;
}

static int
forward_text(void *context, const uint8_t *bytes, size_t size)
{
	struct cterm_rip_engine *engine = context;

	/* Legacy local-scene playback discards plain terminal text. */
	if (engine->scene_depth != 0)
		return 0;
	return engine->host.passthrough(engine->host.context, bytes, size);
}

static int
forward_boundary(void *context)
{
	struct cterm_rip_engine *engine = context;
	return engine->host.boundary == NULL ? 0
	    : engine->host.boundary(engine->host.context);
}

static int
forward_control(void *context, const uint8_t *bytes, size_t size)
{
	struct cterm_rip_engine *engine = context;

	if (engine->host.control == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	return engine->host.control(engine->host.context, bytes, size);
}

static bool
at_column(void *context)
{
	struct cterm_rip_engine *engine = context;

	return engine->host.at_carriage_return_column != NULL
	    && engine->host.at_carriage_return_column(engine->host.context);
}

static bool
text_disabled(void *context)
{
	struct cterm_rip_engine *engine = context;

	return engine->host.text_disabled != NULL
	    && engine->host.text_disabled(engine->host.context);
}

static bool
music_introducer(void *context, uint8_t final)
{
	struct cterm_rip_engine *engine = context;

	return engine->host.music_introducer != NULL
	    && engine->host.music_introducer(engine->host.context, final);
}

static int
dispatch_command(void *context, int level, int sublevel, uint8_t code,
    const char *arguments)
{
	struct cterm_rip_engine *engine = context;
	bool handled;
	int status;

	engine->commands_seen++;
	if (level == 1 && sublevel == 0 && code == 'R') {
		if (strlen(arguments) < 9)
			return 0;
		return cterm_rip_engine_play_scene(engine, arguments + 8);
	}
	if (level == 0 && sublevel == 0 && code == '$'
	    && arguments[0] == '>') {
		const char *end = strchr(arguments + 1, '$');
		char *filename;

		if (end == NULL || end == arguments + 1)
			return 0;
		filename = strndup(arguments + 1,
		    (size_t)(end - arguments - 1));
		if (filename == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		status = cterm_rip_engine_play_scene(engine, filename);
		free(filename);
		return status;
	}
	status = cterm_rip_raster_command(&engine->raster, level, sublevel,
	    code, arguments, &handled);
	if (status != 0 || handled)
		return status;
	if (engine->host.command == NULL)
		return 0;
	status = engine->host.command(engine->host.context, level, sublevel,
	    code, arguments);
	/* v1.10a logs unhandled RIP commands and continues parsing. */
	return status == IO_ERROR_NOT_SUPPORTED ? 0 : status;
}

static void
forward_warning(void *context, const char *message)
{
	struct cterm_rip_engine *engine = context;
	/* A failed raster operation must not poison the following command. */
	engine->raster.error = 0;
	if (engine->host.warning != NULL)
		engine->host.warning(engine->host.context, message);
}

static struct cterm_rip_parser_sink
engine_sink(struct cterm_rip_engine *engine)
{
	return (struct cterm_rip_parser_sink){
		.context = engine,
		.boundary = forward_boundary,
		.passthrough = forward_text,
		.command = dispatch_command,
		.control = forward_control,
		.at_carriage_return_column = at_column,
		.text_disabled = text_disabled,
		.music_introducer = music_introducer,
		.warning = forward_warning,
	};
}

/* Received and cached scenes contain codepage bytes, unlike the live
 * terminal wire path which has already been translated to UTF-8. */
static int
feed_scene_bytes(struct cterm_rip_engine *engine, const uint8_t *bytes, size_t size)
{
	char unicode[4096 * 4];
	while (size != 0) {
		size_t count = size > 4096 ? 4096 : size, consumed;
		const uint8_t *input = bytes;
		size_t input_size = count;

		if (engine->raster.unicode_text) {
			input_size = 0;
			for (size_t i = 0; i < count; ++i) {
				uint32_t scalar = bytes[i] < 0x80 ? bytes[i]
				    : st_codepage_to_unicode(engine->raster.text_codepage, bytes[i], false);
				ptrdiff_t written = st_utf8_encode_one(unicode + input_size,
				    sizeof(unicode) - input_size, scalar);
				if (written < 0) return (int)written;
				input_size += (size_t)written;
			}
			input = (const uint8_t *)unicode;
		}
		int status = cterm_rip_engine_feed(engine, input, input_size, &consumed);
		if (status != 0) return status;
		if (consumed != input_size) return IO_ERROR_IO;
		bytes += count; size -= count;
	}
	return 0;
}

int
cterm_rip_engine_receive_begin(struct cterm_rip_engine *engine)
{
	if (engine->received_scene_parser != NULL || engine->scene_depth != 0)
		return IO_ERROR_BUSY;
	engine->received_scene_parser = malloc(sizeof(engine->parser));
	if (engine->received_scene_parser == NULL) return IO_ERROR_OUT_OF_MEMORY;
	*engine->received_scene_parser = engine->parser;
	cterm_rip_parser_init(&engine->parser, engine->parser.enabled);
	engine->parser.utf8_input = engine->raster.unicode_text;
	++engine->scene_depth;
	return 0;
}

int
cterm_rip_engine_receive_data(struct cterm_rip_engine *engine,
    const uint8_t *bytes, size_t size)
{
	if (engine->received_scene_parser == NULL) return IO_ERROR_INVALID_ARGUMENT;
	return feed_scene_bytes(engine, bytes, size);
}

int
cterm_rip_engine_receive_end(struct cterm_rip_engine *engine, bool success)
{
	int status = 0;
	if (engine->received_scene_parser == NULL) return 0;
	if (success) {
		struct cterm_rip_parser_sink sink = engine_sink(engine);
		status = cterm_rip_parser_flush(&engine->parser, &sink);
	}
	cterm_rip_parser_destroy(&engine->parser);
	engine->parser = *engine->received_scene_parser;
	free(engine->received_scene_parser); engine->received_scene_parser = NULL;
	--engine->scene_depth;
	return status;
}

int
cterm_rip_engine_play_scene(struct cterm_rip_engine *engine, const char *filename)
{
	struct cterm_rip_parser saved;
	struct cterm_rip_parser_sink sink;
	uint8_t bytes[4096];
	FILE *stream;
	size_t size;
	int status = 0;

	if (filename[0] == '\0' || engine->scene_depth >= 8)
		return 0;
	stream = cterm_rip_asset_open_read(engine->raster.rip_directory,
	    filename, false);
	if (stream == NULL)
		return 0;
	saved = engine->parser;
	cterm_rip_parser_init(&engine->parser, saved.enabled);
	engine->parser.utf8_input = engine->raster.unicode_text;
	engine->scene_depth++;
	while ((size = fread(bytes, 1, sizeof(bytes), stream)) != 0) {
		status = feed_scene_bytes(engine, bytes, size);
		if (status != 0)
			break;
	}
	if (status == 0) {
		sink = engine_sink(engine);
		status = cterm_rip_parser_flush(&engine->parser, &sink);
	}
	cterm_rip_parser_destroy(&engine->parser);
	engine->parser = saved;
	engine->scene_depth--;
	fclose(stream);
	return status;
}

int
cterm_rip_engine_init(struct cterm_rip_engine *engine,
    st_console_t *console, const struct cterm_rip_parser_sink *host)
{
	int status;

	if (engine == NULL || console == NULL || host == NULL
	    || host->passthrough == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	memset(engine, 0, sizeof(*engine));
	status = cterm_rip_raster_init(&engine->raster, console);
	if (status != 0)
		return status;
	engine->host = *host;
	/* Legacy init leaves both disabled; RIP_RESET_WINDOWS enables them. */
	cterm_rip_parser_init(&engine->parser, true);
	return 0;
}

void
cterm_rip_engine_destroy(struct cterm_rip_engine *engine)
{
	struct cterm_rip_user_var *variable;

	if (engine == NULL)
		return;
	(void)cterm_rip_engine_receive_end(engine, false);
	cterm_rip_parser_destroy(&engine->parser);
	cterm_rip_engine_clear_mouse(engine);
	free_mouse_fields(&engine->saved_mouse_fields);
	free(engine->graphics_click);
	free(engine->text_click);
	for (size_t i = 0; i < 36; i++)
		free(engine->templates[i]);
	while ((variable = engine->user_vars) != NULL) {
		engine->user_vars = variable->next;
		free(variable->value);
		free(variable);
	}
	cterm_rip_raster_destroy(&engine->raster);
	memset(engine, 0, sizeof(*engine));
}

int
cterm_rip_engine_feed(struct cterm_rip_engine *engine,
    const uint8_t *bytes, size_t size, size_t *consumed)
{
	struct cterm_rip_parser_sink sink;

	if (engine == NULL || engine->host.passthrough == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	sink = engine_sink(engine);
	engine->parser.ansi_interleaving = engine->raster.version == 1;
	return cterm_rip_parser_feed(&engine->parser, &sink, bytes,
	    size, consumed);
}

int
cterm_rip_engine_flush(struct cterm_rip_engine *engine)
{
	struct cterm_rip_parser_sink sink;

	if (engine == NULL || engine->host.passthrough == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	sink = engine_sink(engine);
	return cterm_rip_parser_flush(&engine->parser, &sink);
}
