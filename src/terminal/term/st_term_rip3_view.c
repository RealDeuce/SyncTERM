/* Independent ANSI rendering inside a RIP3 window; no legacy RIP helpers. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_term_rip3_view.h"
#include "st_rip3_font_data.h"
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
	uint32_t *palette;
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
	const struct st_rip3_font_asset *asset=NULL;
	for (size_t i=0;i<st_rip3_font_asset_count();i++) {
		const struct st_rip3_font_asset *candidate=st_rip3_font_asset_at(i);
		if (strcmp(candidate->filename,"RIPscrip.maf")==0) {asset=candidate;break;}
	}
	if (asset==NULL) return IO_ERROR_NOT_FOUND;
	struct st_rip3_maf file;struct st_rip3_maf_font font;
	int status=st_rip3_maf_open(&file,asset->data,asset->size);
	if (status==0) status=st_rip3_maf_find(&font,&file,width,height,(enum st_rip3_maf_mode)number);
	if (status==IO_ERROR_NOT_FOUND) status=st_rip3_maf_find(&font,&file,640,480,(enum st_rip3_maf_mode)number);
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
	st_console_destroy(view->console);free(view->palette);free(view);
}

int
st_term_rip3_view_create(struct st_term_rip3_view **result,
    const struct st_cterm_config *text_config, const struct st_rip3_text_window_layout *layout,
    uint32_t device_width, uint32_t device_height)
{
	if (result==NULL || text_config==NULL || layout==NULL) return IO_ERROR_INVALID_ARGUMENT;
	struct st_term_rip3_view *view=calloc(1,sizeof(*view));
	if (view==NULL) return IO_ERROR_OUT_OF_MEMORY;
	view->layout=*layout;
	view->output=text_config->output;view->output_context=text_config->output_context;view->active=true;
	view->palette=calloc(UINT16_MAX+1u,sizeof(*view->palette));
	if (view->palette==NULL) {st_term_rip3_view_destroy(view);return IO_ERROR_OUT_OF_MEMORY;}
	const struct st_console_display_config display={.backend=ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config screen={.columns=layout->columns,.rows=layout->rows,
	    .cell_width=layout->cell_width,.cell_height=layout->cell_height};
	int status=st_console_create(&view->console,&display,&screen);
	if (status==0) status=st_font_catalog_create(&view->fonts,view->console);
	if (status==0) status=bitmap_font(view->fonts,device_width,device_height,layout->font);
	struct st_cterm_config config=*text_config;
	config.console=view->console;config.fonts=view->fonts;config.font_selector=ST_FONT_CATALOG_FIRST_CUSTOM;
	config.palette_mapping=NULL;config.palette_override=NULL;config.status_line=false;
	config.terminal_rows=0;config.scrollback_lines=0;config.force_lcf=false;
	config.output=output;config.output_context=view;config.size_changed=NULL;
	if (status==0) status=st_cterm_create(&view->text,&config);
	if (status==0) status=st_cterm_start_text(view->text);
	if (status==0) status=st_cterm_set_text_wrap(view->text,layout->wrap);
	if (status==0) status=st_console_get_palette(text_config->console,0,view->palette,UINT16_MAX+1u);
	if (status==0) status=st_console_set_palette(view->console,0,view->palette,UINT16_MAX+1u);
	if (status!=0) {st_term_rip3_view_destroy(view);return status;}
	*result=view;return 0;
}

st_cterm_t *
st_term_rip3_view_text(struct st_term_rip3_view *view)
{
	return view==NULL ? NULL : view->text;
}

const struct st_rip3_text_window_layout *
st_term_rip3_view_layout(const struct st_term_rip3_view *view)
{
	return view==NULL ? NULL : &view->layout;
}

static int
rgb(struct st_term_rip3_view *view, struct st_pixel_buffer *pixels, st_console_t *console)
{
	int status=st_console_get_palette(console,0,view->palette,UINT16_MAX+1u);
	if (status!=0) return status;
	for (size_t i=0;i<(size_t)pixels->width*pixels->height;i++) {
		uint32_t *planes[]={pixels->primary,pixels->alternate};
		for (unsigned p=0;p<2;p++) if (planes[p]!=NULL && !ST_PIXEL_COLOR_IS_RGB(planes[p][i])) {
			if (planes[p][i]>UINT16_MAX) return IO_ERROR_OUT_OF_RANGE;
			planes[p][i]=ST_PIXEL_COLOR_RGB_TAG|view->palette[planes[p][i]];
		}
	}
	return 0;
}

int
st_term_rip3_view_begin(struct st_term_rip3_view *view, st_console_t *parent)
{
	if (view==NULL || parent==NULL) return IO_ERROR_INVALID_ARGUMENT;
	view->parent=parent;
	uint32_t width,height;
	int status=st_console_pixel_size(parent,&width,&height);
	if (status!=0) return status;
	struct st_console_rect area={view->layout.x,view->layout.y,
	    view->layout.columns*view->layout.cell_width,view->layout.rows*view->layout.cell_height};
	if (area.x>=width || area.y>=height) return IO_ERROR_OUT_OF_RANGE;
	if (area.width>width-area.x) area.width=width-area.x;
	if (area.height>height-area.y) area.height=height-area.y;
	struct st_pixel_buffer *pixels=NULL;
	status=st_console_get_pixels(&pixels,parent,&area,false);
	if (status==0) status=rgb(view,pixels,parent);
	const struct st_pixel_blit blit={.width=area.width,.height=area.height,.scale_x=1,.scale_y=1};
	if (status==0) status=st_console_blit_pixels(view->console,pixels,NULL,&blit);
	st_pixel_buffer_destroy(pixels);return status;
}

int
st_term_rip3_view_present(struct st_term_rip3_view *view, st_console_t *parent, bool active)
{
	if (view==NULL || parent==NULL) return IO_ERROR_INVALID_ARGUMENT;
	const struct st_console_rect area={0,0,view->layout.columns*view->layout.cell_width,
	    view->layout.rows*view->layout.cell_height};
	struct st_pixel_buffer *pixels=NULL;
	int status=st_console_get_pixels(&pixels,view->console,&area,false);
	if (status==0) status=rgb(view,pixels,view->console);
	const struct st_pixel_blit blit={.width=area.width,.height=area.height,.scale_x=1,.scale_y=1,
	    .destination_x=(int32_t)view->layout.x,.destination_y=(int32_t)view->layout.y};
	if (status==0) status=st_console_blit_pixels(parent,pixels,NULL,&blit);
	st_pixel_buffer_destroy(pixels);
	uint32_t x,y;bool visible;enum st_console_cursor_shape shape;
	if (status==0) status=st_console_get_position(view->console,&x,&y);
	if (status==0) status=st_console_get_cursor(view->console,&visible,&shape);
	if (status!=0) return status;
	struct st_console_pixel_cursor cursor={.enabled=true,.visible=active && visible,
	    .colour=ST_PIXEL_COLOR_RGB_VALUE(255,255,255),.rectangle={
	        view->layout.x+x*view->layout.cell_width,view->layout.y+y*view->layout.cell_height,
	        shape==ST_CONSOLE_CURSOR_BAR ? 1 : view->layout.cell_width,
	        shape==ST_CONSOLE_CURSOR_UNDERLINE ? 1 : view->layout.cell_height}};
	if (shape==ST_CONSOLE_CURSOR_UNDERLINE) cursor.rectangle.y+=view->layout.cell_height-1;
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
