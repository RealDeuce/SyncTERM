/* BIG24/59 indexed boundary/span flood, original EGAVGA0eaa..1452. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_flood.h"
#include "st_big_number.h"
#include "io_error.h"
#include <string.h>

struct flood {
	struct st_big_flood *work;
	const struct st_big_state *state;
	const struct st_big_memory_sink *sink;
	uint16_t bounds[4], stride, mode_bottom, top_inverse, bottom_inverse;
	uint16_t x, inverse_y, address, left, right;
	uint16_t previous_inverse, previous_left, previous_right;
	uint8_t bit, flags, boundary, constant, row[8], row_or;
	bool pattern;
};
int st_big_flood_bind(struct st_big_flood *f, struct st_big_flood_record *records, size_t storage_count, size_t capacity)
{
	if (f==NULL) return IO_ERROR_INVALID_ARGUMENT;
	*f=(struct st_big_flood){.records=records,.storage_count=storage_count,.capacity=capacity};
	return 0;
}
static int synchronous(int status) { return status==IO_ERROR_IN_PROGRESS?IO_ERROR_PROTOCOL:status; }
static int load_row(struct flood *f)
{
	uint16_t y=(uint16_t)(f->mode_bottom-f->inverse_y);
	f->row_or=0;
	for (size_t bit=0;bit<8;++bit) {
		uint8_t pen=f->pattern?f->state->bgi.fill_pixels[(y&7)*8+bit]:f->constant;
		if (pen>15) return IO_ERROR_OUT_OF_RANGE;
		f->row[bit]=pen; f->row_or|=pen;
	}
	return 0;
}
static int read_byte(const struct flood *f, uint8_t pens[8], uint8_t *boundary)
{
	int status=synchronous(f->sink->read(f->sink->context,f->address,pens));
	if (status!=0) return status;
	*boundary=0;
	for (unsigned bit=0;bit<8;++bit) {
		if (pens[bit]>15) return IO_ERROR_OUT_OF_RANGE;
		if (pens[bit]==f->boundary) *boundary|=(uint8_t)(0x80u>>bit);
	}
	return 0;
}
static int write_byte(const struct flood *f, uint16_t address, uint8_t mask, uint8_t planes)
{
	return synchronous(f->sink->write(f->sink->context,address,mask,planes,f->row,0));
}
static void step_right(struct flood *f)
{
	f->x=(uint16_t)(f->x+1u);
	if (++f->bit==8) { f->bit=0; f->address=(uint16_t)(f->address+1u); }
}
static void step_left(struct flood *f)
{
	f->x=(uint16_t)(f->x-1u);
	if (f->bit==0) { f->bit=7; f->address=(uint16_t)(f->address-1u); }
	else --f->bit;
}
static int full_bytes(struct flood *f)
{
	uint8_t pens[8], boundary;
	int status=read_byte(f,pens,&boundary); if (status!=0 || boundary!=0) return status;
	uint16_t address=f->address, x=f->x, count=0;
	for (;;) {
		/* The original re-reads before collecting each byte, then writes
		 * the entire collected band in plane1/2/4/8 order. */
		status=read_byte(f,pens,&boundary); if (status!=0) return status;
		if (boundary!=0 || f->x>=(uint16_t)(f->bounds[2]-7u)) break;
		f->x=(uint16_t)(f->x+8u); f->address=(uint16_t)(f->address+1u); ++count;
		/* Malformed WORD clip/seed state can make the unsigned limit
		 * unreachable. Reject a full cursor cycle instead of hanging. */
		if (f->x==x && f->address==address) return IO_ERROR_OUT_OF_RANGE;
	}
	for (unsigned plane=1;plane<=8;plane<<=1) for (uint16_t i=0;i<count;++i) {
		status=write_byte(f,(uint16_t)(address+i),255,(uint8_t)plane);
		if (status!=0) return status;
	}
	return 0;
}
static int spans(struct flood *f, const struct st_big_flood_record *seed)
{
	f->x=seed->x; f->inverse_y=seed->inverse_y; f->address=seed->address; f->bit=seed->bit;
	if (f->bit>7) return IO_ERROR_OUT_OF_RANGE;
	int status=load_row(f); if (status!=0) return status;
	uint8_t pens[8], boundary;
	for (;;) {
		status=read_byte(f,pens,&boundary); if (status!=0) return status;
		if (boundary&(0x80u>>f->bit)) break;
		status=write_byte(f,f->address,(uint8_t)(0x80u>>f->bit),15); if (status!=0) return status;
		if (st_big_number_signed(f->x)>=st_big_number_signed(f->bounds[2])) break;
		step_right(f);
		if (f->bit==0) { status=full_bytes(f); if (status!=0) return status; }
	}
	f->right=(uint16_t)(f->x-1u);
	f->x=seed->x; f->address=seed->address; f->bit=seed->bit;
	if (f->x!=f->bounds[0]) {
		step_left(f);
		for (;;) {
			status=read_byte(f,pens,&boundary); if (status!=0) return status;
			if (boundary&(0x80u>>f->bit)) break;
			status=write_byte(f,f->address,(uint8_t)(0x80u>>f->bit),15); if (status!=0) return status;
			if (f->x==f->bounds[0]) break;
			step_left(f);
		}
	}
	step_right(f); f->left=f->x;
	return 0;
}
static bool contained(const struct flood *f)
{
	return st_big_number_signed(f->left)>st_big_number_signed((uint16_t)(f->previous_left-1u))
	    && f->right<(uint16_t)(f->previous_right+1u);
}
static int scan_adjacent(struct flood *f, uint8_t direction)
{
	f->flags=(uint8_t)(direction&4);
	int status=load_row(f); if (status!=0) return status;
	for (;;) {
		uint8_t pens[8], boundary;
		/* There is at least one probe, including inverted left/right bounds. */
		status=read_byte(f,pens,&boundary); if (status!=0) return status;
		if (boundary&(0x80u>>f->bit)) f->flags&=6;
		else if ((f->flags&1)==0) {
			bool candidate=false;
			if (f->row_or==0) { f->flags|=2; f->work->found_directional=true; candidate=true; }
			else {
				/* Boundary and expected-pen comparison use separate reads. */
				status=read_byte(f,pens,&boundary); if (status!=0) return status;
				candidate=pens[f->bit]!=f->row[f->bit];
			}
			if (candidate) {
				/* Flags precede the guard. Refusal returns from THIS scan
				 * (122e ->118e ->1401); other directions/seeds continue. */
				f->flags|=1;
				if (f->work->count>=f->work->capacity) { f->work->overflow=true; return 0; }
				if (f->work->records==NULL || f->work->count>=f->work->storage_count) return IO_ERROR_OUT_OF_RANGE;
				f->work->records[f->work->count++]=(struct st_big_flood_record){f->inverse_y,f->x,f->address,f->bit,f->flags};
			}
		}
		if (f->x>=f->right) return 0;
		step_right(f);
		if (f->bit==0 && (f->flags&1)!=0) for (;;) {
			status=read_byte(f,pens,&boundary); if (status!=0) return status;
			if (boundary!=0 || f->x>=(uint16_t)(f->right-8u)) break;
			f->x=(uint16_t)(f->x+8u); f->address=(uint16_t)(f->address+1u);
		}
	}
}
static int process_seed(struct flood *f, const struct st_big_flood_record *seed)
{
	/* Only the initial public seed has a boundary early-out. A popped
	 * boundary seed still runs its left span and directional continuation. */
	int status=spans(f,seed); if (status!=0) return status;
	bool up=true, down=true;
	if (seed->flags&2) { up=(seed->flags&4)!=0; down=!up; }
	else if (!f->work->found_directional && contained(f)) {
		if ((uint16_t)(f->previous_inverse+1u)==seed->inverse_y) down=false;
		else if ((uint16_t)(f->previous_inverse-1u)==seed->inverse_y) up=false;
	}
	uint16_t address=f->address; uint8_t bit=f->bit;
	if (up && seed->inverse_y<f->top_inverse) {
		f->inverse_y=(uint16_t)(seed->inverse_y+1u); f->address=(uint16_t)(address-f->stride);
		status=scan_adjacent(f,4); if (status!=0) return status;
	}
	if (down && seed->inverse_y!=f->bottom_inverse) {
		f->x=f->left; f->bit=bit;
		f->inverse_y=(uint16_t)(seed->inverse_y-1u); f->address=(uint16_t)(address+f->stride);
		status=scan_adjacent(f,0); if (status!=0) return status;
	}
	f->previous_inverse=seed->inverse_y; f->previous_left=f->left; f->previous_right=f->right;
	return 0;
}
int st_big_flood_fill(struct st_big_flood *work, struct st_big_state *s,
    const struct st_big_memory_sink *sink, uint16_t x, uint16_t y, uint16_t boundary)
{
	if (work==NULL || s==NULL || sink==NULL || sink->read==NULL || sink->write==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (s->bgi.state==0) { s->bgi.error=-1; return IO_ERROR_PROTOCOL; }
	if (s->bgi.width==0 || s->bgi.width>65536 || s->bgi.width%8!=0 || s->bgi.height==0 || s->bgi.height>65536)
		return IO_ERROR_OUT_OF_RANGE;
	struct flood f={.work=work,.state=s,.sink=sink,.boundary=(uint8_t)(boundary&15)};
	if (s->bgi.clip) memcpy(f.bounds,s->bgi.viewport,sizeof(f.bounds));
	else { f.bounds[2]=(uint16_t)(s->bgi.width-1); f.bounds[3]=(uint16_t)(s->bgi.height-1); }
	x=(uint16_t)(x+s->bgi.viewport[0]); y=(uint16_t)(y+s->bgi.viewport[1]);
	/* DDO rejection does not enter/reset the device flood workspace. */
	if (x>f.bounds[2] || y>f.bounds[3] || st_big_number_signed(x)<st_big_number_signed(f.bounds[0])
	    || st_big_number_signed(y)<st_big_number_signed(f.bounds[1])) return 0;
	work->count=0; work->overflow=false; work->found_directional=false;
	f.stride=(uint16_t)(s->bgi.width/8); f.mode_bottom=(uint16_t)(s->bgi.height-1);
	f.inverse_y=(uint16_t)(f.mode_bottom-y);
	f.top_inverse=(uint16_t)(f.mode_bottom-f.bounds[1]); f.bottom_inverse=(uint16_t)(f.mode_bottom-f.bounds[3]);
	f.previous_inverse=f.inverse_y; f.previous_right=f.bounds[2];
	f.x=x; f.bit=(uint8_t)(x&7); f.address=(uint16_t)((uint32_t)y*f.stride+(x>>3));
	if (s->bgi.device_fill_style==1) {
		if (s->bgi.device_fill_color>15) return IO_ERROR_OUT_OF_RANGE;
		f.constant=s->bgi.device_fill_color;
	} else if (s->bgi.device_fill_style>=2) {
		for (size_t i=0;i<64;++i) if (s->bgi.fill_pixels[i]!=0) { f.pattern=true; break; }
	}
	uint8_t pens[8], mask;
	int status=read_byte(&f,pens,&mask); if (status!=0 || (mask&(0x80u>>f.bit))) return status;
	struct st_big_flood_record seed={f.inverse_y,f.x,f.address,f.bit,0};
	for (;;) {
		status=process_seed(&f,&seed); if (status!=0) return status;
		if (work->count==0) break;
		if (work->records==NULL || work->count>work->storage_count) return IO_ERROR_OUT_OF_RANGE;
		seed=work->records[--work->count];
	}
	if (work->overflow) s->bgi.error=-7;
	return 0;
}
