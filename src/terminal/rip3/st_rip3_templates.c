/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_templates.h"
#include "io_error.h"

#include <stdlib.h>
#include <string.h>

struct slot { uint8_t *bytes; size_t length; int publisher; };
struct member {
	struct member *next;
	uint64_t token;unsigned group;enum st_rip3_group_kind kind;bool selected;
	struct {uint8_t *bytes;size_t length;} bodies[36];
};
struct st_rip3_templates {
	struct slot slots[36];
	struct member *members;
	size_t count;
	/* One transient lookup result; stored owner bodies remain unexpanded. */
	uint8_t aggregate[4096];
};

int
st_rip3_templates_create(struct st_rip3_templates **result)
{
	if (result==NULL) return IO_ERROR_INVALID_ARGUMENT;
	struct st_rip3_templates *templates=calloc(1,sizeof(*templates));
	if (templates==NULL) return IO_ERROR_OUT_OF_MEMORY;
	for (size_t i=0;i<36;i++) templates->slots[i].publisher=-1;
	*result=templates;return 0;
}

void
st_rip3_templates_destroy(struct st_rip3_templates *templates)
{
	if (templates==NULL) return;
	while (templates->members!=NULL) st_rip3_templates_button_remove(templates,templates->members->token);
	for (unsigned i=0; i<36; i++) free(templates->slots[i].bytes);
	free(templates);
}

int
st_rip3_templates_set(struct st_rip3_templates *templates, unsigned number,
    const uint8_t *bytes, size_t length)
{
	if (templates==NULL || (bytes==NULL && length!=0)) return IO_ERROR_INVALID_ARGUMENT;
	if (number>=36 || length==SIZE_MAX) return IO_ERROR_OUT_OF_RANGE;
	if (length>4096) length=4096;
	const uint8_t *end=length==0 ? NULL : memchr(bytes,0,length);
	if (end!=NULL) length=(size_t)(end-bytes);
	uint8_t *copy=NULL;
	if (length!=0) {
		copy=malloc(length);
		if (copy==NULL) return IO_ERROR_OUT_OF_MEMORY;
		memcpy(copy,bytes,length);
	}
	free(templates->slots[number].bytes);
	templates->slots[number].bytes=copy;templates->slots[number].length=length;templates->slots[number].publisher=-1;
	return 0;
}

static void
effective(const struct st_rip3_templates *templates,unsigned number,struct st_rip3_template_bytes *result)
{
	const struct slot *slot=&templates->slots[number];
	if (slot->publisher<0) {*result=(struct st_rip3_template_bytes){slot->bytes,slot->length};return;}
	/* Lookup work storage does not change the logical table. It is borrowed
	 * only until the next lookup; all insertion/chain consumers copy it. */
	struct st_rip3_templates *working=(struct st_rip3_templates *)templates;size_t length=0;
	for (const struct member *m=templates->members;m!=NULL;m=m->next) if (m->group==(unsigned)slot->publisher && m->selected) {
		size_t count=m->bodies[number].length;if (count>4096-length) count=4096-length;
		if (count!=0) memcpy(working->aggregate+length,m->bodies[number].bytes,count);
		length+=count;
	}
	*result=(struct st_rip3_template_bytes){working->aggregate,length};
}

int
st_rip3_templates_resolve(const struct st_rip3_templates *templates, unsigned number,
    struct st_rip3_template_bytes *result)
{
	if (templates==NULL || result==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (number>=36) return IO_ERROR_OUT_OF_RANGE;
	bool radio=false,selected=false;
	for (const struct member *m=templates->members;m!=NULL;m=m->next) if (m->group==number && m->kind==ST_RIP3_GROUP_RADIO) {
		radio=true;selected|=m->selected;
	}
	if (radio && !selected) return IO_ERROR_NOT_FOUND;
	struct st_rip3_template_bytes value;effective(templates,number,&value);
	*result=value;return 0;
}

static struct member *
find_member(const struct st_rip3_templates *templates,uint64_t token)
{
	for (struct member *m=templates->members;m!=NULL;m=m->next) if (m->token==token) return m;
	return NULL;
}

int
st_rip3_templates_group_check(const struct st_rip3_templates *templates,unsigned group,enum st_rip3_group_kind kind)
{
	if (templates==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (group>=36 || (kind!=ST_RIP3_GROUP_CHECKBOX && kind!=ST_RIP3_GROUP_RADIO)) return IO_ERROR_OUT_OF_RANGE;
	for (const struct member *m=templates->members;m!=NULL;m=m->next)
		if (m->group==group && m->kind!=kind) return IO_ERROR_CONFLICT;
	return 0;
}

static void
select_member(struct st_rip3_templates *templates,struct member *target,bool selected,uint64_t redraw[128],size_t *count)
{
	*count=0;
	if (target->kind==ST_RIP3_GROUP_RADIO && selected) for (struct member *m=templates->members;m!=NULL;m=m->next)
		if (m!=target && m->group==target->group && m->selected) {m->selected=false;redraw[(*count)++]=m->token;}
	target->selected=selected;redraw[(*count)++]=target->token;
}

int
st_rip3_templates_button_add(struct st_rip3_templates *templates,uint64_t token,unsigned group,
    enum st_rip3_group_kind kind,bool selected,uint64_t redraw[128],size_t *count)
{
	if (templates==NULL || redraw==NULL || count==NULL) return IO_ERROR_INVALID_ARGUMENT;
	int status=st_rip3_templates_group_check(templates,group,kind);
	if (status!=0) return status;
	if (token==0 || find_member(templates,token)!=NULL || templates->count==128) return IO_ERROR_OUT_OF_RANGE;
	struct member *m=calloc(1,sizeof(*m));if (m==NULL) return IO_ERROR_OUT_OF_MEMORY;
	*m=(struct member){.token=token,.group=group,.kind=kind};
	struct member **tail=&templates->members;while (*tail!=NULL) tail=&(*tail)->next;*tail=m;templates->count++;
	select_member(templates,m,selected,redraw,count);return 0;
}

void
st_rip3_templates_button_remove(struct st_rip3_templates *templates,uint64_t token)
{
	if (templates==NULL) return;
	struct member **link=&templates->members;while (*link!=NULL && (*link)->token!=token) link=&(*link)->next;
	if (*link==NULL) return;
	struct member *m=*link;unsigned group=m->group;*link=m->next;templates->count--;
	for (unsigned i=0;i<36;i++) free(m->bodies[i].bytes);free(m);
	for (m=templates->members;m!=NULL;m=m->next) if (m->group==group) return;
	for (unsigned i=0;i<36;i++) if (templates->slots[i].publisher==(int)group) templates->slots[i].publisher=-1;
}

bool
st_rip3_templates_button_selected(const struct st_rip3_templates *templates,uint64_t token)
{
	const struct member *m=templates==NULL ? NULL : find_member(templates,token);return m!=NULL && m->selected;
}

int
st_rip3_templates_button_activate(struct st_rip3_templates *templates,uint64_t token,uint64_t redraw[128],size_t *count)
{
	if (templates==NULL || redraw==NULL || count==NULL) return IO_ERROR_INVALID_ARGUMENT;
	struct member *m=find_member(templates,token);if (m==NULL) return IO_ERROR_NOT_FOUND;
	select_member(templates,m,m->kind==ST_RIP3_GROUP_RADIO || !m->selected,redraw,count);return 0;
}

int
st_rip3_templates_associate(struct st_rip3_templates *templates,uint64_t token,unsigned number,const uint8_t *bytes,size_t length)
{
	if (templates==NULL || (bytes==NULL && length!=0)) return IO_ERROR_INVALID_ARGUMENT;
	if (number>=36 || length==SIZE_MAX) return IO_ERROR_OUT_OF_RANGE;
	struct member *m=find_member(templates,token);if (m==NULL) return 0;
	if (length>4096) length=4096;
	const uint8_t *end=length==0 ? NULL : memchr(bytes,0,length);if (end!=NULL) length=(size_t)(end-bytes);
	uint8_t *copy=length==0 ? NULL : malloc(length);if (length!=0 && copy==NULL) return IO_ERROR_OUT_OF_MEMORY;
	if (length!=0) memcpy(copy,bytes,length);free(m->bodies[number].bytes);
	m->bodies[number].bytes=copy;m->bodies[number].length=length;templates->slots[number].publisher=(int)m->group;return 0;
}

int
st_rip3_templates_preserve(struct st_rip3_templates *templates)
{
	if (templates==NULL) return IO_ERROR_INVALID_ARGUMENT;
	uint8_t *copies[36]={0};size_t lengths[36]={0};
	for (unsigned i=0;i<36;i++) if (templates->slots[i].publisher>=0) {
		struct st_rip3_template_bytes value;effective(templates,i,&value);lengths[i]=value.length;
		if (value.length!=0) {
			copies[i]=malloc(value.length);
			if (copies[i]==NULL) {for (unsigned j=0;j<36;j++) free(copies[j]);return IO_ERROR_OUT_OF_MEMORY;}
			memcpy(copies[i],value.bytes,value.length);
		}
	}
	for (unsigned i=0;i<36;i++) if (templates->slots[i].publisher>=0) {
		free(templates->slots[i].bytes);templates->slots[i].bytes=copies[i];templates->slots[i].length=lengths[i];templates->slots[i].publisher=-1;
	}
	return 0;
}

static void
append(uint8_t result[4096], size_t *length, const uint8_t *bytes, size_t count)
{
	if (count>4096-*length) count=4096-*length;
	if (count!=0) memcpy(result+*length,bytes,count);
	*length+=count;
}

int
st_rip3_templates_chain(const struct st_rip3_templates *templates,
    const uint8_t *numbers, size_t count, const uint8_t *feed, size_t feed_length,
    uint8_t *result, size_t capacity, size_t *length, unsigned *missing)
{
	if (templates==NULL || numbers==NULL || result==NULL || length==NULL
	    || (feed_length!=0 && feed==NULL)) return IO_ERROR_INVALID_ARGUMENT;
	if (count==0 || count>36 || capacity<4096) return IO_ERROR_OUT_OF_RANGE;
	for (size_t i=0; i<count; i++) if (numbers[i]>=36) return IO_ERROR_OUT_OF_RANGE;
	uint8_t buffers[2][4096];
	size_t used=0;unsigned current=0;
	append(buffers[current],&used,feed,feed_length);
	for (size_t i=0; i<count; i++) {
		struct st_rip3_template_bytes value;
		int status=st_rip3_templates_resolve(templates,numbers[i],&value);
		if (status!=0) {
			if (status==IO_ERROR_NOT_FOUND && missing!=NULL) *missing=numbers[i];
			return status;
		}
		unsigned next=current^1;size_t produced=0;
		for (size_t at=0; at<value.length && produced<4096;) {
			if (value.length-at>=3 && memcmp(value.bytes+at,"$?$",3)==0) {
				append(buffers[next],&produced,buffers[current],used);at+=3;
			}
			else append(buffers[next],&produced,value.bytes+at++,1);
		}
		current=next;used=produced;
	}
	if (used!=0) memcpy(result,buffers[current],used);
	*length=used;return 0;
}
