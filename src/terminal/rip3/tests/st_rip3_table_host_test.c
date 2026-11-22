/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_host_internal.h"
#include "st_rip3_table_host.h"
#include "st_rip3_backup.h"
#include "st_rip3_palettes.h"
#include "st_rip3_ports.h"
#include "st_rip3_mouse.h"
#include "io_error.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
		return 1; \
	} \
} while (0)

static int
execute(const struct st_rip3_table_host *host, const char *text,
    char *result, size_t capacity)
{
	struct st_rip3_builtin_reference reference;
	int status = st_rip3_builtin_reference_parse(&reference, (const uint8_t *)text, strlen(text));

	return status == 0 ? st_rip3_table_host_execute(host, &reference, result, capacity) : status;
}

static int
parser(void)
{
	struct st_rip3_builtin_reference reference, before;
	char text[1000];
	size_t length;
	const char *const malformed[] = {
		"", "$", "$$", "$1NAME$", "$_NAME$", "$A$B$", "$A($", "$A)$",
		"$A((B))$", "$A(B)C$", "$A(B$C)$", "$A(ONE)()$", "$NAME()$$", "$A,1$", "$HOSTDIR(\"\")$",
	};

	CHECK(st_rip3_builtin_reference_parse(&reference, (const uint8_t *)"$sCp$", 5) == 0);
	CHECK(strcmp(reference.name, "SCP") == 0 && reference.count == 0);
	CHECK(st_rip3_builtin_reference_parse(&reference, (const uint8_t *)"$SCP( )$", 8) == 0
	    && reference.count == 1 && reference.parameters[0].length == 1 && strcmp(reference.parameters[0].text," ")==0);
	CHECK(st_rip3_builtin_reference_parse(&reference, (const uint8_t *)"$copy(Pal, tBl , S3, BASE, s5)$", 31) == 0);
	CHECK(strcmp(reference.name, "COPY") == 0 && reference.count == 5);
	CHECK(strcmp(reference.parameters[0].text, "Pal") == 0 && reference.parameters[0].length == 3);
	CHECK(strcmp(reference.parameters[1].text, " tBl ") == 0 && strcmp(reference.parameters[4].text, " s5") == 0);
	CHECK(st_rip3_builtin_reference_parse(&reference, (const uint8_t *)"$ABCDEFGHIJKLMNOPQRST$", 22) == 0);
	CHECK(strlen(reference.name) == 20 && reference.count == 0);
	CHECK(st_rip3_builtin_reference_parse(&reference, (const uint8_t *)"$NAME(, x,)$", 12) == 0);
	CHECK(reference.count == 1 && reference.parameters[0].length == 2
	    && strcmp(reference.parameters[0].text," x")==0);
	CHECK(st_rip3_builtin_reference_parse(&reference,(const uint8_t *)"$RAND(9,,2)$",12)==0
	    && reference.count==2 && strcmp(reference.parameters[0].text,"9")==0 && strcmp(reference.parameters[1].text,"2")==0);
	CHECK(st_rip3_builtin_reference_parse(&reference,(const uint8_t *)"$NAME(,,)$",10)==0 && reference.count==0);
	CHECK(st_rip3_builtin_reference_parse(&reference,(const uint8_t *)"$HOSTDIR(MY BBS)$",17)==0
	    && reference.count==1 && strcmp(reference.parameters[0].text,"MY BBS")==0);
	memcpy(text, "$NAME(", 6); length = 6;
	for (unsigned i = 0; i < 40; i++) {
		if (i != 0) text[length++] = ',';
		memcpy(text + length, "ABCDEFGHIJKLMNOPQRST", 20); length += 20;
	}
	memcpy(text + length, ")$", 2); length += 2;
	CHECK(st_rip3_builtin_reference_parse(&reference, (const uint8_t *)text, length) == 0);
	CHECK(reference.count == 40 && reference.parameters[39].length == 20);
	/* Empty fields after forty real arguments consume no additional slot. */
	memcpy(text+length-2,",,,)$",5);length+=3;
	CHECK(st_rip3_builtin_reference_parse(&reference,(const uint8_t *)text,length)==0 && reference.count==40);
	before = reference;
	CHECK(st_rip3_builtin_reference_parse(&reference, (const uint8_t *)"$ABCDEFGHIJKLMNOPQRSTU$", 23) == IO_ERROR_OUT_OF_RANGE);
	memcpy(text + length - 2, ",A)$", 4); length += 2;
	CHECK(st_rip3_builtin_reference_parse(&reference, (const uint8_t *)text, length) == IO_ERROR_OUT_OF_RANGE);
	CHECK(memcmp(&reference, &before, sizeof(reference)) == 0);
	CHECK(st_rip3_builtin_reference_parse(&reference, (const uint8_t *)"$NAME(ABCDEFGHIJKLMNOPQRSTU)$", 29) == IO_ERROR_OUT_OF_RANGE);
	for (size_t i = 0; i < sizeof(malformed) / sizeof(*malformed); i++) {
		CHECK(st_rip3_builtin_reference_parse(&reference, (const uint8_t *)malformed[i], strlen(malformed[i])) == IO_ERROR_INVALID_FORMAT);
		CHECK(memcmp(&reference, &before, sizeof(reference)) == 0);
	}
	{
		const uint8_t nul[] = {'$','A','(','B',0,'C',')','$'};
		CHECK(st_rip3_builtin_reference_parse(&reference, nul, sizeof(nul)) == IO_ERROR_INVALID_FORMAT);
	}
	for (size_t i = 0; i < 18; i++) {
		CHECK(st_rip3_builtin_reference_parse(&reference, (const uint8_t *)"$COPY(PAL,TBL,S3)$", i) != 0);
		CHECK(memcmp(&reference, &before, sizeof(reference)) == 0);
	}
	CHECK(st_rip3_builtin_reference_parse(NULL, (const uint8_t *)"$A$", 3) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_builtin_reference_parse(&reference, NULL, 0) == IO_ERROR_INVALID_ARGUMENT);
	return 0;
}

static int
argument_bytes(void)
{
	struct st_rip3_builtin_reference reference, before;
	static const struct {
		const char *text, *first, *second;
	} examples[]={
		{"$F(MY BBS)$","MY BBS",NULL}, {"$F('AB')$","'AB'",NULL},
		{"$F(\"AB\")$",NULL,NULL}, {"$F(A,B)$","A","B"},
		{"$F(A\\,B)$",NULL,NULL}, {"$F(A(B)$","A(B",NULL},
		{"$F(A(B\\))$","A(B)",NULL}, {"$F(A\\)B)$","A)B",NULL},
		{"$F(A\\$B)$","A$B",NULL}, {"$F(\\$N\\$)$","$N$",NULL},
		{"$F(A$B)$",NULL,NULL}, {"$F(A\\B)$","AB",NULL},
		{"$F(A\\\\B)$",NULL,NULL}, {"$F(A/B)$",NULL,NULL},
		{"$F(A\\)$",NULL,NULL}, {"$F(A\\))$","A)",NULL},
		{"$F(\\\")$",NULL,NULL}, {"$F(\\/)$",NULL,NULL},
	};
	memset(&before,0x5a,sizeof(before));
	for (size_t i=0; i<sizeof(examples)/sizeof(examples[0]); i++) {
		memcpy(&reference,&before,sizeof(reference));
		int status=st_rip3_builtin_reference_parse(&reference,(const uint8_t *)examples[i].text,strlen(examples[i].text));
		if (examples[i].first==NULL) {
			CHECK(status==IO_ERROR_INVALID_FORMAT && memcmp(&reference,&before,sizeof(reference))==0);
			continue;
		}
		CHECK(status==0 && strcmp(reference.name,"F")==0);
		CHECK(reference.count==(examples[i].second==NULL ? 1u : 2u));
		CHECK(strcmp(reference.parameters[0].text,examples[i].first)==0
		    && reference.parameters[0].length==strlen(examples[i].first));
		if (examples[i].second!=NULL) CHECK(strcmp(reference.parameters[1].text,examples[i].second)==0);
	}
	/* Independent byte-set expectations from the published table. Probe each
	 * value as plain payload and after one local quote, preserving high bytes. */
	unsigned plain_count=0, quoted_count=0;
	for (unsigned byte=0; byte<256; byte++) {
		bool payload=byte>=32 && strchr("\"*+,/;<=>?[]|\\",(int)byte)==NULL;
		bool plain=payload && byte!='$' && byte!=')';
		const uint8_t raw[]={'$','F','(',(uint8_t)byte,')','$'};
		const uint8_t quoted[]={'$','F','(','\\',(uint8_t)byte,')','$'};
		for (unsigned mode=0; mode<2; mode++) {
			memcpy(&reference,&before,sizeof(reference));
			bool valid=mode!=0 ? payload : plain;
			int status=st_rip3_builtin_reference_parse(&reference,mode!=0 ? quoted : raw,
			    mode!=0 ? sizeof(quoted) : sizeof(raw));
			if (mode==0 && byte==',') CHECK(status==0 && reference.count==0);
			else if (valid) {
				CHECK(status==0 && reference.count==1 && reference.parameters[0].length==1
				    && (uint8_t)reference.parameters[0].text[0]==byte && reference.parameters[0].text[1]==0);
				if (mode==0) plain_count++; else quoted_count++;
			}
			else CHECK(status==IO_ERROR_INVALID_FORMAT && memcmp(&reference,&before,sizeof(reference))==0);
		}
	}
	CHECK(plain_count==208 && quoted_count==210);
	uint8_t text[1700];size_t length=0;
	memcpy(text,"$F(",3);length=3;
	for (unsigned i=0; i<20; i++) {text[length++]='\\';text[length++]='A';}
	memcpy(text+length,")$",2);length+=2;
	CHECK(st_rip3_builtin_reference_parse(&reference,text,length)==0
	    && reference.count==1 && reference.parameters[0].length==20
	    && strcmp(reference.parameters[0].text,"AAAAAAAAAAAAAAAAAAAA")==0);
	memcpy(&before,&reference,sizeof(before));
	memcpy(text+length-2,"\\A)$",4);length+=2;
	CHECK(st_rip3_builtin_reference_parse(&reference,text,length)==IO_ERROR_OUT_OF_RANGE
	    && memcmp(&reference,&before,sizeof(reference))==0);
	/* Forty decoded arguments can occupy twice as many source bytes. */
	memcpy(text,"$F(",3);length=3;
	for (unsigned argument=0; argument<40; argument++) {
		if (argument!=0) text[length++]=',';
		for (unsigned i=0; i<20; i++) {text[length++]='\\';text[length++]='a';}
	}
	memcpy(text+length,",,)$",4);length+=4;
	CHECK(st_rip3_builtin_reference_parse(&reference,text,length)==0 && reference.count==40
	    && reference.parameters[39].length==20 && strcmp(reference.parameters[39].text,"aaaaaaaaaaaaaaaaaaaa")==0);
	memcpy(&before,&reference,sizeof(before));
	memcpy(text+length-2,"\\B)$",4);length+=2;
	CHECK(st_rip3_builtin_reference_parse(&reference,text,length)==IO_ERROR_OUT_OF_RANGE
	    && memcmp(&reference,&before,sizeof(reference))==0);
	/* Raw CP437 bytes were checked above. UTF-8 payload remains bytes too:
	 * ten U+00E9 encodings fill twenty slots, eleven exceed the byte limit. */
	memcpy(text,"$F(",3);length=3;
	for (unsigned i=0; i<10; i++) {text[length++]=0xc3;text[length++]=0xa9;}
	memcpy(text+length,")$",2);length+=2;
	CHECK(st_rip3_builtin_reference_parse(&reference,text,length)==0
	    && reference.count==1 && reference.parameters[0].length==20
	    && memcmp(reference.parameters[0].text,text+3,20)==0);
	memcpy(&before,&reference,sizeof(before));
	text[length-2]=0xc3;text[length-1]=0xa9;
	memcpy(text+length,")$",2);length+=2;
	CHECK(st_rip3_builtin_reference_parse(&reference,text,length)==IO_ERROR_OUT_OF_RANGE
	    && memcmp(&reference,&before,sizeof(reference))==0);
	return 0;
}

static int
palette_expressions(void)
{
	struct st_rip3_table_host host = {0};
	struct st_rip3_palettes *palettes = NULL;
	struct st_rip3_backup *backup = NULL;
	struct st_rip3_palette palette;
	struct st_rip3_palette_info info;
	struct st_rip3_backup_info saves;
	char output[100], previous[100];
	const uint32_t red = 0xff0000, green = 0x00ff00, blue = 0x0000ff;
	unsigned current;
	const char *const invalid[] = {
		"$COPY(PAL,TBL,S6,BAD)$", "$COPY(PAL,BASE,S6,BASE)$", "$COPY(PAL,BASE,S6,POP)$",
		"$COPY(PAL,BASE,S6,S10)$", "$COPY(PAL,PUSH,S1)$", "$COPY(WRONG,TBL,S1)$",
		"$COPY(PAL,TBL)$", "$BACKSTAT(PAL,WRONG)$", "$BACKSTAT(PAL,USE,EXTRA)$",
		"$SCP(10)$", "$SCP(S0)$", "$SCP(TBL)$", "$SCP(POP)$", "$RCP(PUSH)$", "$RCP(BASE,0)$",
		"$COPY(PAL,12x,BASE)$",
	};

	CHECK(st_rip3_palettes_create(&palettes) == 0);
	CHECK(st_rip3_palettes_backup_create(&backup) == 0);
	host.tables[ST_RIP3_TABLE_PAL] = (struct st_rip3_table_binding){palettes,backup};
	CHECK(st_rip3_palettes_rgb(palettes, 2, 8, &red, 1) == 0);
	CHECK(st_rip3_palettes_switch(palettes, 4) == 0);
	CHECK(st_rip3_palettes_rgb(palettes, 2, 8, &green, 1) == 0);
	CHECK(st_rip3_palettes_protect(palettes, 4, true) == 0);
	CHECK(execute(&host, "$copy(Pal, tBl , S3, BASE, s5)$", output, sizeof(output)) == 0 && output[0] == 0);
	CHECK(execute(&host, "$BACKSTAT(PAL)$", output, sizeof(output)) == 0);
	CHECK(strcmp(output, "1:0:2:8:0:0:0:1:0:1:0:0:0:0") == 0);
	CHECK(st_rip3_palettes_protect(palettes, 4, false) == 0);
	CHECK(st_rip3_palettes_rgb(palettes, 2, 8, &blue, 1) == 0);
	CHECK(st_rip3_palettes_switch(palettes, 0) == 0);
	CHECK(execute(&host, "$RCP(3)$", output, sizeof(output)) == 0 && output[0] == 0);
	CHECK(st_rip3_palettes_current(palettes, &current) == 0 && current == 4);
	CHECK(st_rip3_palettes_get(palettes, 4, &palette) == 0 && palette.rgb[2] == green);
	CHECK(st_rip3_palettes_get(palettes, 0, &palette) == 0 && palette.rgb[2] == red);
	CHECK(st_rip3_palettes_info(palettes, 4, &info) == 0 && info.protected);
	CHECK(st_rip3_backup_info(backup, &saves) == 0 && !saves.slot_used[3]);
	CHECK(execute(&host, "$COPY(PAL,S5,PUSH,S7)$", output, sizeof(output)) == 0);
	CHECK(execute(&host, "$COPY(PAL,POP,TBL,BASE,S9)$", output, sizeof(output)) == 0);
	CHECK(st_rip3_backup_info(backup, &saves) == 0 && saves.stack == 0 && saves.slots == 3);
	CHECK(st_rip3_backup_protect(backup, 7, true) == 0);
	CHECK(execute(&host, "$BACKSTAT(PAL, pRot)$", output, sizeof(output)) == 0);
	CHECK(strcmp(output, "0:0:0:0:0:0:0:1:0:0") == 0);
	memset(output, 'Q', sizeof(output)); memcpy(previous, output, sizeof(output));
	for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
		int expected = i == 1 ? IO_ERROR_INVALID_ARGUMENT : IO_ERROR_INVALID_FORMAT;
		CHECK(execute(&host, invalid[i], output, sizeof(output)) == expected);
		CHECK(memcmp(output, previous, sizeof(output)) == 0);
		CHECK(st_rip3_backup_info(backup, &saves) == 0 && !saves.slot_used[6]);
	}
	CHECK(execute(&host, "$COPY(PAL,BASE,S6,S7)$", output, sizeof(output)) == IO_ERROR_PERMISSION_DENIED);
	CHECK(execute(&host, "$COPY(PAL,BASE,S6)$", output, 0) == IO_ERROR_OUT_OF_RANGE);
	CHECK(execute(&host, "$BACKSTAT(PAL)$", output, 2) == IO_ERROR_OUT_OF_RANGE);
	CHECK(execute(&host, "$COPY(PAL,3,5)$", output, sizeof(output)) == IO_ERROR_NOT_FOUND);
	CHECK(execute(&host, "$BACKSTAT(TW)$", output, sizeof(output)) == IO_ERROR_NOT_SUPPORTED);
	CHECK(execute(&host, "$IFS(JPEG)$", output, sizeof(output)) == IO_ERROR_NOT_SUPPORTED);
	CHECK(memcmp(output, previous, sizeof(output)) == 0);
	CHECK(st_rip3_backup_info(backup, &saves) == 0 && !saves.slot_used[6]);
	CHECK(execute(&host, "$SCP()$", output, sizeof(output)) == 0);
	CHECK(execute(&host, "$RCP$", output, sizeof(output)) == 0);
	CHECK(execute(&host, "$SCP(PUSH)$", output, sizeof(output)) == 0);
	CHECK(execute(&host, "$RCP(POP)$", output, sizeof(output)) == 0);
	CHECK(execute(&host, "$SCP(00)$", output, sizeof(output)) == 0);
	CHECK(execute(&host, "$RCP(0)$", output, sizeof(output)) == 0);
	{
		char command[240] = "$COPY(PAL,TBL";
		size_t length = strlen(command);
		struct st_rip3_builtin_reference reference;

		for (unsigned i = 0; i < 38; i++) {
			memcpy(command + length, ",BASE", 5); length += 5;
		}
		memcpy(command + length, ")$", 3);
		CHECK(execute(&host, command, output, sizeof(output)) == 0);
		CHECK(st_rip3_builtin_reference_parse(&reference, (const uint8_t *)"$SCP$", 5) == 0);
		CHECK(st_rip3_table_host_execute(NULL, &reference, output, sizeof(output)) == IO_ERROR_INVALID_ARGUMENT);
		CHECK(st_rip3_table_host_execute(&host, NULL, output, sizeof(output)) == IO_ERROR_INVALID_ARGUMENT);
		CHECK(st_rip3_table_host_execute(&host, &reference, NULL, 1) == IO_ERROR_INVALID_ARGUMENT);
		reference.count = 41;
		CHECK(st_rip3_table_host_execute(&host, &reference, output, sizeof(output)) == IO_ERROR_OUT_OF_RANGE);
	}
	st_rip3_backup_destroy(backup);
	st_rip3_palettes_destroy(palettes);
	return 0;
}

static int
port_expressions(void)
{
	struct st_rip3_table_host host = {0};
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_backup *backup = NULL;
	const struct st_rip3_rect bounds = {0,0,2,1};
	char output[100];
	uint8_t pixel;

	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_800_600) == 0);
	CHECK(st_rip3_ports_backup_create(&backup) == 0);
	host.tables[ST_RIP3_TABLE_PORT] = (struct st_rip3_table_binding){ports,backup};
	CHECK(st_rip3_ports_define(ports, 2, &bounds, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 2) == 0);
	CHECK(st_rip3_ports_erase(ports, 6) == 0);
	CHECK(execute(&host, "$COPY(PORT,TBL,BASE,PUSH)$", output, sizeof(output)) == 0);
	CHECK(st_rip3_ports_erase(ports, 9) == 0);
	CHECK(execute(&host, "$COPY(PORT,POP,TBL,S0)$", output, sizeof(output)) == 0);
	CHECK(st_rip3_ports_read(ports, 2, 0, 0, &pixel) == 0 && pixel == 6);
	CHECK(st_rip3_ports_erase(ports, 10) == 0);
	CHECK(execute(&host, "$COPY(PORT,S0,TBL)$", output, sizeof(output)) == 0);
	CHECK(st_rip3_ports_read(ports, 2, 0, 0, &pixel) == 0 && pixel == 6);
	CHECK(execute(&host, "$BACKSTAT(PORT,USE)$", output, sizeof(output)) == 0);
	CHECK(strcmp(output, "1:0:0:10:0:0:0:0:0:0:0:0:0:0") == 0);
	CHECK(execute(&host,"$SCB$",output,sizeof(output))==0 && output[0]==0);
	CHECK(execute(&host,"$SCB(3)$",output,sizeof(output))==0);
	CHECK(execute(&host,"$SCB(PUSH)$",output,sizeof(output))==0);
	CHECK(execute(&host,"$RCB$",output,sizeof(output))==0);
	CHECK(execute(&host,"$RCB(3)$",output,sizeof(output))==0);
	CHECK(execute(&host,"$RCB(POP)$",output,sizeof(output))==0);
	CHECK(execute(&host,"$INUSE(PORT,S3)$",output,sizeof(output))==0 && strcmp(output,"0")==0);
	CHECK(execute(&host,"$SCB(10)$",output,sizeof(output))==IO_ERROR_INVALID_FORMAT);
	CHECK(execute(&host,"$SCB(POP)$",output,sizeof(output))==IO_ERROR_INVALID_FORMAT);
	CHECK(execute(&host,"$RCB(PUSH)$",output,sizeof(output))==IO_ERROR_INVALID_FORMAT);
	CHECK(execute(&host,"$SCB(0,1)$",output,sizeof(output))==IO_ERROR_INVALID_FORMAT);
	host.tables[ST_RIP3_TABLE_PORT].live = NULL;
	CHECK(execute(&host, "$COPY(PORT,BASE,TBL)$", output, sizeof(output)) == IO_ERROR_NOT_SUPPORTED);
	CHECK(execute(&host, "$COPY(PORT,BASE,S1)$", output, sizeof(output)) == 0);
	st_rip3_backup_destroy(backup);
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
mouse_expressions(void)
{
	struct st_rip3_mouse *mouse=NULL;struct st_rip3_backup *backup=NULL;
	const struct st_rip3_mouse_sink sink={0};const struct st_rip3_rect area={10,10,30,30};
	CHECK(st_rip3_mouse_create(&mouse,&sink)==0 && st_rip3_mouse_backup_create(&backup)==0);
	CHECK(st_rip3_mouse_define(mouse,ST_RIP3_MOUSE_SIMPLE,NULL,&area,false,false,7,(const uint8_t *)"a",1,NULL)==0);
	struct st_rip3_table_host host={0};host.tables[ST_RIP3_TABLE_MOUSE]=(struct st_rip3_table_binding){mouse,backup};
	char output[100];
	CHECK(execute(&host,"$SMF$",output,sizeof(output))==0 && execute(&host,"$SMF(0)$",output,sizeof(output))==0);
	CHECK(execute(&host,"$SMF(PUSH)$",output,sizeof(output))==0);
	CHECK(execute(&host,"$BACKSTAT(MOUSE)$",output,sizeof(output))==0 && strcmp(output,"1:1:1:8:1:0:0:0:0:0:0:0:0:0")==0);
	CHECK(execute(&host,"$RESET(MOUSE,BASE,CUR)$",output,sizeof(output))==IO_ERROR_INVALID_FORMAT);
	CHECK(execute(&host,"$RESET(MOUSE,BASE)$",output,0)==IO_ERROR_OUT_OF_RANGE);
	CHECK(execute(&host,"$INUSE(MOUSE,BASE)$",output,sizeof(output))==0 && strcmp(output,"1")==0);
	CHECK(execute(&host,"$RESET(MOUSE)$",output,sizeof(output))==0 && st_rip3_mouse_count(mouse)==0);
	CHECK(execute(&host,"$RMF$",output,sizeof(output))==0 && st_rip3_mouse_count(mouse)==1);
	CHECK(execute(&host,"$PROT(MOUSE,S0)$",output,sizeof(output))==0 && execute(&host,"$RESET(MOUSE,ALL)$",output,sizeof(output))==0);
	CHECK(st_rip3_mouse_count(mouse)==0);
	CHECK(execute(&host,"$INUSE(MOUSE,BASE)$",output,sizeof(output))==0 && strcmp(output,"0")==0);
	CHECK(execute(&host,"$INUSE(MOUSE,STACK)$",output,sizeof(output))==0 && strcmp(output,"0")==0);
	CHECK(execute(&host,"$RMF(0)$",output,sizeof(output))==0 && st_rip3_mouse_count(mouse)==1);
	CHECK(execute(&host,"$INUSE(MOUSE,S0)$",output,sizeof(output))==0 && strcmp(output,"1")==0);
	CHECK(execute(&host,"$UNPROT(MOUSE,S0)$",output,sizeof(output))==0 && execute(&host,"$RESET(MOUSE,BACKUP)$",output,sizeof(output))==0);
	CHECK(st_rip3_mouse_count(mouse)==1);
	CHECK(execute(&host,"$INUSE(MOUSE,S0)$",output,sizeof(output))==0 && strcmp(output,"0")==0);
	CHECK(execute(&host,"$RESET(MOUSE,TBL)$",output,sizeof(output))==0 && st_rip3_mouse_count(mouse)==0);
	CHECK(execute(&host,"$COPY(MOUSE,0,1)$",output,sizeof(output))!=0);
	st_rip3_backup_destroy(backup);st_rip3_mouse_destroy(mouse);return 0;
}

/* Literal family IDs/order from the adopted wrapper contract. The callbacks
 * expose restore-versus-delete timing independently of the real payloads. */
struct action_log {char bytes[1024];size_t used;unsigned errors,early_delete;bool check_slots;struct st_rip3_table_host *host;};
struct action_value {struct action_log *log;unsigned id;int value;bool fail_save,fail_restore;};
static void action_event(struct action_log *log,char kind,unsigned id)
{
	if (log->used+2<sizeof(log->bytes)) {log->bytes[log->used++]=kind;log->bytes[log->used++]=(char)('0'+id);log->bytes[log->used]=0;}
}
static int action_clone(const void *source,void **result)
{
	const struct action_value *value=source;
	action_event(value->log,'S',value->id);
	if (value->fail_save) return IO_ERROR_OUT_OF_MEMORY;
	struct action_value *saved=malloc(sizeof(*saved));
	if (saved==NULL) return IO_ERROR_OUT_OF_MEMORY;
	*saved=*value;*result=saved;return 0;
}
static int action_restore(void *object,const void *snapshot)
{
	struct action_value *value=object;const struct action_value *saved=snapshot;
	action_event(value->log,'R',value->id);
	if (value->log->check_slots) for (unsigned i=0;i<ST_RIP3_TABLE_COUNT;i++) {
		struct st_rip3_backup_info info;
		if (st_rip3_backup_info(value->log->host->tables[i].backup,&info)!=0 || !info.slot_used[2]) value->log->early_delete++;
	}
	if (value->fail_restore) return IO_ERROR_IO;
	value->value=saved->value;return 0;
}
static void action_destroy(void *object)
{
	struct action_value *value=object;action_event(value->log,'D',value->id);free(value);
}
static void action_error(void *context,int status)
{
	struct action_log *log=context;if (status!=0) log->errors++;
}
static int aggregate_actions(void)
{
	struct st_rip3_table_host host={0};struct action_log log={.host=&host};
	struct action_value values[ST_RIP3_TABLE_COUNT];
	static const unsigned ids[]={3,0,1,4,2,5,7,6};
	const struct st_rip3_backup_ops ops={action_clone,action_restore,action_destroy};
	host.operation_context=&log;host.operation_failed=action_error;
	for (unsigned i=0;i<ST_RIP3_TABLE_COUNT;i++) {
		values[i]=(struct action_value){.log=&log,.id=ids[i],.value=10+(int)ids[i]};
		host.tables[i].live=&values[i];CHECK(st_rip3_backup_create(&host.tables[i].backup,&ops)==0);
	}
	char output[20]="unchanged";
	CHECK(execute(&host,"$SAVEALL(2)$",output,sizeof(output))==0 && output[0]==0);
	CHECK(strcmp(log.bytes,"S3S2S5S6S4S0S1S7")==0);
	for (unsigned i=0;i<ST_RIP3_TABLE_COUNT;i++) values[i].value=99;
	values[ST_RIP3_TABLE_MOUSE].fail_restore=true;values[ST_RIP3_TABLE_SCREEN].fail_restore=true;
	CHECK(st_rip3_backup_protect(host.tables[ST_RIP3_TABLE_STYLE].backup,2,true)==0);
	log.used=0;log.check_slots=true;
	CHECK(execute(&host,"$RESTOREALL(2)$",output,sizeof(output))==0 && output[0]==0);
	CHECK(strcmp(log.bytes,"R3R2R5R6R4R0R1R7D3D2D5D6D4D0D7")==0);
	CHECK(log.errors==2 && log.early_delete==0);
	for (unsigned i=0;i<ST_RIP3_TABLE_COUNT;i++) {
		struct st_rip3_backup_info info;CHECK(st_rip3_backup_info(host.tables[i].backup,&info)==0);
		CHECK(info.slot_used[2]==(i==ST_RIP3_TABLE_STYLE));
		CHECK(values[i].value==(i==ST_RIP3_TABLE_MOUSE || i==ST_RIP3_TABLE_SCREEN ? 99 : 10+(int)ids[i]));
		values[i].fail_restore=false;
	}
	log.used=0;log.check_slots=false;log.errors=0;
	values[ST_RIP3_TABLE_STYLE].value=88;
	CHECK(execute(&host,"$SAVEALL(2)$",output,sizeof(output))==0);
	CHECK(strcmp(log.bytes,"S3S2S5S6S4S0S7")==0 && log.errors==1);
	log.used=0;log.check_slots=true;
	CHECK(execute(&host,"$RESTOREALL(2)$",output,sizeof(output))==0);
	CHECK(strcmp(log.bytes,"R3R2R5R6R4R0R1R7D3D2D5D6D4D0D7")==0 && values[ST_RIP3_TABLE_STYLE].value==11 && log.early_delete==0);
	log.used=0;log.check_slots=false;log.errors=0;
	values[ST_RIP3_TABLE_PORT].fail_save=true;
	CHECK(execute(&host,"$SAVEALL(PUSH)$",output,sizeof(output))==0);
	CHECK(strcmp(log.bytes,"S3S2S5S6S4S0S1S7")==0 && log.errors==1);
	log.used=0;values[ST_RIP3_TABLE_PORT].fail_save=false;
	CHECK(execute(&host,"$RESTOREALL(POP)$",output,sizeof(output))==0);
	CHECK(strcmp(log.bytes,"R3D3R5D5R6D6R4D4R0D0R1D1R7D7")==0 && log.errors==2);
	log.used=0;CHECK(execute(&host,"$SAVEALL$",output,sizeof(output))==0);
	CHECK(strcmp(log.bytes,"S3S2S5S6S4S0S1S7")==0);
	for (unsigned i=0;i<2;i++) {
		log.used=0;CHECK(execute(&host,"$RESTOREALL(BASE)$",output,sizeof(output))==0);
		CHECK(strcmp(log.bytes,"R3R2R5R6R4R0R1R7")==0);
	}
	log.used=0;log.errors=0;
	for (unsigned i=0;i<ST_RIP3_TABLE_COUNT;i++) values[i].fail_restore=true;
	CHECK(execute(&host,"$RESTOREALL$",output,sizeof(output))==0 && log.errors==8);
	CHECK(strcmp(log.bytes,"R3R2R5R6R4R0R1R7")==0);
	for (unsigned i=0;i<ST_RIP3_TABLE_COUNT;i++) values[i].fail_restore=false;
	log.used=0;strcpy(output,"unchanged");
	const char *invalid[]={"$SAVEALL(POP)$","$RESTOREALL(PUSH)$","$SAVEALL(10)$","$SAVEALL(BASE,2)$"};
	for (unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) CHECK(execute(&host,invalid[i],output,sizeof(output))==IO_ERROR_INVALID_FORMAT);
	CHECK(execute(&host,"$SAVEALL$",output,0)==IO_ERROR_OUT_OF_RANGE && log.used==0 && strcmp(output,"unchanged")==0);
	void *old_live=host.tables[ST_RIP3_TABLE_TW].live;
	host.tables[ST_RIP3_TABLE_TW].live=NULL;log.errors=0;
	CHECK(execute(&host,"$SAVEALL(4)$",output,sizeof(output))==0 && log.errors==1);
	CHECK(strcmp(log.bytes,"S2S5S6S4S0S1S7")==0);
	host.tables[ST_RIP3_TABLE_TW].live=old_live;log.used=0;
	/* The four newly connected names select their own payload families. */
	CHECK(execute(&host,"$SGS(0)$",output,sizeof(output))==0 && execute(&host,"$RGS(0)$",output,sizeof(output))==0);
	CHECK(execute(&host,"$SENV(0)$",output,sizeof(output))==0 && execute(&host,"$RENV(0)$",output,sizeof(output))==0);
	CHECK(strcmp(log.bytes,"S1R1D1S7R7D7")==0);
	for (unsigned i=0;i<ST_RIP3_TABLE_COUNT;i++) st_rip3_backup_destroy(host.tables[i].backup);
	return 0;
}

int
main(void)
{
	CHECK(aggregate_actions()==0);
	CHECK(parser() == 0);
	CHECK(argument_bytes() == 0);
	CHECK(palette_expressions() == 0);
	CHECK(port_expressions() == 0);
	CHECK(mouse_expressions() == 0);
	return 0;
}
