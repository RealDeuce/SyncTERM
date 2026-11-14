/* Original V100 data/borrowed startup and common CHR decoding, BIG28/30. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_font.h"
#include "st_bgi_chr.h"
#include "st_bgi_data.h"
#include "io_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
static uint32_t crc(const uint8_t *p, size_t n)
{
	uint32_t value=UINT32_MAX;
	for (size_t i=0;i<n;++i) { value^=p[i]; for (unsigned bit=0;bit<8;++bit) value=(value>>1)^((value&1)?UINT32_C(0xedb88320):0); }
	return value^UINT32_MAX;
}
static void assets_and_all_glyphs(void)
{
	const size_t sizes[]={16677,5131,13596,18063};
	const uint32_t hashes[]={UINT32_C(0xba867503),UINT32_C(0x18273c62),UINT32_C(0x18438f9b),UINT32_C(0xb38f8e47)};
	const uint16_t counts[]={223,223,254,223},starts[]={32,32,1,32};
	for (unsigned id=1;id<=4;++id) {
		size_t size=0; const uint8_t *data=st_big_font_data(id,&size);
		CHECK(data!=NULL && size==sizes[id-1] && crc(data,size)==hashes[id-1]);
		size_t shared_size; CHECK(data==st_bgi_font_data(id,&shared_size) && shared_size==size);
		struct st_bgi_chr font; CHECK(st_bgi_chr_bind(&font,data+128,size-128)==0);
		CHECK(font.count==counts[id-1] && font.first==starts[id-1] && font.strokes==16+3*font.count);
		CHECK(data[0]==0x50 && data[1]==0x4b && font.body[8]==(id==1?24:id==2?7:25) && font.body[10]==(id==2?254:249));
		for (unsigned code=0;code<256;++code) {
			struct st_bgi_chr_glyph glyph; CHECK(st_bgi_chr_glyph(&font,(uint8_t)code,&glyph)==0);
			bool present=code>=starts[id-1] && code<starts[id-1]+counts[id-1]; CHECK(glyph.present==present);
			struct st_bgi_chr_record record={0},last={0}; bool available;
			size_t seen=0;
			while (true) {
				CHECK(st_bgi_chr_next(&glyph,&record,&available)==0); if (!available) break;
				++seen; CHECK(record.x>=-64 && record.x<=63 && record.y>=-64 && record.y<=63);
				if (record.opcode>=ST_BGI_CHR_MOVE) last=record;
			}
			CHECK(glyph.finished && seen==glyph.records && (present?seen>0:seen==0));
			if (present) CHECK(record.opcode==ST_BGI_CHR_END);
			if (id==2 && code==65) CHECK(glyph.width==6 && seen==9 && last.opcode==ST_BGI_CHR_MOVE && last.x==6 && last.y==0);
			if (id==4 && (code==21 || code==22)) CHECK(!present && seen==0);
			if (id==4 && code==65) CHECK(glyph.width==26 && seen==49 && last.x==26 && last.y==0);
		}
	}
	for (unsigned id=0;id<65536;++id) if (id==0 || id>4) { size_t size=99; CHECK(st_big_font_data(id,&size)==NULL && size==0); }
	CHECK(st_big_font_data(1,NULL)==NULL);
}
static void original_registry_and_rebinding(void)
{
	struct st_big_fonts fonts; int error=-11; CHECK(st_big_fonts_init(&fonts,&error)==0 && error==-11);
	CHECK(fonts.count==10 && fonts.loaded==0 && fonts.active==NULL && fonts.active_size==0);
	CHECK(memcmp(fonts.slots[0].name,"TRIP",4)==0 && memcmp(fonts.slots[9].name,"BOLD",4)==0 && fonts.slots[10].name[0]==0);
	for (unsigned slot=0;slot<20;++slot) {
		if (slot<4) { size_t size; const uint8_t *data=st_big_font_data(slot+1,&size); CHECK(fonts.slots[slot].body==data+128 && fonts.slots[slot].size==size-128); }
		else CHECK(fonts.slots[slot].body==NULL && fonts.slots[slot].size==0);
	}
	size_t size; const uint8_t *original=st_big_font_data(1,&size); uint8_t *copy=malloc(size); CHECK(copy!=NULL); memcpy(copy,original,size);
	fonts.loaded=1; fonts.active=fonts.slots[0].body; fonts.active_size=fonts.slots[0].size;
	const uint8_t *active=fonts.active; uint16_t id=77;
	CHECK(st_big_font_register(&fonts,copy,size,&error,&id)==0 && id==1 && error==-11);
	CHECK(fonts.slots[0].body==copy+128 && fonts.active==active && fonts.loaded==1);
	/* Version fields are independent spaced BYTE tests, not a1.1 tuple. */
	copy[0x61]=2; copy[0x63]=0; copy[0x62]=copy[0x64]=255;
	CHECK(st_big_font_register(&fonts,copy,size,&error,&id)==0 && error==-11);
	copy[0x61]=0; CHECK(st_big_font_register(&fonts,copy,size,&error,&id)==IO_ERROR_PROTOCOL && error==-13);
	copy[0x61]=1; copy[0x63]=2; CHECK(st_big_font_register(&fonts,copy,size,&error,&id)==IO_ERROR_PROTOCOL && error==-13);
	memcpy(copy,original,size); memcpy(copy+0x5b,"XXXX",4); CHECK(st_big_font_register(&fonts,copy,size,&error,&id)==IO_ERROR_PROTOCOL && error==-11);
	memset(copy+0x5b,0,4); CHECK(st_big_font_register(&fonts,copy,size,&error,&id)==0 && id==11 && fonts.count==10 && error==-11);
	memcpy(copy,original,size); copy[128]=0; CHECK(st_big_font_register(&fonts,copy,size,&error,&id)==0);
	struct st_bgi_chr body; CHECK(st_bgi_chr_bind(&body,fonts.slots[0].body,fonts.slots[0].size)==IO_ERROR_PROTOCOL); /* later plus check */
	copy[0x59]=copy[0x5a]=255; const uint8_t *old=fonts.slots[0].body;
	CHECK(st_big_font_register(&fonts,copy,size,&error,&id)==IO_ERROR_OUT_OF_RANGE && error==-13 && fonts.slots[0].body==old);
	CHECK(st_big_font_register(&fonts,copy,1,&error,&id)==IO_ERROR_OUT_OF_RANGE && error==-13);
	memset(copy,0,size); copy[0]=0x50; copy[1]=0x4b; CHECK(st_big_font_register(&fonts,copy,size,&error,&id)==IO_ERROR_OUT_OF_RANGE);
	CHECK(st_big_fonts_init(NULL,&error)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_font_register(&fonts,copy,size,NULL,&id)==IO_ERROR_INVALID_ARGUMENT);
	free(copy);
}
static void records_bounds_and_word_addresses(void)
{
	for (unsigned x=0;x<256;++x) for (unsigned y=0;y<256;++y) {
		const uint8_t bytes[]={(uint8_t)x,(uint8_t)y};
		struct st_bgi_chr_glyph glyph={.body=bytes,.size=2,.present=true}; struct st_bgi_chr_record record; bool available=false;
		CHECK(st_bgi_chr_next(&glyph,&record,&available)==0 && available && glyph.records==1);
		int ex=(int)(x%128),ey=(int)(y%128); if (ex>=64) ex-=128; if (ey>=64) ey-=128;
		CHECK((unsigned)record.opcode==(x>=128?2u:0u)+(y>=128?1u:0u) && record.x==ex && record.y==ey);
	}
	uint8_t data[28]={0x2b,1,0,0,32,20}; data[18]=7;
	memcpy(data+20,(uint8_t[]){0x81,2,0xff,0x83,5,0x86,14,0},8);
	struct st_bgi_chr font; CHECK(st_bgi_chr_bind(&font,data,sizeof(data))==0);
	struct st_bgi_chr_glyph glyph; CHECK(st_bgi_chr_glyph(&font,32,&glyph)==0 && glyph.width==7);
	struct st_bgi_chr_record record; bool available;
	CHECK(st_bgi_chr_next(&glyph,&record,&available)==0 && available && record.opcode==ST_BGI_CHR_MOVE);
	glyph.size=23; struct st_bgi_chr_record before=record;
	CHECK(st_bgi_chr_next(&glyph,&record,&available)==IO_ERROR_OUT_OF_RANGE && glyph.offset==22 && glyph.records==1 && record.opcode==before.opcode && record.x==before.x && record.y==before.y);
	glyph.size=sizeof(data); CHECK(st_bgi_chr_next(&glyph,&record,&available)==0 && record.opcode==ST_BGI_CHR_DRAW);
	CHECK(st_bgi_chr_next(&glyph,&record,&available)==0 && record.opcode==ST_BGI_CHR_IGNORE);
	CHECK(st_bgi_chr_next(&glyph,&record,&available)==0 && record.opcode==ST_BGI_CHR_END && record.x==14);
	CHECK(st_bgi_chr_next(&glyph,&record,&available)==0 && !available && record.x==14);
	font.count=0x8001; CHECK(st_bgi_chr_glyph(&font,32,&glyph)==0 && glyph.present && glyph.width==7); /* WORD metric-table wrap */
	font.count=256; CHECK(st_bgi_chr_glyph(&font,32,&glyph)==0 && !glyph.present); /* low count byte */
	font.count=2; font.first=255; data[21]=9; data[18]=data[19]=0;
	CHECK(st_bgi_chr_glyph(&font,0,&glyph)==0 && glyph.present && glyph.width==9); /* BYTE subtraction wraps */
	font.size=17; CHECK(st_bgi_chr_glyph(&font,255,&glyph)==IO_ERROR_OUT_OF_RANGE);
	uint8_t *endless=malloc(65536); CHECK(endless!=NULL); memset(endless,255,65536);
	glyph=(struct st_bgi_chr_glyph){.body=endless,.size=65536,.present=true};
	for (unsigned i=0;i<32768;++i) CHECK(st_bgi_chr_next(&glyph,&record,&available)==0 && available);
	CHECK(glyph.offset==0 && st_bgi_chr_next(&glyph,&record,&available)==IO_ERROR_OUT_OF_RANGE); free(endless);
	CHECK(st_bgi_chr_bind(&font,data,15)==IO_ERROR_OUT_OF_RANGE);
	CHECK(st_bgi_chr_bind(NULL,data,sizeof(data))==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_bgi_chr_next(NULL,&record,&available)==IO_ERROR_INVALID_ARGUMENT);
}
int main(void) { assets_and_all_glyphs(); original_registry_and_rebinding(); records_bounds_and_word_addresses(); return 0; }
