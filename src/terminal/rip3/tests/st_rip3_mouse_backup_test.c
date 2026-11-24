/* Independent definition/handler snapshot and interaction-boundary oracles. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_mouse.h"
#include "st_rip3_backup.h"
#include "io_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do {if (!(x)) {fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}} while (0)
extern size_t st_rip3_hover_test_attempts,st_rip3_hover_test_live,st_rip3_hover_test_fail_at;
struct fixture {struct st_rip3_mouse *mouse;char output[128];size_t used;unsigned flips;bool busy,pause;};
static void activate(void *context,const uint8_t *program,size_t length,enum st_rip3_mouse_reset reset,int identity,bool pointer,int32_t x,int32_t y)
{
	(void)reset;(void)pointer;(void)x;(void)y;
	struct fixture *f=context;
	if (length+2<sizeof(f->output)-f->used) {
		memcpy(f->output+f->used,program,length);f->used+=length;
		f->output[f->used++]=(char)('0'+identity);f->output[f->used]=0;
	}
	if (f->pause) f->busy=true;
}
static void invert(void *context,const struct st_rip3_rect *area) {(void)area;((struct fixture *)context)->flips++;}
static bool busy(void *context) {return ((struct fixture *)context)->busy;}
static int begin(struct fixture *f,struct st_rip3_backup **backup)
{
	const struct st_rip3_mouse_sink sink={.context=f,.activate=activate,.invert=invert,.busy=busy};
	CHECK(st_rip3_mouse_create(&f->mouse,&sink)==0 && st_rip3_mouse_backup_create(backup)==0);
	const struct st_rip3_rect a={10,10,30,30},b={20,20,40,40};
	CHECK(st_rip3_mouse_define(f->mouse,ST_RIP3_MOUSE_SIMPLE,NULL,&a,true,false,7,(const uint8_t *)"a",1,NULL)==0);
	const struct st_rip3_mouse_binding key={.hotkey='b'};
	CHECK(st_rip3_mouse_define(f->mouse,ST_RIP3_MOUSE_BUTTON,&key,&b,true,false,8,(const uint8_t *)"b",1,NULL)==0);
	CHECK(st_rip3_mouse_query(f->mouse,true,(const uint8_t *)"E",1)==0 && st_rip3_mouse_query(f->mouse,false,(const uint8_t *)"X",1)==0);
	return 0;
}
static void clear(struct fixture *f) {f->used=0;f->output[0]=0;}
static int definitions(void)
{
	struct fixture f={0};struct st_rip3_backup *backup=NULL;CHECK(begin(&f,&backup)==0);
	st_rip3_mouse_move(f.mouse,25,25);CHECK(strcmp(f.output,"E8E7")==0);clear(&f);
	CHECK(st_rip3_mouse_event(f.mouse,25,25,true) && f.flips==1);
	CHECK(st_rip3_backup_save(backup,ST_RIP3_BACKUP_BASE,0,f.mouse)==0 && f.flips==1 && f.used==0);
	CHECK(st_rip3_backup_save(backup,ST_RIP3_BACKUP_SLOT,3,f.mouse)==0);
	CHECK(st_rip3_mouse_query(f.mouse,true,(const uint8_t *)"N",1)==0);
	CHECK(st_rip3_mouse_query(f.mouse,false,(const uint8_t *)"Y",1)==0);
	CHECK(st_rip3_backup_restore(backup,ST_RIP3_BACKUP_BASE,0,f.mouse)==0 && f.flips==2 && f.used==0);
	CHECK(!st_rip3_mouse_event(f.mouse,25,25,false)); /* Never resurrect the saved held press. */
	st_rip3_mouse_move(f.mouse,25,25);CHECK(strcmp(f.output,"E8E7")==0);clear(&f);
	CHECK(st_rip3_mouse_event(f.mouse,25,25,true) && st_rip3_mouse_event(f.mouse,25,25,false));
	CHECK(strcmp(f.output,"b8")==0);clear(&f);
	CHECK(st_rip3_backup_restore(backup,ST_RIP3_BACKUP_BASE,0,f.mouse)==0);
	CHECK(st_rip3_mouse_key(f.mouse,'b') && strcmp(f.output,"b8")==0);clear(&f);
	CHECK(st_rip3_backup_protect(backup,3,true)==0);
	CHECK(st_rip3_backup_restore(backup,ST_RIP3_BACKUP_SLOT,3,f.mouse)==0);
	struct st_rip3_backup_info info;CHECK(st_rip3_backup_info(backup,&info)==0 && info.slot_used[3]);
	CHECK(st_rip3_backup_protect(backup,3,false)==0 && st_rip3_backup_restore(backup,ST_RIP3_BACKUP_SLOT,3,f.mouse)==0);
	CHECK(st_rip3_backup_info(backup,&info)==0 && !info.slot_used[3]);
	CHECK(st_rip3_backup_save(backup,ST_RIP3_BACKUP_STACK,0,f.mouse)==0);
	st_rip3_mouse_kill(f.mouse);st_rip3_mouse_clear_queries(f.mouse);
	CHECK(st_rip3_backup_save(backup,ST_RIP3_BACKUP_BASE,0,f.mouse)==0);
	CHECK(st_rip3_backup_restore(backup,ST_RIP3_BACKUP_STACK,0,f.mouse)==0 && st_rip3_mouse_count(f.mouse)==2);
	st_rip3_mouse_move(f.mouse,25,25);CHECK(strcmp(f.output,"E8E7")==0);clear(&f);
	CHECK(st_rip3_backup_restore(backup,ST_RIP3_BACKUP_BASE,0,f.mouse)==0 && st_rip3_mouse_count(f.mouse)==0);
	st_rip3_mouse_move(f.mouse,25,25);CHECK(f.used==0 && !st_rip3_mouse_key(f.mouse,'b'));
	st_rip3_mouse_destroy(f.mouse);st_rip3_backup_destroy(backup);CHECK(st_rip3_hover_test_live==0);return 0;
}
static int suspended_traversal(void)
{
	struct fixture f={0};struct st_rip3_backup *backup=NULL;CHECK(begin(&f,&backup)==0);
	CHECK(st_rip3_backup_save(backup,ST_RIP3_BACKUP_BASE,0,f.mouse)==0);
	f.pause=true;st_rip3_mouse_move(f.mouse,25,25);
	CHECK(strcmp(f.output,"E8")==0 && f.busy && st_rip3_mouse_pending(f.mouse));clear(&f);
	CHECK(st_rip3_backup_restore(backup,ST_RIP3_BACKUP_BASE,0,f.mouse)==0 && f.used==0);
	f.busy=f.pause=false;st_rip3_mouse_resume(f.mouse);
	CHECK(f.used==0 && !st_rip3_mouse_pending(f.mouse));
	st_rip3_mouse_move(f.mouse,25,25);CHECK(strcmp(f.output,"E8E7")==0);
	st_rip3_mouse_destroy(f.mouse);st_rip3_backup_destroy(backup);CHECK(st_rip3_hover_test_live==0);return 0;
}
static int failures(void)
{
	/* Every allocation in definition capture is independently failed. The old
	 * reusable snapshot and held gesture survive a rejected save. */
	struct fixture f={0};struct st_rip3_backup *backup=NULL;CHECK(begin(&f,&backup)==0);
	CHECK(st_rip3_backup_save(backup,ST_RIP3_BACKUP_BASE,0,f.mouse)==0);
	size_t baseline=st_rip3_hover_test_live,first=st_rip3_hover_test_attempts;
	CHECK(st_rip3_backup_save(backup,ST_RIP3_BACKUP_SLOT,0,f.mouse)==0);
	size_t count=st_rip3_hover_test_attempts-first;CHECK(count>=5);
	CHECK(st_rip3_backup_clear(backup,ST_RIP3_BACKUP_SLOT,0)==0 && st_rip3_hover_test_live==baseline);
	for (size_t fail=0;fail<count;fail++) {
		CHECK(st_rip3_mouse_event(f.mouse,25,25,true));unsigned flips=f.flips;
		st_rip3_hover_test_fail_at=st_rip3_hover_test_attempts+fail;
		CHECK(st_rip3_backup_save(backup,ST_RIP3_BACKUP_BASE,0,f.mouse)==IO_ERROR_OUT_OF_MEMORY);
		st_rip3_hover_test_fail_at=SIZE_MAX;
		CHECK(st_rip3_hover_test_live==baseline && f.flips==flips);
		CHECK(st_rip3_mouse_event(f.mouse,25,25,false) && strcmp(f.output,"b8")==0);clear(&f);
	}
	/* Missing/invalid requests never begin applying. A failure after entering
	 * restore does cancel input, while retaining old definitions and handlers. */
	CHECK(st_rip3_mouse_event(f.mouse,25,25,true));unsigned flips=f.flips;
	CHECK(st_rip3_backup_restore(backup,ST_RIP3_BACKUP_SLOT,9,f.mouse)==IO_ERROR_NOT_FOUND && f.flips==flips);
	CHECK(st_rip3_mouse_event(f.mouse,25,25,false) && strcmp(f.output,"b8")==0);clear(&f);
	for (size_t fail=0;fail<count;fail++) {
		CHECK(st_rip3_mouse_event(f.mouse,25,25,true));flips=f.flips;
		st_rip3_hover_test_fail_at=st_rip3_hover_test_attempts+fail;
		CHECK(st_rip3_backup_restore(backup,ST_RIP3_BACKUP_BASE,0,f.mouse)==IO_ERROR_OUT_OF_MEMORY);
		st_rip3_hover_test_fail_at=SIZE_MAX;
		CHECK(st_rip3_hover_test_live==baseline && f.flips==flips+1 && st_rip3_mouse_count(f.mouse)==2);
		CHECK(!st_rip3_mouse_event(f.mouse,25,25,false) && f.used==0);
		st_rip3_mouse_move(f.mouse,25,25);CHECK(strcmp(f.output,"E8E7")==0);clear(&f);
	}
	CHECK(st_rip3_backup_restore(backup,ST_RIP3_BACKUP_BASE,0,f.mouse)==0);
	st_rip3_mouse_destroy(f.mouse);st_rip3_backup_destroy(backup);CHECK(st_rip3_hover_test_live==0);return 0;
}
static int template_value(struct st_rip3_templates *templates,unsigned n,const char *expected)
{
	struct st_rip3_template_bytes value;
	CHECK(st_rip3_templates_resolve(templates,n,&value)==0 && value.length==strlen(expected));
	CHECK(value.length==0 || memcmp(value.bytes,expected,value.length)==0);return 0;
}
static int grouped_capture(void)
{
	struct fixture f={0};struct st_rip3_backup *backup=NULL;CHECK(begin(&f,&backup)==0);
	struct st_rip3_templates *templates=NULL;CHECK(st_rip3_templates_create(&templates)==0);
	st_rip3_mouse_kill(f.mouse);st_rip3_mouse_bind_templates(f.mouse,templates);
	const struct st_rip3_rect area={10,10,30,30};
	const struct st_rip3_mouse_binding binding={.hotkey='b',.group=2,.group_kind=ST_RIP3_GROUP_CHECKBOX,.selected=true};
	CHECK(st_rip3_mouse_define(f.mouse,ST_RIP3_MOUSE_BUTTON,&binding,&area,false,false,8,(const uint8_t *)"b",1,NULL)==0);
	/* Definition sequence is three: the two begin() fields were removed.
	 * A selected field has no associations until actually reached. */
	CHECK(st_rip3_templates_associate(templates,3,2,(const uint8_t *)"A",1)==0);
	CHECK(st_rip3_templates_associate(templates,3,3,(const uint8_t *)"X",1)==0);
	CHECK(st_rip3_backup_save(backup,ST_RIP3_BACKUP_BASE,0,f.mouse)==0);
	CHECK(st_rip3_mouse_key(f.mouse,'b') && strcmp(f.output,"b8")==0);clear(&f);
	CHECK(template_value(templates,2,"")==0 && template_value(templates,3,"")==0);
	CHECK(st_rip3_templates_set(templates,2,(const uint8_t *)"Z",1)==0);
	st_rip3_mouse_hotkeys(f.mouse,false);
	CHECK(st_rip3_backup_restore(backup,ST_RIP3_BACKUP_BASE,0,f.mouse)==0 && f.used==0);
	CHECK(template_value(templates,2,"Z")==0 && template_value(templates,3,"X")==0);
	CHECK(!st_rip3_mouse_key(f.mouse,'b'));st_rip3_mouse_hotkeys(f.mouse,true);
	CHECK(st_rip3_templates_button_selected(templates,4) && !st_rip3_templates_button_selected(templates,3));
	CHECK(st_rip3_templates_associate(templates,3,2,(const uint8_t *)"stale",5)==0);
	CHECK(template_value(templates,2,"Z")==0);
	CHECK(st_rip3_templates_associate(templates,4,2,(const uint8_t *)"changed",7)==0);
	CHECK(st_rip3_backup_restore(backup,ST_RIP3_BACKUP_BASE,0,f.mouse)==0);
	CHECK(template_value(templates,2,"A")==0 && template_value(templates,3,"X")==0);
	/* Applying failure cancels input but leaves complete old maps/publications.
	 * No restored field is admitted by a failed definition allocation. */
	CHECK(st_rip3_mouse_event(f.mouse,20,20,true));
	st_rip3_hover_test_fail_at=st_rip3_hover_test_attempts+1;
	CHECK(st_rip3_backup_restore(backup,ST_RIP3_BACKUP_BASE,0,f.mouse)==IO_ERROR_OUT_OF_MEMORY);
	st_rip3_hover_test_fail_at=SIZE_MAX;
	CHECK(!st_rip3_mouse_event(f.mouse,20,20,false) && f.used==0);
	CHECK(template_value(templates,2,"A")==0 && st_rip3_templates_button_selected(templates,5));
	st_rip3_mouse_kill(f.mouse);CHECK(template_value(templates,2,"Z")==0 && template_value(templates,3,"")==0);
	CHECK(st_rip3_backup_restore(backup,ST_RIP3_BACKUP_BASE,0,f.mouse)==0);
	CHECK(template_value(templates,2,"Z")==0 && template_value(templates,3,"")==0);
	st_rip3_mouse_destroy(f.mouse);st_rip3_backup_destroy(backup);st_rip3_templates_destroy(templates);
	CHECK(st_rip3_hover_test_live==0);return 0;
}
struct visual_counts {size_t attempts,fail_at,owned,detached,attached;unsigned selects,destroyed;};
struct visual_value {struct visual_counts *counts;bool owned;};
static int select_visual(void *context,bool selected) {(void)selected;((struct visual_value *)context)->counts->selects++;return 0;}
static void destroy_visual(void *context)
{
	struct visual_value *v=context;v->counts->destroyed++;
	if (v->owned) {v->counts->owned--;free(v);}
}
static int copy_visual(const void *context,void **result,struct st_rip3_mouse *owner)
{
	const struct visual_value *source=context;struct visual_counts *counts=source->counts;
	if (counts->attempts++==counts->fail_at) return IO_ERROR_OUT_OF_MEMORY;
	struct visual_value *copy=malloc(sizeof(*copy));if (copy==NULL) return IO_ERROR_OUT_OF_MEMORY;
	*copy=(struct visual_value){counts,true};counts->owned++;
	if (owner!=NULL) counts->attached++;else counts->detached++;
	*result=copy;return 0;
}
static int visual_lifetime(void)
{
	struct st_rip3_mouse *mouse=NULL;struct st_rip3_backup *backup=NULL;const struct st_rip3_mouse_sink sink={0};
	CHECK(st_rip3_mouse_create(&mouse,&sink)==0 && st_rip3_mouse_backup_create(&backup)==0);
	struct visual_counts counts={.fail_at=SIZE_MAX};struct visual_value values[2]={{&counts,false},{&counts,false}};
	const struct st_rip3_rect area={10,10,30,30};
	for (unsigned i=0;i<2;i++) {
		const struct st_rip3_mouse_visual visual={.context=&values[i],.select=select_visual,.destroy=destroy_visual,.copy=copy_visual};
		CHECK(st_rip3_mouse_define(mouse,ST_RIP3_MOUSE_BUTTON,NULL,&area,false,false,7,NULL,0,&visual)==0);
	}
	CHECK(st_rip3_backup_save(backup,ST_RIP3_BACKUP_BASE,0,mouse)==0 && counts.detached==2 && counts.owned==2 && counts.selects==0);
	counts.fail_at=counts.attempts+1;
	CHECK(st_rip3_backup_save(backup,ST_RIP3_BACKUP_BASE,0,mouse)==IO_ERROR_OUT_OF_MEMORY && counts.owned==2);
	counts.fail_at=counts.attempts+1;
	CHECK(st_rip3_backup_restore(backup,ST_RIP3_BACKUP_BASE,0,mouse)==IO_ERROR_OUT_OF_MEMORY && counts.owned==2 && st_rip3_mouse_count(mouse)==2);
	counts.fail_at=SIZE_MAX;
	CHECK(st_rip3_backup_restore(backup,ST_RIP3_BACKUP_BASE,0,mouse)==0 && counts.owned==4 && counts.selects==0);
	st_rip3_mouse_destroy(mouse);CHECK(counts.owned==2);
	st_rip3_backup_destroy(backup);CHECK(counts.owned==0 && st_rip3_hover_test_live==0);return 0;
}
int main(void) {CHECK(definitions()==0 && failures()==0 && grouped_capture()==0 && visual_lifetime()==0 && suspended_traversal()==0);return 0;}
