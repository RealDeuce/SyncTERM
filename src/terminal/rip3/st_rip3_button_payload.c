/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "io_error.h"
#include "st_rip3_button_payload.h"
#include "st_rip3_graphical_bytes.h"
#include "st_rip3_mouse.h"

int
st_rip3_button_payload_parse_encoded(struct st_rip3_button_payload *result, const uint8_t *text, size_t length,
    bool converted_codepage)
{
	if (result == NULL || (text == NULL && length != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	int identity = st_rip3_mouse_identity(&text, &length);
	while (length != 0 && (text[0] == ' ' || (text[0] >= 9 && text[0] <= 13))) {
		text++;
		length--;
	}
	if (length == 0)
		return IO_ERROR_INVALID_FORMAT;
	struct st_rip3_button_payload value = {
	    .identity = identity,
	    .icon     = {text,          length},
	    .label    = {text + length, 0     },
	    .host     = {text + length, 0     }
        };
	for (size_t i = 0; i + 1 < length; i++)
		if (text[i] == '<' && text[i + 1] == '>') {
			value.icon.length = i;
			value.label       = (struct st_rip3_button_text) {text + i + 2, length - i - 2};
			break;
		}
	for (size_t i = 0; i + 1 < value.label.length; i++)
		if (value.label.data[i] == '<' && value.label.data[i + 1] == '>') {
			value.host =
			    (struct st_rip3_button_text) {value.label.data + i + 2, value.label.length - i - 2};
			value.label.length = i;
			break;
		}
	if (st_rip3_graphical_units(value.icon.data, value.icon.length, converted_codepage) > 127
	    || st_rip3_graphical_units(value.label.data, value.label.length, converted_codepage) > 255)
		return IO_ERROR_OUT_OF_RANGE;
	*result = value;
	return 0;
}

int
st_rip3_button_payload_parse(struct st_rip3_button_payload *result, const uint8_t *text, size_t length)
{
	return st_rip3_button_payload_parse_encoded(result, text, length, false);
}
