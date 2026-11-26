/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <string.h>

#include "io_error.h"
#include "st_rip3_builtin_reference.h"
#include "st_rip3_graphical_bytes.h"
#include "st_rip3_host_internal.h"

/* Documented names, not an implementation/capability claim. Pinned 9.1's
 * inventory plus the APP/SAVE/RESTORE fixed-slot families and HLP-attested
 * SHIFT/CTRL/HOSTDIR/XFER (5.1) and PROTECT (7.1). Their unrecovered
 * semantics remain unsupported. Renamed/removed 1.54 names are not aliases. */
static const char *const builtin_names[] = {
    "ADOW",
    "ALARM",
    "AMPM",
    "APP",
    "APP0",
    "APP1",
    "APP2",
    "APP3",
    "APP4",
    "APP5",
    "APP6",
    "APP7",
    "APP8",
    "APP9",
    "ATW",
    "AVP",
    "BACKSTAT",
    "BASEMATH",
    "BAUDEMUL",
    "BEEP",
    "BLIP",
    "BSMODE",
    "BSXMIT",
    "CDCS",
    "CHARSET",
    "CLS",
    "COFF",
    "COLORMODE",
    "COLORS",
    "COMPAT",
    "CON",
    "COORDSIZE",
    "COPY",
    "CTRL",
    "CUR",
    "CURSOR",
    "CURX",
    "CURY",
    "D",
    "DATE",
    "DATETIME",
    "DAY",
    "DCS",
    "DOW",
    "DOY",
    "DTW",
    "DVP",
    "DWAYOFF",
    "DWAYON",
    "EGW",
    "ETW",
    "FIELDID",
    "FILEDEL",
    "FILEEXISTS",
    "FYEAR",
    "GOTOURL",
    "HKEYOFF",
    "HKEYON",
    "HOSTDIR",
    "HOUR",
    "IFS",
    "IMGSTYLE",
    "INUSE",
    "ISEXTWIN",
    "ISPALETTE",
    "ISPROT",
    "LANG",
    "M",
    "MCURSOR",
    "MHOUR",
    "MIN",
    "MKILL",
    "MONTH",
    "MONTHNUM",
    "MSTAT",
    "MTW",
    "MUSIC",
    "MVP",
    "NOREFRESH",
    "NULL",
    "OFF",
    "OFFSCREEN",
    "OPTION",
    "OVERFLOW",
    "PALENTRY",
    "PASSWORD",
    "PCB",
    "PHASER",
    "PORTH",
    "PORTW",
    "PORTX0",
    "PORTX1",
    "PORTY0",
    "PORTY1",
    "PROT",
    "PROTECT",
    "RAND",
    "RBS",
    "RCB",
    "RCP",
    "REFRESH",
    "RENV",
    "RESET",
    "RESTORE",
    "RESTORE0",
    "RESTORE1",
    "RESTORE2",
    "RESTORE3",
    "RESTORE4",
    "RESTORE5",
    "RESTORE6",
    "RESTORE7",
    "RESTORE8",
    "RESTORE9",
    "RESTOREALL",
    "RESX",
    "RESY",
    "REVPHASER",
    "RGS",
    "RIPVER",
    "RMF",
    "RTW",
    "SAVE",
    "SAVE0",
    "SAVE1",
    "SAVE2",
    "SAVE3",
    "SAVE4",
    "SAVE5",
    "SAVE6",
    "SAVE7",
    "SAVE8",
    "SAVE9",
    "SAVEALL",
    "SBAROFF",
    "SBARON",
    "SBS",
    "SCB",
    "SCP",
    "SDCS",
    "SEC",
    "SENV",
    "SGS",
    "SHIFT",
    "SMF",
    "STATBAR",
    "STW",
    "T",
    "TABOFF",
    "TABON",
    "TERMINFO",
    "TEXTXY",
    "TIME",
    "TIMEZONE",
    "TWERASEEOL",
    "TWFONT",
    "TWGOTO",
    "TWH",
    "TWHOME",
    "TWIN",
    "TWW",
    "TWX0",
    "TWX1",
    "TWY0",
    "TWY1",
    "UNPROT",
    "USERNAME",
    "VT102OFF",
    "VT102ON",
    "WDAY",
    "WORLD",
    "WORLDH",
    "WORLDW",
    "WOY",
    "WOYM",
    "X",
    "XFER",
    "XY",
    "XYM",
    "Y",
    "YEAR",

};

static uint8_t
upper(uint8_t ch)
{
	return ch >= 'a' && ch <= 'z' ? (uint8_t)(ch - 'a' + 'A') : ch;
}

static bool
letter(uint8_t ch)
{
	return upper(ch) >= 'A' && upper(ch) <= 'Z';
}

bool
st_rip3_bookmark_name(const char *name, size_t length)
{
	if (name == NULL || length != 8)
		return false;
	char canonical[8];
	for (size_t i = 0; i < sizeof(canonical); i++)
		canonical[i] = (char)upper((uint8_t)name[i]);
	return memcmp(canonical, "USERNAME", sizeof(canonical)) == 0
	       || memcmp(canonical, "PASSWORD", sizeof(canonical)) == 0;
}

static bool
argument_byte(uint8_t ch)
{
	if (ch < 0x20)
		return false;
	switch (ch) {
		case '"':
		case '*':
		case '+':
		case ',':
		case '/':
		case ';':
		case '<':
		case '=':
		case '>':
		case '?':
		case '[':
		case ']':
		case '|':
			return false;
		default:
			return true;
	}
}

int
st_rip3_builtin_reference_parse_encoded(struct st_rip3_builtin_reference *result, const uint8_t *text,
    size_t length, bool converted, size_t *rejected_span)
{
	struct st_rip3_builtin_reference reference = {0};
	size_t                           at = 1, name_length = 0, end;

	if (rejected_span != NULL)
		*rejected_span = 0;
	if (result == NULL || text == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (length < 3 || text[0] != '$' || text[length - 1] != '$')
		return IO_ERROR_INVALID_FORMAT;
	end = length - 1;
	if (!letter(text[at]))
		return IO_ERROR_INVALID_FORMAT;
	while (at < end && (letter(text[at]) || (text[at] >= '0' && text[at] <= '9') || text[at] == '_')) {
		if (name_length == 20)
			return IO_ERROR_OUT_OF_RANGE;
		reference.name[name_length++] = (char)upper(text[at++]);
	}
	if (at == end) {
		*result = reference;
		return 0;
	}
	if (text[at] != '(')
		return IO_ERROR_INVALID_FORMAT;
	if (rejected_span != NULL)
		*rejected_span = length;
	/* Normalized arguments may occupy several UTF-8 bytes per wire byte.
	 * HOSTDIR checks its protocol limit in the directory handler. */
	bool   host_directory = strcmp(reference.name, "HOSTDIR") == 0;
	size_t capacity       = converted || host_directory ? sizeof(reference.parameters[0].text) - 1
	                                                    : ST_RIP3_BUILTIN_PARAMETER_LIMIT;
	at++;
	size_t stored = 0;
	bool   quote  = false;
	while (at < end) {
		uint8_t ch = text[at++];
		if (quote) {
			if (ch == '\\' || !argument_byte(ch))
				return IO_ERROR_INVALID_FORMAT;
			/* The prefix already occupied a slot: overwriting can complete
			 * byte 20 without requiring an additional payload byte. */
			reference.parameters[reference.count].text[stored - 1] = (char)ch;
			quote                                                  = false;
			continue;
		}
		if (ch == ',' || ch == ')') {
			if (stored != 0) {
				struct st_rip3_builtin_parameter *parameter =
				    &reference.parameters[reference.count++];
				if (converted && !host_directory
				    && st_rip3_graphical_prefix((const uint8_t *)parameter->text, stored,
					   ST_RIP3_BUILTIN_PARAMETER_LIMIT, true)
				           < stored)
					return IO_ERROR_OUT_OF_RANGE;
				parameter->length = stored;
			}
			stored = 0;
			if (ch == ',')
				continue; /* Empty fields occupy no slot. */
			if (at != end)
				return IO_ERROR_INVALID_FORMAT;
			*result = reference;
			return 0;
		}
		if (ch == '$' || !argument_byte(ch))
			return IO_ERROR_INVALID_FORMAT;
		if (reference.count == 40 || stored == capacity)
			return IO_ERROR_OUT_OF_RANGE;
		reference.parameters[reference.count].text[stored++] = (char)ch;
		quote                                                = ch == '\\';
	}
	if (rejected_span != NULL)
		*rejected_span = 1; /* Exhausted the candidate without an unquoted closing parenthesis. */
	return IO_ERROR_INVALID_FORMAT;
}

int
st_rip3_builtin_reference_parse(struct st_rip3_builtin_reference *result, const uint8_t *text, size_t length)
{
	return st_rip3_builtin_reference_parse_encoded(result, text, length, false, NULL);
}

int
st_rip3_builtin_reference_words(struct st_rip3_builtin_reference *result,
    const struct st_rip3_builtin_reference                       *reference)
{
	if (result == NULL || reference == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (reference->count > 40)
		return IO_ERROR_OUT_OF_RANGE;
	struct st_rip3_builtin_reference value = *reference;
	for (size_t i = 0; i < value.count; i++) {
		const struct st_rip3_builtin_parameter *source = &reference->parameters[i];
		if (source->length >= sizeof(source->text) || source->text[source->length] != 0)
			return IO_ERROR_INVALID_FORMAT;
		size_t first = 0, last = source->length;
		while (first < last && source->text[first] == ' ')
			first++;
		while (last > first && source->text[last - 1] == ' ')
			last--;
		memset(value.parameters[i].text, 0, sizeof(value.parameters[i].text));
		value.parameters[i].length = last - first;
		memcpy(value.parameters[i].text, source->text + first, last - first);
	}
	*result = value;
	return 0;
}

bool
st_rip3_builtin_known(const char *name, size_t length)
{
	char   canonical[21];
	size_t low = 0, high = sizeof(builtin_names) / sizeof(*builtin_names);

	if (name == NULL || length == 0 || length > 20 || !letter((uint8_t)name[0]))
		return false;
	for (size_t i = 0; i < length; i++) {
		uint8_t ch = (uint8_t)name[i];
		if (!letter(ch) && !(ch >= '0' && ch <= '9') && ch != '_')
			return false;
		canonical[i] = (char)upper(ch);
	}
	canonical[length] = 0;
	while (low < high) {
		size_t mid        = low + (high - low) / 2;
		int    comparison = strcmp(canonical, builtin_names[mid]);

		if (comparison == 0)
			return true;
		if (comparison < 0)
			high = mid;
		else
			low = mid + 1;
	}
	return false;
}

bool
st_rip3_builtin_graphical(const char *name, size_t length)
{
	/* PR126 registration classes 1 and 3, independent of callback effects. */
	static const char *const names[] = {
	    "ADOW",
	    "AMPM",
	    "BACKSTAT",
	    "BASEMATH",
	    "BAUDEMUL",
	    "BSMODE",
	    "BSXMIT",
	    "CDCS",
	    "CHARSET",
	    "COLORMODE",
	    "COLORS",
	    "COORDSIZE",
	    "CTRL",
	    "CUR",
	    "CURSOR",
	    "CURX",
	    "CURY",
	    "DATE",
	    "DATETIME",
	    "DAY",
	    "DCS",
	    "DOW",
	    "DOY",
	    "FIELDID",
	    "FILEEXISTS",
	    "FYEAR",
	    "HOSTDIR",
	    "HOUR",
	    "IFS",
	    "IMGSTYLE",
	    "INUSE",
	    "ISEXTWIN",
	    "ISPALETTE",
	    "ISPROT",
	    "LANG",
	    "M",
	    "MCURSOR",
	    "MHOUR",
	    "MIN",
	    "MONTH",
	    "MONTHNUM",
	    "MSTAT",
	    "NULL",
	    "OFFSCREEN",
	    "OPTION",
	    "OVERFLOW",
	    "PALENTRY",
	    "PORTH",
	    "PORTW",
	    "PORTX0",
	    "PORTX1",
	    "PORTY0",
	    "PORTY1",
	    "RAND",
	    "RESX",
	    "RESY",
	    "RIPVER",
	    "SDCS",
	    "SEC",
	    "SHIFT",
	    "STATBAR",
	    "TERMINFO",
	    "TEXTXY",
	    "TIME",
	    "TIMEZONE",
	    "TWFONT",
	    "TWGOTO",
	    "TWH",
	    "TWIN",
	    "TWW",
	    "TWX0",
	    "TWX1",
	    "TWY0",
	    "TWY1",
	    "WDAY",
	    "WORLDH",
	    "WORLDW",
	    "WOY",
	    "WOYM",
	    "X",
	    "XY",
	    "XYM",
	    "Y",
	    "YEAR",
	};
	if (!st_rip3_builtin_known(name, length))
		return false;
	for (size_t i = 0; i < sizeof(names) / sizeof(*names); i++) {
		if (strlen(names[i]) != length)
			continue;
		size_t j = 0;
		while (j < length && upper((uint8_t)name[j]) == (uint8_t)names[i][j])
			j++;
		if (j == length)
			return true;
	}
	return false;
}

int
st_rip3_host_number(const char *text, unsigned minimum, unsigned maximum, unsigned *result)
{
	unsigned value = 0;

	if (text == NULL || result == NULL || minimum > maximum)
		return IO_ERROR_INVALID_ARGUMENT;
	if (text[0] == 0)
		return IO_ERROR_INVALID_FORMAT;
	for (size_t i = 0; text[i] != 0; i++) {
		unsigned digit;

		if (text[i] < '0' || text[i] > '9')
			return IO_ERROR_INVALID_FORMAT;
		digit = (unsigned)(text[i] - '0');
		if (value > maximum / 10 || (value == maximum / 10 && digit > maximum % 10))
			return IO_ERROR_INVALID_FORMAT;
		value = value * 10 + digit;
	}
	if (value < minimum)
		return IO_ERROR_INVALID_FORMAT;
	*result = value;
	return 0;
}
