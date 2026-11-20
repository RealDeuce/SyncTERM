/* Authored coordinate/cell expectations, independently of the layout service. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_text_window.h"
#include "io_error.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) {fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;} } while (0)

int
main(void)
{
	static const struct {
		unsigned width,height,font,x,y,cw,ch;
	} cases[]={
	    {640,480,0,80,54,8,11},{800,600,0,100,68,10,13},{1024,768,0,128,87,12,17},
	    {640,480,2,80,96,8,19},{800,600,2,100,120,10,24},{1024,768,2,128,153,12,30},
	    {640,350,1,70,40,7,8},{720,400,1,78,45,7,9},{1280,800,4,320,160,32,32},
	    {640,480,3,70,96,7,19},{800,600,3,87,120,8,24},{1024,768,3,112,153,11,30}};
	for (size_t i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
		struct st_rip3_frame frame;
		CHECK(st_rip3_frame_init_device(&frame,cases[i].width,cases[i].height)==0);
		/* WORLD changes do not move an ordinary text window. */
		CHECK(st_rip3_frame_world(&frame,17,29)==0);
		struct st_rip3_text_window_layout layout;
		CHECK(st_rip3_text_window_normal(&layout,&frame,10,5,20,10,1,cases[i].font)==0);
		CHECK(layout.x==cases[i].x && layout.y==cases[i].y && layout.columns==11 && layout.rows==6
		    && layout.cell_width==cases[i].cw && layout.cell_height==cases[i].ch && layout.wrap);
		struct st_rip3_text_window_layout saved=layout,other=layout;
		other.wrap=false;CHECK(st_rip3_text_window_same(&layout,&other));
		other.x1++;CHECK(!st_rip3_text_window_same(&layout,&other));
		CHECK(st_rip3_text_window_normal(&layout,&frame,20,10,10,5,1,cases[i].font)==0);
		CHECK(st_rip3_text_window_same(&layout,&saved) && layout.x==cases[i].x && layout.y==cases[i].y);
		saved=layout;
		CHECK(st_rip3_text_window_normal(&layout,&frame,10,5,20,10,2,cases[i].font)==IO_ERROR_OUT_OF_RANGE);
		CHECK(memcmp(&layout,&saved,sizeof(layout))==0);
	}
	struct st_rip3_frame frame;
	CHECK(st_rip3_frame_init_device(&frame,640,480)==0);
	static const unsigned columns[]={80,91,80,91,40},rows[]={43,43,25,25,25};
	for (unsigned font=0;font<5;font++) {
		struct st_rip3_text_window_layout layout;
		CHECK(st_rip3_text_window_normal(&layout,&frame,columns[font]-1,rows[font]-1,
		    columns[font]-1,rows[font]-1,1,font)==0 && layout.columns==1 && layout.rows==1);
		CHECK(st_rip3_text_window_normal(&layout,&frame,columns[font],rows[font],0,0,1,font)==0
		    && layout.x0==0 && layout.y0==0 && layout.x1==columns[font]-1 && layout.y1==rows[font]-1
		    && layout.columns==columns[font] && layout.rows==rows[font]);
		CHECK(st_rip3_text_window_normal(&layout,&frame,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,0,font)==0
		    && layout.x0==columns[font]-1 && layout.y0==rows[font]-1 && layout.columns==1 && layout.rows==1);
		struct st_rip3_text_window_layout saved=layout;
		CHECK(st_rip3_text_window_normal(&layout,&frame,0,0,20,10,1,5)==IO_ERROR_OUT_OF_RANGE);
		CHECK(memcmp(&layout,&saved,sizeof(layout))==0);
	}
	CHECK(st_rip3_frame_init_device(&frame,640,350)==0);
	struct st_rip3_text_window_layout example;
	CHECK(st_rip3_text_window_normal(&example,&frame,10,15,20,25,1,2)==0
	    && example.x0==10 && example.y0==15 && example.x1==20 && example.y1==24
	    && example.x==80 && example.y==210 && example.columns==11 && example.rows==10
	    && example.cell_width==8 && example.cell_height==14);
	CHECK(!st_rip3_text_window_same(NULL,NULL));
	return 0;
}
