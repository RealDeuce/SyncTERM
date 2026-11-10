/* Controllable bitmap driver; rendering remains in the ordinary presenter. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "console_test_internal.h"
#include "io_error.h"
#include "st_time.h"
#include "string_compat.h"
#include <stdlib.h>
#include <string.h>

bool
st_console_test_requested(int argc, char *const argv[])
{
	if ((st_console_build_features() & ST_CONSOLE_BUILD_TEST) != 0)
		for (int i = 1; i < argc; ++i) {
			const char *arg = argv[i];
			if (strcmp(arg, "--test-control") == 0) return true;
			if (strcmp(arg, "--test-directory") == 0) { ++i; continue; }
			if (arg[0] == '-'
#ifdef _WIN32
			    || arg[0] == '/'
#endif
			) {
				if (arg[1] != 0 && arg[2] == 0 && strchr("bBnNwW", arg[1]) != NULL) ++i;
			}
		}
	return false;
}

void
st_console_test_number(struct st_console_test_application *app,
	const char *name, int64_t number)
{
	if (app->count == sizeof(app->fields) / sizeof(app->fields[0])
	    || strlen(name) >= sizeof(app->fields[0].name)) {
		app->error = IO_ERROR_OUT_OF_RANGE;
		return;
	}
	struct st_console_test_field *field = &app->fields[app->count++];
	memcpy(field->name, name, strlen(name) + 1);
	field->number = number;
}

void
st_console_test_text(struct st_console_test_application *app,
	const char *name, const char *text)
{
	size_t index = app->count;
	st_console_test_number(app, name, 0);
	if (app->count == index) return;
	app->fields[index].text = strdup(text == NULL ? "" : text);
	if (app->fields[index].text == NULL) app->error = IO_ERROR_OUT_OF_MEMORY;
}

void
st_console_test_capture_destroy(struct st_console_test_capture *capture)
{
	if (capture == NULL) return;
	for (size_t i = 0; i < capture->glyph_count; ++i) free(capture->glyphs[i].text);
	for (size_t i = 0; i < capture->application.count; ++i) free(capture->application.fields[i].text);
	free(capture->glyphs); free(capture->cells); free(capture->palette);
	free(capture->title); free(capture->clipboard); free(capture->url);
	st_pixel_buffer_destroy(capture->planes);
	st_rgb_buffer_destroy(capture->rgb);
	free(capture);
}

#if ST_CONSOLE_TEST
struct st_console_test_state *
st_console_test_state(st_console_display_t *display)
{
	return display != NULL && display->backend == ST_CONSOLE_BACKEND_TEST
	    ? display->backend_state : NULL;
}

void st_console_test_event_read(st_console_display_t *display)
{
	if (st_console_test_state(display) == NULL) return;
	(void)st_mutex_lock(&display->backend_mutex);
	++st_console_test_state(display)->events_dequeued;
	(void)st_mutex_unlock(&display->backend_mutex);
}

static int test_present(st_console_t *console)
{
	struct st_console_test_state *state = st_console_test_state(console->display);
	uint64_t now = 0;
	(void)st_time_monotonic_ms(&now);
	state->timestamp = state->clock_override ? state->clock_ms : now;
	int status = st_console_bitmap_presenter_frame(state->presenter, console,
	    state->width, state->height, (uint32_t)state->timestamp, NULL, NULL, &state->frame);
	if (status == 0) ++state->presentation;
	return status;
}

static int test_attach(st_console_t *console)
{
	struct st_console_test_state *state = st_console_test_state(console->display);
	if (!st_console_test_dimensions(console->pixels->width, console->pixels->height)) return IO_ERROR_OUT_OF_RANGE;
	console->test_identifier = ++state->next_console_id;
	return 0;
}

static int test_resize(st_console_t *console, uint32_t columns, uint32_t rows)
{
	struct st_console_test_state *state = st_console_test_state(console->display);
	if (!st_console_test_dimensions((uint64_t)columns * console->cell_width + console->pixel_extra_width,
	    (uint64_t)rows * console->cell_height + console->pixel_extra_height)) return IO_ERROR_OUT_OF_RANGE;
	struct st_console_bitmap_geometry next = {
	    columns * console->cell_width + console->pixel_extra_width,
	    rows * console->cell_height + console->pixel_extra_height,
	    console->aspect_width, console->aspect_height, console->preserve_window_size};
	uint32_t minimum_width, minimum_height, width, height;
	int status = st_console_mode_change_size(&state->geometry, &next,
	    state->width, state->height, 0, 0, &width, &height,
	    &minimum_width, &minimum_height);
	if (status == 0 && !st_console_test_dimensions(width, height)) status = IO_ERROR_OUT_OF_RANGE;
	if (status == 0) {
		state->geometry = next;
		state->width = width; state->height = height;
	}
	return status;
}

static int test_activate(st_console_t *console)
{
	return test_resize(console, console->columns, console->rows);
}

static int replace_text(char **target, const char *text)
{
	char *copy = strdup(text == NULL ? "" : text);
	if (copy == NULL) return IO_ERROR_OUT_OF_MEMORY;
	free(*target); *target = copy;
	return 0;
}
static int test_title(st_console_display_t *d, const char *s) { return replace_text(&st_console_test_state(d)->title, s); }
static int test_clipboard(st_console_display_t *d, const char *s) { return replace_text(&st_console_test_state(d)->clipboard, s); }
static int test_get_clipboard(st_console_display_t *d, char **s)
{
	*s = strdup(st_console_test_state(d)->clipboard);
	return *s == NULL ? IO_ERROR_OUT_OF_MEMORY : 0;
}
static int test_url(st_console_display_t *d, const char *s)
{
	struct st_console_test_state *state = st_console_test_state(d);
	int status = replace_text(&state->url, s);
	if (status == 0) ++state->url_serial;
	return status;
}
static int test_pointer(st_console_display_t *d, bool visible) { (void)d; (void)visible; return 0; }
static int test_pointer_shape(st_console_display_t *d, enum st_console_pointer_shape shape) { (void)d; (void)shape; return 0; }
static int test_fullscreen(st_console_display_t *d, bool value) { st_console_test_state(d)->fullscreen = value; return 0; }
static int test_scaling(st_console_display_t *d, double *value)
{
	struct st_console_test_state *s = st_console_test_state(d);
	return st_console_bitmap_geometry_scaling(&s->geometry, s->width, s->height, value);
}
static void test_destroy(st_console_display_t *display)
{
	struct st_console_test_state *state = display->backend_state;
	st_console_test_control_destroy(display);
	st_console_bitmap_presenter_destroy(state->presenter);
	free(state->clipboard); free(state->url); free(state->title); free(state);
	display->backend_state = NULL;
}
static const struct st_console_backend_ops test_ops = {
	.presents_pixels = true, .deferred_present = true, .animated_present = true,
	.attach = test_attach, .present = test_present, .resize = test_resize,
	.activate = test_activate, .destroy = test_destroy,
	.set_title = test_title, .set_clipboard_text = test_clipboard,
	.get_clipboard_text = test_get_clipboard, .open_url = test_url,
	.set_pointer_visible = test_pointer, .set_pointer_shape = test_pointer_shape,
	.set_fullscreen = test_fullscreen, .get_scaling = test_scaling,
};

int st_console_test_init(st_console_t *console)
{
	if (console->pixels == NULL) return IO_ERROR_NOT_SUPPORTED;
	if (!st_console_test_dimensions(console->pixels->width, console->pixels->height)) return IO_ERROR_OUT_OF_RANGE;
	struct st_console_test_state *state = calloc(1, sizeof(*state));
	if (state == NULL) return IO_ERROR_OUT_OF_MEMORY;
	console->display->backend_state = state;
	state->clipboard = strdup(""); state->url = strdup(""); state->title = strdup("");
	state->geometry = (struct st_console_bitmap_geometry){console->pixels->width,
	    console->pixels->height, console->aspect_width, console->aspect_height, console->preserve_window_size};
	int status = st_console_scaled_window_size(state->geometry.width,
	    state->geometry.height, state->geometry.aspect_width, state->geometry.aspect_height,
	    console->display->config.initial_scaling, &state->width, &state->height);
	if (console->display->initial_window_width != 0) {
		state->width = console->display->initial_window_width;
		state->height = console->display->initial_window_height;
	}
	if (status == 0 && !st_console_test_dimensions(state->width, state->height)) status = IO_ERROR_OUT_OF_RANGE;
	if (status == 0 && (state->clipboard == NULL || state->url == NULL || state->title == NULL)) status = IO_ERROR_OUT_OF_MEMORY;
	if (status == 0) status = st_console_bitmap_presenter_create(&state->presenter, console);
	if (status != 0) { test_destroy(console->display); return status; }
	console->display->backend_ops = &test_ops;
	(void)test_attach(console);
	return test_present(console);
}

void st_console_test_set_application_reader(st_console_display_t *display,
	st_console_test_application_reader reader, void *context)
{
	struct st_console_test_state *state = st_console_test_state(display);
	if (state != NULL) { state->reader = reader; state->reader_context = context; }
}

int st_console_test_host_resize(st_console_display_t *display, uint32_t width, uint32_t height)
{
	if (!st_console_test_dimensions(width, height)) return IO_ERROR_OUT_OF_RANGE;
	(void)st_mutex_lock(&display->backend_mutex);
	struct st_console_test_state *state = st_console_test_state(display);
	state->width = width; state->height = height;
	(void)st_mutex_unlock(&display->backend_mutex);
	return 0;
}

static int compare_glyphs(const void *left, const void *right)
{
	const struct st_console_test_glyph *a = left, *b = right;
	if (a->font != b->font) return a->font < b->font ? -1 : 1;
	return a->glyph == b->glyph ? 0 : a->glyph < b->glyph ? -1 : 1;
}
static int capture_glyphs(st_console_t *console, struct st_console_test_capture *capture)
{
	size_t count = (size_t)capture->columns * capture->rows;
	capture->glyphs = calloc(count, sizeof(*capture->glyphs));
	if (capture->glyphs == NULL) return IO_ERROR_OUT_OF_MEMORY;
	for (size_t i = 0; i < count; ++i) {
		capture->glyphs[i].font = capture->cells[i].glyph_font;
		capture->glyphs[i].glyph = capture->cells[i].glyph;
	}
	qsort(capture->glyphs, count, sizeof(*capture->glyphs), compare_glyphs);
	for (size_t i = 0; i < count; ++i) {
		struct st_console_test_glyph key = capture->glyphs[i];
		if (capture->glyph_count != 0 && compare_glyphs(&key, &capture->glyphs[capture->glyph_count - 1]) == 0) continue;
		struct st_console_test_glyph *glyph = &capture->glyphs[capture->glyph_count++];
		*glyph = key;
		size_t required = 0;
		(void)st_console_font_glyph_text_locked(console, glyph->font, glyph->glyph, NULL, 0, &required);
		if (required >= UINT32_MAX) return IO_ERROR_OUT_OF_RANGE;
		glyph->text_size = required;
		glyph->text = calloc(required + 1, 1);
		if (glyph->text == NULL) return IO_ERROR_OUT_OF_MEMORY;
		(void)st_console_font_glyph_text_locked(console, glyph->font, glyph->glyph, glyph->text, required + 1, &required);
	}
	return 0;
}

int st_console_test_capture(st_console_display_t *display, struct st_console_test_capture **result)
{
	if (st_console_test_state(display) == NULL || result == NULL) return IO_ERROR_INVALID_ARGUMENT;
	*result = NULL;
	struct st_console_test_capture *capture = calloc(1, sizeof(*capture));
	if (capture == NULL) return IO_ERROR_OUT_OF_MEMORY;
	/* Normal lifetime order: lifecycle -> console state -> backend -> worker.
	 * The application owner stays here until its state and all display data
	 * are copied. A background presenter cannot interleave another frame. */
	(void)st_mutex_lock(&display->lifecycle_mutex);
	st_console_t *console = atomic_load_explicit(&display->active, memory_order_acquire);
	if (console == NULL) { (void)st_mutex_unlock(&display->lifecycle_mutex); free(capture); return IO_ERROR_NOT_CONNECTED; }
	(void)st_rwlock_write_lock(&console->state_lock);
	int status = st_console_present_locked(console, true);
	(void)st_mutex_lock(&display->backend_mutex);
	struct st_console_test_state *state = st_console_test_state(display);
	if (status != 0) goto finished;
	capture->id = ++state->capture_id;
	capture->console_id = console->test_identifier;
	capture->presentation = state->presentation; capture->generation = state->frame.generation;
	capture->timestamp = state->timestamp; capture->events_dequeued = state->events_dequeued;
	capture->columns = console->columns; capture->rows = console->rows;
	capture->cell_width = console->cell_width; capture->cell_height = console->cell_height;
	capture->pixel_top = console->pixel_top;
	capture->cursor_x = console->cursor_x; capture->cursor_y = console->cursor_y;
	capture->pixel_cursor = console->pixel_cursor;
	capture->cursor_custom = console->cursor_custom;
	capture->cursor_visible = console->cursor_visible; capture->cursor_shape = console->cursor_shape;
	capture->cursor = console->cursor_custom ? console->custom_cursor : console->default_cursor;
	capture->blink_mode = console->blink_mode; capture->video_flags = console->video_flags;
	capture->fullscreen = state->fullscreen;
	capture->pointer_visible = console->pointer_visible; capture->pointer_shape = console->pointer_shape;
	capture->output_width = state->width; capture->output_height = state->height;
	capture->destination = state->frame.destination;
	(void)test_scaling(display, &capture->scaling);
	capture->title = strdup(state->title); capture->clipboard = strdup(state->clipboard); capture->url = strdup(state->url);
	capture->url_serial = state->url_serial;
	size_t count = (size_t)console->columns * console->rows;
	capture->cells = malloc(count * sizeof(*capture->cells));
	capture->palette = malloc(sizeof(console->palette));
	if (capture->title == NULL || capture->clipboard == NULL || capture->url == NULL || capture->cells == NULL || capture->palette == NULL) { status = IO_ERROR_OUT_OF_MEMORY; goto finished; }
	for (size_t i = 0; i < count; ++i) capture->cells[i] = *st_console_cell_at(console, i);
	memcpy(capture->palette, console->palette, sizeof(console->palette));
	status = st_pixel_buffer_create(&capture->planes, console->pixels->width, console->pixels->height, true);
	if (status != 0) goto finished;
	struct st_console_rect all = {0, 0, console->pixels->width, console->pixels->height};
	st_console_copy_pixels_locked(console, capture->planes->primary, all.width, false, &all);
	st_console_copy_pixels_locked(console, capture->planes->alternate, all.width, true, &all);
	status = st_rgb_buffer_create(&capture->rgb, state->width, state->height);
	if (status != 0) goto finished;
	/* The presenter returns its composed image plus drawable placement. */
	memset(capture->rgb->pixels, 0, (size_t)state->width * state->height * sizeof(uint32_t));
	const struct st_rgb_buffer *image = state->frame.pixels;
	if (image->width != state->frame.destination.width || image->height != state->frame.destination.height) {
		status = IO_ERROR_CONFLICT; goto finished;
	}
	for (uint32_t y = 0; y < image->height; ++y)
		memcpy(capture->rgb->pixels + (size_t)(y + state->frame.destination.y) * state->width + state->frame.destination.x,
		    image->pixels + (size_t)y * image->width, (size_t)image->width * sizeof(uint32_t));
	status = capture_glyphs(console, capture);
	if (status == 0) {
		if (state->reader != NULL) state->reader(state->reader_context, &capture->application);
		else st_console_test_number(&capture->application, "session_active", 0);
	}
	if (status == 0) status = capture->application.error;
finished:
	(void)st_mutex_unlock(&display->backend_mutex);
	(void)st_rwlock_unlock(&console->state_lock);
	(void)st_mutex_unlock(&display->lifecycle_mutex);
	if (status != 0) st_console_test_capture_destroy(capture);
	else *result = capture;
	return status;
}
#endif
