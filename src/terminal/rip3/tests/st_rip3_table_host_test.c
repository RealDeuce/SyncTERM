/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_host_internal.h"
#include "st_rip3_table_host.h"
#include "st_rip3_backup.h"
#include "st_rip3_palettes.h"
#include "st_rip3_ports.h"
#include "io_error.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
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
	host.tables[ST_RIP3_TABLE_PORT].live = NULL;
	CHECK(execute(&host, "$COPY(PORT,BASE,TBL)$", output, sizeof(output)) == IO_ERROR_NOT_SUPPORTED);
	CHECK(execute(&host, "$COPY(PORT,BASE,S1)$", output, sizeof(output)) == 0);
	st_rip3_backup_destroy(backup);
	st_rip3_ports_destroy(ports);
	return 0;
}

int
main(void)
{
	CHECK(parser() == 0);
	CHECK(argument_bytes() == 0);
	CHECK(palette_expressions() == 0);
	CHECK(port_expressions() == 0);
	return 0;
}
