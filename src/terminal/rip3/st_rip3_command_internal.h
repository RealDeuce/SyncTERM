/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_COMMAND_INTERNAL_H_
#define ST_RIP3_COMMAND_INTERNAL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "st_rip3_commands.h"

/* Consume a private decoder/renderer status below the void parser boundary. */
void st_rip3_command_result(const struct st_rip3_command_sink *sink, int status, bool operation);
struct st_rip3_environment;
struct st_rip3_frame;
struct st_rip3_port_info;
struct st_rip3_fill;
struct st_rip3_outline_font;
struct st_rip3_outline_layout;
struct st_rip3_outline_style;
struct st_rip3_style;
struct st_rip3_text_position;
struct st_rip3_text_context;
int st_rip3_command_text_glyphs(const struct st_rip3_text_context *context, const uint8_t *text, size_t length,
    const uint8_t **glyphs, size_t *count, uint8_t **owned);
int st_rip3_command_expanded_glyphs(const struct st_rip3_text_context *context, const uint8_t *text, size_t length,
    const uint8_t **glyphs, size_t *count, uint8_t **owned);
int st_rip3_command_outline_font(struct st_rip3_outline_font *result, const struct st_rip3_text_context *context,
    const struct st_rip3_outline_style *style, const struct st_rip3_frame *frame, unsigned orientation);
int st_rip3_command_draw_outline(const struct st_rip3_command_host *host, const struct st_rip3_style *style,
    const struct st_rip3_outline_layout *layout, const struct st_rip3_outline_font *font, unsigned rgb_bits,
    const struct st_rip3_text_position *anchor, const uint8_t *glyphs, size_t length,
    const uint8_t *foreground_override);
int st_rip3_button_create_execute(const struct st_rip3_command_host *host, const uint8_t *command, size_t length,
    bool *operation);
int st_rip3_mouse_commands_execute(const struct st_rip3_command_host *host, const uint8_t *command, size_t length,
    bool *operation);
int st_rip3_button_commands_execute(const struct st_rip3_command_host *host, const uint8_t *command, size_t length,
    bool *operation, const struct st_rip3_command_sink *sink);

/* Private wire-field and execution helpers. Every status is consumed by the
 * public void processing boundary; none is a terminal/session return status. */
struct st_rip3_fields {
	const uint8_t *bytes;
	size_t         remaining;
};

/* Read one ordinary wire field. A leading period consumes one byte;
 * otherwise only a final field can use fewer digits than its width.
 * A NULL result validates the field without interpreting its value. */
int st_rip3_command_next_field(struct st_rip3_fields *fields, uint64_t *result, size_t width, unsigned radix,
    bool final);
int st_rip3_command_environment(const struct st_rip3_command_host *host, unsigned *number,
    struct st_rip3_environment *value);
int st_rip3_command_frame(const struct st_rip3_command_host *host, unsigned environment,
    struct st_rip3_frame *result);
int st_rip3_command_ink(const struct st_rip3_command_host *host, unsigned bits, uint32_t color, uint32_t *result);
int st_rip3_command_index(const struct st_rip3_command_host *host, unsigned bits, uint32_t color, uint8_t *result);
/* Resolve fixed drawing-context phase into the device kernel's viewport-
 * relative phase, without changing the caller's stored mask/phase record. */
void st_rip3_command_fill(const struct st_rip3_port_info *port, const struct st_rip3_fill *source,
    struct st_rip3_fill *result);
int st_rip3_bitmap_commands_execute(const struct st_rip3_command_host *host, const uint8_t *command, size_t length,
    bool *operation);
int st_rip3_scroll_blit_command(const struct st_rip3_command_host *host, const uint8_t *command, size_t length,
    bool *operation);
int st_rip3_bitmap_load_commands_execute(const struct st_rip3_command_host *host, const uint8_t *command,
    size_t length, bool *operation);
int st_rip3_image_commands_execute(const struct st_rip3_command_host *host, const uint8_t *command, size_t length,
    bool *operation);
int st_rip3_image_argument_prepare(const struct st_rip3_command_host *host, const uint8_t *command, size_t length,
    size_t *filename_offset);
int st_rip3_command_jpeg(const struct st_rip3_command_host *host, const uint8_t *filename, size_t length,
    bool *operation);
int st_rip3_command_received_jpeg(const struct st_rip3_command_host *host, const char *filename);
int st_rip3_command_bitmap_playback(const struct st_rip3_command_host *host, const char *filename);
int st_rip3_get_image_commands_execute(const struct st_rip3_command_host *host, const uint8_t *command,
    size_t length, bool *operation);
int st_rip3_text_commands_execute(const struct st_rip3_command_host *host, const uint8_t *command, size_t length,
    bool *operation);
int st_rip3_font_commands_execute(const struct st_rip3_command_host *host, const uint8_t *command, size_t length,
    bool *operation);
int st_rip3_font_argument_prepare(const struct st_rip3_command_host *host, const uint8_t *command, size_t length,
    struct st_rip3_outline_style *outline, size_t *name_offset);
int st_rip3_font_commands_expanded(const struct st_rip3_command_host *host,
    const struct st_rip3_outline_style *outline, const uint8_t *name, size_t length);
int st_rip3_curve_commands_execute(const struct st_rip3_command_host *host, const uint8_t *command, size_t length,
    bool *operation);
int st_rip3_skew_commands_execute(const struct st_rip3_command_host *host, const uint8_t *command, size_t length,
    bool *operation);
int st_rip3_affine_command_execute(const struct st_rip3_command_host *host, const uint8_t *command, size_t length,
    bool *operation);
int st_rip3_bezier_commands_execute(const struct st_rip3_command_host *host, const uint8_t *command, size_t length,
    bool *operation);
int st_rip3_rounded_commands_execute(const struct st_rip3_command_host *host, const uint8_t *command,
    size_t length, bool *operation);
int st_rip3_path_commands_execute(const struct st_rip3_command_host *host, const uint8_t *command, size_t length,
    bool *operation);
int st_rip3_drawing_commands_execute(const struct st_rip3_command_host *host, const uint8_t *command,
    size_t length, bool *operation);
int st_rip3_port_commands_execute(const struct st_rip3_command_host *host, const uint8_t *command, size_t length,
    bool *operation);

#endif
