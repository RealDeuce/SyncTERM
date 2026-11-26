/* Independent ANSI rendering inside a RIP3 window; no legacy RIP helpers. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_term_rip3_view.h"
#include "st_rip3_maf.h"
#include "st_builtin_fonts.h"
#include "io_error.h"

#include <stdlib.h>
#include <string.h>

struct st_term_rip3_view {
	st_console_t *console;
	st_font_catalog_t *fonts;
	st_cterm_t *text;
	struct st_rip3_text_window_layout layout;
	st_console_t *parent;
	st_cterm_output_callback_t output;
	void *output_context;
	bool active;
};

static void
output(void *context, enum st_cterm_output_origin origin, const uint8_t *bytes, size_t length)
{
	struct st_term_rip3_view *view=context;
	/* A reply is the peer's capture fence: publish preceding ANSI output
	 * before allowing it onto the transport. No parser reentry is required. */
	if (view->active && view->parent!=NULL) (void)st_term_rip3_view_present(view,view->parent,true);
	if (view->output!=NULL) view->output(view->output_context,origin,bytes,length);
}

static int
bitmap_font(st_font_catalog_t *catalog, unsigned width, unsigned height, unsigned number)
{
	struct st_rip3_maf_font font;
	int status=st_rip3_maf_builtin_font(&font,width,height,(enum st_rip3_maf_mode)number);
	if (status!=0) return status;
	if (font.glyph_size>SIZE_MAX/256) return IO_ERROR_OUT_OF_RANGE;
	uint8_t *bytes=calloc(256,font.glyph_size);
	if (bytes==NULL) return IO_ERROR_OUT_OF_MEMORY;
	memcpy(bytes+font.glyph_size,font.glyphs,255*font.glyph_size);
	struct st_console_font source;
	if (!st_builtin_font_get(0,8,16,&source)) status=IO_ERROR_NOT_SUPPORTED;
	else {
		const struct st_console_font native={.bitmap=bytes,.bitmap_size=256*font.glyph_size,
		    .width=font.width,.height=font.height,.glyph_count=256,.row_stride=font.row_bytes,
		    .glyph_stride=font.glyph_size,.mapping=source.mapping,.mapping_count=source.mapping_count,
		    .fallback_glyph=63};
		status=st_font_catalog_load_bitmap(catalog,ST_FONT_CATALOG_FIRST_CUSTOM,&native,ST_CODEPAGE_CP437);
	}
	free(bytes);return status;
}

void
st_term_rip3_view_destroy(struct st_term_rip3_view *view)
{
	if (view==NULL) return;
	st_cterm_destroy(&view->text);st_font_catalog_destroy(view->fonts);
	st_console_destroy(view->console);free(view);
}

int
st_term_rip3_view_create(struct st_term_rip3_view **result,
    const struct st_cterm_config *text_config, const struct st_rip3_text_window_layout *layout,
    uint32_t device_width, uint32_t device_height)
{
	if (result==NULL || text_config==NULL || layout==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (layout->columns==0 || layout->rows==0 || layout->cell_width==0 || layout->cell_height==0
	    || device_width==0 || device_height==0) return IO_ERROR_INVALID_ARGUMENT;
	uint64_t pixel_width=(uint64_t)layout->columns*layout->cell_width;
	uint64_t pixel_height=(uint64_t)layout->rows*layout->cell_height;
	uint64_t budget=(uint64_t)device_width*device_height;
	budget=budget>UINT64_MAX/36 ? UINT64_MAX : budget*36;
	/* Match the session's finite offscreen resource policy before allocating
	 * logical cells. Valid but oversized requests fail nonfatally. */
	if (pixel_width>UINT32_MAX || pixel_height>UINT32_MAX || pixel_width*pixel_height>budget)
		return IO_ERROR_OUT_OF_MEMORY;
	struct st_term_rip3_view *view=calloc(1,sizeof(*view));
	if (view==NULL) return IO_ERROR_OUT_OF_MEMORY;
	view->layout=*layout;
	view->output=text_config->output;view->output_context=text_config->output_context;
	view->active=!layout->extended || !(layout->flags&4);
	view->parent=text_config->console;
	int status=st_console_create_raster_view(&view->console,view->parent,layout->columns,layout->rows,
	    layout->cell_width,layout->cell_height,layout->x,layout->y);
	if (status==0) status=st_font_catalog_create(&view->fonts,view->console);
	if (status==0) status=bitmap_font(view->fonts,device_width,device_height,layout->font);
	struct st_cterm_config config=*text_config;
	config.console=view->console;config.fonts=view->fonts;config.font_selector=ST_FONT_CATALOG_FIRST_CUSTOM;
	config.palette_mapping=NULL;config.palette_override=NULL;config.status_line=false;
	config.terminal_rows=0;config.scrollback_lines=0;config.force_lcf=false;
	/* A positioned subwindow retires only its own local content. The ordinary
	 * window at the display origin feeds the connection's scrollback. */
	if (layout->extended || layout->x!=0 || layout->y!=0)
		config.history_display=NULL;
	config.output=output;config.output_context=view;config.size_changed=NULL;
	if (status==0) status=st_cterm_create(&view->text,&config);
	if (status==0) status=st_cterm_start_text(view->text);
	if (status==0) status=st_cterm_set_text_wrap(view->text,layout->wrap);
	if (status!=0) {st_term_rip3_view_destroy(view);return status;}
	*result=view;return 0;
}

st_cterm_t *
st_term_rip3_view_text(struct st_term_rip3_view *view)
{
	return view==NULL ? NULL : view->text;
}

st_console_t *
st_term_rip3_view_console(struct st_term_rip3_view *view)
{
	return view==NULL ? NULL : view->console;
}

const struct st_rip3_text_window_layout *
st_term_rip3_view_layout(const struct st_term_rip3_view *view)
{
	return view==NULL ? NULL : &view->layout;
}

int
st_term_rip3_view_begin(struct st_term_rip3_view *view, st_console_t *parent)
{
	if (view==NULL || parent==NULL || parent!=view->parent) return IO_ERROR_INVALID_ARGUMENT;
	return st_console_raster_view_adopt(view->console);
}

int
st_term_rip3_view_present(struct st_term_rip3_view *view, st_console_t *parent, bool active)
{
	if (view==NULL || parent==NULL || parent!=view->parent) return IO_ERROR_INVALID_ARGUMENT;
	uint32_t width,height;
	int status=st_console_present(view->console);
	if (status==0) status=st_console_pixel_size(parent,&width,&height);
	if (status!=0) return status;
	uint32_t x,y;bool visible;enum st_console_cursor_shape shape;
	if (status==0) status=st_console_get_position(view->console,&x,&y);
	if (status==0) status=st_console_get_cursor(view->console,&visible,&shape);
	if (status!=0) return status;
	uint64_t cursor_x=(uint64_t)x*view->layout.cell_width;
	uint64_t cursor_y=(uint64_t)y*view->layout.cell_height;
	if (shape==ST_CONSOLE_CURSOR_UNDERLINE) cursor_y+=view->layout.cell_height-1;
	struct st_console_pixel_cursor cursor={.enabled=true,.colour=ST_PIXEL_COLOR_RGB_VALUE(255,255,255)};
	if (view->layout.x<width && view->layout.y<height
	    && cursor_x<width-view->layout.x && cursor_y<height-view->layout.y) {
		cursor.visible=active && visible;
		cursor.rectangle=(struct st_console_rect){(uint32_t)(view->layout.x+cursor_x),(uint32_t)(view->layout.y+cursor_y),
		    shape==ST_CONSOLE_CURSOR_BAR ? 1 : view->layout.cell_width,
		    shape==ST_CONSOLE_CURSOR_UNDERLINE ? 1 : view->layout.cell_height};
		if (cursor.rectangle.width>width-cursor.rectangle.x) cursor.rectangle.width=width-cursor.rectangle.x;
		if (cursor.rectangle.height>height-cursor.rectangle.y) cursor.rectangle.height=height-cursor.rectangle.y;
	}
	status=st_console_set_cursor(parent,false,ST_CONSOLE_CURSOR_UNDERLINE);
	if (status==0) status=st_console_set_pixel_cursor(parent,&cursor);
	return status;
}

int
st_term_rip3_view_active(struct st_term_rip3_view *view, st_console_t *parent, bool active)
{
	if (view==NULL || parent==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (view->active==active) return 0;
	int status;
	if (active) {
		/* RIP may have painted beneath a deactivated window. Activation must
		 * reveal its cursor over the current screen, never replay old pixels. */
		status=st_term_rip3_view_begin(view,parent);
		if (status==0) status=st_term_rip3_view_present(view,parent,true);
	}
	else {
		struct st_console_pixel_cursor cursor;
		status=st_console_get_pixel_cursor(parent,&cursor);
		if (status==0) {cursor.visible=false;status=st_console_set_pixel_cursor(parent,&cursor);}
	}
	if (status==0) view->active=active;
	return status;
}
