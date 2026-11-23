/* Text-window control references; rendering belongs to the session owner. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_host_internal.h"
#include "st_rip3_text_window.h"
#include "st_rip3_environments.h"
#include "io_error.h"

#include <string.h>
#include <inttypes.h>
#include <stdio.h>

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

enum window_query { Q_TWIN,Q_EXT,Q_CURSOR,Q_X,Q_Y,Q_FONT,Q_WIDTH,Q_HEIGHT,Q_X0,Q_X1,Q_Y0,Q_Y1 };

static int
query_kind(const char *name)
{
	static const char *const names[]={"TWIN","ISEXTWIN","CURSOR","CURX","CURY","TWFONT","TWW","TWH",
	    "TWX0","TWX1","TWY0","TWY1"};
	for (size_t i=0;i<sizeof(names)/sizeof(*names);i++) if (strcmp(name,names[i])==0) return (int)i;
	return -1;
}

static int
query_frame(const struct st_rip3_host_variables *host, struct st_rip3_frame *result)
{
	struct st_rip3_environments *environments=host->tables==NULL ? NULL : host->tables->tables[ST_RIP3_TABLE_ENV].live;
	if (environments!=NULL) {
		unsigned current;int status=st_rip3_environments_current(environments,&current);
		return status!=0 ? status : st_rip3_environments_frame(environments,current,result);
	}
	if (host->frame==NULL) return IO_ERROR_NOT_SUPPORTED;
	*result=*host->frame;
	return st_rip3_frame_world(result,result->world_width,result->world_height);
}

static int
world_value(uint64_t pixels, uint32_t world, uint32_t device, uint64_t *result)
{
	if (device==0) return IO_ERROR_INVALID_ARGUMENT;
	uint64_t whole=pixels/device,remainder=pixels%device;
	/* Avoid an overflowing pixels*WORLD product for legal wide offscreen
	 * windows. The remainder product is bounded by the frame's word WORLD. */
	if (world!=0 && whole>UINT64_MAX/world) return IO_ERROR_OUT_OF_RANGE;
	uint64_t value=whole*world,part=remainder*world/device;
	if (part>UINT64_MAX-value) return IO_ERROR_OUT_OF_RANGE;
	*result=value+part;return 0;
}

static int
window_query(const struct st_rip3_host_variables *host,
    const struct st_rip3_builtin_reference *words, int kind, char *result, size_t capacity)
{
	bool size=kind==Q_WIDTH || kind==Q_HEIGHT,corner=kind>=Q_X0;
	bool vertical=kind==Q_HEIGHT || kind==Q_Y0 || kind==Q_Y1;
	bool far=kind==Q_X1 || kind==Q_Y1;
	if (words->count>(size || corner ? 3u : 1u)) return IO_ERROR_INVALID_FORMAT;
	unsigned number=0;bool current=words->count==0;
	int status=words->count==0 ? 0 : window_number(words->parameters[0].text,&current,&number);
	if (status!=0) return status;
	enum { TEXT,BOUND,CELL } type=TEXT;
	enum { CELLS,WORLD,DEVICE } domain=size ? CELLS : WORLD;
	if (words->count>=2) {
		const char *word=words->parameters[1].text;
		if (strcmp(word,"BOUND")==0) type=BOUND;
		else if (size && strcmp(word,"CELL")==0) type=CELL;
		else if (strcmp(word,"TEXT")!=0) return IO_ERROR_INVALID_FORMAT;
		domain=type==TEXT && size ? CELLS : WORLD;
	}
	if (words->count==3) {
		const char *word=words->parameters[2].text;
		if (strcmp(word,"WORLD")==0) domain=WORLD;
		else if (strcmp(word,"DEVICE")==0) domain=DEVICE;
		else if (type==TEXT && size && strcmp(word,vertical ? "ROWS" : "COLS")==0) domain=CELLS;
		else return IO_ERROR_INVALID_FORMAT;
	}
	const struct st_rip3_text_window_operations *windows=host->text_windows;
	if (windows==NULL || windows->info==NULL) return IO_ERROR_NOT_SUPPORTED;
	if (current) status=current_number(windows,&number);
	struct st_rip3_text_window_info info;
	if (status==0) status=windows->info(windows->context,number,&info);
	if (status!=0) return status;
	bool active=info.defined && info.active;
	const char *answer=NULL;
	uint64_t value=0;
	struct st_rip3_text_window_snapshot snapshot={0};
	if (kind==Q_TWIN) answer=active ? "YES" : "NO";
	else if (kind==Q_EXT) answer=!active ? "-1" : info.extended ? "1" : "0";
	else if (!active) answer=corner ? "-1" : kind==Q_CURSOR ? "NO" : "0";
	else {
		if (windows->snapshot==NULL) return IO_ERROR_NOT_SUPPORTED;
		status=windows->snapshot(windows->context,number,&snapshot);
		if (status!=0) return status;
		const struct st_rip3_text_window_layout *layout=&snapshot.layout;
		if (kind==Q_CURSOR) answer=snapshot.cursor_visible ? "YES" : "NO";
		else if (kind==Q_X) value=snapshot.column;
		else if (kind==Q_Y) value=snapshot.row;
		else if (kind==Q_FONT) {
			/* Startup font/default initialization is still under #105. */
			if (!snapshot.font_known) return IO_ERROR_NOT_SUPPORTED;
			if (layout->font>4) return IO_ERROR_OUT_OF_RANGE;
			value=layout->font+1;
		}
		else if (size && domain==CELLS) value=vertical ? layout->rows : layout->columns;
		else if (corner && words->count<2) {
			if (layout->extended) answer="-1";
			else value=vertical ? far ? layout->y1 : layout->y0 : far ? layout->x1 : layout->x0;
		}
		else {
			uint64_t extent=type==CELL ? vertical ? layout->cell_height : layout->cell_width
			    : type==BOUND ? vertical ? layout->bound_height : layout->bound_width
			    : vertical ? (uint64_t)layout->rows*layout->cell_height : (uint64_t)layout->columns*layout->cell_width;
			value=extent;
			if (corner) {
				value=type==BOUND ? vertical ? layout->bound_y : layout->bound_x : vertical ? layout->y : layout->x;
				if (far) {if (extent>UINT64_MAX-value) return IO_ERROR_OUT_OF_RANGE;value+=extent;}
			}
			if (domain==WORLD) {
				struct st_rip3_frame frame;status=query_frame(host,&frame);
				if (status==0) status=world_value(value,vertical ? frame.world_height : frame.world_width,
				    vertical ? frame.device_height : frame.device_width,&value);
				if (status!=0) return status;
			}
		}
	}
	char text[32];
	if (answer==NULL) {
		int length=snprintf(text,sizeof(text),"%" PRIu64,value);
		if (length<0 || (size_t)length>=sizeof(text)) return IO_ERROR_IO;
		answer=text;
	}
	size_t length=strlen(answer);
	if (length>=capacity) return IO_ERROR_OUT_OF_RANGE;
	memcpy(result,answer,length+1);return 0;
}

static int
maximize_windows(const struct st_rip3_text_window_operations *windows,
    const struct st_rip3_builtin_reference *words)
{
	struct {
		bool current, all;
		unsigned number;
	} targets[40] = {0};
	size_t count = words->count == 0 ? 1 : words->count;
	targets[0].current = words->count == 0;
	for (size_t i = 0; i < words->count; ++i) {
		const char *parameter = words->parameters[i].text;
		targets[i].all = strcmp(parameter, "ALL") == 0;
		if (!targets[i].all) {
			int status = window_number(parameter, &targets[i].current, &targets[i].number);
			if (status != 0) return status;
		}
	}
	if (windows == NULL || windows->maximize == NULL) return IO_ERROR_NOT_SUPPORTED;
	int first_error = 0;
	for (size_t i = 0; i < count; ++i) {
		unsigned first = targets[i].number, last = first;
		if (targets[i].all)
			last = 35;
		else if (targets[i].current) {
			int status = current_number(windows, &first);
			if (status != 0) {
				if (first_error == 0) first_error = status;
				continue;
			}
			last = first;
		}
		for (unsigned number = first; number <= last; ++number) {
			int status = windows->maximize(windows->context, number);
			if (first_error == 0 && status != 0) first_error = status;
		}
	}
	return first_error;
}

int
st_rip3_text_window_host_execute(const struct st_rip3_host_variables *host,
    const struct st_rip3_builtin_reference *reference, char *result, size_t capacity)
{
	bool activation=strcmp(reference->name,"ATW")==0 || strcmp(reference->name,"DTW")==0;
	bool visibility=strcmp(reference->name,"CON")==0 || strcmp(reference->name,"COFF")==0;
	bool erase=strcmp(reference->name,"ETW")==0;
	bool erase_eol=strcmp(reference->name,"TWERASEEOL")==0;
	int kind=query_kind(reference->name);
	bool query=kind>=0;
	bool home=strcmp(reference->name,"TWHOME")==0;
	bool maximize=strcmp(reference->name,"MTW")==0;
	if (!activation && !visibility && !erase && !erase_eol && !query && !home && !maximize
	    && strcmp(reference->name,"TWGOTO")!=0)
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
	if (query) return window_query(host,&words,kind,result,capacity);
	if (maximize) {
		status=maximize_windows(windows,&words);
		if (status==0) result[0]=0;
		return status;
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
