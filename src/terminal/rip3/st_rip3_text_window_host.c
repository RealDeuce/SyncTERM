/* Text-window control references; rendering belongs to the session owner. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_host_internal.h"
#include "st_rip3_text_window.h"
#include "io_error.h"

#include <string.h>

static int
window_number(const char *text, bool *current, unsigned *number)
{
	*current=strcmp(text,"CUR")==0;
	return *current ? 0 : st_rip3_host_number(text,0,35,number);
}

static int
current_number(const struct st_rip3_text_window_operations *windows, unsigned *number)
{
	if (windows->current==NULL) return IO_ERROR_NOT_SUPPORTED;
	int status=windows->current(windows->context,number);
	return status!=0 ? status : *number<36 ? 0 : IO_ERROR_OUT_OF_RANGE;
}

static int
coordinate(const char *text, bool *set, uint64_t *value)
{
	*set=strcmp(text,"CUR")!=0;
	*value=0;
	if (!*set) return 0;
	if (*text==0) return IO_ERROR_INVALID_FORMAT;
	for (size_t i=0;text[i]!=0;i++) {
		if (text[i]<'0' || text[i]>'9') return IO_ERROR_INVALID_FORMAT;
		unsigned digit=(unsigned)(text[i]-'0');
		/* All actual TW dimensions fit below UINT64_MAX. Saturating an
		 * otherwise legal 20-digit argument preserves nearest-border clamp,
		 * while still validating every remaining character. */
		*value=*value>(UINT64_MAX-digit)/10 ? UINT64_MAX : *value*10+digit;
	}
	return 0;
}

static int
position(const struct st_rip3_text_window_operations *windows,
    const struct st_rip3_builtin_reference *words, bool home)
{
	if (words->count>(home ? 1u : 3u)) return IO_ERROR_INVALID_FORMAT;
	bool current=words->count==0;
	unsigned number=0;
	int status=words->count==0 ? 0 : window_number(words->parameters[0].text,&current,&number);
	struct st_rip3_text_cursor_position target={.set_column=home,.set_row=home};
	if (status==0 && words->count>1)
		status=coordinate(words->parameters[1].text,&target.set_column,&target.column);
	if (status==0 && words->count>2)
		status=coordinate(words->parameters[2].text,&target.set_row,&target.row);
	if (status!=0) return status;
	/* TWGOTO without coordinates is a syntactically validated no-op. */
	if (!home && words->count<2) return 0;
	if (windows==NULL || windows->position==NULL) return IO_ERROR_NOT_SUPPORTED;
	if (current) status=current_number(windows,&number);
	return status!=0 ? status : windows->position(windows->context,number,&target);
}

int
st_rip3_text_window_host_execute(const struct st_rip3_host_variables *host,
    const struct st_rip3_builtin_reference *reference, char *result, size_t capacity)
{
	bool activation=strcmp(reference->name,"ATW")==0 || strcmp(reference->name,"DTW")==0;
	bool visibility=strcmp(reference->name,"CON")==0 || strcmp(reference->name,"COFF")==0;
	bool erase=strcmp(reference->name,"ETW")==0;
	bool erase_eol=strcmp(reference->name,"TWERASEEOL")==0;
	bool query=strcmp(reference->name,"TWIN")==0 || strcmp(reference->name,"ISEXTWIN")==0;
	bool home=strcmp(reference->name,"TWHOME")==0;
	if (!activation && !visibility && !erase && !erase_eol && !query && !home && strcmp(reference->name,"TWGOTO")!=0)
		return IO_ERROR_NOT_SUPPORTED;
	if (capacity==0) return IO_ERROR_OUT_OF_RANGE;
	struct st_rip3_builtin_reference words;
	int status=st_rip3_builtin_reference_words(&words,reference);
	if (status!=0) return status;
	for (size_t i=0;i<words.count;i++) {
		for (size_t j=0;j<words.parameters[i].length;j++) {
			unsigned char ch=(unsigned char)words.parameters[i].text[j];
			if (ch>='a' && ch<='z') words.parameters[i].text[j]=(char)(ch-'a'+'A');
		}
	}
	const struct st_rip3_text_window_operations *windows=host->text_windows;
	if (query) {
		if (words.count>1) return IO_ERROR_INVALID_FORMAT;
		unsigned number=0;bool current=words.count==0;
		if (words.count==1) status=window_number(words.parameters[0].text,&current,&number);
		if (status!=0) return status;
		if (windows==NULL || windows->info==NULL) return IO_ERROR_NOT_SUPPORTED;
		if (current) status=current_number(windows,&number);
		struct st_rip3_text_window_info info;
		if (status==0) status=windows->info(windows->context,number,&info);
		if (status!=0) return status;
		bool active=info.defined && info.active;
		const char *answer=strcmp(reference->name,"TWIN")==0 ? active ? "YES" : "NO"
		    : !active ? "-1" : info.extended ? "1" : "0";
		size_t length=strlen(answer);
		if (length>=capacity) return IO_ERROR_OUT_OF_RANGE;
		memcpy(result,answer,length+1);return 0;
	}
	if (!activation && !visibility && !erase && !erase_eol) {
		status=position(windows,&words,home);
		if (status==0) result[0]=0;
		return status;
	}
	bool selected[36]={0},current_requested=words.count==0;
	for (size_t i=0;i<words.count;i++) {
		const char *parameter=words.parameters[i].text;
		if (strcmp(parameter,"ALL")==0) {
			for (unsigned n=0;n<36;n++) selected[n]=true;
		}
		else {
			unsigned number=0;bool current;
			status=window_number(parameter,&current,&number);
			if (status!=0) return status;
			if (current) current_requested=true;
			else selected[number]=true;
		}
	}
	/* A later bad parameter must not cause even a current-slot lookup,
	 * much less partial effects on earlier valid targets. */
	if (windows==NULL || (erase ? windows->erase==NULL : erase_eol ? windows->erase_eol==NULL
	    : activation ? windows->activate==NULL : windows->cursor==NULL))
		return IO_ERROR_NOT_SUPPORTED;
	if (current_requested) {
		unsigned number;
		status=current_number(windows,&number);
		if (status!=0) return status;
		selected[number]=true;
	}
	unsigned numbers[36];size_t count=0;
	for (unsigned n=0;n<36;n++) if (selected[n]) numbers[count++]=n;
	bool enabled=strcmp(reference->name,activation ? "ATW" : "CON")==0;
	status=erase ? windows->erase(windows->context,numbers,count)
	    : erase_eol ? windows->erase_eol(windows->context,numbers,count)
	    : activation ? windows->activate(windows->context,numbers,count,enabled)
	    : windows->cursor(windows->context,numbers,count,enabled);
	if (status==0) result[0]=0;
	return status;
}
