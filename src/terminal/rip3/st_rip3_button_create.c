/* Connected plain/clipboard construction. Statuses stay below the parser. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_command_internal.h"
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
	bool extended;
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
    const struct st_rip3_frame *frame,const uint8_t *text,size_t length)
{
	if (length==0) {label->length=0;return 0;}
	memset(label,0,sizeof(*label));
	if (host->text==NULL) return IO_ERROR_NOT_SUPPORTED;
	const uint8_t *glyphs;uint8_t *owned=NULL;size_t count;
	int status=st_rip3_command_text_glyphs(host->text,text,length,&glyphs,&count,&owned);
	if (status!=0) return status;
	if (count>sizeof(label->bytes)) {free(owned);return IO_ERROR_OUT_OF_RANGE;}
	memcpy(label->bytes,glyphs,count);label->length=count;free(owned);
	bool drop=false;
	if ((style->flags&8192)!=0 && style->orientation!=1 && style->orientation!=3)
		for (size_t i=0;i<count;i++) drop|=descender(label->bytes[i]);
	label->extended=drawing->font_mode==ST_RIP3_FONT_OUTLINE;
	const struct st_rip3_text_position zero={0,0};
	if (!label->extended) {
		label->classic_style=drawing->font;
		/* Button orientation/justification and shadow own these attributes. */
		label->classic_style.flags=(style->flags&32)!=0 ? 32 : 0;
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
	font_style.flags&=~UINT32_C(0x30);
	status=st_rip3_outline_layout_prepare(&label->layout,&font_style,frame);
	if (status==0) status=st_rip3_command_outline_font(&label->outline,host->text,&font_style,frame,label->layout.font_orientation);
	if (status==0) status=st_rip3_outline_font_advances(label->advances,&label->outline,font_style.string_rotation,label->bytes,count);
	int32_t bbox=0;int64_t total;
	if (status==0 && label->layout.bounding_box_spacing) status=st_rip3_outline_font_bbox_width(&bbox,&label->outline);
	if (status==0) status=st_rip3_outline_layout_positions(&label->layout,&zero,label->advances,count,bbox,label->positions,&total);
	if (status!=0) return status;
	label->layout.shadow=(style->flags&32)!=0 ? (struct st_rip3_point){1,1} : (struct st_rip3_point){0,0};
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
	if (!label->extended) {
		struct st_rip3_classic_text_plan prefix,one;const struct st_rip3_text_position zero={0,0};
		struct st_rip3_classic_text_style font_style=label->classic_style;font_style.flags=0;
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
	if (status!=0 || !(style->flags&2048) || width<1) return status;
	double x=anchor.x-direction.y*distance,y=anchor.y+direction.x*distance;
	int64_t x0=(int64_t)floor(x+0.5),y0=(int64_t)floor(y+0.5);
	int64_t x1=(int64_t)floor(x+direction.x*(width-1)+0.5),y1=(int64_t)floor(y+direction.y*(width-1)+0.5);
	if (x1<x0) {int64_t swap=x0;x0=x1;x1=swap;}
	if (y1<y0) {int64_t swap=y0;y0=y1;y1=swap;}
	return fill(host,port,(struct box){x0,y0,x1+1,y1+1},color);
}

static int
paint_label(struct label *label,const struct st_rip3_command_host *host,
    const struct st_rip3_port_info *port,const struct st_rip3_button_style *style,
    const struct st_rip3_frame *frame,struct box face,uint8_t hotkey)
{
	if (label->length==0) return 0;
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
	struct st_rip3_text_position anchor={x-label->x,y-label->y};
	uint8_t foreground=style->colors[ST_RIP3_BUTTON_LABEL_FOREGROUND];
	uint8_t background=style->colors[ST_RIP3_BUTTON_LABEL_BACKGROUND];
	int status;
	if (!label->extended) status=st_rip3_draw_classic_text(host->ports,&label->classic,&label->classic_style,
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

struct icon {
	struct st_rip3_command_host host;
	struct st_rip3_frame frame;
	struct st_rip3_port_info port;
	struct st_rip3_style drawing;
	struct st_rip3_button_style style;
	struct box face;
	char name[128];
	uint8_t label[255],hotkey;size_t label_length;
	bool selected;
};

static int
icon_load(struct icon *icon,struct st_rip3_bitmap **bitmap,unsigned port)
{
	int status=IO_ERROR_NOT_FOUND;
	if (icon->selected && (icon->style.flags&4096)!=0) {
		char hot[132];size_t size=strlen(icon->name);
		const char *extension=strrchr(icon->name,'.');
		if (extension!=NULL) size=(size_t)(extension-icon->name);
		memcpy(hot,icon->name,size);memcpy(hot+size,".BMH",5);
		status=st_rip3_bitmap_load(bitmap,icon->host.resources,hot);
		if (status==0) return 0;
	}
	status=st_rip3_bitmap_load(bitmap,icon->host.resources,icon->name);
	if (status!=0) icon->host.bitmap_errors->show(icon->host.bitmap_errors->context,icon->name,port,NULL);
	else if (icon->selected && (icon->style.flags&4096)!=0) icon->style.flags=(icon->style.flags&~4096u)|2u;
	return status;
}

static int
paint_icon(struct icon *icon,const struct st_rip3_bitmap *bitmap)
{
	struct st_rip3_palette palette;
	int status=st_rip3_palettes_current_palette(icon->host.palettes,&palette);
	const struct st_rip3_bitmap_destination area={icon->face.x0,icon->face.y0,icon->face.x1-1,icon->face.y1-1};
	if (status==0) status=st_rip3_draw_bitmap(icon->host.ports,bitmap,&area,&palette,ST_RIP3_BITMAP_MATCH,ST_RIP3_TRANSFER_COPY,NULL);
	return status;
}

static int
invert_icon(struct icon *icon)
{
	struct box area=expand(icon->face,(icon->style.flags&512)!=0 ? (int)(icon->style.bevel&255u) : 0);
	if (area.x0<icon->port.clip.x0) area.x0=icon->port.clip.x0;
	if (area.y0<icon->port.clip.y0) area.y0=icon->port.clip.y0;
	if (area.x1>(int64_t)icon->port.clip.x1+1) area.x1=(int64_t)icon->port.clip.x1+1;
	if (area.y1>(int64_t)icon->port.clip.y1+1) area.y1=(int64_t)icon->port.clip.y1+1;
	if (area.x0>=area.x1 || area.y0>=area.y1) return 0;
	struct st_rip3_palette palette;int status=st_rip3_palettes_current_palette(icon->host.palettes,&palette);
	const struct st_rip3_rect clipped={(int32_t)area.x0,(int32_t)area.y0,(int32_t)(area.x1-1),(int32_t)(area.y1-1)};
	return status==0 ? st_rip3_ports_invert_rgb(icon->host.ports,&clipped,&palette) : status;
}

static int
redraw_icon(void *context)
{
	struct icon *icon=context;struct st_rip3_bitmap *bitmap=NULL;
	int status=icon_load(icon,&bitmap,0);
	if (status==0) status=paint_icon(icon,bitmap);
	st_rip3_bitmap_destroy(bitmap);
	if (status==0) status=paint_effects(&icon->host,&icon->port,&icon->style,icon->face);
	if (status==0) {
		struct label label;
		status=prepare_label(&label,&icon->host,&icon->drawing,&icon->style,&icon->frame,icon->label,icon->label_length);
		if (status==0) status=paint_label(&label,&icon->host,&icon->port,&icon->style,&icon->frame,icon->face,icon->hotkey);
	}
	if (status==0 && icon->selected && (icon->style.flags&2)!=0) status=invert_icon(icon);
	return status;
}

static int
select_icon(void *context,bool selected)
{
	struct icon *icon=context;icon->selected=selected;
	return st_rip3_ports_button_view(icon->host.ports,&icon->port,redraw_icon,icon);
}

int
st_rip3_button_create_execute(const struct st_rip3_command_host *host,
    const uint8_t *command,size_t length,bool *operation)
{
	if (host->button_styles==NULL || host->styles==NULL || host->ports==NULL) return IO_ERROR_NOT_SUPPORTED;
	struct st_rip3_environment env;unsigned number;
	int status=st_rip3_command_environment(host,&number,&env);
	if (status!=0) return status;
	size_t fixed=2+4*env.coordinate_size+4;
	if (length<fixed) return IO_ERROR_INVALID_FORMAT;
	uint64_t values[7];size_t at=2;
	for (unsigned i=0;i<7;i++) {
		size_t width=i<4 ? env.coordinate_size : i==4 ? 2 : 1;
		status=st_rip3_command_field(values+i,command+at,width,env.base_math);if (status!=0) return status;at+=width;
	}
	if (values[4]>255) return IO_ERROR_OUT_OF_RANGE;
	if (values[6]!=0) return IO_ERROR_NOT_SUPPORTED;
	struct st_rip3_button_style style;
	status=st_rip3_button_styles_current_value(host->button_styles,&style);
	if (status!=0) return status;
	bool mouse=(style.flags&1024)!=0;
	/* Type precedence is ICON, clipboard, plain. Group and key decorations
	 * only apply to resident mouse buttons; inapplicable flags do not reject. */
	bool is_icon=(style.flags&128)!=0;
	if (mouse && ((style.flags&16384)!=0 || (style.flags2&1)!=0)) return IO_ERROR_NOT_SUPPORTED;
	if (mouse && host->mouse==NULL) return IO_ERROR_NOT_SUPPORTED;
	if ((style.flags&64)!=0 && (host->clipboard==NULL || host->offscreen_pixel_budget==NULL))
		return IO_ERROR_NOT_SUPPORTED;
	bool clipboard=!is_icon && (style.flags&1)!=0;
	struct st_rip3_port_info source;
	if ((style.flags&1)!=0) {
		if (host->clipboard==NULL || !host->clipboard->set) return IO_ERROR_NOT_FOUND;
		status=st_rip3_ports_info(host->ports,host->clipboard->port,&source);
		if (status!=0) return status;
		if (!source.defined) return IO_ERROR_NOT_FOUND;
		/* Activation/protection gate writes, not clipboard reads. The whole
		 * positive viewport must have at least some physical backing. */
		if (source.viewport.x1<source.viewport.x0 || source.viewport.y1<source.viewport.y0
		    || source.clip.x1<source.clip.x0 || source.clip.y1<source.clip.y0) return IO_ERROR_NOT_FOUND;
	}
	struct st_rip3_button_payload payload;
	status=st_rip3_button_payload_parse(&payload,command+at,length-at);
	if (status!=0) return status;
	if (is_icon && payload.icon.length==0) return IO_ERROR_INVALID_FORMAT;
	if (payload.icon.length!=0) {
		char filename[128];memcpy(filename,payload.icon.data,payload.icon.length);filename[payload.icon.length]=0;
		/* Filename expansion retains its separate rendering-stage work. */
		if (strchr(filename,'$')!=NULL || strstr(filename,"<<")!=NULL) return IO_ERROR_NOT_SUPPORTED;
		if (!st_rip3_resource_name_valid(filename)) return IO_ERROR_INVALID_FORMAT;
	}
	if (!is_icon && !clipboard && !mouse && payload.label.length==0 && !(style.width!=0 && style.height!=0) && values[2]==0 && values[3]==0) return 0;
	struct st_rip3_frame frame;
	status=st_rip3_environments_frame(host->environments,number,&frame);if (status!=0) return status;
	unsigned old_port,target;
	status=st_rip3_ports_current(host->ports,&old_port);if (status!=0) return status;
	unsigned restore_port=old_port;
	target=mouse ? 0 : old_port;
	struct st_rip3_port_info port;
	status=st_rip3_ports_info(host->ports,target,&port);if (status!=0 || !port.viewport_active) return status;
	if (port.clip.x1<port.clip.x0 || port.clip.y1<port.clip.y0) return 0;
	if (!is_icon && !clipboard && !(style.width!=0 && style.height!=0)) {
		if (values[2]==0 && values[3]==0) return 0;
		if (values[0]>values[2]) {uint64_t swap=values[0];values[0]=values[2];values[2]=swap;}
		if (values[1]>values[3]) {uint64_t swap=values[1];values[1]=values[3];values[3]=swap;}
	}
	struct box face={(int64_t)(values[0]*frame.device_width/frame.world_width),
	    (int64_t)(values[1]*frame.device_height/frame.world_height),
	    (int64_t)(values[2]*frame.device_width/frame.world_width),
	    (int64_t)(values[3]*frame.device_height/frame.world_height)};
	if (clipboard) {
		/* The whole clipboard viewport is the image, in DEVICE pixels.
		 * Fixed style dimensions and command far corners belong to plain
		 * buttons. Preserve source clipping/alignment in the shared blitter. */
		face.x1=face.x0+(int64_t)source.viewport.x1-source.viewport.x0+1;
		face.y1=face.y0+(int64_t)source.viewport.y1-source.viewport.y0+1;
	}
	else if (!is_icon && style.width!=0 && style.height!=0) {face.x1=face.x0+style.width;face.y1=face.y0+style.height;}
	if (!is_icon && (face.x1<=face.x0 || face.y1<=face.y0)) return 0;
	struct st_rip3_style drawing;
	status=st_rip3_styles_current(host->styles,&number);
	if (status==0) status=st_rip3_styles_get(host->styles,number,&drawing);
	if (status!=0) return status;
	struct icon *icon=NULL;struct st_rip3_bitmap *bitmap=NULL;
	if (is_icon) {
		if (host->resources==NULL || host->palettes==NULL || host->bitmap_errors==NULL || host->bitmap_errors->show==NULL)
			return IO_ERROR_NOT_SUPPORTED;
		icon=calloc(1,sizeof(*icon));if (icon==NULL) return IO_ERROR_OUT_OF_MEMORY;
		icon->host=*host;icon->style=style;icon->frame=frame;icon->port=port;icon->drawing=drawing;
		icon->selected=(values[5]&1)!=0;icon->hotkey=(uint8_t)values[4];
		memcpy(icon->name,payload.icon.data,payload.icon.length);
		memcpy(icon->label,payload.label.data,payload.label.length);icon->label_length=payload.label.length;
		*operation=true;status=icon_load(icon,&bitmap,target);
		if (status!=0) goto done;
		uint64_t width=(uint64_t)bitmap->pixels->width*frame.device_width/frame.world_width;
		uint64_t height=(uint64_t)bitmap->pixels->height*frame.device_height/frame.world_height;
		if (width==0 || height==0) {status=IO_ERROR_OUT_OF_RANGE;goto done;}
		face.x1=face.x0+(int64_t)width;face.y1=face.y0+(int64_t)height;
		icon->face=face;style=icon->style;
	}
	unsigned bevel=(style.flags&512)!=0 ? style.bevel&255u : 0;
	struct box hit=expand(face,(int)bevel);
	if (hit.x0<port.clip.x0) hit.x0=port.clip.x0;
	if (hit.y0<port.clip.y0) hit.y0=port.clip.y0;
	if (hit.x1>(int64_t)port.clip.x1+1) hit.x1=(int64_t)port.clip.x1+1;
	if (hit.y1>(int64_t)port.clip.y1+1) hit.y1=(int64_t)port.clip.y1+1;
	if (mouse && (hit.x0>=hit.x1 || hit.y0>=hit.y1)) {status=0;goto done;}
	if (mouse && st_rip3_mouse_count(host->mouse)==128) {status=IO_ERROR_OUT_OF_RANGE;goto done;}
	*operation=true;status=st_rip3_ports_switch(host->ports,target);
	if (status==0 && is_icon) status=paint_icon(icon,bitmap);
	if (status==0 && clipboard) {
		if (face.x0>INT32_MAX || face.y0>INT32_MAX) status=IO_ERROR_OUT_OF_RANGE;
		else status=st_rip3_ports_clipboard_blit(host->ports,host->clipboard->port,(int32_t)face.x0,(int32_t)face.y0);
	}
	if (status==0 && !is_icon && !clipboard && (style.flags2&32)==0) {
		if ((style.flags2&64)==0) status=fill(host,&port,face,style.colors[ST_RIP3_BUTTON_SURFACE]);
		else {
			struct box clipped=face;
			if (clipped.x0<port.clip.x0) clipped.x0=port.clip.x0;
			if (clipped.y0<port.clip.y0) clipped.y0=port.clip.y0;
			if (clipped.x1>(int64_t)port.clip.x1+1) clipped.x1=(int64_t)port.clip.x1+1;
			if (clipped.y1>(int64_t)port.clip.y1+1) clipped.y1=(int64_t)port.clip.y1+1;
			uint8_t foreground,background;
			status=st_rip3_command_ink(host,env.rgb_bits,drawing.fill_color,&foreground);
			if (status==0) status=st_rip3_command_ink(host,env.rgb_bits,drawing.background_color,&background);
			if (status==0 && clipped.x0<clipped.x1 && clipped.y0<clipped.y1) {
				struct st_rip3_rect area={(int32_t)clipped.x0,(int32_t)clipped.y0,(int32_t)(clipped.x1-1),(int32_t)(clipped.y1-1)};
				struct st_rip3_fill pattern;st_rip3_command_fill(&port,&drawing.fill,&pattern);
				status=st_rip3_draw_bar(host->ports,&area,&pattern,foreground,background,ST_RIP3_COPY);
			}
		}
	}
	if (status==0) status=paint_effects(host,&port,&style,face);
	if (status==0 && (style.flags&64)!=0) {
		/* Capture the original face/bevel extent, before label output. The
		 * shared helper owns slot choice, extra allocation unit, budget and
		 * destructive replacement; the surrounding recess is not included. */
		struct box image=expand(face,(int)bevel);
		const struct st_rip3_clipboard_area area={image.x0,image.y0,image.x1,image.y1};
		status=st_rip3_clipboard_capture(host->ports,host->clipboard,&area,*host->offscreen_pixel_budget);
		if (status==0) status=st_rip3_button_styles_autoclip(host->button_styles,style.flags,&style.flags);
		/* Replacing the current clipboard destroys its old destination and
		 * selects PORT0. Preserve that helper side effect, also on failure;
		 * later label painting uses the new live context, not the new image. */
		unsigned current;
		int queried=st_rip3_ports_current(host->ports,&current);
		if (queried==0 && !mouse && current!=target) {
			restore_port=current;
			if (status==0) status=st_rip3_ports_info(host->ports,current,&port);
		}
		if (status==0) status=queried;
	}
	if (status==0) {
		/* Realization can fail; it is part of label rendering, after the
		 * capture/style commit. Raw payload bounds were checked earlier. */
		struct label label;
		status=prepare_label(&label,host,&drawing,&style,&frame,payload.label.data,payload.label.length);
		if (status==0) status=paint_label(&label,host,&port,&style,&frame,face,(uint8_t)values[4]);
	}
	if (status==0 && is_icon && icon->selected && (style.flags&2)!=0) status=invert_icon(icon);
	int restored=st_rip3_ports_switch(host->ports,restore_port);
	if (status==0) status=restored;
	if (status==0 && mouse) {
		/* RECESS belongs to the initial surrounding scene, outside the
		 * persistent hit/image. Later hot redraws must not repaint it. */
		if (icon!=NULL) icon->style.flags&=~16u;
		int64_t ox=(int64_t)port.bounds.x0+port.viewport.x0,oy=(int64_t)port.bounds.y0+port.viewport.y0;
		struct st_rip3_rect area={(int32_t)(ox+hit.x0),(int32_t)(oy+hit.y0),
		    (int32_t)(ox+hit.x1),(int32_t)(oy+hit.y1)};
		const struct st_rip3_mouse_binding binding={(uint8_t)values[4],(values[5]&2)!=0};
		const struct st_rip3_mouse_visual visual={icon,select_icon,free};
		bool redraw=is_icon && (icon->style.flags&64)==0 && (style.flags&(2|4096))!=0;
		status=st_rip3_mouse_define(host->mouse,ST_RIP3_MOUSE_BUTTON,&binding,&area,(style.flags&2)!=0,(style.flags&4)!=0,payload.identity,
		    payload.host.data,payload.host.length,redraw ? &visual : NULL);
		if (status==0 && redraw) icon=NULL;
	}
done:
	st_rip3_bitmap_destroy(bitmap);free(icon);
	return status;
}
