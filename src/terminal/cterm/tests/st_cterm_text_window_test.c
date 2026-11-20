/* Literal ANSI continuations and tab positions, independent of stored fields. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_cterm.h"
#include "cterm.h"
#include "io_error.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) {fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;} } while (0)

extern bool st_cterm_window_test_tracking;
extern size_t st_cterm_window_test_attempts,st_cterm_window_test_live,st_cterm_window_test_fail_at;

struct fixture {st_console_t *console;st_font_catalog_t *fonts;st_cterm_t *text;};

static int
create(struct fixture *f, unsigned columns)
{
	const struct st_console_display_config display={.backend=ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry={.columns=columns,.rows=5,.cell_width=8,.cell_height=8};
	CHECK(st_console_create(&f->console,&display,&geometry)==0 && st_font_catalog_create(&f->fonts,f->console)==0);
	const struct st_cterm_config config={.console=f->console,.fonts=f->fonts,.emulation=ST_CTERM_ANSI_BBS,
	    .encoding=ST_CTERM_ENCODING_CODEPAGE,.codepage=ST_CODEPAGE_CP437,.quiet=true,
	    .graphics_mode=ST_CTERM_GRAPHICS_RIP_3,.rip3_session_owner=true};
	CHECK(st_cterm_create(&f->text,&config)==0 && st_cterm_start_text(f->text)==0);return 0;
}

static void
destroy(struct fixture *f)
{
	st_cterm_destroy(&f->text);st_font_catalog_destroy(f->fonts);st_console_destroy(f->console);
}

static int
feed(struct fixture *f, const char *text, size_t length)
{
	size_t used=0;CHECK(st_cterm_write(f->text,text,length,&used)==0 && used==length);return 0;
}

static int
position(struct fixture *f, int x, int y)
{
	struct st_cterm_state state;
	CHECK(st_cterm_state(f->text,&state)==0 && state.x==x && state.y==y);return 0;
}

static int
continuations(void)
{
	static const char sequence[]="\x1b[38;2;12;34;56m";
	for (size_t split=0;split<sizeof(sequence)-1;split++) {
		struct fixture source={0},next={0};
		CHECK(create(&source,17)==0 && create(&next,23)==0);
		CHECK(feed(&source,sequence,split)==0);
		CHECK(st_cterm_inherit_window_interpreter(next.text,source.text,true)==0);
		destroy(&source); /* No inherited pointer may still alias this object. */
		CHECK(feed(&next,sequence+split,sizeof(sequence)-1-split)==0 && feed(&next,"F",1)==0);
		struct st_cterm_state state;
		CHECK(st_cterm_state(next.text,&state)==0 && state.fg_color==0x0c2238 && state.attr==7);
		CHECK(position(&next,2,1)==0);destroy(&next);
	}
	return 0;
}

static int
tabs_and_saved_cursor(void)
{
	struct fixture source={0},small={0},large={0};
	CHECK(create(&source,17)==0 && create(&small,4)==0 && create(&large,23)==0);
	const char setup[]="\x1b[3g\x1b[1;6H\x1bH\x1b[3;3H\x1b" "7\x1b[31;44m";
	CHECK(feed(&source,setup,sizeof(setup)-1)==0);
	CHECK(st_cterm_inherit_window_interpreter(small.text,source.text,true)==0);
	CHECK(st_cterm_inherit_window_interpreter(large.text,small.text,true)==0);
	destroy(&source);destroy(&small);
	CHECK(feed(&large,"\t",1)==0 && position(&large,6,1)==0);
	const char restore[]="\x1b[2;2H\x1b" "8";
	CHECK(feed(&large,restore,sizeof(restore)-1)==0 && position(&large,1,1)==0);
	struct st_cterm_state state;
	CHECK(st_cterm_state(large.text,&state)==0 && state.attr==7 && state.top_margin==1 && state.bottom_margin==5);
	destroy(&large);
	/* Unmodified default stops extend when the new window grows. */
	CHECK(create(&source,4)==0 && create(&large,23)==0);
	CHECK(st_cterm_inherit_window_interpreter(large.text,source.text,true)==0);
	destroy(&source);
	CHECK(feed(&large,"\t\t",2)==0 && position(&large,17,1)==0);destroy(&large);return 0;
}

static int
failures(void)
{
	struct fixture source={0},next={0};CHECK(create(&source,17)==0 && create(&next,23)==0);
	const char setup[]="\x1b[3g\x1b[1;6H\x1bH\x1b[3;1H\x1bJ\x1b[H\x1b]0;partial";
	CHECK(feed(&source,setup,sizeof(setup)-1)==0 && source.text->tab_count==1 && source.text->vtab_count==1);
	int *old_tabs=next.text->tabs;char *old_string=next.text->strbuf;
	st_cterm_window_test_tracking=true;
	for (size_t i=0;i<3;i++) {
		st_cterm_window_test_attempts=0;st_cterm_window_test_fail_at=i;
		CHECK(st_cterm_inherit_window_interpreter(next.text,source.text,true)==IO_ERROR_OUT_OF_MEMORY
		    && st_cterm_window_test_live==0 && next.text->tabs==old_tabs && next.text->strbuf==old_string
		    && next.text->string==0 && next.text->save_xpos==0);
	}
	st_cterm_window_test_tracking=false;st_cterm_window_test_fail_at=SIZE_MAX;
	CHECK(st_cterm_inherit_window_interpreter(next.text,source.text,true)==0
	    && next.text->strbuf!=source.text->strbuf && strcmp(next.text->strbuf,"0;partial")==0);
	destroy(&source);
	CHECK(feed(&next," title\x07",7)==0 && feed(&next,"\t",1)==0 && position(&next,6,1)==0);
	/* ANSI-BBS renders the CP437 VT byte as a glyph; CSI Y addresses the
	 * inherited vertical tab stop without depending on that byte policy. */
	CHECK(feed(&next,"\x1b[Y",3)==0 && position(&next,6,3)==0);
	CHECK(st_cterm_inherit_window_interpreter(next.text,next.text,true)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_cterm_inherit_window_interpreter(NULL,next.text,true)==IO_ERROR_INVALID_ARGUMENT);
	destroy(&next);
	CHECK(create(&source,17)==0 && create(&next,23)==0);
	old_tabs=next.text->tabs;
	source.text->graphics_mode=ST_CTERM_GRAPHICS_RIP_1;
	CHECK(st_cterm_inherit_window_interpreter(next.text,source.text,true)==IO_ERROR_NOT_SUPPORTED && next.text->tabs==old_tabs);
	source.text->graphics_mode=ST_CTERM_GRAPHICS_RIP_3;
	const char dcs[]="\x1bPq";CHECK(feed(&source,dcs,sizeof(dcs)-1)==0);
	CHECK(st_cterm_inherit_window_interpreter(next.text,source.text,true)==IO_ERROR_NOT_SUPPORTED && next.text->tabs==old_tabs);
	destroy(&source);destroy(&next);return 0;
}

int
extended_tabs(void)
{
	struct fixture source={0},next={0};
	CHECK(create(&source,17)==0 && create(&next,23)==0);
	const char setup[]="\x1b[3g\x1b[1;6H\x1bH\x1b[2;2H\x1b[3";
	CHECK(feed(&source,setup,sizeof(setup)-1)==0);
	int *fresh_tabs=next.text->tabs;
	CHECK(st_cterm_inherit_window_interpreter(next.text,source.text,false)==0 && next.text->tabs==fresh_tabs);
	CHECK(feed(&next,"1mA",3)==0 && position(&next,2,1)==0 && next.text->attr==4);
	CHECK(feed(&next,"\t",1)==0 && position(&next,9,1)==0);
	const char saved[]="\x1b" "8";
	CHECK(feed(&next,saved,sizeof(saved)-1)==0 && position(&next,1,1)==0);
	CHECK(feed(&source,"2mB",3)==0 && position(&source,3,2)==0 && source.text->attr==2);
	destroy(&source);destroy(&next);return 0;
}

static int
window_copy(void)
{
	struct fixture source={0},next={0};CHECK(create(&source,17)==0 && create(&next,17)==0);
	uint16_t copied=123;
	CHECK(st_console_copy_font(NULL,source.console,0,&copied)==IO_ERROR_INVALID_ARGUMENT && copied==123);
	CHECK(st_console_copy_font(next.console,source.console,UINT16_MAX,&copied)==IO_ERROR_NOT_FOUND && copied==123);
	CHECK(st_console_copy_font(source.console,source.console,0,&copied)==0 && copied==0);
	st_cterm_destroy(&source.text);
	uint8_t bitmap[256*8]={0};
	for (unsigned y=0;y<8;y++) bitmap['A'*8+y]=(uint8_t)(0x80u>>y);
	const struct st_console_font_mapping mapping[]={{'A','A'}};
	const struct st_console_font font={.bitmap=bitmap,.bitmap_size=sizeof(bitmap),.width=8,.height=8,
	    .glyph_count=256,.row_stride=1,.glyph_stride=8,.mapping=mapping,.mapping_count=1};
	CHECK(st_font_catalog_load_bitmap(source.fonts,46,&font,ST_CODEPAGE_CP437)==0);
	const struct st_cterm_config config={.console=source.console,.fonts=source.fonts,.emulation=ST_CTERM_ANSI_BBS,
	    .encoding=ST_CTERM_ENCODING_CODEPAGE,.codepage=ST_CODEPAGE_CP437,.quiet=true,.font_selector=46,
	    .graphics_mode=ST_CTERM_GRAPHICS_RIP_3,.rip3_session_owner=true};
	CHECK(st_cterm_create(&source.text,&config)==0 && st_cterm_start_text(source.text)==0);
	const char setup[]="\x1b[3g\x1b[1;6H\x1bH\x1b[2;4r\x1b[?6h\x1b[2;3H\x1b" "7\x1b[38;2;18;52;86;48;2;0;0;0m\x1b[?25l";
	CHECK(feed(&source,setup,sizeof(setup)-1)==0);
	CHECK(st_cterm_copy_window_state(next.text,source.text)==0 && position(&next,3,3)==0);
	struct st_cterm_state state;
	CHECK(st_cterm_state(next.text,&state)==0 && state.fg_color==0x123456 && state.bg_color==0
	    && state.ext_attr.origin_mode && state.top_margin==2 && state.bottom_margin==4);
	CHECK(next.text->tabs!=source.text->tabs && next.text->fg_tc_str!=source.text->fg_tc_str);
	destroy(&source);memset(bitmap,0,sizeof(bitmap));
	CHECK(st_cterm_position_text(next.text,0,0)==0 && feed(&next,"A",1)==0 && st_cterm_text_boundary(next.text)==0);
	struct st_pixel_buffer *pixels=NULL;const struct st_console_rect cell={0,0,8,8};
	CHECK(st_console_get_pixels(&pixels,next.console,&cell,false)==0);
	for (unsigned y=0;y<8;y++) for (unsigned x=0;x<8;x++) {
		if ((pixels->primary[y*8+x]&0xffffff)!=(x==y ? 0x123456 : 0))
			fprintf(stderr,"copied glyph %u,%u raw=%08x\n",x,y,(unsigned)pixels->primary[y*8+x]);
		CHECK((pixels->primary[y*8+x]&0xffffff)==(x==y ? 0x123456 : 0));
	}
	st_pixel_buffer_destroy(pixels);
	const char restore[]="\x1b" "8";CHECK(feed(&next,restore,sizeof(restore)-1)==0 && position(&next,3,3)==0);
	CHECK(st_cterm_position_text(next.text,0,0)==0 && feed(&next,"\t",1)==0 && position(&next,6,1)==0);
	bool visible=true;CHECK(st_cterm_get_text_cursor_visible(next.text,&visible)==0 && !visible);
	CHECK(create(&source,17)==0);
	CHECK(feed(&source,"\x1b[3",3)==0 && st_cterm_select_window_interpreter(next.text,source.text)==IO_ERROR_NOT_SUPPORTED);
	CHECK(feed(&source,"1m",2)==0 && st_cterm_select_window_interpreter(next.text,source.text)==0 && position(&next,6,1)==0);
	CHECK(st_cterm_state(next.text,&state)==0 && state.fg_color==0x123456 && next.text->save_xpos==3);
	destroy(&source);destroy(&next);return 0;
}

int
main(void)
{
	CHECK(continuations()==0 && tabs_and_saved_cursor()==0 && failures()==0 && extended_tabs()==0 && window_copy()==0);return 0;
}
