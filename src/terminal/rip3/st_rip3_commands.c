/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_command_internal.h"
#include "st_rip3_audio.h"
#include "st_rip3_environments.h"
#include "st_rip3_input.h"
#include "st_rip3_mouse.h"
#include "st_rip3_query_internal.h"
#include "st_rip3_number.h"
#include "st_rip3_palettes.h"
#include "st_rip3_ports.h"
#include "st_rip3_styles.h"
#include "st_rip3_text_window.h"
#include "st_rip3_maf.h"
#include "string_compat.h"
#include "io_error.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int
st_rip3_command_field(uint64_t *result, const uint8_t *bytes, size_t length, unsigned radix)
{
	uint8_t digits[5];

	if (length == 0 || length > sizeof(digits))
		return IO_ERROR_INVALID_FORMAT;
	/* This is a documented wire quirk, not a change to the strict reusable
	 * digit codec or to the frozen legacy number reader. */
	for (size_t i = 0; i < length; i++)
		digits[i] = radix == 36 && bytes[i] == '-' ? '0' : bytes[i];
	return st_rip3_number_decode(result, digits, length, radix);
}

int
st_rip3_command_environment(const struct st_rip3_command_host *host, unsigned *number,
    struct st_rip3_environment *value)
{
	int status;

	if (host->environments == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_environments_current(host->environments, number);
	if (status == 0)
		status = st_rip3_environments_get(host->environments, *number, value);
	return status;
}

int
st_rip3_command_frame(const struct st_rip3_command_host *host,
    unsigned environment, struct st_rip3_frame *result)
{
	struct st_rip3_frame frame;
	struct st_rip3_port_info screen;
	int status = st_rip3_environments_frame(host->environments, environment, &frame);

	if (status == 0)
		status = st_rip3_ports_info(host->ports, 0, &screen);
	if (status != 0)
		return status;
	if (screen.bounds.x0 != 0 || screen.bounds.y0 != 0
	    || (uint32_t)screen.bounds.x1 + 1 != frame.device_width
	    || (uint32_t)screen.bounds.y1 + 1 != frame.device_height)
		return IO_ERROR_INVALID_ARGUMENT;
	*result = frame;
	return 0;
}

static int
desktop_palette(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation)
{
	struct st_rip3_environment environment;
	unsigned number, colors[16];
	uint64_t value;
	size_t count = length - 1, start = 0, entries = 16, offset = 0;
	const uint8_t *arguments = command + 1;
	int status;

	/* General comment boundaries remain unresolved under #7. Do not treat
	 * trailing text as a partially valid palette update. */
	if (memchr(arguments, ' ', count) != NULL)
		return IO_ERROR_NOT_SUPPORTED;
	if (host->palettes == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_command_environment(host, &number, &environment);
	if (status != 0)
		return status;
	if (command[0] == 'a') {
		if (count < 3 || count > 4)
			return IO_ERROR_INVALID_FORMAT;
		status = st_rip3_command_field(&value, arguments, 2, environment.base_math);
		if (status != 0)
			return status;
		if (value > 15)
			return IO_ERROR_OUT_OF_RANGE;
		start = (size_t)value;
		entries = 1;
		offset = 2;
	}
	else if (count < 31 || count > 32)
		return IO_ERROR_INVALID_FORMAT;
	for (size_t i = 0; i < entries; i++) {
		size_t width = i + 1 == entries ? count - offset : 2;

		status = st_rip3_command_field(&value, arguments + offset, width, environment.base_math);
		if (status != 0)
			return status;
		if (value > 63)
			return IO_ERROR_OUT_OF_RANGE;
		colors[i] = (unsigned)value;
		offset += width;
	}
	/* No state changes before every field passes. ENV supplies only the
	 * radix; the bound palette's current entry and protection own the edit. */
	*operation = true;
	return st_rip3_palettes_desktop(host->palettes, start, colors, entries);
}

static int
host_query(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation)
{
	struct st_rip3_environment environment;
	unsigned number;
	uint64_t mode, window, reserved;
	int status;

	if (length < 6)
		return IO_ERROR_INVALID_FORMAT;
	status = st_rip3_command_environment(host,&number,&environment);
	if (status == 0)
		status = st_rip3_command_field(&mode,command+2,1,environment.base_math);
	if (status == 0)
		status = st_rip3_command_field(&window,command+3,1,environment.base_math);
	if (status == 0)
		status = st_rip3_command_field(&reserved,command+4,2,environment.base_math);
	if (status != 0)
		return status;
	(void)window; /* Immediate and hover queries have no numbered target. */
	if ((mode!=0 && mode!=5 && mode!=6) || (mode==0 && reserved!=0)) return IO_ERROR_NOT_SUPPORTED;
	*operation=true;
	if (mode==5 || mode==6) {
		if (host->mouse==NULL) return IO_ERROR_NOT_SUPPORTED;
		return st_rip3_mouse_query_encoded(host->mouse,mode==5,command+6,length-6,host->codepage_converted);
	}
	/* Private ownership/preflight status is consumed by the void command
	 * boundary. Rejected source syntax has not started an operation. Nested
	 * execution diagnostics remain inside the evaluator's void processing. */
	status=st_rip3_query_start(host->query,command+6,length-6);
	if (status==IO_ERROR_INVALID_ARGUMENT || status==IO_ERROR_INVALID_FORMAT || status==IO_ERROR_OUT_OF_RANGE)
		*operation=false;
	return status;
}

static int
style_protection(struct st_rip3_styles *styles, unsigned number, uint64_t flags,
    unsigned protect, unsigned unprotect, const struct st_rip3_command_sink *sink)
{
	int status = 0;
	if ((flags&protect) != 0) {
		/* The original setter diagnoses attempts to protect zero, but its
		 * caller still selects/applies every remaining flag afterward. */
		if (number == 0) {
			/* Source and destination requests are distinct observations, even
			 * for 0 -> 0. Diagnose here so neither is collapsed into the final
			 * command status, and continue through the remaining ordered steps. */
			if (sink != NULL && sink->diagnostic != NULL)
				sink->diagnostic(sink->context,ST_RIP3_COMMAND_OPERATION_FAILED);
		}
		else status = st_rip3_styles_protect(styles,number,true);
	}
	if ((flags&unprotect) != 0 && number != 0) {
		int cleared = st_rip3_styles_protect(styles,number,false);
		if (status == 0) status = cleared;
	}
	return status;
}

static int
switch_style(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation, const struct st_rip3_command_sink *sink)
{
	struct st_rip3_environment environment;
	uint64_t number, flags;
	unsigned current, environment_number;
	int status, source, destination;
	size_t count = length-2;
	if (memchr(command+2,' ',count) != NULL) return IO_ERROR_NOT_SUPPORTED;
	if (count < 2 || count > 3) return IO_ERROR_INVALID_FORMAT;
	status = st_rip3_command_environment(host,&environment_number,&environment);
	if (status == 0) status = st_rip3_command_field(&number,command+2,1,environment.base_math);
	if (status == 0) status = st_rip3_command_field(&flags,command+3,count-1,environment.base_math);
	if (status != 0) return status;
	if (number > 35) return 0; /* The shipped handler ignores invalid slots. */
	if (host->styles == NULL) return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_styles_current(host->styles,&current);
	if (status != 0) return status;
	*operation = true;
	source = style_protection(host->styles,current,flags,4,8,sink);
	status = st_rip3_styles_switch(host->styles,(unsigned)number);
	if (status != 0) return status;
	destination = style_protection(host->styles,(unsigned)number,flags,1,2,sink);
	return source != 0 ? source : destination;
}

static int
switch_text_window(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation)
{
	if (length<3 || length>4) return IO_ERROR_INVALID_FORMAT;
	struct st_rip3_environment environment;unsigned number;
	int status=st_rip3_command_environment(host,&number,&environment);
	uint64_t target,reserved=0;
	if (status==0) status=st_rip3_command_field(&target,command+2,1,environment.base_math);
	if (status==0 && length==4) status=st_rip3_command_field(&reserved,command+3,1,environment.base_math);
	if (status!=0) return status;
	if (target>=36) return IO_ERROR_OUT_OF_RANGE;
	if (host->text_window==NULL || host->text_window->select==NULL) return IO_ERROR_NOT_SUPPORTED;
	*operation=true;return host->text_window->select(host->text_window->context,(unsigned)target);
}

static int
normal_text_window(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation, const struct st_rip3_command_sink *sink)
{
	struct st_rip3_environment environment;unsigned number;
	int status=st_rip3_command_environment(host,&number,&environment);
	if (status!=0) return status;
	if (length!=1+4*environment.coordinate_size+2) return IO_ERROR_INVALID_FORMAT;
	bool zero=true;uint64_t values[6];size_t at=1;
	for (unsigned i=0;i<6;i++) {
		size_t width=i<4 ? environment.coordinate_size : 1;
		status=st_rip3_command_field(values+i,command+at,width,environment.base_math);
		if (status!=0) return status;
		if (values[i]!=0) zero=false;
		at+=width;
	}
	if (zero) {
		if (host->text_window!=NULL && host->text_window->deactivate!=NULL) {
			*operation=true;return host->text_window->deactivate(host->text_window->context);
		}
		if (host->text_active==NULL) return IO_ERROR_NOT_SUPPORTED;
		*operation=true;*host->text_active=false;return 0;
	}
	if (host->text_window==NULL || host->text_window->define==NULL) return IO_ERROR_NOT_SUPPORTED;
	struct st_rip3_frame frame;struct st_rip3_text_window_layout layout;
	status=st_rip3_environments_device(host->environments,&frame);
	if (status==0) status=st_rip3_text_window_normal(&layout,&frame,(uint32_t)values[0],
	    (uint32_t)values[1],(uint32_t)values[2],(uint32_t)values[3],(unsigned)values[4],(unsigned)values[5]);
	if (status!=0) return status;
	uint64_t near_x=values[0]<values[2] ? values[0] : values[2],far_x=values[0]>values[2] ? values[0] : values[2];
	uint64_t near_y=values[1]<values[3] ? values[1] : values[3],far_y=values[1]>values[3] ? values[1] : values[3];
	if ((near_x!=layout.x0 || far_x!=layout.x1 || near_y!=layout.y0 || far_y!=layout.y1)
	    && sink!=NULL && sink->diagnostic!=NULL)
		sink->diagnostic(sink->context,ST_RIP3_COMMAND_COORDINATES_CLAMPED);
	*operation=true;return host->text_window->define(host->text_window->context,&layout);
}

static int
extended_text_window(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation)
{
	struct st_rip3_environment environment;unsigned number;
	int status=st_rip3_command_environment(host,&number,&environment);
	if (status!=0) return status;
	size_t prefix=1+4*environment.coordinate_size+5;
	if (length<=prefix || length>prefix+7) return IO_ERROR_INVALID_FORMAT;
	uint64_t fields[9]={0};size_t at=1;
	for (unsigned i=0;i<7;i++) {
		size_t width=i<4 ? environment.coordinate_size : i<6 ? 2 : 1;
		status=st_rip3_command_field(fields+i,command+at,width,environment.base_math);
		if (status!=0) return status;
		at+=width;
	}
	size_t remaining=length-at,flags=remaining>4 ? 4 : remaining;
	status=st_rip3_command_field(fields+7,command+at,flags,environment.base_math);
	if (status==0 && remaining>4)
		status=st_rip3_command_field(fields+8,command+at+4,remaining-4,environment.base_math);
	if (status!=0) return status;
	if (fields[4]==0 || fields[5]==0 || fields[7]>63 || (!(fields[7]&8) && fields[6]>4))
		return IO_ERROR_OUT_OF_RANGE;
	if (host->text_window==NULL || host->text_window->define==NULL) return IO_ERROR_NOT_SUPPORTED;
	struct st_rip3_frame frame;struct st_rip3_text_cell cells[5];
	status=st_rip3_environments_frame(host->environments,number,&frame);
	for (unsigned i=0;status==0 && i<5;i++) {
		struct st_rip3_maf_font font;
		status=st_rip3_maf_builtin_font(&font,frame.device_width,frame.device_height,(enum st_rip3_maf_mode)i);
		if (status==0) cells[i]=(struct st_rip3_text_cell){font.width,font.height};
	}
	struct st_rip3_text_window_layout layout;
	if (status==0) status=st_rip3_text_window_extended(&layout,&frame,cells,
	    (uint32_t)fields[0],(uint32_t)fields[1],(uint32_t)fields[2],(uint32_t)fields[3],
	    (uint32_t)fields[4],(uint32_t)fields[5],(unsigned)fields[6],(unsigned)fields[7]);
	if (status!=0) return status;
	*operation=true;return host->text_window->define(host->text_window->context,&layout);
}

static int
text_cursor(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation)
{
	uint64_t column=0,row=0;
	if (command[0]=='H') {
		if (length!=1) return IO_ERROR_INVALID_FORMAT;
	}
	else {
		/* Unlike w's XY fields, g always has two digits per coordinate. */
		if (length!=5) return IO_ERROR_INVALID_FORMAT;
		struct st_rip3_environment environment;unsigned number;
		int status=st_rip3_command_environment(host,&number,&environment);
		if (status==0) status=st_rip3_command_field(&column,command+1,2,environment.base_math);
		if (status==0) status=st_rip3_command_field(&row,command+3,2,environment.base_math);
		if (status!=0) return status;
	}
	if (host->text_window==NULL || host->text_window->position==NULL) return IO_ERROR_NOT_SUPPORTED;
	*operation=true;
	return host->text_window->position(host->text_window->context,(uint32_t)column,(uint32_t)row);
}

static int
color_mode(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation)
{
	struct st_rip3_environment value;
	unsigned number;
	uint64_t mode,bits;
	if (length!=3) return IO_ERROR_INVALID_FORMAT;
	int status=st_rip3_command_environment(host,&number,&value);
	if (status==0) status=st_rip3_command_field(&mode,command+1,1,value.base_math);
	if (status==0) status=st_rip3_command_field(&bits,command+2,1,value.base_math);
	if (status!=0) return status;
	if (mode>1) return IO_ERROR_OUT_OF_RANGE;
	/* The wire setter's RGB gate differs from the COLORMODE variable:
	 * unsupported precision leaves the current mode unchanged. Palette
	 * mode ignores the precision field after its numeric validation. */
	if (mode==1 && bits!=8) return 0;
	value.rgb_bits=mode==0 ? 0 : 8;
	*operation=true;
	return st_rip3_environments_set(host->environments,number,&value);
}

static int
execute(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation, const struct st_rip3_command_sink *sink)
{
	struct st_rip3_environment value;
	unsigned number;
	uint64_t first, second;
	const uint8_t *arguments, *comment;
	size_t count;
	bool baud, delay, commented;
	int status;

	*operation = false;
	if (host == NULL || command == NULL || length == 0 || length == SIZE_MAX)
		return IO_ERROR_INVALID_ARGUMENT;
	if (memchr(command, 0, length) != NULL)
		return IO_ERROR_INVALID_FORMAT;
	baud = length >= 2 && command[0] == '3' && command[1] == 'e';
	delay = length >= 2 && command[0] == '3' && command[1] == 'D';
	/* Comments bypass every expansion/service, and terminal-only grouping
	 * commands carry no state, including when nested or unmatched. */
	if (command[0] == '!')
		return 0;
	if (command[0] == '(' || command[0] == ')')
		return length == 1 ? 0 : IO_ERROR_INVALID_FORMAT;
	if (command[0]=='*') {
		if (length!=1) return IO_ERROR_INVALID_FORMAT;
		if (host->soft_reset==NULL) return IO_ERROR_NOT_SUPPORTED;
		*operation=true;return host->soft_reset(host->reset_context);
	}
	if (command[0]=='w') return normal_text_window(host,command,length,operation,sink);
	if (command[0]=='b') return extended_text_window(host,command,length,operation);
	if (command[0]=='M') return color_mode(host,command,length,operation);
	if (length>=2 && command[0]=='1' && command[1]=='U')
		return st_rip3_button_create_execute(host,command,length,operation);
	if (length>=2 && command[0]=='1' && (command[1]=='M' || command[1]=='K' || command[1]=='k' || command[1]=='c'))
		return st_rip3_mouse_commands_execute(host,command,length,operation);
	if (length>=2 && (command[0]=='1' || command[0]=='2') && command[1]=='B')
		return st_rip3_button_commands_execute(host,command,length,operation);
	if (command[0]=='g' || command[0]=='H') return text_cursor(host,command,length,operation);
	if (command[0]=='e' || command[0]=='>') {
		if (length!=1) return IO_ERROR_INVALID_FORMAT;
		if (host->text_window==NULL || host->text_window->erase==NULL) return IO_ERROR_NOT_SUPPORTED;
		*operation=true;return host->text_window->erase(host->text_window->context,command[0]=='e');
	}
	if (length >= 2 && command[0] == '2' && command[1] == 'Y')
		return switch_style(host,command,length,operation,sink);
	if (length >= 2 && command[0] == '2' && command[1] == 'T')
		return switch_text_window(host,command,length,operation);
	if (length >= 2 && command[0] == '1' && command[1] == 0x1b)
		return host_query(host,command,length,operation);
	if (length >= 2 && (command[0] == '1' || command[0] == '2') && command[1] == 'W')
		return st_rip3_bitmap_commands_execute(host,command,length,operation);
	if (length >= 2 && command[0] == '1' && command[1] == 'C')
		return st_rip3_get_image_commands_execute(host,command,length,operation);
	if (length >= 2 && command[0] == '1' && (command[1] == 'b' || command[1] == 'I'))
		return st_rip3_bitmap_load_commands_execute(host,command,length,operation);
	if (length >= 2 && command[0] == '1' && (command[1] == 'p' || command[1] == 'i'))
		return st_rip3_image_commands_execute(host,command,length,operation);
	if (length >= 2 && command[0] == '1' && command[1] == 'w') {
		char *filename;
		bool off;

		if (length < 7)
			return IO_ERROR_INVALID_FORMAT;
		if (memcmp(command + 2, "0000", 4) != 0)
			return IO_ERROR_NOT_SUPPORTED;
		off = length == 11 && memcmp(command + 6, "$OFF$", 5) == 0;
		if (host->audio == NULL || (host->resources == NULL && !off))
			return IO_ERROR_NOT_SUPPORTED;
		filename = strndup((const char *)command + 6, length - 6);
		if (filename == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		*operation = true;
		status = st_rip3_audio_play(host->audio, host->resources, filename);
		free(filename);
		return status;
	}
	if ((length >= 2 && command[0] == '1' && command[1] == 'P')
	    || command[0] == 'v' || (length >= 2 && command[0] == '2'
	    && (command[1] == 'P' || command[1] == 'p' || command[1] == 'C' || command[1] == 's')))
		return st_rip3_port_commands_execute(host, command, length, operation);
	if (command[0] == 'm' || command[0] == 'T' || command[0] == '@')
		return st_rip3_text_commands_execute(host,command,length,operation);
	if (command[0] == 'y' || command[0] == 'q')
		return st_rip3_font_commands_execute(host,command,length,operation);
	if (command[0] == 'A' || command[0] == 'C' || command[0] == 'G' || command[0] == 'o'
	    || command[0] == 'O' || command[0] == 'V' || command[0] == 'I'
	    || command[0] == 'i' || command[0] == '_')
		return st_rip3_curve_commands_execute(host, command, length, operation);
	if (command[0] == '&' || command[0] == '-' || command[0] == ']' || command[0] == '+' || command[0] == '[')
		return st_rip3_skew_commands_execute(host,command,length,operation);
	if (command[0] == 'Z' || command[0] == 'z' || command[0] == 't' || command[0] == 'x')
		return st_rip3_bezier_commands_execute(host,command,length,operation);
	if (command[0] == 'U' || command[0] == 'u')
		return st_rip3_rounded_commands_execute(host,command,length,operation);
	if (command[0] == 'P' || command[0] == 'l' || command[0] == 'p' || command[0] == '<')
		return st_rip3_path_commands_execute(host, command, length, operation);
	if (command[0] == 'c' || command[0] == 'k' || command[0] == 'W' || command[0] == 'Y'
	    || command[0] == 'L' || command[0] == 'R' || command[0] == 'B' || command[0] == 'K'
	    || command[0] == 'S' || command[0] == 's' || command[0] == 'E' || command[0] == '='
	    || command[0] == 'X' || command[0] == 'j' || command[0] == 'N')
		return st_rip3_drawing_commands_execute(host, command, length, operation);
	if (command[0] == 'Q' || command[0] == 'a')
		return desktop_palette(host, command, length, operation);
	if (command[0] != 'J' && command[0] != 'n' && command[0] != 'f' && !baud && !delay)
		return IO_ERROR_NOT_SUPPORTED;
	arguments = command + (baud || delay ? 2 : 1); count = length - (baud || delay ? 2 : 1);
	/* The pinned f example explicitly retains the command before a spaced
	 * inline comment. Unseparated extra-data rules remain under issue #7.
	 * Text-valued commands never take this numeric-only branch. */
	comment = memchr(arguments, ' ', count);
	commented = comment != NULL;
	if (comment != NULL)
		count = (size_t)(comment - arguments);
	if (commented && command[0] != 'f')
		return IO_ERROR_NOT_SUPPORTED;
	if (count == 0)
		return IO_ERROR_INVALID_FORMAT;
	if (command[0] == 'J') {
		if (count > 2)
			return IO_ERROR_INVALID_FORMAT;
		status = st_rip3_command_field(&first, arguments, count, 36);
		if (status != 0)
			return status;
		status = st_rip3_command_environment(host, &number, &value);
		if (status != 0)
			return status;
		value.base_math = first == 64 ? 64 : 36;
	}
	else {
		status = st_rip3_command_environment(host, &number, &value);
		if (status != 0)
			return status;
		if (baud || delay) {
			if (count > 4)
				return IO_ERROR_INVALID_FORMAT;
			status = st_rip3_command_field(&first, arguments, count, value.base_math);
			if (status != 0)
				return status;
			if (delay) {
				if (host->input == NULL)
					return IO_ERROR_NOT_SUPPORTED;
				*operation = true;
				st_rip3_input_delay(host->input,(uint32_t)first);
				return 0;
			}
			value.baud_rate = first;
		}
		else if (command[0] == 'n') {
			if (count > 4)
				return IO_ERROR_INVALID_FORMAT;
			status = st_rip3_command_field(&first, arguments, 1, value.base_math);
			if (status == 0 && count > 1) {
				status = st_rip3_command_field(&second, arguments + 1, count - 1, value.base_math);
				if (status == 0 && second != 0)
					return IO_ERROR_NOT_SUPPORTED;
			}
			if (status != 0)
				return status;
			value.coordinate_size = first >= 2 && first <= 5 ? (unsigned)first : 2;
		}
		else {
			size_t width = value.coordinate_size;

			if (commented && count != width * 2)
				return IO_ERROR_NOT_SUPPORTED;
			if (count <= width || count > width * 2)
				return IO_ERROR_INVALID_FORMAT;
			status = st_rip3_command_field(&first, arguments, width, value.base_math);
			if (status == 0)
				status = st_rip3_command_field(&second, arguments + width, count - width, value.base_math);
			if (status != 0)
				return status;
			if (first > 65535 || second > 65535)
				return IO_ERROR_OUT_OF_RANGE;
			value.world_width = (uint32_t)first;
			value.world_height = (uint32_t)second;
		}
	}
	*operation = true;
	return st_rip3_environments_set(host->environments, number, &value);
}

void
st_rip3_commands_process(const struct st_rip3_command_host *host,
    const struct st_rip3_command_sink *sink, const uint8_t *command, size_t length)
{
	bool operation;
	int status = execute(host, command, length, &operation, sink);
	st_rip3_command_result(sink,status,operation);
}

void
st_rip3_command_result(const struct st_rip3_command_sink *sink, int status, bool operation)
{
	enum st_rip3_command_diagnostic kind;

	if (status == 0)
		return;
	if (status == IO_ERROR_NOT_SUPPORTED)
		kind = ST_RIP3_COMMAND_UNSUPPORTED;
	else if (status == IO_ERROR_OUT_OF_MEMORY)
		kind = ST_RIP3_COMMAND_RESOURCE_FAILURE;
	else if (!operation && (status == IO_ERROR_INVALID_ARGUMENT
	    || status == IO_ERROR_INVALID_FORMAT || status == IO_ERROR_OUT_OF_RANGE
	    || status == IO_ERROR_INVALID_ENCODING))
		kind = ST_RIP3_COMMAND_INVALID;
	else
		kind = ST_RIP3_COMMAND_OPERATION_FAILED;
	/* The command boundary is owned by the enclosing framer. Discard this
	 * command only; neither syntax nor service errors escape to the session. */
	if (sink != NULL && sink->diagnostic != NULL)
		sink->diagnostic(sink->context, kind);
}
