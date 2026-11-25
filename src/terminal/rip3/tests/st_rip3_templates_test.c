/* Independent intended-composition, ownership and failure expectations. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_templates.h"
#include "io_error.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { \
	fprintf(stderr,"line %d: %s\n",__LINE__,#expression);return 1; } } while (0)
extern bool st_rip3_templates_test_fail;

static int required_group(struct st_rip3_templates *t,unsigned n,bool required)
{
	uint64_t redraw[128];size_t count;
	if (required) return st_rip3_templates_button_add(t,1000+n,n,ST_RIP3_GROUP_RADIO,false,redraw,&count);
	st_rip3_templates_button_remove(t,1000+n);return 0;
}
static int
set(struct st_rip3_templates *slots, unsigned number, const char *value)
{
	CHECK(st_rip3_templates_set(slots,number,(const uint8_t *)value,strlen(value))==0);return 0;
}

static int
chain(struct st_rip3_templates *slots, const uint8_t *numbers, size_t count,
    const char *feed, const char *expected)
{
	uint8_t result[4096];size_t length=SIZE_MAX;
	CHECK(st_rip3_templates_chain(slots,numbers,count,(const uint8_t *)feed,strlen(feed),
	    result,sizeof(result),&length,NULL)==0 && length==strlen(expected) && memcmp(result,expected,length)==0);
	return 0;
}

static int value(struct st_rip3_templates *t,unsigned n,const char *expected)
{
	struct st_rip3_template_bytes v;CHECK(st_rip3_templates_resolve(t,n,&v)==0);
	CHECK(v.length==strlen(expected) && (v.length==0 || memcmp(v.bytes,expected,v.length)==0));return 0;
}
static int groups(void)
{
	struct st_rip3_templates *t=NULL;uint64_t redraw[128];size_t count;
	CHECK(st_rip3_templates_create(&t, false)==0);
	CHECK(st_rip3_templates_button_add(t,1,2,ST_RIP3_GROUP_CHECKBOX,false,redraw,&count)==0);
	CHECK(st_rip3_templates_button_add(t,2,2,ST_RIP3_GROUP_CHECKBOX,true,redraw,&count)==0);
	CHECK(value(t,2,"")==0 && value(t,3,"")==0);
	CHECK(st_rip3_templates_button_activate(t,1,redraw,&count)==0);
	CHECK(st_rip3_templates_associate(t,1,2,(const uint8_t *)"A",1)==0
	    && st_rip3_templates_associate(t,1,3,(const uint8_t *)"X",1)==0);
	CHECK(value(t,2,"A")==0 && value(t,3,"X")==0);
	CHECK(st_rip3_templates_button_activate(t,2,redraw,&count)==0);
	CHECK(st_rip3_templates_associate(t,2,2,(const uint8_t *)"B",1)==0
	    && st_rip3_templates_associate(t,2,2,(const uint8_t *)"C",1)==0
	    && st_rip3_templates_associate(t,2,3,(const uint8_t *)"Y",1)==0);
	CHECK(value(t,2,"A")==0 && value(t,3,"X")==0);
	CHECK(st_rip3_templates_button_activate(t,2,redraw,&count)==0);
	CHECK(value(t,2,"AC")==0 && value(t,3,"XY")==0);
	CHECK(set(t,2,"Z")==0);st_rip3_templates_button_remove(t,2);
	CHECK(value(t,2,"Z")==0 && value(t,3,"X")==0);
	CHECK(st_rip3_templates_associate(t,1,2,(const uint8_t *)"A",1)==0 && value(t,2,"A")==0);
	st_rip3_templates_button_remove(t,1);CHECK(value(t,2,"Z")==0 && value(t,3,"")==0);
	CHECK(st_rip3_templates_associate(t,2,2,(const uint8_t *)"stale",5)==0 && value(t,2,"Z")==0);
	CHECK(st_rip3_templates_button_add(t,3,4,ST_RIP3_GROUP_RADIO,false,redraw,&count)==0);
	struct st_rip3_template_bytes missing;
	CHECK(set(t,4,"ordinary")==0 && st_rip3_templates_resolve(t,4,&missing)==IO_ERROR_NOT_FOUND);
	CHECK(st_rip3_templates_button_activate(t,3,redraw,&count)==0 && value(t,4,"ordinary")==0);
	CHECK(st_rip3_templates_associate(t,3,4,NULL,0)==0 && value(t,4,"")==0);
	CHECK(st_rip3_templates_group_check(t,4,ST_RIP3_GROUP_CHECKBOX)==IO_ERROR_CONFLICT);
	CHECK(st_rip3_templates_button_add(t,4,4,ST_RIP3_GROUP_RADIO,true,redraw,&count)==0
	    && count==2 && redraw[0]==3 && !st_rip3_templates_button_selected(t,3));
	CHECK(st_rip3_templates_associate(t,4,4,(const uint8_t *)"preserved",9)==0);
	CHECK(st_rip3_templates_preserve(t)==0);st_rip3_templates_button_remove(t,3);st_rip3_templates_button_remove(t,4);
	CHECK(value(t,4,"preserved")==0);
	CHECK(st_rip3_templates_button_add(t,5,2,ST_RIP3_GROUP_CHECKBOX,true,redraw,&count)==0
	    && st_rip3_templates_button_add(t,6,2,ST_RIP3_GROUP_CHECKBOX,true,redraw,&count)==0);
	uint8_t a[3000],b[2000];memset(a,'A',sizeof(a));memset(b,'B',sizeof(b));
	CHECK(st_rip3_templates_associate(t,5,2,a,sizeof(a))==0 && st_rip3_templates_associate(t,6,2,b,sizeof(b))==0);
	struct st_rip3_template_bytes aggregate;
	CHECK(st_rip3_templates_resolve(t,2,&aggregate)==0 && aggregate.length==4096);
	for (size_t i=0;i<aggregate.length;i++) CHECK(aggregate.bytes[i]==(i<3000 ? 'A' : 'B'));
	st_rip3_templates_test_fail=true;
	CHECK(st_rip3_templates_associate(t,5,2,(const uint8_t *)"lost",4)==IO_ERROR_OUT_OF_MEMORY);
	CHECK(st_rip3_templates_preserve(t)==IO_ERROR_OUT_OF_MEMORY);
	CHECK(st_rip3_templates_button_add(t,7,2,ST_RIP3_GROUP_CHECKBOX,false,redraw,&count)==IO_ERROR_OUT_OF_MEMORY);
	st_rip3_templates_test_fail=false;
	CHECK(st_rip3_templates_resolve(t,2,&aggregate)==0 && aggregate.length==4096);
	CHECK(st_rip3_templates_button_activate(t,5,redraw,&count)==0);
	CHECK(st_rip3_templates_resolve(t,2,&aggregate)==0 && aggregate.length==2000);
	for (size_t i=0;i<aggregate.length;i++) CHECK(aggregate.bytes[i]=='B');
	/* Another group can publish the slot, without resurrecting old publishers. */
	CHECK(st_rip3_templates_button_add(t,8,8,ST_RIP3_GROUP_CHECKBOX,true,redraw,&count)==0);
	CHECK(st_rip3_templates_associate(t,8,2,(const uint8_t *)"other",5)==0 && value(t,2,"other")==0);
	st_rip3_templates_button_remove(t,6);CHECK(value(t,2,"other")==0);
	st_rip3_templates_button_remove(t,8);CHECK(value(t,2,"Z")==0);
	st_rip3_templates_destroy(t);return 0;
}

static int snapshot_groups(void)
{
	struct st_rip3_templates *t=NULL;uint64_t redraw[128];size_t count;
	struct st_rip3_group_snapshot *saved=NULL,*prepared=NULL;
	CHECK(st_rip3_templates_create(&t, false)==0);
	CHECK(st_rip3_templates_button_add(t,10,2,ST_RIP3_GROUP_CHECKBOX,true,redraw,&count)==0);
	CHECK(st_rip3_templates_button_add(t,20,2,ST_RIP3_GROUP_CHECKBOX,true,redraw,&count)==0);
	CHECK(st_rip3_templates_associate(t,10,2,(const uint8_t *)"A",1)==0);
	CHECK(st_rip3_templates_associate(t,10,3,(const uint8_t *)"X",1)==0);
	CHECK(st_rip3_templates_associate(t,20,2,(const uint8_t *)"B",1)==0);
	CHECK(st_rip3_templates_associate(t,20,3,(const uint8_t *)"Y",1)==0);
	CHECK(value(t,2,"AB")==0 && value(t,3,"XY")==0);
	CHECK(st_rip3_templates_capture_groups(t,(const uint64_t[]){10,20},2,&saved)==0);
	CHECK(st_rip3_templates_button_activate(t,10,redraw,&count)==0 && set(t,2,"Z")==0);
	CHECK(st_rip3_templates_prepare_groups(saved,(const uint64_t[]){30,40},2,&prepared)==0);
	st_rip3_templates_apply_groups(t,prepared);prepared=NULL;
	CHECK(value(t,2,"Z")==0 && value(t,3,"XY")==0);
	CHECK(st_rip3_templates_button_activate(t,30,redraw,&count)==0);
	CHECK(st_rip3_templates_associate(t,30,2,(const uint8_t *)"changed",7)==0);
	CHECK(st_rip3_templates_prepare_groups(saved,(const uint64_t[]){50,60},2,&prepared)==0);
	st_rip3_templates_apply_groups(t,prepared);prepared=NULL;
	CHECK(value(t,2,"AB")==0 && value(t,3,"XY")==0);
	st_rip3_templates_button_remove(t,50);st_rip3_templates_button_remove(t,60);
	CHECK(st_rip3_templates_prepare_groups(saved,(const uint64_t[]){70,80},2,&prepared)==0);
	st_rip3_templates_apply_groups(t,prepared);prepared=NULL;
	CHECK(value(t,2,"Z")==0 && value(t,3,"")==0);
	/* Same numeric group with the other kind cannot retain publication. */
	st_rip3_templates_button_remove(t,70);st_rip3_templates_button_remove(t,80);
	CHECK(st_rip3_templates_button_add(t,90,2,ST_RIP3_GROUP_RADIO,true,redraw,&count)==0);
	CHECK(st_rip3_templates_associate(t,90,3,(const uint8_t *)"radio",5)==0);
	CHECK(st_rip3_templates_prepare_groups(saved,(const uint64_t[]){100,110},2,&prepared)==0);
	st_rip3_templates_apply_groups(t,prepared);prepared=NULL;
	CHECK(value(t,3,"")==0);
	/* Another surviving group retains cross-slot publication, even empty. */
	CHECK(st_rip3_templates_button_add(t,120,4,ST_RIP3_GROUP_CHECKBOX,false,redraw,&count)==0);
	CHECK(st_rip3_templates_associate(t,120,3,(const uint8_t *)"H",1)==0);
	st_rip3_templates_free_groups(saved);saved=NULL;
	CHECK(st_rip3_templates_capture_groups(t,(const uint64_t[]){100,110,120},3,&saved)==0);
	CHECK(st_rip3_templates_button_activate(t,120,redraw,&count)==0 && value(t,3,"H")==0);
	CHECK(st_rip3_templates_prepare_groups(saved,(const uint64_t[]){130,140,150},3,&prepared)==0);
	st_rip3_templates_apply_groups(t,prepared);prepared=NULL;
	CHECK(value(t,3,"")==0);
	CHECK(st_rip3_templates_button_activate(t,150,redraw,&count)==0 && value(t,3,"H")==0);
	st_rip3_templates_free_groups(saved);saved=NULL;
	/* Full per-owner bodies survive the aggregate cap; OFF and empty entries
	 * are retained, never replaced by the currently assembled bytes. */
	uint8_t a[3000],b[2000];memset(a,'A',sizeof(a));memset(b,'B',sizeof(b));
	CHECK(st_rip3_templates_associate(t,130,2,a,sizeof(a))==0);
	CHECK(st_rip3_templates_associate(t,140,2,b,sizeof(b))==0);
	CHECK(st_rip3_templates_associate(t,140,35,NULL,0)==0);
	CHECK(st_rip3_templates_associate(t,140,34,(const uint8_t *)"$TIME$",6)==0);
	CHECK(st_rip3_templates_capture_groups(t,(const uint64_t[]){130,140,150},3,&saved)==0);
	CHECK(st_rip3_templates_prepare_groups(saved,(const uint64_t[]){160,170,180},3,&prepared)==0);
	st_rip3_templates_apply_groups(t,prepared);prepared=NULL;
	extern bool st_rip3_templates_test_defined(const struct st_rip3_templates *,uint64_t,unsigned);
	CHECK(st_rip3_templates_test_defined(t,170,35) && !st_rip3_templates_test_defined(t,170,33));
	CHECK(value(t,34,"$TIME$")==0);
	CHECK(st_rip3_templates_button_activate(t,160,redraw,&count)==0);
	struct st_rip3_template_bytes v;CHECK(st_rip3_templates_resolve(t,2,&v)==0 && v.length==2000);
	for (size_t i=0;i<v.length;i++) CHECK(v.bytes[i]=='B');
	/* Every capture/prepare allocation fails independently, leaving both live
	 * state and a reusable saved snapshot intact. */
	extern size_t st_rip3_templates_test_attempts,st_rip3_templates_test_fail_at;
	for (size_t fail=0;;fail++) {
		st_rip3_templates_test_fail_at=st_rip3_templates_test_attempts+fail;
		int status=st_rip3_templates_capture_groups(t,(const uint64_t[]){160,170,180},3,&prepared);
		st_rip3_templates_test_fail_at=SIZE_MAX;
		if (status==0) {st_rip3_templates_free_groups(prepared);prepared=NULL;break;}
		CHECK(status==IO_ERROR_OUT_OF_MEMORY && prepared==NULL && fail<32);
		CHECK(st_rip3_templates_resolve(t,2,&v)==0 && v.length==2000);
	}
	for (size_t fail=0;;fail++) {
		st_rip3_templates_test_fail_at=st_rip3_templates_test_attempts+fail;
		int status=st_rip3_templates_prepare_groups(saved,(const uint64_t[]){190,200,210},3,&prepared);
		st_rip3_templates_test_fail_at=SIZE_MAX;
		if (status==0) {st_rip3_templates_free_groups(prepared);prepared=NULL;break;}
		CHECK(status==IO_ERROR_OUT_OF_MEMORY && prepared==NULL && fail<32);
	}
	CHECK(st_rip3_templates_prepare_groups(saved,(const uint64_t[]){190,200,210},3,&prepared)==0);
	st_rip3_templates_apply_groups(t,prepared);
	CHECK(st_rip3_templates_resolve(t,2,&v)==0 && v.length==4096);
	for (size_t i=0;i<v.length;i++) CHECK(v.bytes[i]==(i<3000 ? 'A' : 'B'));
	st_rip3_templates_free_groups(saved);st_rip3_templates_destroy(t);return 0;
}

int
main(void)
{
	CHECK(groups()==0 && snapshot_groups()==0);
	struct st_rip3_templates *slots=NULL;struct st_rip3_template_bytes view;
	CHECK(st_rip3_templates_create(&slots, false)==0);
	for (unsigned i=0; i<36; i++) CHECK(st_rip3_templates_resolve(slots,i,&view)==0 && view.length==0);
	CHECK(set(slots,3,"plain")==0 && set(slots,4,"inserts $?$ here")==0 && set(slots,5,"two $?$ and $?$ codes")==0);
	CHECK(chain(slots,(const uint8_t[]){3},1,"LOST","plain")==0);
	CHECK(chain(slots,(const uint8_t[]){4},1,"SOMETHING","inserts SOMETHING here")==0);
	CHECK(chain(slots,(const uint8_t[]){5},1,"HERE","two HERE and HERE codes")==0);
	CHECK(set(slots,1,"red green $?$ blue")==0 && set(slots,14,"LOUD QUIET $?$ YELL")==0
	    && set(slots,3,"soft $?$ hard smooth gritty")==0);
	CHECK(chain(slots,(const uint8_t[]){1,14,3},3,"(host command)",
	    "soft LOUD QUIET red green (host command) blue YELL hard smooth gritty")==0);
	CHECK(set(slots,3,"$D(1)$")==0 && set(slots,4,"FOUR")==0);
	CHECK(chain(slots,(const uint8_t[]){3,4},2,"X","FOUR")==0);
	CHECK(set(slots,3,"$?$")==0 && chain(slots,(const uint8_t[]){3},1,"$?$","$?$")==0);
	CHECK(set(slots,4,"print(\"$?$\")^m")==0);
	CHECK(chain(slots,(const uint8_t[]){4,4},2,"HELLO","print(\"print(\"HELLO\")^m\")^m")==0);
	uint8_t levels[36];memset(levels,3,sizeof(levels));
	CHECK(chain(slots,levels,36,"unchanged","unchanged")==0);
	/* Blank ordinary slots discard their feed; required blanks report the
	 * missing group without publishing either a partial result or length. */
	CHECK(chain(slots,(const uint8_t[]){0},1,"X","")==0);
	CHECK(required_group(slots,0,true)==0);
	view=(struct st_rip3_template_bytes){.length=99};
	CHECK(st_rip3_templates_resolve(slots,0,&view)==IO_ERROR_NOT_FOUND && view.length==99);
	uint8_t result[4096];memset(result,0x5a,sizeof(result));size_t length=99;unsigned missing=99;
	CHECK(st_rip3_templates_chain(slots,(const uint8_t[]){3,0,4},3,(const uint8_t *)"X",1,result,sizeof(result),&length,&missing)
	    ==IO_ERROR_NOT_FOUND && missing==0 && length==99);
	for (size_t i=0; i<sizeof(result); i++) CHECK(result[i]==0x5a);
	CHECK(set(slots,0,"selected")==0 && st_rip3_templates_resolve(slots,0,&view)==IO_ERROR_NOT_FOUND);
	uint64_t redraw[128];size_t changed;
	CHECK(st_rip3_templates_button_activate(slots,1000,redraw,&changed)==0
	    && st_rip3_templates_resolve(slots,0,&view)==0 && view.length==8);
	CHECK(set(slots,0,"")==0 && st_rip3_templates_resolve(slots,0,&view)==0 && view.length==0);
	CHECK(required_group(slots,0,false)==0 && st_rip3_templates_resolve(slots,0,&view)==0);
	/* Definitions own their capped bytes and can safely be replaced from
	 * their own borrowed view. Storage counts raw, unexpanded bytes. */
	uint8_t source[5000];memset(source,'A',sizeof(source));
	CHECK(st_rip3_templates_set(slots,35,source,sizeof(source))==0);memset(source,'B',sizeof(source));
	CHECK(st_rip3_templates_resolve(slots,35,&view)==0 && view.length==4096 && view.bytes[4095]=='A');
	CHECK(st_rip3_templates_set(slots,35,view.bytes,view.length)==0);
	CHECK(st_rip3_templates_chain(slots,(const uint8_t[]){35},1,NULL,0,result,sizeof(result),&length,NULL)==0 && length==4096);
	for (size_t i=0; i<length; i++) CHECK(result[i]=='A');
	CHECK(set(slots,35,"x$?$y$?$z")==0);
	CHECK(st_rip3_templates_chain(slots,(const uint8_t[]){35},1,source,sizeof(source),result,sizeof(result),&length,NULL)==0
	    && length==4096 && result[0]=='x');
	for (size_t i=1; i<length; i++) CHECK(result[i]=='B');
	const uint8_t terminated[]={0x80,0xff,0,'X'};
	CHECK(st_rip3_templates_set(slots,34,terminated,sizeof(terminated))==0
	    && st_rip3_templates_resolve(slots,34,&view)==0 && view.length==2 && view.bytes[0]==0x80 && view.bytes[1]==0xff);
	st_rip3_templates_test_fail=true;
	CHECK(st_rip3_templates_set(slots,3,(const uint8_t *)"lost",4)==IO_ERROR_OUT_OF_MEMORY);
	CHECK(st_rip3_templates_resolve(slots,3,&view)==0 && view.length==3 && memcmp(view.bytes,"$?$",3)==0);
	struct st_rip3_templates *old=slots;
	CHECK(st_rip3_templates_create(&slots, false)==IO_ERROR_OUT_OF_MEMORY && slots==old);
	st_rip3_templates_test_fail=false;
	CHECK(st_rip3_templates_create(NULL, false)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_templates_set(NULL,0,NULL,0)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_templates_set(slots,36,NULL,0)==IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_templates_set(slots,0,NULL,1)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_templates_set(slots,0,source,SIZE_MAX)==IO_ERROR_OUT_OF_RANGE);
	CHECK(required_group(slots,36,true)==IO_ERROR_OUT_OF_RANGE);
	CHECK(required_group(NULL,0,true)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_templates_resolve(slots,36,&view)==IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_templates_resolve(slots,0,NULL)==IO_ERROR_INVALID_ARGUMENT);
	length=99;missing=99;memset(result,0x5a,sizeof(result));
	CHECK(st_rip3_templates_chain(slots,levels,37,NULL,0,result,sizeof(result),&length,&missing)==IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_templates_chain(slots,levels,0,NULL,0,result,sizeof(result),&length,&missing)==IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_templates_chain(slots,(const uint8_t[]){3,36},2,NULL,0,result,sizeof(result),&length,&missing)==IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_templates_chain(slots,levels,1,NULL,1,result,sizeof(result),&length,&missing)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_templates_chain(slots,levels,1,NULL,0,result,4095,&length,&missing)==IO_ERROR_OUT_OF_RANGE);
	CHECK(length==99 && missing==99);
	for (size_t i=0; i<sizeof(result); i++) CHECK(result[i]==0x5a);
	st_rip3_templates_destroy(slots);st_rip3_templates_destroy(NULL);
	return 0;
}
