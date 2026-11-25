/* Connected plain/clipboard construction. Statuses stay below the parser. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_command_internal.h"
#include "st_rip3_button_render.h"
#include "st_rip3_graphical.h"
#include "st_rip3_graphical_bytes.h"
#include "st_codepage.h"
#include "st_utf8.h"
#include "st_rip3_button_styles.h"
#include "st_rip3_button_payload.h"
#include "st_rip3_clipboard.h"
#include "st_rip3_styles.h"
#include "st_rip3_environments.h"
#include "st_rip3_ports.h"
#include "st_rip3_mouse.h"
#include "st_rip3_resources.h"
#include "st_rip3_bitmap_file.h"
#include "st_rip3_bitmap_stamp.h"
#include "st_rip3_palettes.h"
#include "st_pixels.h"
#include "st_rip3_text_commands.h"
#include "st_rip3_outline_font.h"
#include "st_rip3_outline_layout.h"
#include "st_rip3_outline_text.h"
#include "io_error.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct box {int64_t x0,y0,x1,y1;}; /* Half-open DEVICE edges. */
struct label {
	uint8_t bytes[255];size_t length;
	struct st_rip3_classic_font classic;
	struct st_rip3_classic_text_style classic_style;
	struct st_rip3_outline_font outline;
	struct st_rip3_outline_layout layout;
	struct st_rip3_text_position positions[255];
	int32_t advances[255];
	bool outline_label, default_outline;
	double x,y,width,height,ascent,descent;
};

static int
fill(const struct st_rip3_command_host *host,const struct st_rip3_port_info *port,
    struct box area,uint8_t color)
{
	if (area.x0<port->clip.x0) area.x0=port->clip.x0;
	if (area.y0<port->clip.y0) area.y0=port->clip.y0;
	if (area.x1>(int64_t)port->clip.x1+1) area.x1=(int64_t)port->clip.x1+1;
	if (area.y1>(int64_t)port->clip.y1+1) area.y1=(int64_t)port->clip.y1+1;
	if (area.x0>=area.x1 || area.y0>=area.y1) return 0;
	const struct st_rip3_rect clipped={(int32_t)area.x0,(int32_t)area.y0,(int32_t)(area.x1-1),(int32_t)(area.y1-1)};
	return st_rip3_ports_fill(host->ports,&clipped,color,ST_RIP3_COPY);
}

static int
edge(const struct st_rip3_command_host *host,const struct st_rip3_port_info *port,
    struct box b,uint8_t top,uint8_t bottom,uint8_t corner)
{
	if (b.x1<=b.x0 || b.y1<=b.y0) return 0;
	int status=fill(host,port,(struct box){b.x0,b.y0,b.x1,b.y0+1},top);
	if (status==0) status=fill(host,port,(struct box){b.x0,b.y0,b.x0+1,b.y1},top);
	if (status==0) status=fill(host,port,(struct box){b.x0,b.y1-1,b.x1,b.y1},bottom);
	if (status==0) status=fill(host,port,(struct box){b.x1-1,b.y0,b.x1,b.y1},bottom);
	if (status==0) status=fill(host,port,(struct box){b.x1-1,b.y0,b.x1,b.y0+1},corner);
	if (status==0) status=fill(host,port,(struct box){b.x0,b.y1-1,b.x0+1,b.y1},corner);
	return status;
}

static struct box
expand(struct box b,int amount)
{
	return (struct box){b.x0-amount,b.y0-amount,b.x1+amount,b.y1+amount};
}

static bool
descender(uint8_t c)
{
	static const uint8_t low[256]={
	    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,
	    0,0,0,0,0,0,0,1,0,0,1,0,0,0,0,0,1,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,
	    1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,1,
	    0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	    0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0};
	return low[c]!=0;
}

static int
prepare_label(struct label *label,const struct st_rip3_command_host *host,
    const struct st_rip3_style *drawing,const struct st_rip3_button_style *style,
    const struct st_rip3_frame *frame,const uint8_t *text,size_t length,bool expanded)
{
	if (length==0) {label->length=0;return 0;}
	memset(label,0,sizeof(*label));
	if (host->text==NULL) return IO_ERROR_NOT_SUPPORTED;
	const uint8_t *glyphs;uint8_t *owned=NULL;size_t count;
	int status=expanded ? st_rip3_command_expanded_glyphs(host->text,text,length,&glyphs,&count,&owned)
	    : st_rip3_command_text_glyphs(host->text,text,length,&glyphs,&count,&owned);
	if (status!=0) return status;
	if (count>sizeof(label->bytes)) {free(owned);return IO_ERROR_OUT_OF_RANGE;}
	memcpy(label->bytes,glyphs,count);label->length=count;free(owned);
	bool drop=false;
	if ((style->flags&8192)!=0 && style->orientation!=1 && style->orientation!=3)
		for (size_t i=0;i<count;i++) drop|=descender(label->bytes[i]);
	label->default_outline = drawing->font_mode == ST_RIP3_FONT_CLASSIC && drawing->font.font == 0
	    && (host->text->font == NULL || host->text->font->number != 0);
	label->outline_label = label->default_outline || drawing->font_mode == ST_RIP3_FONT_OUTLINE;
	const struct st_rip3_text_position zero={0,0};
	if (!label->outline_label) {
		label->classic_style=drawing->font;
		/* Button orientation/justification and shadow own these attributes. */
		label->classic_style.flags = ST_RIP3_CLASSIC_LEFT | ST_RIP3_CLASSIC_TOP;
		if ((style->flags & 32) != 0)
			label->classic_style.flags |= ST_RIP3_CLASSIC_SHADOW;
		if (host->text->font!=NULL && host->text->font->number==drawing->font.font)
			label->classic=*host->text->font;
		else status=st_rip3_classic_font_builtin_device(&label->classic,frame->device_width,frame->device_height,drawing->font.font);
		struct st_rip3_classic_text_plan plan;
		if (status==0) status=st_rip3_classic_text_plan(&label->classic,&label->classic_style,label->bytes,count,&zero,&plan);
		if (status!=0) return status;
		label->ascent=plan.ascent;label->descent=plan.descent;
		double height=plan.ascent+(drop ? plan.descent : 0)+1;
		label->width=drawing->font.direction==0 ? plan.advance : height;
		label->height=drawing->font.direction==0 ? height : plan.advance;
		if (drawing->font.direction!=0) label->y=1-plan.advance;
		return 0;
	}
	struct st_rip3_outline_style font_style=drawing->outline;
	if (label->default_outline) {
		const struct st_rip3_classic_text_style classic={0,0,drawing->font.magnification,
		    ST_RIP3_CLASSIC_LEFT | ST_RIP3_CLASSIC_BASELINE};
		status=st_rip3_classic_default_outline(&label->outline,&label->layout,&classic,frame);
		font_style.string_rotation=0;
	}
	else {
		font_style.flags&=~UINT32_C(0x30);
		status=st_rip3_outline_layout_prepare(&label->layout,&font_style,frame);
		if (status==0)
			status=st_rip3_command_outline_font(&label->outline,host->text,&font_style,frame,label->layout.font_orientation);
	}
	if (status==0)
		status=st_rip3_outline_font_advances(label->advances,&label->outline,font_style.string_rotation,label->bytes,count);
	int32_t bbox=0;int64_t total;
	if (status==0 && label->layout.bounding_box_spacing) status=st_rip3_outline_font_bbox_width(&bbox,&label->outline);
	if (status==0) status=st_rip3_outline_layout_positions(&label->layout,&zero,label->advances,count,bbox,label->positions,&total);
	if (status!=0) return status;
	label->layout.shadow=(style->flags&32)!=0 ? (struct st_rip3_point){1,1} : (struct st_rip3_point){0,0};
	if (label->default_outline) {
		uint64_t scale=(uint64_t)label->outline.points*label->outline.dpi_y;
		label->ascent=(double)(scale*750/(72*1000));
		label->descent=drop ? (double)(scale*250/(72*1000)) : 0;
		label->width=(double)total;
		return 0;
	}
	double right=0,bottom=0;
	for (size_t i=0;i<count;i++) {
		double x0=label->positions[i].x+label->outline.bounds.xmin,x1=label->positions[i].x+label->outline.bounds.xmax;
		double y0=label->positions[i].y+label->outline.bounds.ymin,y1=label->positions[i].y+label->outline.bounds.ymax;
		if (!drop && label->layout.character_turns==0) y1=label->positions[i].y;
		if (i==0 || x0<label->x) label->x=x0;
		if (i==0 || y0<label->y) label->y=y0;
		if (i==0 || x1>right) right=x1;
		if (i==0 || y1>bottom) bottom=y1;
	}
	label->width=right-label->x;label->height=bottom-label->y;return 0;
}

static int
decorate_label(struct label *label,const struct st_rip3_command_host *host,
    const struct st_rip3_port_info *port,const struct st_rip3_button_style *style,
    struct st_rip3_text_position anchor,uint8_t hotkey)
{
	if (hotkey==0 || hotkey==255 || !(style->flags&1024)) return 0;
	if (hotkey>='a' && hotkey<='z') hotkey-='a'-'A';
	size_t index=label->length;
	for (size_t i=0;i<label->length;i++) {
		uint8_t c=label->bytes[i];if (c>='a' && c<='z') c-='a'-'A';
		if (c==hotkey) {index=i;break;}
	}
	if (index==label->length) return 0;
	uint8_t color=style->colors[ST_RIP3_BUTTON_UNDERLINE];
	struct st_rip3_point direction={1,0};double width=0,distance=1;
	int status=0;
	if (!label->outline_label) {
		struct st_rip3_classic_text_plan prefix,one;const struct st_rip3_text_position zero={0,0};
		struct st_rip3_classic_text_style font_style=label->classic_style;
		font_style.flags &= ~ST_RIP3_CLASSIC_SHADOW;
		status=st_rip3_classic_text_plan(&label->classic,&font_style,label->bytes,index,&zero,&prefix);
		if (status==0) status=st_rip3_classic_text_plan(&label->classic,&font_style,label->bytes+index,1,&zero,&one);
		if (status!=0) return status;
		if (font_style.direction!=0) direction=(struct st_rip3_point){0,-1};
		anchor.x+=direction.x*prefix.advance;anchor.y+=direction.y*prefix.advance;width=one.advance;
		if ((style->flags2&2)!=0) status=st_rip3_draw_classic_text(host->ports,&label->classic,&font_style,
		    label->bytes+index,1,&anchor,color,0,ST_RIP3_COPY);
		distance+=label->ascent+(descender(label->bytes[index]) ? label->descent : 0);
	}
	else {
		anchor=label->positions[index];
		struct st_rip3_outline_layout layout=label->layout;layout.shadow=(struct st_rip3_point){0,0};
		if ((style->flags2&2)!=0) status=st_rip3_draw_outline_positioned(host->ports,&label->outline,&layout,
		    label->bytes+index,1,&anchor,color,0,ST_RIP3_COPY);
		if (label->default_outline) {
			width=label->advances[index];
			distance=3;
		}
		else {
			static const struct st_rip3_point directions[]={{1,0},{0,-1},{-1,0},{0,1}};
			direction=directions[label->layout.character_turns];int32_t advance;
			if (status==0) status=st_rip3_outline_font_advances(&advance,&label->outline,
			    label->layout.character_turns*900,label->bytes+index,1);
			if (status!=0) return status;
			width=advance;
			if (descender(label->bytes[index])) {
				const struct st_rip3_outline_bounds *b=&label->outline.bounds;
				double drop=direction.x>0 ? (double)b->ymax : direction.x<0 ? -(double)b->ymin
				    : direction.y>0 ? -(double)b->xmin : (double)b->xmax;
				if (drop>0) distance+=drop;
			}
		}
	}
	if (status!=0 || !(style->flags&2048) || width<1) return status;
	double x=anchor.x-direction.y*distance,y=anchor.y+direction.x*distance;
	int64_t x0=(int64_t)floor(x+0.5),y0=(int64_t)floor(y+0.5);
	int64_t x1=(int64_t)floor(x+direction.x*(width-1)+0.5),y1=(int64_t)floor(y+direction.y*(width-1)+0.5);
	if (x1<x0) {int64_t swap=x0;x0=x1;x1=swap;}
	if (y1<y0) {int64_t swap=y0;y0=y1;y1=swap;}
	return fill(host,port,(struct box){x0,y0,x1+1,y1+1},color);
}

static struct st_rip3_text_position
default_label_anchor(const struct label *label,const struct st_rip3_port_info *port,
    const struct st_rip3_button_style *style,const struct st_rip3_frame *frame,struct box face)
{
	double ox=(double)port->bounds.x0+port->viewport.x0;
	double oy=(double)port->bounds.y0+port->viewport.y0;
	double left=trunc((face.x0+ox)*frame->world_width/frame->device_width);
	double right=trunc((face.x1+ox)*frame->world_width/frame->device_width);
	double top=trunc((face.y0+oy)*frame->world_height/frame->device_height);
	double bottom=trunc((face.y1+oy)*frame->world_height/frame->device_height);
	double face_width=trunc((double)(face.x1-face.x0)*frame->world_width/frame->device_width);
	double face_height=trunc((double)(face.y1-face.y0)*frame->world_height/frame->device_height);
	double width=trunc(label->width*frame->world_width/frame->device_width);
	double ascent=trunc(label->ascent*frame->world_height/frame->device_height);
	double descent=trunc(label->descent*frame->world_height/frame->device_height);
	double height=ascent+descent;
	double x=left+trunc((face_width-width)/2);
	double y=top+trunc((face_height-height)/2);
	if (style->orientation==1) x=left-width-8;
	else if (style->orientation==3) x=right+8;
	else if ((style->flags2&8)!=0) x=left+((style->flags&8)!=0 ? 20 : 10);
	else if ((style->flags2&16)!=0) x=right-width-((style->flags&8)!=0 ? 19 : 9);
	if (style->orientation==0) y=top-height-5;
	else if (style->orientation==4) y=bottom+5;
	return (struct st_rip3_text_position){trunc(x*frame->device_width/frame->world_width)-ox,
	    trunc(y*frame->device_height/frame->world_height)-oy
	    +trunc(ascent*frame->device_height/frame->world_height)};
}

static struct st_rip3_text_position
label_anchor(const struct label *label,const struct st_rip3_port_info *port,
    const struct st_rip3_button_style *style,const struct st_rip3_frame *frame,struct box face)
{
	if (label->default_outline)
		return default_label_anchor(label,port,style,frame,face);
	double x=((double)face.x0+face.x1-label->width)/2;
	double y=((double)face.y0+face.y1-label->height)/2;
	if (style->orientation==0) y=face.y0-label->height-1;
	else if (style->orientation==1) x=face.x0-label->width-1;
	else if (style->orientation==3) x=(double)face.x1+1;
	else if (style->orientation==4) y=(double)face.y1+1;
	if (style->orientation!=1 && style->orientation!=3) {
		double origin=(double)port->bounds.x0+port->viewport.x0;
		if ((style->flags2&8)!=0) {
			double world=floor((face.x0+origin)*frame->world_width/frame->device_width);
			x=floor((world+((style->flags&8)!=0 ? 20 : 10))*frame->device_width/frame->world_width)-origin;
		}
		else if ((style->flags2&16)!=0) {
			double world=floor((face.x1+origin)*frame->world_width/frame->device_width);
			double width=floor(label->width*frame->world_width/frame->device_width);
			x=floor((world-width-((style->flags&8)!=0 ? 19 : 9))*frame->device_width/frame->world_width)-origin;
		}
	}
	return (struct st_rip3_text_position){x-label->x,y-label->y};
}

static int
paint_label(struct label *label,const struct st_rip3_command_host *host,
    const struct st_rip3_port_info *port,const struct st_rip3_button_style *style,
    const struct st_rip3_frame *frame,struct box face,uint8_t hotkey)
{
	if (label->length==0) return 0;
	struct st_rip3_text_position anchor=label_anchor(label,port,style,frame,face);
	uint8_t foreground=style->colors[ST_RIP3_BUTTON_LABEL_FOREGROUND];
	uint8_t background=style->colors[ST_RIP3_BUTTON_LABEL_BACKGROUND];
	int status;
	if (!label->outline_label) status=st_rip3_draw_classic_text(host->ports,&label->classic,&label->classic_style,
	    label->bytes,label->length,&anchor,foreground,background,ST_RIP3_COPY);
	else {
		for (size_t i=0;i<label->length;i++) {label->positions[i].x+=anchor.x;label->positions[i].y+=anchor.y;}
		status=st_rip3_draw_outline_positioned(host->ports,&label->outline,&label->layout,label->bytes,label->length,
		    label->positions,foreground,background,ST_RIP3_COPY);
	}
	if (status==0) status=decorate_label(label,host,port,style,anchor,hotkey);
	return status;
}

static int
paint_effects(const struct st_rip3_command_host *host,const struct st_rip3_port_info *port,
    const struct st_rip3_button_style *style,struct box face)
{
	unsigned bevel=(style->flags&512)!=0 ? style->bevel&255u : 0;
	uint8_t bright=style->colors[ST_RIP3_BUTTON_BRIGHT],dark=style->colors[ST_RIP3_BUTTON_DARK];
	uint8_t corner=style->colors[ST_RIP3_BUTTON_CORNER];int status=0;
	for (unsigned i=bevel;status==0 && i>0;i--) status=edge(host,port,expand(face,(int)i),bright,dark,corner);
	if (status==0 && (style->flags&16)!=0) status=edge(host,port,expand(face,(int)bevel+1),0,0,0);
	if (status==0 && (style->flags&16)!=0) status=edge(host,port,expand(face,(int)bevel+2),dark,bright,corner);
	if (status==0 && (style->flags&32768)!=0) status=edge(host,port,expand(face,-1),dark,bright,corner);
	if (status==0 && (style->flags&8)!=0) {
		static const unsigned limits[]={12,25,40,75,150,200,250,300};
		static const unsigned insets[][2]={{1,1},{3,3},{4,4},{6,6},{7,5},{8,6},{10,7},{11,8},{13,9}};
		unsigned i=0;while (i<8 && (uint64_t)(face.y1-face.y0)>=limits[i]) i++;
		struct box chisel={face.x0+insets[i][0],face.y0+insets[i][1],face.x1-insets[i][0],face.y1-insets[i][1]};
		status=edge(host,port,chisel,dark,bright,corner);
	}
	return status;
}

/* A resident field and at most one renderer job share this logical descriptor.
 * No scene pixels are retained here. The field destructor invalidates it before
 * dropping its reference; a suspended job cannot observe a reused instance. */
struct button {
	struct st_rip3_command_host host;
	struct st_rip3_text_context text;
	struct st_rip3_frame frame;
	struct st_rip3_port_info port;
	struct st_rip3_style drawing;
	struct st_rip3_button_style style;
	struct box face,hit,baked;
	uint64_t generation;
	unsigned target,rgb_bits;
	size_t references,name_length,label_length,program_length;
	uint8_t name_source[4*127],label_source[4*255];
	char name[4*255+1];
	uint8_t hotkey;int identity;
	bool alive,mouse,default_enter,selected,auto_stamped,inversion_applied,label_chisel;
	bool label_expanded;
	uint8_t label_value[4*255+1];size_t label_value_length;
	/* Reuse prepared ICON resources for provider/file-free feedback undo.
	 * These are the existing decoded objects, never a new scene pixel plane. */
	struct st_rip3_bitmap *images[2];
	uint8_t program[];
};
enum render_stage { FILENAME, FILENAME_VALUE, FACE, LABEL, LABEL_VALUE, FINISH };
struct render_job {
	struct button *button;
	struct st_rip3_bitmap *bitmap;
	enum render_stage stage;
	uint64_t scene;
	bool creation,worker_owned,operation,prepare_undo,undo_selected;
};
struct st_rip3_button_renderer {
	const struct st_rip3_command_host *host;
	struct st_rip3_command_sink sink;
	struct st_rip3_graphical *worker;
	struct render_job *job;
	bool converted;
};

static void
release_button(struct button *button)
{
	if (--button->references==0) {st_rip3_bitmap_destroy(button->images[0]);st_rip3_bitmap_destroy(button->images[1]);free(button);}
}

static void clear_inversion(struct button *button);

static struct st_rip3_rect
button_screen_bounds(const struct button *button)
{
	int64_t x = (int64_t)button->port.bounds.x0 + button->port.viewport.x0;
	int64_t y = (int64_t)button->port.bounds.y0 + button->port.viewport.y0;
	return (struct st_rip3_rect){(int32_t)(x + button->hit.x0), (int32_t)(y + button->hit.y0),
	    (int32_t)(x + button->hit.x1), (int32_t)(y + button->hit.y1)};
}

static bool
temporary_inversion(struct button *button, bool enabled)
{
	if (!button->mouse || (button->style.flags & 16384) != 0 || (button->style.flags2 & 1) != 0)
		return false;
	const struct st_rip3_rect area = button_screen_bounds(button);
	return st_rip3_mouse_button_inversion(button->host.mouse, button, &area, enabled);
}

static void
remove_button(void *context)
{
	struct button *button=context;
	if (button->alive) {
		/* Capture cancellation has already removed temporary feedback. Deleting
		 * a definition must not erase persistently selected button pixels. */
		(void)temporary_inversion(button, false);
		button->alive=false;
		struct st_rip3_button_renderer *renderer=button->host.buttons;
		if (renderer!=NULL && renderer->job!=NULL && renderer->job->button==button)
			st_rip3_button_renderer_cancel(renderer);
	}
	release_button(button);
}

static bool
valid_job(const struct render_job *job)
{
	const struct button *button=job->button;
	return button->alive && st_rip3_ports_generation(button->host.ports,button->target)==button->generation
	    && st_rip3_mouse_scene(button->host.mouse)==job->scene;
}

static int
filename_value(struct button *button,const uint8_t *bytes,size_t length)
{
	size_t used=0;
	if (button->text.encoding==ST_RIP3_TEXT_CP437) {
		for (size_t i=0;i<length;i++) {
			uint32_t scalar=bytes[i]<0x80 ? bytes[i] : st_codepage_to_unicode(ST_CODEPAGE_CP437,bytes[i],false);
			ptrdiff_t count=st_utf8_encode_one(button->name+used,sizeof(button->name)-used-1,scalar);
			if (count<0) return (int)count;
			used+=(size_t)count;
		}
	}
	else {
		if (length>=sizeof(button->name)) return IO_ERROR_OUT_OF_RANGE;
		if (length!=0) memcpy(button->name,bytes,length);used=length;
	}
	button->name[used]=0;
	if (used==0) return (button->style.flags&128)!=0 ? IO_ERROR_INVALID_FORMAT : 0;
	return st_rip3_resource_name_valid(button->name) ? 0 : IO_ERROR_INVALID_FORMAT;
}

static bool
uses_hot_icon(const struct button *button)
{
	return button->mouse && (button->style.flags & (128u | 4096u)) == (128u | 4096u);
}

static int
load_icon(struct button *button,struct st_rip3_bitmap **bitmap)
{
	int status=IO_ERROR_NOT_FOUND;
	bool hot = button->selected && uses_hot_icon(button);
	if (hot) {
		char hot[4*255+5];size_t size=strlen(button->name);
		const char *extension=strrchr(button->name,'.');
		if (extension!=NULL) size=(size_t)(extension-button->name);
		memcpy(hot,button->name,size);memcpy(hot+size,".BMH",5);
		status=st_rip3_bitmap_load(bitmap,button->host.resources,hot);
		if (status==0) return 0;
	}
	status=st_rip3_bitmap_load(bitmap,button->host.resources,button->name);
	if (status!=0) button->host.bitmap_errors->show(button->host.bitmap_errors->context,button->name,button->target,NULL,false);
	else if (hot) button->style.flags=(button->style.flags&~4096u)|2u;
	return status;
}

static int
paint_icon(struct button *button,const struct st_rip3_bitmap *bitmap)
{
	struct st_rip3_palette palette;
	int status=st_rip3_palettes_current_palette(button->host.palettes,&palette);
	const struct st_rip3_bitmap_destination area={button->face.x0,button->face.y0,button->face.x1-1,button->face.y1-1};
	if (status==0) status=st_rip3_draw_bitmap(button->host.ports,bitmap,&area,&palette,ST_RIP3_BITMAP_MATCH,ST_RIP3_TRANSFER_COPY,NULL);
	return status;
}

static int
invert_button(void *context)
{
	struct button *button=context;
	if (temporary_inversion(button, !button->inversion_applied)) return 0;
	struct box area=button->hit;
	if (area.x0<button->port.clip.x0) area.x0=button->port.clip.x0;
	if (area.y0<button->port.clip.y0) area.y0=button->port.clip.y0;
	if (area.x1>(int64_t)button->port.clip.x1+1) area.x1=(int64_t)button->port.clip.x1+1;
	if (area.y1>(int64_t)button->port.clip.y1+1) area.y1=(int64_t)button->port.clip.y1+1;
	if (area.x0>=area.x1 || area.y0>=area.y1) return 0;
	struct st_rip3_palette palette;int status=st_rip3_palettes_current_palette(button->host.palettes,&palette);
	const struct st_rip3_rect clipped={(int32_t)area.x0,(int32_t)area.y0,(int32_t)(area.x1-1),(int32_t)(area.y1-1)};
	return status==0 ? st_rip3_ports_invert_rgb(button->host.ports,&clipped,&palette) : status;
}

static void
clear_inversion(struct button *button)
{
	if (!button->inversion_applied) return;
	if (!temporary_inversion(button, false) && button->alive
	    && st_rip3_ports_generation(button->host.ports,button->target)==button->generation)
		(void)st_rip3_ports_button_context(button->host.ports,button->target,button->generation,&button->port,invert_button,button);
	button->inversion_applied=false;
}

static void
abort_button(void *context)
{
	struct button *button=context;clear_inversion(button);button->selected=false;
	struct st_rip3_button_renderer *renderer=button->host.buttons;
	if (renderer!=NULL && renderer->job!=NULL && renderer->job->button==button)
		st_rip3_button_renderer_cancel(renderer);
}

static int
paint_face(void *context)
{
	struct render_job *job=context;struct button *b=job->button;
	const struct st_rip3_command_host *host=&b->host;
	struct st_rip3_button_style *style=&b->style;
	int status=0;
	if (b->inversion_applied) {status=invert_button(b);b->inversion_applied=false;if (status!=0) return status;}
	if ((style->flags&128)!=0) status=paint_icon(b,job->bitmap);
	else if ((style->flags&1)!=0) {
		if (host->clipboard==NULL || !host->clipboard->set) return IO_ERROR_NOT_FOUND;
		struct box image=b->auto_stamped ? b->baked : b->face;
		if (image.x0<INT32_MIN || image.y0<INT32_MIN || image.x1>INT32_MAX || image.y1>INT32_MAX)
			return IO_ERROR_OUT_OF_RANGE;
		const struct st_rip3_rect face={(int32_t)image.x0,(int32_t)image.y0,(int32_t)image.x1,(int32_t)image.y1};
		status=st_rip3_ports_button_clipboard(host->ports,host->clipboard->port,&face);
	}
	else if ((style->flags2&32)==0) {
		if ((style->flags2&64)==0) status=fill(host,&b->port,b->face,style->colors[ST_RIP3_BUTTON_SURFACE]);
		else {
			struct box area=b->face;
			if (area.x0<b->port.clip.x0) area.x0=b->port.clip.x0;
			if (area.y0<b->port.clip.y0) area.y0=b->port.clip.y0;
			if (area.x1>(int64_t)b->port.clip.x1+1) area.x1=(int64_t)b->port.clip.x1+1;
			if (area.y1>(int64_t)b->port.clip.y1+1) area.y1=(int64_t)b->port.clip.y1+1;
			uint8_t foreground,background;
			status=st_rip3_command_ink(host,b->rgb_bits,b->drawing.fill_color,&foreground);
			if (status==0) status=st_rip3_command_ink(host,b->rgb_bits,b->drawing.background_color,&background);
			if (status==0 && area.x0<area.x1 && area.y0<area.y1) {
				const struct st_rip3_rect rect={(int32_t)area.x0,(int32_t)area.y0,(int32_t)(area.x1-1),(int32_t)(area.y1-1)};
				struct st_rip3_fill pattern;st_rip3_command_fill(&b->port,&b->drawing.fill,&pattern);
				status=st_rip3_draw_bar(host->ports,&rect,&pattern,foreground,background,ST_RIP3_COPY);
			}
		}
	}
	if (status==0) status=paint_effects(host,&b->port,style,b->face);
	return status;
}

static int
capture_face(void *context)
{
	struct button *b=context;
	if (b->host.clipboard->set && b->host.clipboard->port==b->target) return 0;
	unsigned bevel=(b->style.flags&512)!=0 ? b->style.bevel&255u : 0;
	struct box image=expand(b->face,(int)bevel);
	const struct st_rip3_clipboard_area area={image.x0,image.y0,image.x1,image.y1};
	int status=st_rip3_clipboard_capture_button(b->host.ports,b->host.clipboard,&area,*b->host.offscreen_pixel_budget);
	if (status==0) status=st_rip3_button_styles_autoclip(b->host.button_styles,b->style.flags,&b->style.flags);
	if (status==0) {b->auto_stamped=true;b->baked=image;}
	return status;
}

struct label_paint {struct button *button;const uint8_t *bytes;size_t length;bool expanded;};
static int
render_label(void *context)
{
	struct label_paint *paint=context;struct button *b=paint->button;struct label label;
	int status=prepare_label(&label,&b->host,&b->drawing,&b->style,&b->frame,paint->bytes,paint->length,paint->expanded);
	struct st_rip3_button_style layout=b->style;
	if (b->label_chisel) layout.flags|=8;
	if (status==0) status=paint_label(&label,&b->host,&b->port,&layout,&b->frame,b->face,b->hotkey);
	if (status==0 && paint->length<=sizeof(b->label_value)) {
		if (paint->length!=0 && paint->bytes!=b->label_value) memcpy(b->label_value,paint->bytes,paint->length);
		b->label_value_length=paint->length;b->label_expanded=paint->expanded;
	}
	return status;
}

static void
hit_bounds(struct button *b)
{
	b->hit=expand(b->face,(b->style.flags&512)!=0 ? (int)(b->style.bevel&255u) : 0);
	if (b->hit.x0<b->port.clip.x0) b->hit.x0=b->port.clip.x0;
	if (b->hit.y0<b->port.clip.y0) b->hit.y0=b->port.clip.y0;
	if (b->hit.x1>(int64_t)b->port.clip.x1+1) b->hit.x1=(int64_t)b->port.clip.x1+1;
	if (b->hit.y1>(int64_t)b->port.clip.y1+1) b->hit.y1=(int64_t)b->port.clip.y1+1;
}

static int
prepare_button(const struct st_rip3_command_host *host,const uint8_t *command,size_t length,
    bool converted,struct button **result)
{
	*result=NULL;
	if (host->button_styles==NULL || host->styles==NULL || host->ports==NULL) return IO_ERROR_NOT_SUPPORTED;
	struct st_rip3_environment env;unsigned env_number,number;
	int status=st_rip3_command_environment(host,&env_number,&env);if (status!=0) return status;
	struct st_rip3_fields fields = {command+2,length-2};
	uint64_t values[6];
	for (unsigned i=0;i<6;i++) {
		size_t width=i<4 ? env.coordinate_size : i==4 ? 2 : 1;
		status=st_rip3_command_next_field(&fields,values+i,width,env.base_math,false);
		if (status!=0) return status;
	}
	status=st_rip3_command_next_field(&fields,NULL,1,env.base_math,false);
	if (status!=0) return status;
	if (values[4]>255) return IO_ERROR_OUT_OF_RANGE;
	struct st_rip3_button_style style;status=st_rip3_button_styles_current_value(host->button_styles,&style);if (status!=0) return status;
	bool mouse=(style.flags&1024)!=0,is_icon=(style.flags&128)!=0,clipboard=!is_icon && (style.flags&1)!=0;
	bool radio=(style.flags&16384)!=0,checkbox=(style.flags2&1)!=0;
	if (radio && checkbox) return IO_ERROR_CONFLICT;
	enum st_rip3_group_kind group_kind=radio ? ST_RIP3_GROUP_RADIO : checkbox ? ST_RIP3_GROUP_CHECKBOX : ST_RIP3_GROUP_NONE;
	if (mouse && group_kind!=ST_RIP3_GROUP_NONE) {
		status=st_rip3_mouse_group_check(host->mouse,style.group,group_kind);if (status!=0) return status;
	}
	if (mouse && host->mouse==NULL) return IO_ERROR_NOT_SUPPORTED;
	if (mouse && st_rip3_mouse_full(host->mouse)) return IO_ERROR_OUT_OF_RANGE;
	if ((style.flags&64)!=0 && (host->clipboard==NULL || host->offscreen_pixel_budget==NULL)) return IO_ERROR_NOT_SUPPORTED;
	if (is_icon && (host->resources==NULL || host->palettes==NULL || host->bitmap_errors==NULL || host->bitmap_errors->show==NULL))
		return IO_ERROR_NOT_SUPPORTED;
	struct st_rip3_port_info source={0};
	if ((style.flags&1)!=0) {
		if (host->clipboard==NULL || !host->clipboard->set) return IO_ERROR_NOT_FOUND;
		status=st_rip3_ports_info(host->ports,host->clipboard->port,&source);if (status!=0) return status;
		if (!source.defined || source.viewport.x1<source.viewport.x0 || source.viewport.y1<source.viewport.y0
		    || source.clip.x1<source.clip.x0 || source.clip.y1<source.clip.y0) return IO_ERROR_NOT_FOUND;
	}
	struct st_rip3_button_payload payload;
	status=st_rip3_button_payload_parse_encoded(&payload,fields.bytes,fields.remaining,converted);if (status!=0) return status;
	if (is_icon && payload.icon.length==0) return IO_ERROR_INVALID_FORMAT;
	if (!is_icon && !clipboard && !mouse && payload.label.length==0 && !(style.width!=0 && style.height!=0) && values[2]==0 && values[3]==0) return 0;
	struct st_rip3_frame frame;status=st_rip3_environments_frame(host->environments,env_number,&frame);if (status!=0) return status;
	unsigned target;status=st_rip3_ports_current(host->ports,&target);if (status!=0) return status;if (mouse) target=0;
	struct st_rip3_port_info port;status=st_rip3_ports_info(host->ports,target,&port);if (status!=0 || !port.viewport_active) return status;
	if (port.clip.x1<port.clip.x0 || port.clip.y1<port.clip.y0) return 0;
	if (!is_icon && !clipboard && !(style.width!=0 && style.height!=0)) {
		if (values[2]==0 && values[3]==0) return 0;
		if (values[0]>values[2]) {uint64_t swap=values[0];values[0]=values[2];values[2]=swap;}
		if (values[1]>values[3]) {uint64_t swap=values[1];values[1]=values[3];values[3]=swap;}
	}
	struct box face={(int64_t)(values[0]*frame.device_width/frame.world_width),(int64_t)(values[1]*frame.device_height/frame.world_height),
	    (int64_t)(values[2]*frame.device_width/frame.world_width),(int64_t)(values[3]*frame.device_height/frame.world_height)};
	if (clipboard) {face.x1=face.x0+(int64_t)source.viewport.x1-source.viewport.x0+1;face.y1=face.y0+(int64_t)source.viewport.y1-source.viewport.y0+1;}
	else if (!is_icon && style.width!=0 && style.height!=0) {face.x1=face.x0+style.width;face.y1=face.y0+style.height;}
	if (!is_icon && (face.x1<=face.x0 || face.y1<=face.y0)) return 0;
	struct st_rip3_style drawing;status=st_rip3_styles_current(host->styles,&number);
	if (status==0) status=st_rip3_styles_get(host->styles,number,&drawing);if (status!=0) return status;
	if (payload.icon.length>sizeof(((struct button *)0)->name_source) || payload.label.length>sizeof(((struct button *)0)->label_source)
	    || payload.host.length>SIZE_MAX-sizeof(struct button)) return IO_ERROR_OUT_OF_RANGE;
	struct button *b=calloc(1,sizeof(*b)+payload.host.length);if (b==NULL) return IO_ERROR_OUT_OF_MEMORY;
	b->host=*host;if (host->text!=NULL) b->text=*host->text;
	b->host.text=host->text!=NULL ? &b->text : NULL;
	b->frame=frame;b->port=port;b->drawing=drawing;b->style=style;b->face=face;b->label_chisel=(style.flags&8)!=0;
	b->target=target;b->generation=st_rip3_ports_generation(host->ports,target);b->rgb_bits=env.rgb_bits;
	b->references=1;b->alive=true;b->mouse=mouse;b->selected=mouse && group_kind!=ST_RIP3_GROUP_NONE && (values[5]&1)!=0;
	b->hotkey=(uint8_t)values[4];b->default_enter=(values[5]&2)!=0;b->identity=payload.identity;
	b->name_length=payload.icon.length;b->label_length=payload.label.length;b->program_length=payload.host.length;
	if (payload.icon.length!=0) memcpy(b->name_source,payload.icon.data,payload.icon.length);
	if (payload.label.length!=0) memcpy(b->label_source,payload.label.data,payload.label.length);
	if (payload.host.length!=0) memcpy(b->program,payload.host.data,payload.host.length);
	hit_bounds(b);
	if (!is_icon && mouse && (b->hit.x0>=b->hit.x1 || b->hit.y0>=b->hit.y1)) {release_button(b);return 0;}
	*result=b;return 0;
}

static int select_button(void *context,bool selected);
static int restore_button(void *context,bool selected);
static void commit_button(void *context,bool selected);
static int copy_button(const void *context,void **result,struct st_rip3_mouse *owner);

static int
finish_button(struct render_job *job)
{
	struct button *b=job->button;
	if (b->selected && (b->style.flags&2)!=0) {
		int status=st_rip3_ports_button_context(b->host.ports,b->target,b->generation,&b->port,invert_button,b);
		if (status!=0) return status;
		b->inversion_applied=true;
	}
	if (!job->creation || !b->mouse) return 0;
	if (st_rip3_mouse_full(b->host.mouse)) return IO_ERROR_OUT_OF_RANGE;
	const struct st_rip3_rect area = button_screen_bounds(b);
	const struct st_rip3_mouse_binding binding={.hotkey=b->hotkey,.default_enter=b->default_enter,.group=b->style.group,
	    .group_kind=(b->style.flags&16384)!=0 ? ST_RIP3_GROUP_RADIO : (b->style.flags2&1)!=0 ? ST_RIP3_GROUP_CHECKBOX : ST_RIP3_GROUP_NONE,
	    .selected=b->selected};
	const struct st_rip3_mouse_visual visual={.context=b,.select=select_button,.destroy=remove_button,.abort=abort_button,
	    .restore=restore_button,.committed=commit_button,.copy=copy_button};
	bool redraw = binding.group_kind != ST_RIP3_GROUP_NONE || (b->style.flags & 2) != 0 || uses_hot_icon(b);
	if (redraw) b->references++;
	int status=st_rip3_mouse_define(b->host.mouse,ST_RIP3_MOUSE_BUTTON,&binding,&area,(b->style.flags&2)!=0,(b->style.flags&4)!=0,
	    b->identity,b->program,b->program_length,redraw ? &visual : NULL);
	if (status!=0 && redraw) release_button(b);
	return status;
}

static int
face_stage(struct render_job *job)
{
	struct button *b=job->button;int status=0;
	if ((b->style.flags&128)!=0) {
		job->operation=true;status=load_icon(b,&job->bitmap);if (status!=0) return status;
		if (job->creation) {
			uint64_t width=(uint64_t)job->bitmap->pixels->width*b->frame.device_width/b->frame.world_width;
			uint64_t height=(uint64_t)job->bitmap->pixels->height*b->frame.device_height/b->frame.world_height;
			if (width==0 || height==0) return IO_ERROR_OUT_OF_RANGE;
			b->face.x1=b->face.x0+(int64_t)width;b->face.y1=b->face.y0+(int64_t)height;hit_bounds(b);
			if (b->mouse && (b->hit.x0>=b->hit.x1 || b->hit.y0>=b->hit.y1)) {job->stage=FINISH;return 0;}
		}
	}
	if (b->mouse && job->creation && st_rip3_mouse_full(b->host.mouse)) return IO_ERROR_OUT_OF_RANGE;
	if (job->prepare_undo && uses_hot_icon(b) && b->images[job->undo_selected ? 1 : 0]==NULL) {
		/* Restored definitions have no feedback cache. Prepare the committed
		 * ICON state during this actual preview, with the same resolved name,
		 * so later cancellation runs no providers or file operations. */
		struct st_rip3_bitmap *undo=NULL;bool selected=b->selected;b->selected=job->undo_selected;
		status=load_icon(b,&undo);b->selected=selected;
		if (status!=0) return status;
		if (uses_hot_icon(b)) b->images[job->undo_selected ? 1 : 0]=undo;
		else st_rip3_bitmap_destroy(undo);
	}
	job->operation=true;
	status=st_rip3_ports_button_context(b->host.ports,b->target,b->generation,&b->port,paint_face,job);
	if (status==0 && job->bitmap!=NULL && uses_hot_icon(b)) {
		unsigned state=b->selected ? 1 : 0;st_rip3_bitmap_destroy(b->images[state]);
		b->images[state]=job->bitmap;job->bitmap=NULL;
	}
	if (!uses_hot_icon(b)) {
		st_rip3_bitmap_destroy(b->images[0]);st_rip3_bitmap_destroy(b->images[1]);b->images[0]=b->images[1]=NULL;
	}
	st_rip3_bitmap_destroy(job->bitmap);job->bitmap=NULL;
	if (status!=0) return status;
	b->style.flags&=~16u; /* Initial recess is not resident feedback. */
	if (job->creation && (b->style.flags&64)!=0) {
		status=st_rip3_ports_button_context(b->host.ports,b->target,b->generation,&b->port,capture_face,b);
	}
	return status;
}

static int
step_job(struct render_job *job,struct st_rip3_graphical *worker)
{
	struct button *b=job->button;int status=0;
	if (job->stage==FILENAME) {
		if (worker!=NULL && b->name_length!=0) {
			st_rip3_graphical_start(worker,ST_RIP3_GRAPHICAL_FILENAME,b->name_source,b->name_length);
			job->worker_owned=true;job->stage=FILENAME_VALUE;
		}
		else {status=filename_value(b,b->name_source,b->name_length);job->stage=FACE;}
	}
	else if (job->stage==FILENAME_VALUE || job->stage==LABEL_VALUE) {
		st_rip3_graphical_process(worker,1);
		if (st_rip3_graphical_pending(worker)) return 0;
		const struct st_rip3_condition_bytes *value=st_rip3_graphical_result(worker);
		if (value==NULL) return IO_ERROR_CANCELLED;
		if (job->stage==FILENAME_VALUE) {status=filename_value(b,value->bytes,value->length);job->stage=FACE;}
		else {
			struct label_paint paint={b,value->bytes,value->length,true};
			status=st_rip3_ports_button_context(b->host.ports,b->target,b->generation,&b->port,render_label,&paint);
			job->stage=FINISH;
		}
		st_rip3_graphical_cancel(worker);job->worker_owned=false;
	}
	else if (job->stage==FACE) {status=face_stage(job);if (job->stage!=FINISH) job->stage=LABEL;}
	else if (job->stage==LABEL) {
		if (worker!=NULL && b->label_length!=0) {
			st_rip3_graphical_start(worker,ST_RIP3_GRAPHICAL_LABEL,b->label_source,b->label_length);
			job->worker_owned=true;job->stage=LABEL_VALUE;
		}
		else {
			struct label_paint paint={b,b->label_source,b->label_length,worker!=NULL};
			status=st_rip3_ports_button_context(b->host.ports,b->target,b->generation,&b->port,render_label,&paint);
			job->stage=FINISH;
		}
	}
	return status;
}

static int
run_synchronous(struct button *b,bool creation,bool previous,bool *operation)
{
	struct render_job job={.button=b,.creation=creation,.scene=st_rip3_mouse_scene(b->host.mouse),
	    .prepare_undo=!creation && previous!=b->selected,.undo_selected=previous};
	int status=0;
	while (status==0 && job.stage!=FINISH && valid_job(&job)) status=step_job(&job,NULL);
	if (status==0 && valid_job(&job) && (!creation || !b->mouse || (b->hit.x0<b->hit.x1 && b->hit.y0<b->hit.y1))) status=finish_button(&job);
	if (operation!=NULL) *operation=job.operation;
	st_rip3_bitmap_destroy(job.bitmap);return status;
}

static int
select_button(void *context,bool selected)
{
	struct button *b=context;bool previous=b->selected;b->selected=selected;
	struct st_rip3_button_renderer *renderer=b->host.buttons;
	if (renderer==NULL) return run_synchronous(b,false,previous,NULL);
	if (renderer->job!=NULL && renderer->job->button==b) st_rip3_button_renderer_cancel(renderer);
	if (renderer->job!=NULL || st_rip3_graphical_pending(renderer->worker)) return IO_ERROR_BUSY;
	struct render_job *job=calloc(1,sizeof(*job));if (job==NULL) return IO_ERROR_OUT_OF_MEMORY;
	b->references++;*job=(struct render_job){.button=b,.scene=st_rip3_mouse_scene(b->host.mouse),
	    .prepare_undo=previous!=selected,.undo_selected=previous};
	renderer->job=job;return 0;
}

static void
commit_button(void *context,bool selected)
{
	((struct button *)context)->selected=selected;
}

static int
restore_button(void *context,bool selected)
{
	struct button *b=context;struct st_rip3_button_renderer *renderer=b->host.buttons;
	if (renderer!=NULL && renderer->job!=NULL && renderer->job->button==b) st_rip3_button_renderer_cancel(renderer);
	if (!b->alive || st_rip3_ports_generation(b->host.ports,b->target)!=b->generation) {
		clear_inversion(b);
		return 0;
	}
	b->selected=selected;
	if (!uses_hot_icon(b)) {
		/* Plain/clipboard and ICON fallback feedback is only the existing
		 * RGB complement. Never paste/read files or run providers to undo it. */
		bool desired=selected && (b->style.flags&2)!=0;
		if (desired==b->inversion_applied) return 0;
		int status=st_rip3_ports_button_context(b->host.ports,b->target,b->generation,&b->port,invert_button,b);
		if (status==0) b->inversion_applied=desired;return status;
	}
	struct st_rip3_bitmap *image=b->images[selected ? 1 : 0];
	if (image==NULL) return 0; /* A failed preview never replaces prepared art. */
	struct render_job job={.button=b,.bitmap=image,.scene=st_rip3_mouse_scene(b->host.mouse)};
	int status=st_rip3_ports_button_context(b->host.ports,b->target,b->generation,&b->port,paint_face,&job);
	struct label_paint paint={b,b->label_value,b->label_value_length,b->label_expanded};
	if (status==0) status=st_rip3_ports_button_context(b->host.ports,b->target,b->generation,&b->port,render_label,&paint);
	if (status==0) status=finish_button(&job);return status;
}

static int
copy_button(const void *context,void **result,struct st_rip3_mouse *owner)
{
	const struct button *source=context;
	struct button *copy=malloc(sizeof(*copy)+source->program_length);if (copy==NULL) return IO_ERROR_OUT_OF_MEMORY;
	memcpy(copy,source,sizeof(*copy)+source->program_length);
	copy->references=1;copy->alive=owner!=NULL;copy->selected=false;copy->inversion_applied=false;
	copy->host.text=source->host.text!=NULL ? &copy->text : NULL;
	if (owner!=NULL) {
		copy->host.mouse=owner;
		copy->generation=st_rip3_ports_generation(copy->host.ports,copy->target);
	}
	/* Logical configuration/source survives; held-preview resources do not. */
	copy->images[0]=copy->images[1]=NULL;copy->name[0]=0;
	memset(copy->label_value,0,sizeof(copy->label_value));copy->label_value_length=0;copy->label_expanded=false;
	*result=copy;return 0;
}

int
st_rip3_button_renderer_create(struct st_rip3_button_renderer **result,const struct st_rip3_command_host *host,
    const struct st_rip3_command_sink *sink,struct st_rip3_graphical *worker,bool converted_codepage)
{
	if (result==NULL || host==NULL || sink==NULL || worker==NULL) return IO_ERROR_INVALID_ARGUMENT;
	struct st_rip3_button_renderer *renderer=calloc(1,sizeof(*renderer));if (renderer==NULL) return IO_ERROR_OUT_OF_MEMORY;
	renderer->host=host;renderer->sink=*sink;renderer->worker=worker;renderer->converted=converted_codepage;
	*result=renderer;return 0;
}

static void
discard_job(struct st_rip3_button_renderer *renderer,bool feedback)
{
	if (renderer==NULL || renderer->job==NULL) return;
	struct render_job *job=renderer->job;renderer->job=NULL;
	if (feedback) clear_inversion(job->button);
	if (job->worker_owned) st_rip3_graphical_cancel(renderer->worker);
	st_rip3_bitmap_destroy(job->bitmap);release_button(job->button);free(job);
}

void
st_rip3_button_renderer_cancel(struct st_rip3_button_renderer *renderer)
{
	discard_job(renderer,true);
}

void
st_rip3_button_renderer_destroy(struct st_rip3_button_renderer *renderer)
{
	st_rip3_button_renderer_cancel(renderer);free(renderer);
}

bool
st_rip3_button_renderer_pending(const struct st_rip3_button_renderer *renderer)
{
	return renderer!=NULL && renderer->job!=NULL;
}

void
st_rip3_button_renderer_start(struct st_rip3_button_renderer *renderer,const uint8_t *command,size_t length)
{
	if (renderer==NULL) return;
	if (renderer->job!=NULL || st_rip3_graphical_pending(renderer->worker)) {
		st_rip3_command_result(&renderer->sink,IO_ERROR_BUSY,false);return;
	}
	struct button *b=NULL;int status=prepare_button(renderer->host,command,length,renderer->converted,&b);
	if (status==0 && b!=NULL) {
		struct render_job *job=calloc(1,sizeof(*job));
		if (job==NULL) {release_button(b);status=IO_ERROR_OUT_OF_MEMORY;}
		else {*job=(struct render_job){.button=b,.creation=true,.scene=st_rip3_mouse_scene(b->host.mouse)};renderer->job=job;}
	}
	st_rip3_command_result(&renderer->sink,status,false);
}

void
st_rip3_button_renderer_process(struct st_rip3_button_renderer *renderer,size_t budget)
{
	if (renderer==NULL) return;
	for (size_t i=0;i<budget && renderer->job!=NULL;i++) {
		struct render_job *job=renderer->job;
		if (!valid_job(job)) {st_rip3_button_renderer_cancel(renderer);return;}
		int status=step_job(job,renderer->worker);
		if (status==0 && !valid_job(job)) {st_rip3_button_renderer_cancel(renderer);return;}
		if (status==0 && job->stage==FINISH) {
			struct button *b=job->button;
			if (!job->creation || !b->mouse || (b->hit.x0<b->hit.x1 && b->hit.y0<b->hit.y1)) status=finish_button(job);
			st_rip3_command_result(&renderer->sink,status,job->operation);discard_job(renderer,status!=0);return;
		}
		if (status!=0) {
			/* The worker has already consumed/diagnosed a hard expansion abort. */
			if (status!=IO_ERROR_CANCELLED) st_rip3_command_result(&renderer->sink,status,job->operation);
			st_rip3_button_renderer_cancel(renderer);return;
		}
		if (st_rip3_graphical_state(renderer->worker)==ST_RIP3_GRAPHICAL_PROMPT) return;
	}
}

int
st_rip3_button_create_execute(const struct st_rip3_command_host *host,const uint8_t *command,size_t length,bool *operation)
{
	if (host->buttons!=NULL) {st_rip3_button_renderer_start(host->buttons,command,length);*operation=true;return 0;}
	struct button *b=NULL;int status=prepare_button(host,command,length,false,&b);
	if (status==0 && b!=NULL) {status=run_synchronous(b,true,b->selected,operation);release_button(b);}
	return status;
}
