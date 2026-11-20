/* Text-window activation references; rendering belongs to the session owner. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_host_internal.h"
#include "st_rip3_text_window.h"
#include "io_error.h"

#include <string.h>

int
st_rip3_text_window_host_execute(const struct st_rip3_host_variables *host,
    const struct st_rip3_builtin_reference *reference, char *result, size_t capacity)
{
	bool active;
	if (strcmp(reference->name,"ATW")==0) active=true;
	else if (strcmp(reference->name,"DTW")==0) active=false;
	else return IO_ERROR_NOT_SUPPORTED;
	if (capacity==0) return IO_ERROR_OUT_OF_RANGE;
	struct st_rip3_builtin_reference words;
	int status=st_rip3_builtin_reference_words(&words,reference);
	if (status!=0) return status;
	bool selected[36]={0},current_requested=words.count==0;
	for (size_t i=0;i<words.count;i++) {
		char parameter[21];
		for (size_t j=0;j<=words.parameters[i].length;j++) {
			unsigned char ch=(unsigned char)words.parameters[i].text[j];
			parameter[j]=(char)(ch>='a' && ch<='z' ? ch-'a'+'A' : ch);
		}
		if (strcmp(parameter,"CUR")==0) current_requested=true;
		else if (strcmp(parameter,"ALL")==0) {
			for (unsigned n=0;n<36;n++) selected[n]=true;
		}
		else {
			unsigned number;
			status=st_rip3_host_number(parameter,0,35,&number);
			if (status!=0) return status;
			selected[number]=true;
		}
	}
	/* A later bad parameter must not cause even a current-slot lookup, much
	 * less a partial activation/deactivation of earlier valid targets. */
	const struct st_rip3_text_window_operations *windows=host->text_windows;
	if (windows==NULL || windows->activate==NULL) return IO_ERROR_NOT_SUPPORTED;
	if (current_requested) {
		unsigned number;
		if (windows->current==NULL) return IO_ERROR_NOT_SUPPORTED;
		status=windows->current(windows->context,&number);
		if (status!=0) return status;
		if (number>=36) return IO_ERROR_OUT_OF_RANGE;
		selected[number]=true;
	}
	unsigned numbers[36];size_t count=0;
	for (unsigned n=0;n<36;n++) if (selected[n]) numbers[count++]=n;
	status=windows->activate(windows->context,numbers,count,active);
	if (status==0) result[0]=0;
	return status;
}
