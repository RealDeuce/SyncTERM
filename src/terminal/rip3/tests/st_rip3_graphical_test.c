/* Independent PR126 one-pass, prefix, prompt and provider-effect vectors. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_graphical.h"
#include "st_rip3_geometry.h"
#include "st_rip3_user_variables.h"
#include "st_rip3_templates.h"
#include "st_rip3_table_host.h"
#include "st_rip3_environments.h"
#include "io_error.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do {if (!(x)) {fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}} while (0)
struct fixture {
	struct st_rip3_graphical *worker;
	struct st_rip3_variables *session,*persistent;
	struct st_rip3_user_variables *user;
	struct st_rip3_template_host templates;
	struct st_rip3_host_variables variables;
	struct st_rip3_table_host tables;
	struct st_rip3_host_sink sink;
	struct st_rip3_frame frame;
	struct st_rip3_environments *env;
	unsigned errors;
	bool format_fail;
};
static void diagnostic(void *ctx,enum st_rip3_host_diagnostic kind) {(void)kind;((struct fixture *)ctx)->errors++;}
static int format(void *ctx,const struct st_rip3_variable_reference *reference,
    enum st_rip3_user_variable_source source,const struct st_rip3_variable_value *input,struct st_rip3_variable_value *out)
{
	(void)source;
	struct fixture *f=ctx;
	if (f->format_fail && strcmp(reference->name,"MISSING")==0) return IO_ERROR_IO;
	*out=*input;return 0;
}
static int initialize(struct fixture *f,bool converted)
{
	CHECK(st_rip3_variables_create(&f->session)==0 && st_rip3_variables_create(&f->persistent)==0);
	struct st_rip3_user_variable_host user={.session=f->session,.persistent=f->persistent,.context=f,.format=format};
	CHECK(st_rip3_user_variables_create(&f->user,&user)==0);
	CHECK(st_rip3_templates_create(&f->templates.slots)==0);
	CHECK(st_rip3_environments_create(&f->env,ST_RIP3_CANVAS_800_600)==0);
	f->tables.tables[ST_RIP3_TABLE_ENV].live=f->env;
	CHECK(st_rip3_frame_init(&f->frame,ST_RIP3_CANVAS_800_600)==0);
	f->variables=(struct st_rip3_host_variables){.frame=&f->frame,.user_variables=f->user,.templates=&f->templates,.tables=&f->tables};
	f->sink=(struct st_rip3_host_sink){.context=f,.diagnostic=diagnostic};
	CHECK(st_rip3_graphical_create(&f->worker,&f->variables,&f->sink,converted)==0);return 0;
}
static void destroy(struct fixture *f)
{
	st_rip3_graphical_destroy(f->worker);st_rip3_user_variables_destroy(f->user);
	st_rip3_variables_destroy(f->session);st_rip3_variables_destroy(f->persistent);
	st_rip3_templates_destroy(f->templates.slots);st_rip3_environments_destroy(f->env);
}
static int set(struct fixture *f,const char *name,const uint8_t *bytes,size_t length)
{
	CHECK(st_rip3_variables_set(f->session,name,strlen(name),bytes,length)==0);return 0;
}
static void finish(struct fixture *f)
{
	for (unsigned i=0;i<10000 && st_rip3_graphical_pending(f->worker);i++) {
		st_rip3_graphical_process(f->worker,1);
		if (st_rip3_graphical_state(f->worker)==ST_RIP3_GRAPHICAL_PROMPT) break;
	}
}
static int result(struct fixture *f,const char *expected)
{
	const struct st_rip3_condition_bytes *v=st_rip3_graphical_result(f->worker);
	CHECK(v!=NULL && v->length==strlen(expected) && memcmp(v->bytes,expected,v->length)==0);return 0;
}
static int run(struct fixture *f,enum st_rip3_graphical_consumer consumer,const char *source,const char *expected)
{
	st_rip3_graphical_start(f->worker,consumer,(const uint8_t *)source,strlen(source));finish(f);
	return result(f,expected);
}
int main(void)
{
	struct fixture f={0};CHECK(initialize(&f,false)==0);
	CHECK(set(&f,"V",(const uint8_t *)"$RESX$",6)==0);
	CHECK(st_rip3_templates_set(f.templates.slots,3,(const uint8_t *)"$RESX$",6)==0);
	CHECK(st_rip3_templates_set(f.templates.slots,4,(const uint8_t *)"$?3$",4)==0);
	for (unsigned consumer=0;consumer<4;consumer++) {
		CHECK(run(&f,(enum st_rip3_graphical_consumer)consumer,"\\$V$ ^M `` \\\\ x\\","$V$ ^M `` \\ x")==0);
		CHECK(run(&f,(enum st_rip3_graphical_consumer)consumer,"$?3$ $>SCENE.RIP$ $WORLD(CUR,1,2)$ $MKILL$", "$?3$ $>SCENE.RIP$ $WORLD(CUR,1,2)$ $MKILL$")==0);
		CHECK(run(&f,(enum st_rip3_graphical_consumer)consumer,
		    "$(F*?[]\\$NAME$ $<A(,).BMP$ $)\\$OFF\\$$",
		    "$(F*?[]\\$NAME$ $<A(,).BMP$ $)\\$OFF\\$$")==0);
		CHECK(run(&f,(enum st_rip3_graphical_consumer)consumer,"<<?3>>/<<?4>>/<<V>>","800/$?3$/800")==0);
		CHECK(run(&f,(enum st_rip3_graphical_consumer)consumer,"$COORDSIZE(CUR,5)$$RESX$","800")==0);
		struct st_rip3_environment env;CHECK(st_rip3_environments_get(f.env,0,&env)==0 && env.coordinate_size==5 && env.world_width==640);
		CHECK(run(&f,(enum st_rip3_graphical_consumer)consumer,
		    "$CDCS$/$LANG(TWOCHAR)$/<<LANG>>/$MSTAT$/$RESX(1)$","USASCII/en/eng/$MSTAT$/$RESX(1)$")==0);
		CHECK(run(&f,(enum st_rip3_graphical_consumer)consumer,
		    "$CDCS(1)$/$LANG(OTHER)$/<<IF LANG=eng>>yes<<ELSE>>no<<ENDIF>>",
		    "$CDCS(1)$/$LANG(OTHER)$/yes")==0);
	}
	CHECK(set(&f,"PASSWORD",(const uint8_t *)"stored",6)==0);
	for (int consumer=ST_RIP3_GRAPHICAL_FILENAME;consumer<=ST_RIP3_GRAPHICAL_WAVE;consumer++) {
		CHECK(run(&f,(enum st_rip3_graphical_consumer)consumer,
		    "A$PASSWORD$B<<USERNAME>>C$&password=fallback$D$INUSE(TV,password)$", "ABCD0")==0);
	}
	CHECK(run(&f,ST_RIP3_GRAPHICAL_TEXT,"$&V$","$RESX$")==0);
	CHECK(run(&f,ST_RIP3_GRAPHICAL_IMAGE,"$&V$","$RESX$")==0);
	CHECK(st_rip3_variables_set(f.persistent,"SAVED",5,(const uint8_t *)"",0)==0);
	CHECK(run(&f,ST_RIP3_GRAPHICAL_IMAGE,
	    "$INUSE(tv,v)$/$INUSE(TV,RESX)$/$INUSE(TV,ABSENT)$/$INUSE(TV,SAVED)$","1/1/0/1")==0);
	CHECK(run(&f,ST_RIP3_GRAPHICAL_IMAGE,"$INUSE(TV,0INVALID)$","$INUSE(TV,0INVALID)$")==0);
	CHECK(run(&f,ST_RIP3_GRAPHICAL_METRIC,"$&V$ $-=SIDE=Y$","$&V$ $-=SIDE=Y$")==0);
	char big[256];memset(big,'A',255);big[255]=0;CHECK(set(&f,"BIG",(const uint8_t *)big,255)==0);
	CHECK(run(&f,ST_RIP3_GRAPHICAL_LABEL,"$&BIG$$-=SIDE=Y$",big)==0);
	struct st_rip3_variable_value value;CHECK(st_rip3_variables_get(f.session,"SIDE",4,&value)==IO_ERROR_NOT_FOUND);
	/* Angle expansion must discard suffixes before the ordinary pass. */
	CHECK(run(&f,ST_RIP3_GRAPHICAL_LABEL,"<<BIG>>$-=SIDE=Y$",big)==0);
	CHECK(st_rip3_variables_get(f.session,"SIDE",4,&value)==IO_ERROR_NOT_FOUND);
	char image_name[1025], overlong[1045];
	memset(image_name, 'A', sizeof(image_name) - 1);
	image_name[sizeof(image_name) - 1] = '\0';
	snprintf(overlong, sizeof(overlong), "%s$-=SIDE=Y$", image_name);
	CHECK(run(&f,ST_RIP3_GRAPHICAL_IMAGE,overlong,image_name)==0);
	CHECK(st_rip3_variables_get(f.session,"SIDE",4,&value)==IO_ERROR_NOT_FOUND);
	const uint8_t nul[]={'A',0,'$','M','K','I','L','L','$'};
	CHECK(st_rip3_templates_set(f.templates.slots,5,nul,sizeof(nul))==0);
	CHECK(run(&f,ST_RIP3_GRAPHICAL_TEXT,"<<?5>>Z","AZ")==0);
	CHECK(set(&f,"NUL",nul,sizeof(nul))==0 && run(&f,ST_RIP3_GRAPHICAL_TEXT,"$&NUL$Z","AZ")==0);
	unsigned errors=f.errors;st_rip3_graphical_start(f.worker,ST_RIP3_GRAPHICAL_TEXT,(const uint8_t *)"X<<MKILL>>$-=SIDE=Y$",20);finish(&f);
	CHECK(st_rip3_graphical_state(f.worker)==ST_RIP3_GRAPHICAL_FAILED && f.errors==errors+1);
	CHECK(st_rip3_variables_get(f.session,"SIDE",4,&value)==IO_ERROR_NOT_FOUND);
	for (unsigned transparent=0;transparent<2;transparent++) for (unsigned cancel=0;cancel<2;cancel++) {
		const char *source=transparent ? "P$&MISSING$Z" : "P$MISSING$Z";
		st_rip3_graphical_start(f.worker,ST_RIP3_GRAPHICAL_TEXT,(const uint8_t *)source,strlen(source));finish(&f);
		CHECK(st_rip3_graphical_state(f.worker)==ST_RIP3_GRAPHICAL_PROMPT);
		for (unsigned i=0;i<3;i++) st_rip3_graphical_process(f.worker,99);
		CHECK(st_rip3_graphical_state(f.worker)==ST_RIP3_GRAPHICAL_PROMPT);
		st_rip3_user_variables_respond(f.user,(const uint8_t *)"Ready",5,cancel!=0);finish(&f);
		CHECK(result(&f,cancel ? (transparent ? "PZ" : "PNONEZ") : "PReadyZ")==0);
		CHECK(st_rip3_variables_get(f.session,"MISSING",7,&value)==IO_ERROR_NOT_FOUND);
	}
	const char *hard="$-=FIRST=X$$&MISSING$$-=SIDE=Y$";
	st_rip3_graphical_start(f.worker,ST_RIP3_GRAPHICAL_TEXT,(const uint8_t *)hard,strlen(hard));finish(&f);
	CHECK(st_rip3_graphical_state(f.worker)==ST_RIP3_GRAPHICAL_PROMPT);
	f.format_fail=true;st_rip3_user_variables_respond(f.user,(const uint8_t *)"Ready",5,false);finish(&f);
	CHECK(st_rip3_graphical_state(f.worker)==ST_RIP3_GRAPHICAL_FAILED && st_rip3_graphical_result(f.worker)==NULL);
	CHECK(st_rip3_variables_get(f.session,"FIRST",5,&value)==0 && value.length==1 && value.bytes[0]=='X');
	CHECK(st_rip3_variables_get(f.session,"SIDE",4,&value)==IO_ERROR_NOT_FOUND);
	f.format_fail=false;
	st_rip3_graphical_start(f.worker,ST_RIP3_GRAPHICAL_TEXT,(const uint8_t *)"$&PENDING$",10);finish(&f);
	CHECK(st_rip3_graphical_state(f.worker)==ST_RIP3_GRAPHICAL_PROMPT);
	st_rip3_graphical_cancel(f.worker);CHECK(st_rip3_user_variables_state(f.user)==ST_RIP3_USER_VARIABLE_IDLE);
	CHECK(run(&f,ST_RIP3_GRAPHICAL_TEXT,"alive","alive")==0);
	destroy(&f);f=(struct fixture){0};CHECK(initialize(&f,true)==0);
	char encoded[2045];for (size_t i=0;i<1022;i++) {encoded[2*i]=(char)0xc3;encoded[2*i+1]=(char)0xa9;}encoded[2044]=0;
	CHECK(run(&f,ST_RIP3_GRAPHICAL_TEXT,encoded,encoded)==0);
	/* Removing the quote recovers its own wire-byte budget without splitting
	 * the normalized representation of the quoted codepage byte. */
	char quoted[2047];quoted[0]='\\';memcpy(quoted+1,encoded,sizeof(encoded));
	encoded[2042]=0;
	CHECK(run(&f,ST_RIP3_GRAPHICAL_TEXT,quoted,encoded)==0);
	encoded[2042]=(char)0xc3;
	CHECK(st_rip3_templates_set(f.templates.slots,6,(const uint8_t *)encoded,2044)==0);
	CHECK(run(&f,ST_RIP3_GRAPHICAL_TEXT,"<<?6>>$COORDSIZE(CUR,5)$",encoded)==0);
	struct st_rip3_environment env;CHECK(st_rip3_environments_get(f.env,0,&env)==0 && env.coordinate_size==2);
	destroy(&f);return 0;
}
