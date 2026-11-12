/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_key.h"
#include "cterm.h"
#include "cterm_render.h"
#include "st_cterm.h"
#include "st_utf8.h"
#include "st_unicode_text.h"
#include <stdlib.h>

int cterm_key_replay(struct cterminal *terminal, const struct cterm_key *key)
{
	switch (key->kind) {
	case CTERM_KEY_NORMAL:
		(void)st_cterm_handle_key(terminal,(uint16_t)key->value,key->modifiers);
		break;
	case CTERM_KEY_CODEPAGE:
		(void)st_cterm_handle_codepage_key(terminal,(uint8_t)key->value);
		break;
	case CTERM_KEY_TEXT:
		if (!st_cterm_handle_text_key(terminal,key->value)) {
			/* The session sends unhandled Unicode text. Deferred events
			 * have already left that frame, so finish its normal path here. */
			char utf8[4], *normalized=NULL; size_t size=0;
			ptrdiff_t length=st_utf8_encode_one(utf8,sizeof(utf8),key->value);
			int status=length<0?(int)length
			    :st_unicode_text_normalize(&normalized,&size,utf8,(size_t)length,NULL);
			if (status==0 && terminal->wire_encoding==ST_CTERM_ENCODING_UTF8) {
				if (terminal->keystroke_cb!=NULL)
					terminal->keystroke_cb(normalized,size,terminal->keystroke_cbdata);
			} else if (status==0) {
				uint32_t scalar; size_t used; uint8_t byte;
				/* A single input scalar normalizes to one grapheme. Match
				 * the session's codepage fallback: send an encodable scalar,
				 * notify for an unencodable or multi-scalar grapheme. */
				if (st_utf8_decode_one(&scalar,&used,normalized,size)==0 && used==size
				    && st_codepage_from_unicode(&byte,terminal->screen.codepage,scalar)) {
					if (terminal->keystroke_cb!=NULL)
						terminal->keystroke_cb((const char *)&byte,1,terminal->keystroke_cbdata);
				} else cterm_render_beep(terminal);
			}
			free(normalized);
			if (status!=0) return status;
		}
		break;
	case CTERM_KEY_DOORWAY:
		(void)st_cterm_handle_doorway_key(terminal,(uint8_t)key->value);
		break;
	}
	return 0;
}
