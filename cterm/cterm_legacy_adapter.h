/* CTerm-private bridge from the extracted parser to modern components. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_CTERM_LEGACY_ADAPTER_H_
#define ST_CTERM_LEGACY_ADAPTER_H_

#include "audio.h"
#include "st_audio_runtime.h"
#include "st_codepage.h"
#include "st_console.h"
#include "st_hyperlink.h"
#include "st_unicode_text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define CIOLIBEXPORT
#define TRUE 1
#define FALSE 0
#define FREE_AND_NULL(value) do { free(value); (value) = NULL; } while (0)

enum {
	BLACK, BLUE, GREEN, CYAN, RED, MAGENTA, BROWN, LIGHTGRAY,
	DARKGRAY, LIGHTBLUE, LIGHTGREEN, LIGHTCYAN, LIGHTRED,
	LIGHTMAGENTA, YELLOW, WHITE
};

#define BLINK 128
#define _NOCURSOR 0
#define _SOLIDCURSOR 1
#define _NORMALCURSOR 2
#define C80 3
#define C64_40X25 64
#define C128_40X25 65
#define C128_80X25 66

#define CIO_KEY_HOME        (0x47 << 8)
#define CIO_KEY_UP          (0x48 << 8)
#define CIO_KEY_END         (0x4f << 8)
#define CIO_KEY_DOWN        (0x50 << 8)
#define CIO_KEY_F(x)        (((x) < 11) ? ((0x3a + (x)) << 8) : ((0x7a + (x)) << 8))
#define CIO_KEY_IC          (0x52 << 8)
#define CIO_KEY_DC          (0x53 << 8)
#define CIO_KEY_LEFT        (0x4b << 8)
#define CIO_KEY_RIGHT       (0x4d << 8)
#define CIO_KEY_PPAGE       (0x49 << 8)
#define CIO_KEY_NPAGE       (0x51 << 8)
#define CIO_KEY_SHIFT_F(x)  (((x) < 11) ? ((0x53 + (x)) << 8) : ((0x7c + (x)) << 8))
#define CIO_KEY_CTRL_F(x)   (((x) < 11) ? ((0x5d + (x)) << 8) : ((0x7e + (x)) << 8))
#define CIO_KEY_ALT_F(x)    (((x) < 11) ? ((0x67 + (x)) << 8) : ((0x80 + (x)) << 8))
#define CIO_KEY_BACKTAB     (0x0f << 8)
#define CIO_KEY_SHIFT_UP    0x91e0
#define CIO_KEY_CTRL_UP     (0x8d << 8)
#define CIO_KEY_SHIFT_LEFT  0x93e0
#define CIO_KEY_CTRL_LEFT   (0x73 << 8)
#define CIO_KEY_SHIFT_RIGHT 0x94e0
#define CIO_KEY_CTRL_RIGHT  (0x74 << 8)
#define CIO_KEY_SHIFT_DOWN  0x96e0
#define CIO_KEY_CTRL_DOWN   (0x91 << 8)
#define CIO_KEY_SHIFT_END   0x95e0
#define CIO_KEY_CTRL_END    (0x75 << 8)

#define CIOLIB_VIDEO_ALTCHARS             (UINT32_C(1) << 0)
#define CIOLIB_VIDEO_NOBRIGHT             (UINT32_C(1) << 1)
#define CIOLIB_VIDEO_BGBRIGHT             (UINT32_C(1) << 2)
#define CIOLIB_VIDEO_BLINKALTCHARS        (UINT32_C(1) << 3)
#define CIOLIB_VIDEO_NOBLINK              (UINT32_C(1) << 4)

#define CIOLIB_FG_PRESTEL_CTRL_MASK UINT32_C(0x7f000000)
#define CIOLIB_FG_PRESTEL_CTRL_SHIFT 24
#define CIOLIB_COLOR_RGB UINT32_C(0x80000000)
#define CIOLIB_BG_DOUBLE_HEIGHT UINT32_C(0x01000000)
#define CIOLIB_BG_PRESTEL UINT32_C(0x02000000)
#define CIOLIB_BG_PIXEL_GRAPHICS UINT32_C(0x04000000)
#define CIOLIB_BG_REVEAL UINT32_C(0x08000000)
#define CIOLIB_BG_DIRTY UINT32_C(0x10000000)
#define CIOLIB_BG_SEPARATED UINT32_C(0x20000000)
#define CIOLIB_BG_PRESTEL_TERMINAL UINT32_C(0x40000000)

#define CONIO_OPT_LOADABLE_FONTS     (UINT32_C(1) << 0)
#define CONIO_OPT_PALETTE_SETTING    (UINT32_C(1) << 1)
#define CONIO_OPT_SET_PIXEL          (UINT32_C(1) << 2)
#define CONIO_OPT_CUSTOM_CURSOR      (UINT32_C(1) << 3)
#define CONIO_OPT_EXTENDED_PALETTE   (UINT32_C(1) << 4)
#define CONIO_OPT_FONT_SELECT        (UINT32_C(1) << 5)
#define CONIO_OPT_KEY_EVENTS         (UINT32_C(1) << 6)
#define CONIO_OPT_BRIGHT_BACKGROUND  (UINT32_C(1) << 7)
#define CONIO_FIRST_FREE_FONT 46
#define TOTAL_DAC_SIZE 282

struct text_info {
	unsigned char winleft, wintop, winright, winbottom;
	unsigned char attribute, normattr, currmode;
	unsigned char screenheight, screenwidth, curx, cury;
};

struct vmem_cell {
	uint8_t legacy_attr;
	uint8_t ch;
	uint8_t font;
	uint32_t fg;
	uint32_t bg;
	uint16_t hyperlink_id;
};

struct ciolib_key_event {
	uint16_t evdev;
	bool pressed;
};

struct ciolib_pixels {
	uint32_t *pixels;
	uint32_t *pixelsb;
	uint32_t width;
	uint32_t height;
};

struct ciolib_mask {
	uint8_t *bits;
	uint32_t width;
	uint32_t height;
};

struct video_params {
	int mode, palette, cols, rows, curs_start, curs_end;
	int charheight, charwidth, default_attr, flags;
	int aspect_width, aspect_height, xres, yres;
};

struct ciolib_screen {
	struct vmem_cell *vmem;
	struct text_info text_info;
};

struct st_cterm_cio_api {
	uint32_t options;
	bool vmem_gettext;
	bool mouse;
};

struct st_cterm_legacy_screen {
	st_console_t *console;
	st_audio_runtime_t *audio;
	st_hyperlink_registry_t *hyperlinks;
	enum st_codepage codepage;
	struct st_console_font_selection fonts;
	uint16_t font_identifiers[UINT8_MAX + 1U];
	bool font_valid[UINT8_MAX + 1U];
	uint16_t selectors[5];
	uint16_t current_hyperlink;
	uint32_t mode_palette[16];
	struct video_params video_params[1];
	struct st_cterm_cio_api api;
	int hold_update;
	int wscroll;
	int error;
	bool prestel_reveal;
	bool hyperlink_root_registered;
	st_unicode_presentation_t *unicode;
	uint32_t provisional_x;
	uint32_t provisional_y;
};

struct cterminal;

int st_cterm_adapter_cells_to_modern(struct cterminal *cterm,
	const struct vmem_cell *source, struct st_console_cell *destination,
	size_t count);

void st_cterm_adapter_gettextinfo(struct cterminal *cterm,
	struct text_info *info);
int st_cterm_adapter_getvideoflags(struct cterminal *cterm);
void st_cterm_adapter_setvideoflags(struct cterminal *cterm, int flags);
void st_cterm_adapter_window(struct cterminal *cterm, int left, int top,
	int right, int bottom);
void st_cterm_adapter_gotoxy(struct cterminal *cterm, int x, int y);
int st_cterm_adapter_wherex(struct cterminal *cterm);
int st_cterm_adapter_wherey(struct cterminal *cterm);
void st_cterm_adapter_textattr(struct cterminal *cterm, int attribute);
void st_cterm_adapter_setcolour(struct cterminal *cterm, uint32_t foreground,
	uint32_t background);
void st_cterm_adapter_cursor(struct cterminal *cterm, int cursor);
void st_cterm_adapter_getcustomcursor(struct cterminal *cterm, int *start,
	int *end, int *range, int *blink, int *visible);
void st_cterm_adapter_setcustomcursor(struct cterminal *cterm, int start,
	int end, int range, int blink, int visible);
int st_cterm_adapter_putch(struct cterminal *cterm, int value);
int st_cterm_adapter_cputs(struct cterminal *cterm, const char *text);
void st_cterm_adapter_clrscr(struct cterminal *cterm);
void st_cterm_adapter_clreol(struct cterminal *cterm);
int st_cterm_adapter_puttext(struct cterminal *cterm, int left, int top,
	int right, int bottom, const void *bytes);
int st_cterm_adapter_vmem_puttext(struct cterminal *cterm, int left, int top,
	int right, int bottom, const struct vmem_cell *cells);
int st_cterm_adapter_vmem_gettext(struct cterminal *cterm, int left, int top,
	int right, int bottom, struct vmem_cell *cells);
int st_cterm_adapter_movetext(struct cterminal *cterm, int left, int top,
	int right, int bottom, int destination_left, int destination_top);
int st_cterm_adapter_movetext_clear(struct cterminal *cterm, int left,
	int top, int right, int bottom, int destination_left,
	int destination_top, const struct vmem_cell *fill);
struct ciolib_pixels *st_cterm_adapter_getpixels(struct cterminal *cterm,
	uint32_t left, uint32_t top, uint32_t right, uint32_t bottom, int force);
int st_cterm_adapter_setpixels(struct cterminal *cterm, uint32_t left,
	uint32_t top, uint32_t right, uint32_t bottom, uint32_t x_offset,
	uint32_t y_offset, uint32_t mask_x, uint32_t mask_y,
	struct ciolib_pixels *pixels, struct ciolib_mask *mask);
void st_cterm_adapter_freepixels(struct ciolib_pixels *pixels);
int st_cterm_adapter_setpalette(struct cterminal *cterm, uint32_t entry,
	uint16_t red, uint16_t green, uint16_t blue);
int st_cterm_adapter_getpalette(struct cterminal *cterm, uint32_t entry,
	uint8_t *red, uint8_t *green, uint8_t *blue);
uint32_t st_cterm_adapter_map_rgb(uint16_t red, uint16_t green,
	uint16_t blue);
void st_cterm_adapter_replace_font(struct cterminal *cterm, uint8_t selector,
	const char *name, const void *data, size_t size);
int st_cterm_adapter_get_modepalette(struct cterminal *cterm,
	uint32_t *palette);
int st_cterm_adapter_getfont(struct cterminal *cterm, int slot);
int st_cterm_adapter_setfont(struct cterminal *cterm, int selector,
	int force, int slot);
uint8_t st_cterm_adapter_attrfont(struct cterminal *cterm, uint8_t attribute);
void st_cterm_adapter_attr2palette(struct cterminal *cterm,
	uint8_t attribute, uint32_t *foreground, uint32_t *background);
int st_cterm_adapter_set_modepalette(struct cterminal *cterm,
	const uint32_t *palette);
int st_cterm_adapter_find_vmode(struct cterminal *cterm, int mode);
int st_cterm_adapter_set_hold_update(struct cterminal *cterm, int hold);
int st_cterm_adapter_hold_update(const struct cterminal *cterm);
int st_cterm_adapter_default_palette(uint32_t entry, uint8_t *red,
	uint8_t *green, uint8_t *blue);
void st_cterm_adapter_record_error(struct cterminal *cterm, int error);
int st_cterm_adapter_take_error(struct cterminal *cterm);
uint16_t st_cterm_adapter_add_hyperlink(struct cterminal *cterm,
	const char *uri, const char *parameter);
char *st_cterm_adapter_hyperlink_url(struct cterminal *cterm,
	uint16_t identifier);
char *st_cterm_adapter_hyperlink_params(struct cterminal *cterm,
	uint16_t identifier);
void st_cterm_adapter_set_hyperlink(struct cterminal *cterm,
	uint16_t identifier);
uint16_t st_cterm_adapter_get_hyperlink(struct cterminal *cterm);
uint64_t st_cterm_adapter_timer64(void);
uint16_t st_cterm_adapter_crc16(const void *data, size_t size,
	uint16_t initial);
int st_cterm_adapter_base64_decode(void *destination, size_t capacity,
	const char *source);
int st_cterm_adapter_asprintf(char **result, const char *format, ...);
int st_cterm_adapter_strnicmp(const char *left, const char *right,
	size_t count);
struct ciolib_screen *st_cterm_adapter_savescreen(struct cterminal *cterm);
void st_cterm_adapter_freescreen(struct ciolib_screen *screen);

#ifndef ST_CTERM_ADAPTER_IMPLEMENTATION
#define cio_api (cterm->screen.api)
#define vparams (cterm->screen.video_params)
#define gettextinfo(i) st_cterm_adapter_gettextinfo(cterm, (i))
#define getvideoflags() st_cterm_adapter_getvideoflags(cterm)
#define setvideoflags(f) st_cterm_adapter_setvideoflags(cterm, (f))
#define window(l,t,r,b) st_cterm_adapter_window(cterm,(l),(t),(r),(b))
#define gotoxy(x,y) st_cterm_adapter_gotoxy(cterm,(x),(y))
#define wherex() st_cterm_adapter_wherex(cterm)
#define wherey() st_cterm_adapter_wherey(cterm)
#define textattr(a) st_cterm_adapter_textattr(cterm,(a))
#define setcolour(f,b) st_cterm_adapter_setcolour(cterm,(f),(b))
#define ciolib_setcolour(f,b) st_cterm_adapter_setcolour(cterm,(f),(b))
#define ciolib_setcursortype(c) st_cterm_adapter_cursor(cterm,(c))
#define _setcursortype(c) st_cterm_adapter_cursor(cterm,(c))
#define getcustomcursor(s,e,r,b,v) st_cterm_adapter_getcustomcursor(cterm,(s),(e),(r),(b),(v))
#define setcustomcursor(s,e,r,b,v) st_cterm_adapter_setcustomcursor(cterm,(s),(e),(r),(b),(v))
#define putch(c) st_cterm_adapter_putch(cterm,(c))
#define cputs(s) st_cterm_adapter_cputs(cterm,(s))
#define clrscr() st_cterm_adapter_clrscr(cterm)
#define clreol() st_cterm_adapter_clreol(cterm)
#define puttext(l,t,r,b,p) st_cterm_adapter_puttext(cterm,(l),(t),(r),(b),(p))
#define vmem_puttext(l,t,r,b,p) st_cterm_adapter_vmem_puttext(cterm,(l),(t),(r),(b),(p))
#define ciolib_vmem_puttext(l,t,r,b,p) st_cterm_adapter_vmem_puttext(cterm,(l),(t),(r),(b),(p))
#define vmem_gettext(l,t,r,b,p) st_cterm_adapter_vmem_gettext(cterm,(l),(t),(r),(b),(p))
#define movetext(l,t,r,b,dl,dt) st_cterm_adapter_movetext(cterm,(l),(t),(r),(b),(dl),(dt))
#define movetext_clear(l,t,r,b,dl,dt,f) st_cterm_adapter_movetext_clear(cterm,(l),(t),(r),(b),(dl),(dt),(f))
#define getpixels(l,t,r,b,f) st_cterm_adapter_getpixels(cterm,(l),(t),(r),(b),(f))
#define setpixels(l,t,r,b,xo,yo,mx,my,p,m) st_cterm_adapter_setpixels(cterm,(l),(t),(r),(b),(xo),(yo),(mx),(my),(p),(m))
#define freepixels(p) st_cterm_adapter_freepixels(p)
#define setpalette(e,r,g,b) st_cterm_adapter_setpalette(cterm,(e),(r),(g),(b))
#define getpalette(e,r,g,b) st_cterm_adapter_getpalette(cterm,(e),(r),(g),(b))
#define map_rgb(r,g,b) st_cterm_adapter_map_rgb((r),(g),(b))
#define replace_font(i,n,d,s) st_cterm_adapter_replace_font(cterm,(i),(n),(d),(s))
#define get_modepalette(p) st_cterm_adapter_get_modepalette(cterm,(p))
#define set_modepalette(p) st_cterm_adapter_set_modepalette(cterm,(p))
#define getfont(s) st_cterm_adapter_getfont(cterm,(s))
#define setfont(f,force,s) st_cterm_adapter_setfont(cterm,(f),(force),(s))
#define ciolib_attrfont(a) st_cterm_adapter_attrfont(cterm,(a))
#define attr2palette(a,f,b) st_cterm_adapter_attr2palette(cterm,(a),(f),(b))
#define ciolib_add_hyperlink(u,p) st_cterm_adapter_add_hyperlink(cterm,(u),(p))
#define ciolib_get_hyperlink_url(i) st_cterm_adapter_hyperlink_url(cterm,(i))
#define ciolib_get_hyperlink_params(i) st_cterm_adapter_hyperlink_params(cterm,(i))
#define ciolib_set_current_hyperlink(i) st_cterm_adapter_set_hyperlink(cterm,(i))
#define ciolib_get_current_hyperlink() st_cterm_adapter_get_hyperlink(cterm)
#define savescreen() st_cterm_adapter_savescreen(cterm)
#define freescreen(s) st_cterm_adapter_freescreen(s)
#define find_vmode(mode) st_cterm_adapter_find_vmode(cterm,(mode))
#define _wscroll (cterm->screen.wscroll)
#define xp_timer64() st_cterm_adapter_timer64()
#define crc16(d,n) st_cterm_adapter_crc16((d),(n),0)
#define icrc16(c,d,n) st_cterm_adapter_crc16((d),(n),(c))
#define ucrc16(v,c) st_cterm_adapter_crc16(&(uint8_t){(uint8_t)(v)},1,(c))
#define b64_decode(d,c,s,n) st_cterm_adapter_base64_decode((d),(c),(s))
#define asprintf st_cterm_adapter_asprintf
#define strnicmp st_cterm_adapter_strnicmp
#endif

#endif
