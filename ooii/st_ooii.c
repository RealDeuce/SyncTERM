/* Operation Overkill ][ terminal codes and resources, extracted from SyncTERM. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#include "st_ooii.h"
#include "io_error.h"
#include "string_compat.h"
#include "value_convert_time.h"
#include "ooii_bmenus.h"
#include "ooii_cmenus.h"
#include "ooii_logons.h"
#include "ooii_sounds.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct st_ooii {
	struct st_ooii_config config;
	int mode;
	size_t mode_generation;
	int error;
	unsigned attribute;
	uint32_t random;
	size_t match[2];
	uint8_t command[256];
	uint8_t *end;
	size_t command_size;
	bool discard;
};

static void
write_text(st_ooii_t *ooii, const void *bytes, ptrdiff_t size)
{
	if (ooii->error == 0)
		ooii->error = ooii->config.render(ooii->config.context, bytes,
		    size < 0 ? strlen(bytes) : (size_t)size);
}

static void
play_sound(st_ooii_t *ooii, const uint8_t *samples, size_t size)
{
	if (ooii->error == 0 && ooii->config.sound != NULL)
		ooii->config.sound(ooii->config.context, samples, size);
}

static unsigned
current_attributes(st_ooii_t *ooii)
{
	return ooii->config.attributes == NULL ? ooii->attribute
	    : ooii->config.attributes(ooii->config.context);
}

static unsigned
random_choice(st_ooii_t *ooii, unsigned count)
{
	/* Per-session, noncryptographic choices of artwork and sound variants. */
	ooii->random ^= ooii->random << 13;
	ooii->random ^= ooii->random >> 17;
	ooii->random ^= ooii->random << 5;
	return ooii->random % count;
}

static void
advance_code(st_ooii_t *ooii, uint8_t **cursor)
{
	if (*cursor < ooii->end)
		++*cursor;
}

static int
block_number(const char *text)
{
	long long value;

	errno = 0;
	value = strtoll(text, NULL, 10);
	/* Leave room for the legacy renderer's one-based coordinate offsets. */
	if (value > INT_MAX - 1LL) return INT_MAX - 1;
	if (value < INT_MIN + 1LL) return INT_MIN + 1;
	return (int)value;
}

static const unsigned char stormColors[7] = {7, 12, 8, 15, 14, 13, 1};

static char          diseases[11][14] = // #,str length
{
	"None",                         // 0
	"Pneumonia",                    // 1
	"Malaria",                      // 2
	"Yellow Fever",                 // 3
	"Hydrocism",                    // 4
	"Staph",                        // 5
	"Rabies",                       // 6
	"Polio",                        // 7
	"Black Plague",                 // 8
	"Delyria",                      // 9
	"Poison"
};                                      // 10


static char weapons[27][20] = {
	"\xfa",              // 0
	"Steel Chain",    // 1
	"Salanger",       // 2
	"Natase",         // 3
	"TransAxe",       // 4 {  Power Pack   }
	"Electric Sword", // 5 { Power Pack   }
	"Tevix-Bahn",     // 6     { Cirze Supply }
	"Xendrix",        // 7 { NOT FOR SALE!  Infinex }
	"Trilasm",        // 8 { ZZETs        }
	"Exavator",       // 9 { ZZETs        }
	"Raxhaven",       // 10        { Power Pack   }
	"SanPlizer",      // 11        { Power Pack   }
	"Devastator",     // 12        { Cirze Supply }
	"Z-Tempest",      // 13        { Cirze Supply }
	"Herculean",      // 14        { NOT FOR SALE!  Infinex }
	"Trignet",        // 15        { NOT FOR SALE!  Firenet }
	"Extinguisher",   // 16
	"Shovel",         // 17
	"Nail Gun",       // 18
	"Jack Knife",     // 19
	"Metal Shears",   // 20
	"Wall Hook",      // 21
	"Hoist",          // 22
	"Propeller",      // 23
	"Pickaxe",        // 24
	"Sledge Hammer",  // 25
	"Hydra-Jack"
};                        // 26


static char ammos[6][20] = {
	"\xfa",            // 0
	"ZZETs",        // 1   {  (Trilasm/Exavator) }
	"Power Pack",   // 2   {  (SanPlizer/RaxHaven/E-Sword/)        }
	"Cirze Supply", // 3   {  (Z-Tempest/Devastator/Tevix-Bahn)    }
	"Infinex",      // 4   {  NOT FOR SALE!! (Herculean/Xendrix)   }
	"Firenet"
};                      // 5   {  NOT FOR SALE!! (Trignet)             }


static char equips[46][20] = {
	"\xfa",              // 0
	"Rations",        // 1
	"Hemp Rope",      // 2
	"Silencer",       // 3
	"Transmitter",    // 4
	"Summoner",       // 5
	"MedPack",        // 6
	"Thumpers",       // 7
	"Galacticom",     // 8
	"Gasmask",        // 9
	"Duroplast",      // 10
	"Aerial Scan",    // 11
	"VacPack",        // 12
	"Telegrasp",      // 13        { NOT FOR SALE! }
	"Red Key",        // 14        { NOT FOR SALE! }
	"Blue Key",       // 15        { NOT FOR SALE! }
	"Green Key",      // 16        { NOT FOR SALE! }
	"Techkit",        // 17        { NOT FOR SALE! }
	"Rusted Wire",    // 18        { NOT FOR SALE! }{Arming Device}
	"Clay Package",   // 19        { NOT FOR SALE! }{Plutonium}
	"Alarm Clock",    // 20        { NOT FOR SALE! }{Timer}
	"Broken Keypad",  // 21        { NOT FOR SALE! }{Keypad}
	"Code Key",       // 22        { NOT FOR SALE! }{Entry Into AFBase}
	"Alien Device",   // 23        { NOT FOR SALE! }{Entry Into SpaceShip}
	"Trikorder",      // 24        { NOT FOR SALE! }
	"Remote Control", // 25        { NOT FOR SALE! }
	"Human Skull",    // 26        { NOT FOR SALE! }
	"Floppy Disk",    // 27        { NOT FOR SALE! }{Entry to Computer Silo}
	"Alien Skull",    // 28        { NOT FOR SALE! }
	"Hydrite Egg",    // 29        { NOT FOR SALE! }
	"Lazer Key",      // 30        { NOT FOR SALE! }{Entry thru AFB doorways }
	"Landmine",       // 31        { For Sale Only In AirForce Base}
	"Zimmer",         // 32        { For Sale Only In AirForce Base}
	"Re-Bar Scrap",   // 33
	"Door Hinge",     // 34
	"Broken Wrench",  // 35
	"Steel Coupling", // 36
	"Pipe Fitting",   // 37
	"Rotted Cabling", // 38
	"Pipe Die",       // 39
	"Chrome Pipe",    // 40
	"Gate Valve",     // 41
	"Butane Tank",    // 42
	"Drill Bit",      // 43
	"I-Bar Clamp",    // 44
	"Copper Tubing"
};                        // 45


static char suits[4][20] = {
	"\xfa",                  // 0
	"Environmental Suit", // 1
	"Combat Suit",        // 2
	"Anti-G Suit"
};                            // 3


static char armors[13][20] = {
	"\xfa",                  // 0
	"Leather",            // 1
	"HeXonium",           // 2
	"Laser Vest",         // 3
	"Shezvarin",          // 4
	"Reflecto",           // 5
	"Mudrane Sheathing",  // 6
	"Breonesin Sheeting", // 7
	"Barbed Wire",        // 8
	"Asbestos Pads",      // 9
	"Cuprid Vest",        // 10
	"Sheet Metal",        // 11
	"PolyCarbon"
};                            // 12

static void
term_gotoxy(st_ooii_t *ooii, int x, int y)
{
	char ansistr[32];

	snprintf(ansistr, sizeof(ansistr), "\x1b[%d;%dH", y, x);
	write_text(ooii, ansistr, strlen(ansistr));
}

static void
term_clearscreen(st_ooii_t *ooii)
{
	write_text(ooii, "\x1b[0m\x1b[2J\x1b[H", 11);
}

static const int term_colours[8] = {0, 4, 2, 6, 1, 5, 3, 7};

static void
term_setattr(st_ooii_t *ooii, int attr)
{
	char str[16];
	int  fg, ofg;
	int  bg, obg;
	int  bl, obl;
	int  br, obr;
	int  oa;

	attr &= 255;
	str[0] = 0;
	if (current_attributes(ooii) == (unsigned)attr)
		return;

	bl = attr & 0x80;
	bg = (attr >> 4) & 0x7;
	fg = attr & 0x07;
	br = attr & 0x08;

	oa = (int)current_attributes(ooii);
	obl = oa & 0x80;
	obg = (oa >> 4) & 0x7;
	ofg = oa & 0x07;
	obr = oa & 0x08;

	(void)strlcpy(str, "\033[", sizeof(str));
	if (obl != bl) {
		if (!bl)
			strlcat(str, "25;", sizeof(str));
		else
			strlcat(str, "5;", sizeof(str));
	}
	if (br != obr) {
		if (br)
			strlcat(str, "1;", sizeof(str));
		else
			strlcat(str, "22;", sizeof(str));
	}
	if (fg != ofg)
		snprintf(str + strlen(str), sizeof(str) - strlen(str), "3%d;", term_colours[fg]);
	if (bg != obg)
		snprintf(str + strlen(str), sizeof(str) - strlen(str), "4%d;", term_colours[bg]);
	str[strlen(str) - 1] = 'm';
	ooii->attribute = (unsigned)attr;
	write_text(ooii, str, strlen(str));
}

static void
readInPix(st_ooii_t *ooii, char codeCh, int ooii_mode)
{
	int fptr = 0;

	term_clearscreen(ooii);
	term_gotoxy(ooii, 1, 1);
	switch ((char)codeCh) {
                // complex pictures
		case 'A':
			fptr = 0; /* Main = 0 */
			break;
		case 'B':
			fptr = 1; /* Barracks = 1 */
			break;
		case 'C':
			fptr = 3; /* Head-quarters = 3 */
			break;
		case 'D':
			fptr = 2; /* Communications = 2 */
			break;
		case 'E':
			fptr = 4; /* Wastelands = 4 */
			break;

                // base pictures
		case 'F':
			fptr = 0; /* Main menu = 0 */
			break;
		case 'G':
			fptr = 1; /* Barracks = 1 */
			break;
		case 'H':
			fptr = 3; /* Storage = 3 */
			break;
		case 'I':
			fptr = 5; /* Tech Centre = 5 */
			break;
		case 'J':
			fptr = 2; /* Control Room = 2 */
			break;
		case 'K':
			fptr = 4; /* Radio Room = 4 */
			break;

                // logon pictures
		case '0':
			fptr = (int)random_choice(ooii, 10);
			break;
	}

	if ((codeCh >= 'A') && (codeCh <= 'E')) {
		write_text(ooii,
		    ooii_cmenus[ooii_mode - 1][fptr],
		    (ptrdiff_t)ooii_cmenus_sizes[ooii_mode - 1][fptr]);
	}
	else if ((codeCh >= 'F') && (codeCh <= 'K')) {
		write_text(ooii,
		    ooii_bmenus[ooii_mode - 1][fptr],
		    (ptrdiff_t)ooii_bmenus_sizes[ooii_mode - 1][fptr]);
	}
	else if (codeCh == '0') {
		write_text(ooii,
		    ooii_logon[ooii_mode - 1][fptr],
		    (ptrdiff_t)ooii_logon_sizes[ooii_mode - 1][fptr]);
	}

        /*
         * We don't overwrite the status line, so we don't need to redraw it
         * statusLine();
         */

	return;
}

static ptrdiff_t
readInText(st_ooii_t *ooii, unsigned char *codeStr)
{
	unsigned char *origCodeStr = codeStr;

	switch ((char)codeStr[0]) {
		case '1':
			term_setattr(ooii, 6);
			write_text(ooii,
			    "You mosey on over to the bar and take a seat on a scuffed barstool.  The\r\n",
			    -1);
			write_text(ooii,
			    "slighty deformed keeper grunts, \"Woth ja leeke?\"  A galacticom on the top\r\n",
			    -1);
			write_text(ooii,
			    "shelf behind him translates his jumble into \"What would you like?\"  You\r\n",
			    -1);
			write_text(ooii,
			    "start to wonder what ever happened to the old standard human language.\r\n\r\n",
			    -1);
			term_setattr(ooii, 10);
			write_text(ooii, "\"Give me the House Special,\" you smirk.\r\n\r\n", -1);
			term_setattr(ooii, 2);
			write_text(ooii,
			    "The bartender stares at you with one eye and then drools in agreement.  He\r\n",
			    -1);
			write_text(ooii,
			    "vanishes from behind the bar and pops back up a few seconds later.  He\r\n",
			    -1);
			write_text(ooii,
			    "slides you the mysterious drink as he begins to chuckle.  White smoke\r\n",
			    -1);
			write_text(ooii,
			    "slithers from the bubbling green slime over onto the bar counter.\r\n\r\n",
			    -1);
			term_setattr(ooii, 10);
			write_text(ooii,
			    "\"What do I owe you?\" you inquire to the barman.  He promptly responds\r\n",
			    -1);
			write_text(ooii,
			    "with that same smirk on his face, \"Youkla telph me.\"\r\n",
			    -1);
			break;
	}

	return codeStr - origCodeStr;
}

static void
getBlock(st_ooii_t *ooii, unsigned char **codeStr, char *menuBlock, size_t maxlen)
{
	size_t pos = 0;

	menuBlock[0] = 0;
	if (*codeStr < ooii->end && **codeStr == '_')
		(*codeStr)++;
	while (*codeStr < ooii->end && **codeStr && **codeStr != '_' && **codeStr != '|'
	    && pos < maxlen - 1) {
		menuBlock[pos++] = (char)*((*codeStr)++);
		menuBlock[pos] = 0;
	}
	while (*codeStr < ooii->end && **codeStr && **codeStr != '_' && **codeStr != '|')
		(*codeStr)++;
}

static void
strljust(char *buf, size_t len, char pad)
{
	size_t buflen = strlen(buf);

	if (buflen < len) {
		memset(buf + buflen, pad, len - buflen);
		buf[len] = 0;
	}
}

static void
strrjust(char *buf, size_t len, char pad)
{
	size_t buflen = strlen(buf);

	if (buflen < len) {
		memmove(buf + (len - buflen), buf, buflen + 1);
		memset(buf, pad, len - buflen);
	}
}

static ptrdiff_t
readSmallMenu(st_ooii_t *ooii, unsigned char *codeStr)
{
#define MultA 2
#define MultE 3
#define MultS 3
#define MultW 2

	long           aPrice[5] = {0, 4500, 14500, 29000, 42500};
	long           ePrice[13] = {0, 50, 200, 300, 500, 800, 1000, 1500, 1750, 2000, 3000, 5000, 15000};
	long           sPrice[3] = {0, 4500, 13500};
	long           wPrice[15] = {
		0, 700, 4250, 9500, 18000, 32500, 55750, 0, 7500, 12000, 19250, 30500, 41000, 64750, 0
	};

	char           tempBlock[255];
	char           menuBlock[255];
	int            yy;
	int            zz;
	unsigned char *origCodeStr = codeStr;
	char           buf[266];

	switch ((char)codeStr[0]) {
		case 'a':
			term_setattr(ooii, 10);
			write_text(ooii, "Supply Room\r\n", -1);
			term_setattr(ooii, 2);
			write_text(ooii, "~~~~~~~~~~~\r\n", -1);
			term_setattr(ooii, 11);
			write_text(ooii, "-[A]  Armor\r\n", -1);
			write_text(ooii, "-[E]  Equipment\r\n", -1);
			write_text(ooii, "-[P]  Power Supply/Ammo\r\n", -1);
			write_text(ooii, "-[S]  Suits/Outfits\r\n", -1);
			write_text(ooii, "-[W]  Weapons\r\n\r\n", -1);
			write_text(ooii, "-[I]  Supply Inventory\r\n", -1);
			write_text(ooii, "-[C]  Check\r\n\r\n", -1);
			write_text(ooii, "-[*]  Back to HQ\r\n\r\n", -1);
			term_setattr(ooii, 9);
			write_text(ooii, "-<Supply Room>  \xe9 ", -1);
			break;

		case 'b':
			term_setattr(ooii, 15);
			write_text(ooii, "Recruit Chambers\r\n", -1);
			term_setattr(ooii, 4);
			write_text(ooii, "~~~~~~~~~~~~~~~~\r\n", -1);
			term_setattr(ooii, 11);
			write_text(ooii, "-[A]  Adjust Specs\r\n", -1);
			write_text(ooii, "-[C]  Check Status\r\n", -1);
			write_text(ooii, "-[M]  Morgue Listing\r\n", -1);
			write_text(ooii, "-[R]  Roster Status\r\n", -1);
			write_text(ooii, "-[T]  Training Room\r\n", -1);
			write_text(ooii, "-[U]  Use Equipment\r\n", -1);
			write_text(ooii, "-[V]  View Recruit\r\n\r\n", -1);
			write_text(ooii, "-[*]  Exit to Barracks\r\n\r\n", -1);
			term_setattr(ooii, 9);
			write_text(ooii, "-<Chambers>  \xe9 ", -1);
			break;

		case 'c':
			term_setattr(ooii, 13);
			write_text(ooii, "Adjust Specifications:\r\n", -1);
			term_setattr(ooii, 5);
			write_text(ooii, "~~~~~~~~~~~~~~~~~~~~~\r\n", -1);
			term_setattr(ooii, 10);
			write_text(ooii, "-[A]  Ansi Color On/Off\r\n", -1);
			write_text(ooii, "-[C]  Combat Action/Stat-Random\r\n", -1);
			write_text(ooii, "-[D]  Disable/Enable Ansiterm\r\n", -1);
			write_text(ooii, "-[E]  Expert Menus On/Off\r\n", -1);
			write_text(ooii, "-[F]  Fight-Text Delay Change\r\n", -1);
			write_text(ooii, "-[P]  Password Change\r\n", -1);
			write_text(ooii, "-[Q]  Quote for Combat\r\n", -1);
			write_text(ooii, "-[S]  Scanner Scroll/Stationary\r\n", -1);
			write_text(ooii, "-[T]  Terminate Yourself\r\n\r\n", -1);
			write_text(ooii, "-[*]  Exit\r\n\r\n", -1);
			term_setattr(ooii, 9);
			write_text(ooii, "-<Specs>  \xe9 ", -1);
			break;

		case 'd':
			advance_code(ooii, &codeStr);

			term_setattr(ooii, 13);
			write_text(ooii, "Storage Bank:\r\n", -1);
			term_setattr(ooii, 5);
			write_text(ooii, "~~~~~~~~~~~~\r\n", -1);
			term_setattr(ooii, 15);

			write_text(ooii, "Crystals in Savings : ", -1);
			getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
			strlcat(menuBlock, "\r\n", sizeof(menuBlock));
			write_text(ooii, menuBlock, -1);

			write_text(ooii, "Crystals on Loan    : ", -1);
			getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
			strlcat(menuBlock, "\r\n", sizeof(menuBlock));
			write_text(ooii, menuBlock, -1);

			write_text(ooii, "\r\n", -1);
			term_setattr(ooii, 11);

			write_text(ooii, "-[D]  Deposit\r\n", -1);
			write_text(ooii, "-[W]  Withdraw\r\n", -1);
			write_text(ooii, "-[L]  Make a Loan\r\n", -1);
			write_text(ooii, "-[P]  Pay off Loan\r\n", -1);

			getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
			zz = block_number(menuBlock);
			if (zz > 0)
				write_text(ooii, "-[S]  Squadron Account\r\n", -1);

			write_text(ooii, "-[T]  Transfer Crystals\r\n\r\n", -1);
			write_text(ooii, "-[*]  Exit to Chambers\r\n\r\n", -1);

			getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
			term_setattr(ooii, 15);

			write_text(ooii, "You have ", -1);
			write_text(ooii, menuBlock, -1);
			write_text(ooii, " crystals onhand.\r\n\r\n", -1);
			term_setattr(ooii, 9);
			write_text(ooii, "-<Bank>  \xe9 ", -1);
			break;

		case 'e':
			advance_code(ooii, &codeStr);

			term_setattr(ooii, 14);
			write_text(ooii,
			    "You are now in the Games Room, a delightful place to leisurely relax from\r\n",
			    -1);
			write_text(ooii,
			    "the everyday hack n' slash.   A small bar is located on the south wall.\r\n\r\n",
			    -1);

			term_setattr(ooii, 15);
			getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
			write_text(ooii, "Games Left :[ ", -1);
			term_setattr(ooii, 11);
			write_text(ooii, menuBlock, -1);
			write_text(ooii, "\r\n\r\n", -1);
			term_setattr(ooii, 10);

			write_text(ooii, "-[A]  Arm Wrestling\r\n", -1);
			write_text(ooii, "-[B]  Barl's Bar\r\n", -1);
			write_text(ooii, "-[L]  Loot Crystals\r\n", -1);
			write_text(ooii, "-[M]  Meat Darts\r\n", -1);
			write_text(ooii, "-[R]  Roach Races\r\n", -1);
			write_text(ooii, "-[S]  SharpShooter\r\n\r\n", -1);
			write_text(ooii, "-[*]  Exit to Barracks\r\n\r\n", -1);

			term_setattr(ooii, 9);
			write_text(ooii, "-<Games Room>  \xe9 ", -1);
			break;

		case 'f':
			advance_code(ooii, &codeStr);

			term_setattr(ooii, 15);
			write_text(ooii, "Sentry Post\r\n", -1);
			term_setattr(ooii, 4);
			write_text(ooii, "~~~~~~~~~~~\r\n", -1);
			term_setattr(ooii, 11);

			getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
			yy = block_number(menuBlock);
			if (yy > 0) {
				write_text(ooii, "-[O]  Offer Guard Items\r\n\r\n", -1);
				write_text(ooii, "-[V]  View Gate Guard\r\n", -1);
				write_text(ooii, "-[U]  Upgrade Gate Guard\r\n", -1);
			}
			else {
				write_text(ooii, "-[R]  Requisition New Guard\r\n", -1);
			}

			write_text(ooii, "-[C]  Check Your Status\r\n\r\n", -1);
			write_text(ooii, "-[*]  Exit to Barracks\r\n\r\n", -1);
			term_setattr(ooii, 9);
			write_text(ooii, "-<Sentry Post>  \xe9 ", -1);
			break;

		case 'g':
			advance_code(ooii, &codeStr);

			term_setattr(ooii, 15);
			write_text(ooii, "Electronic-Mail\r\n", -1);
			term_setattr(ooii, 7);
			write_text(ooii, "~~~~~~~~~~~~~~~\r\n", -1);

			getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));

			if (menuBlock[0] > 48) {
				term_setattr(ooii, 14);
				write_text(ooii, "-[M]  Mass Mail to Squadron\r\n\r\n", -1);
			}

			term_setattr(ooii, 11);
			write_text(ooii, "-[R]  Read E-Mail\r\n", -1);
			write_text(ooii, "-[S]  Send E-Mail\r\n\r\n", -1);
			write_text(ooii, "-[*]  Back to Comm. Post\r\n\r\n", -1);
			term_setattr(ooii, 9);
			write_text(ooii, "-<E-Mail>  \xe9 ", -1);
			break;

		case 'h':
			term_setattr(ooii, 11);
			write_text(ooii, "\r\nHelp Information:\r\n", -1);
			term_setattr(ooii, 3);
			write_text(ooii, "~~~~~~~~~~~~~~~~\r\n", -1);
			term_setattr(ooii, 15);
			write_text(ooii, "-[1]  Scenario\r\n", -1);
			write_text(ooii, "-[2]  Character\r\n", -1);
			write_text(ooii, "-[3]  Complex\r\n", -1);
			write_text(ooii, "-[4]  Wastelands\r\n", -1);
			write_text(ooii, "-[5]  Bases\r\n\r\n", -1);
			write_text(ooii, "-[6]  Bulletin Info\r\n", -1);
			write_text(ooii, "-[7]  Logon News Info\r\n\r\n", -1);
			write_text(ooii, "-[0]  Exit\r\n\r\n", -1);
			break;

		case 'i':
			advance_code(ooii, &codeStr);

			term_setattr(ooii, 11);
			write_text(ooii, "Medical Center\r\n", -1);
			term_setattr(ooii, 3);
			write_text(ooii, "~~~~~~~~~~~~~~\r\n", -1);
			term_setattr(ooii, 10);

			write_text(ooii, "Radiation   :[ ", -1);
			getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
			zz = block_number(menuBlock);
			if (zz < 44)
				term_setattr(ooii, 15);
			else
				term_setattr(ooii, 12);
			write_text(ooii, menuBlock, -1);
			write_text(ooii, "%\r\n", -1);

			term_setattr(ooii, 10);
			write_text(ooii, "Hit Points  :[ ", -1);
			getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
			zz = block_number(menuBlock);
			if (zz > 5)
				term_setattr(ooii, 15);
			else
				term_setattr(ooii, 12);

			write_text(ooii, menuBlock, -1);
			getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
			write_text(ooii, " (", -1);
			write_text(ooii, menuBlock, -1);
			write_text(ooii, ")\r\n", -1);

			term_setattr(ooii, 10);
			write_text(ooii, "Infectious  :[ ", -1);
			getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
			zz = block_number(menuBlock);
			if (zz < 0 || zz >= 11)
				break;
			term_setattr(ooii, 15);

			write_text(ooii, diseases[zz], -1);
			write_text(ooii, "\r\n\r\n", -1);
			term_setattr(ooii, 3);

			write_text(ooii, "-[M]  Medical Treatment\r\n", -1);
			write_text(ooii, "-[R]  Radiation Treatment\r\n\r\n", -1);
			write_text(ooii, "-[N]  Neutralize Disease\r\n", -1);
			write_text(ooii, "-[V]  Vaccinate Disease\r\n\r\n", -1);
			write_text(ooii, "-[*]  Back to HQ\r\n\r\n", -1);
			term_setattr(ooii, 9);
			write_text(ooii, "-<Medical Center>  \xe9 ", -1);
			break;

		case 'j':
			term_setattr(ooii, 13);
			write_text(ooii, "Records Room\r\n", -1);
			term_setattr(ooii, 5);
			write_text(ooii, "~~~~~~~~~~~~\r\n", -1);
			term_setattr(ooii, 10);
			write_text(ooii, "-[A]  Total Ability\r\n", -1);
			write_text(ooii, "-[B]  Bravery\r\n", -1);
			write_text(ooii, "-[C]  Combat Accuracy\r\n", -1);
			write_text(ooii, "-[E]  Experience\r\n", -1);
			write_text(ooii, "-[K]  Total Kills\r\n", -1);
			write_text(ooii, "-[O]  Owned Crystals\r\n", -1);
			write_text(ooii, "-[P]  Power\r\n", -1);
			write_text(ooii, "-[S]  Squadrons\r\n", -1);
			write_text(ooii, "-[W]  Wealth\r\n\r\n", -1);
			write_text(ooii, "-[H]  Hall of Fame\r\n\r\n", -1);
			write_text(ooii, "-[*]  Back to HQ\r\n\r\n", -1);
			term_setattr(ooii, 9);
			write_text(ooii, "-<Records>  \xe9 ", -1);
			break;

		case 'k':
			advance_code(ooii, &codeStr);

			term_setattr(ooii, 10);
			write_text(ooii, "Supply Armor\r\n", -1);
			term_setattr(ooii, 2);
			write_text(ooii, "~~~~~~~~~~~~\r\n", -1);
			term_setattr(ooii, 15);

			getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
			zz = block_number(menuBlock);
			if (zz < 0 || zz >= 13)
				break;
			write_text(ooii, armors[zz], -1);
			write_text(ooii, " / ", -1);
			getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
			write_text(ooii, menuBlock, -1);
			write_text(ooii, " Hits\r\n\r\n", -1);

			term_setattr(ooii, 11);
			write_text(ooii, "-[B]  Buy Armor\r\n", -1);
			write_text(ooii, "-[S]  Sell Armor\r\n\r\n", -1);
			write_text(ooii, "-[*]  Back to Supply Room\r\n\r\n", -1);
			term_setattr(ooii, 9);
			write_text(ooii, "-<Supply Armor>  \xe9 ", -1);
			break;

		case 'l':
			advance_code(ooii, &codeStr);
			getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));

			term_setattr(ooii, 9);
			write_text(ooii, "Armor Types           Hits     Crystals\r\n", -1);
			term_setattr(ooii, 3);
			write_text(ooii, "~~~~~~~~~~~           ~~~~     ~~~~~~~~\r\n", -1);
			term_setattr(ooii, 11);

			for (zz = 1; zz < 5; zz++) {
				snprintf(buf, sizeof(buf), "-[%d]  - %s", zz, armors[zz]);
				strljust(buf, 23, ' ');
				write_text(ooii, buf, -1);

				switch (zz) {
					case 1:
						snprintf(buf, sizeof(buf), "50");
						break;
					case 2:
						snprintf(buf, sizeof(buf), "80");
						break;
					case 3:
						snprintf(buf, sizeof(buf), "110");
						break;
					case 4:
						snprintf(buf, sizeof(buf), "150");
						break;
				}
				strrjust(buf, 3, ' ');
				write_text(ooii, buf, -1);

				if (menuBlock[0] == 48)
					snprintf(buf, sizeof(buf), "%ld", aPrice[zz]);
				else
					snprintf(buf, sizeof(buf), "%ld", aPrice[zz] * MultA);

				strrjust(buf, 13, ' ');
				write_text(ooii, buf, -1);
				write_text(ooii, "\r\n", -1);
			}
			break;

		case 'm':
			advance_code(ooii, &codeStr);

			if (codeStr[0] == '1') {
				term_setattr(ooii, 10);
				write_text(ooii, "Supply Equipment\r\n", -1);
				term_setattr(ooii, 2);
				write_text(ooii, "~~~~~~~~~~~~~~~~\r\n", -1);
			}
			else {
				term_setattr(ooii, 11);
				write_text(ooii, "\r\n-[B]  Buy Equipment\r\n", -1);
				write_text(ooii, "-[S]  Sell Equipment\r\n\r\n", -1);
				write_text(ooii, "-[*]  Back to Supply Room\r\n\r\n", -1);
				term_setattr(ooii, 9);
				write_text(ooii, "-<Supply Equip>  \xe9 ", -1);
			}
			break;

		case 'n':
			advance_code(ooii, &codeStr);
			getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));

			term_setattr(ooii, 9);
			write_text(ooii, "Equipment                 Crystals\r\n", -1);
			term_setattr(ooii, 3);
			write_text(ooii, "~~~~~~~~~                 ~~~~~~~~\r\n", -1);
			term_setattr(ooii, 11);

			for (zz = 1; zz < 13; zz++) {
				if (zz <= 9)
					snprintf(buf, sizeof(buf), "-[%d]  - %s", zz, equips[zz]);
				else
					snprintf(buf, sizeof(buf), "-[%d] - %s", zz, equips[zz]);

				strljust(buf, 28, ' ');
				write_text(ooii, buf, -1);

				if (menuBlock[0] == 48)
					snprintf(buf, sizeof(buf), "%ld", ePrice[zz]);
				else
					snprintf(buf, sizeof(buf), "%ld", ePrice[zz] * MultE);

				strrjust(buf, 6, ' ');
				write_text(ooii, buf, -1);
				write_text(ooii, "\r\n", -1);
			}

			write_text(ooii, "\r\n", -1);
			break;

		case 'o':
			advance_code(ooii, &codeStr);
			getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
			zz = block_number(menuBlock);
			if (zz < 0 || zz >= 4)
				break;

			term_setattr(ooii, 10);
			write_text(ooii, "Supply Suits\r\n", -1);
			term_setattr(ooii, 2);
			write_text(ooii, "~~~~~~~~~~~~\r\n", -1);
			term_setattr(ooii, 15);
			write_text(ooii, "Suit : ", -1);
			write_text(ooii, suits[zz], -1);

			term_setattr(ooii, 11);
			write_text(ooii, "\r\n\r\n-[B]  Buy a Suit\r\n", -1);
			write_text(ooii, "-[S]  Sell a Suit\r\n\r\n", -1);
			write_text(ooii, "-[*]  Back to Supply Room\r\n\r\n", -1);
			term_setattr(ooii, 9);
			write_text(ooii, "-<Supply Suits>  \xe9 ", -1);
			break;

		case 'p':
			advance_code(ooii, &codeStr);
			getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));

			term_setattr(ooii, 9);
			write_text(ooii, "Suits/Outfits               Crystals\r\n", -1);
			term_setattr(ooii, 3);
			write_text(ooii, "~~~~~~~~~~~~~               ~~~~~~~~\r\n", -1);
			term_setattr(ooii, 11);

			for (zz = 1; zz < 3; zz++) {
				snprintf(buf, sizeof(buf), "-[%d]  - %s", zz, suits[zz]);
				strljust(buf, 30, ' ');
				write_text(ooii, buf, -1);

				if (menuBlock[0] == 48)
					snprintf(buf, sizeof(buf), "%ld", sPrice[zz]);
				else
					snprintf(buf, sizeof(buf), "%ld", sPrice[zz] * MultS);

				strrjust(buf, 6, ' ');
				write_text(ooii, buf, -1);
				write_text(ooii, "\r\n", -1);
			}
			break;

		case 'q':
			advance_code(ooii, &codeStr);

			term_setattr(ooii, 10);
			write_text(ooii, "Supply Weapons\r\n", -1);
			term_setattr(ooii, 2);
			write_text(ooii, "~~~~~~~~~~~~~~\r\n", -1);
			term_setattr(ooii, 15);

			for (yy = 1; yy < 3; yy++) {
				getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
				zz = block_number(menuBlock);
				if (zz >= 0 && zz < 27)
					write_text(ooii, weapons[zz], -1);
				write_text(ooii, " / ", -1);
				getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
				zz = block_number(menuBlock);
				if (zz >= 0 && zz < 6)
					write_text(ooii, ammos[zz], -1);
				write_text(ooii, " / ", -1);
				getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
				write_text(ooii, menuBlock, -1);
				write_text(ooii, " Rds\r\n", -1);
			}

			term_setattr(ooii, 11);
			write_text(ooii, "\r\n-[B]  Buy Weapon\r\n", -1);
			write_text(ooii, "-[S]  Sell Weapon\r\n\r\n", -1);
			write_text(ooii, "-[*]  Back to Supply Room\r\n\r\n", -1);
			term_setattr(ooii, 9);
			write_text(ooii, "-<Supply Weapons>  \xe9 ", -1);
			break;

		case 'r':
			advance_code(ooii, &codeStr);
			getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));

			term_setattr(ooii, 9);
			write_text(ooii, "Hand-to-Hand Weapons        Crystals\r\n", -1);
			term_setattr(ooii, 3);
			write_text(ooii, "~~~~~~~~~~~~~~~~~~~~        ~~~~~~~~\r\n", -1);
			term_setattr(ooii, 11);

			for (zz = 1; zz < 7; zz++) {
				snprintf(buf, sizeof(buf), "-[%d]  - %s", zz, weapons[zz]);
				strljust(buf, 30, ' ');
				write_text(ooii, buf, -1);

				if (menuBlock[0] == 48)
					snprintf(buf, sizeof(buf), "%ld", wPrice[zz]);
				else
					snprintf(buf, sizeof(buf), "%ld", wPrice[zz] * MultW);

				strrjust(buf, 6, ' ');
				write_text(ooii, buf, -1);
				write_text(ooii, "\r\n", -1);
			}

			term_setattr(ooii, 9);
			write_text(ooii, "\r\nLong-Range Weapons          Crystals\r\n", -1);
			term_setattr(ooii, 3);
			write_text(ooii, "~~~~~~~~~~~~~~~~~~          ~~~~~~~~\r\n", -1);
			term_setattr(ooii, 11);

			for (zz = 7; zz < 13; zz++) {
				if (zz < 10)
					snprintf(buf, sizeof(buf), "-[%d]  - %s", zz, weapons[zz + 1]);
				else
					snprintf(buf, sizeof(buf), "-[%d] - %s", zz, weapons[zz + 1]);

				strljust(buf, 30, ' ');
				write_text(ooii, buf, -1);

				if (menuBlock[0] == 48)
					snprintf(buf, sizeof(buf), "%ld", wPrice[zz + 1]);
				else
					snprintf(buf, sizeof(buf), "%ld", wPrice[zz + 1] * MultW);

				strrjust(buf, 6, ' ');
				write_text(ooii, buf, -1);
				write_text(ooii, "\r\n", -1);
			}
			break;

		case 's':
			advance_code(ooii, &codeStr);
			getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
			zz = block_number(menuBlock);

			term_setattr(ooii, 10);
			write_text(ooii, "Squadrons Menu\r\n", -1);
			term_setattr(ooii, 2);
			write_text(ooii, "~~~~~~~~~~~~~~\r\n", -1);
			term_setattr(ooii, 11);

			write_text(ooii, "-[S]  Show Squadrons/Leaders\r\n", -1);

			if (zz > 0)
				write_text(ooii, "-[M]  Show Squadron Members\r\n", -1);
			write_text(ooii, "\r\n", -1);

			if (zz == 0)
				write_text(ooii, "-[D]  Declare a Squadron\r\n", -1);
			else if (zz < 10)
				write_text(ooii, "-[Q]  Quit your Squadron\r\n", -1);
			else
				write_text(ooii, "-[L]  Leader Options\r\n", -1);

			if (zz == 0)
				write_text(ooii, "-[R]  Recruit to a Squadron\r\n", -1);

			write_text(ooii, "\r\n-[*]  Return to HQ\r\n\r\n", -1);

			term_setattr(ooii, 9);
			write_text(ooii, "-<Squadrons>  \xe9 ", -1);
			break;

		case 't':
			advance_code(ooii, &codeStr);
			getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));

			term_setattr(ooii, 15);
			write_text(ooii, "Sighting : ", -1);
			term_setattr(ooii, 13);
			write_text(ooii, menuBlock, -1);
			term_setattr(ooii, 11);

			write_text(ooii, "\r\n\r\n-[L]  ", -1);
			term_setattr(ooii, 3);
			write_text(ooii, "Long-Range Combat\r\n", -1);
			term_setattr(ooii, 11);
			write_text(ooii, "-[H]  ", -1);
			term_setattr(ooii, 3);
			write_text(ooii, "Hand-to-Hand Combat\r\n", -1);

			getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
			zz = block_number(menuBlock);

			if (!zz) {
				term_setattr(ooii, 11);
				write_text(ooii, "-[R]  ", -1);
				term_setattr(ooii, 3);
				write_text(ooii, "Run like hell!\r\n", -1);
			}

			term_setattr(ooii, 11);
			write_text(ooii, "\r\n-[C]  ", -1);
			term_setattr(ooii, 5);
			write_text(ooii, "Check Status\r\n", -1);
			term_setattr(ooii, 11);
			write_text(ooii, "-[V]  ", -1);
			term_setattr(ooii, 5);
			write_text(ooii, "View Opponent\r\n\r\n", -1);
			term_setattr(ooii, 11);
			write_text(ooii, "-[M]  ", -1);
			term_setattr(ooii, 7);
			write_text(ooii, "Toggle Combat Modes\r\n\r\n", -1);
			term_setattr(ooii, 9);
			write_text(ooii, "-<Combat>  \xe9 ", -1);
			break;

		case 'u':
			advance_code(ooii, &codeStr);
			getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
			(void)strlcpy(tempBlock, menuBlock, sizeof(tempBlock));

			term_setattr(ooii, 15);
			write_text(ooii, "Camp :[ ", -1);
			term_setattr(ooii, 10);
			write_text(ooii, menuBlock, -1);
			term_setattr(ooii, 15);
			write_text(ooii, " ]:\r\n", -1);
			term_setattr(ooii, 4);

			for (zz = 0; (size_t)zz < (10 + strlen(menuBlock)); zz++)
				write_text(ooii, "~", -1);
			write_text(ooii, "\r\n", -1);

			term_setattr(ooii, 3);
			getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
			zz = block_number(menuBlock);
			if (zz > 0)
				write_text(ooii, "-[A]  Attack recruit\r\n", -1);

			write_text(ooii, "-[L]  Leave a message\r\n\r\n", -1);

			getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
			zz = block_number(menuBlock);
			if (!zz)
				write_text(ooii, "-[B]  Borrow camper's items\r\n", -1);
			else
				write_text(ooii, "-[S]  Steal camper's items\r\n", -1);

			write_text(ooii, "-[G]  Give camper items\r\n\r\n", -1);
			write_text(ooii, "-[V]  View ", -1);
			write_text(ooii, tempBlock, -1);

			write_text(ooii, "\r\n-[C]  Check your status\r\n\r\n", -1);
			write_text(ooii, "-[D]  Drop your items\r\n", -1);
			write_text(ooii, "-[P]  Pick up items\r\n\r\n", -1);
			write_text(ooii, "-[*]  Leave campsite\r\n\r\n", -1);
			term_setattr(ooii, 9);
			write_text(ooii, "-<Camp>  \xe9 ", -1);
			break;

		case 'v':
			advance_code(ooii, &codeStr);
			term_setattr(ooii, 10);
			write_text(ooii, "Hit Points   :[ ", -1);
			term_setattr(ooii, 15);
			getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
			write_text(ooii, menuBlock, -1);
			write_text(ooii, " ", -1);

			getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
			snprintf(buf, sizeof(buf), "(%s)\r\n", menuBlock);
			write_text(ooii, buf, -1);

			term_setattr(ooii, 10);
			write_text(ooii, "Applications :[ ", -1);
			term_setattr(ooii, 15);
			getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
			snprintf(buf, sizeof(buf), "%s current, ", menuBlock);
			write_text(ooii, buf, -1);
			getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
			snprintf(buf, sizeof(buf), "%s total\r\n", menuBlock);
			write_text(ooii, buf, -1);

			term_setattr(ooii, 10);
			write_text(ooii, "Radiation    :[ ", -1);
			term_setattr(ooii, 15);
			getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
			write_text(ooii, menuBlock, -1);
			write_text(ooii, "%\r\n\r\n", -1);

			term_setattr(ooii, 15);
			write_text(ooii, "Also, you are ", -1);
			getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
			zz = block_number(menuBlock);
			switch (zz) {
				case 0:
					write_text(ooii, "not very hungry.\r\n", -1);
					break;
				case 1:
					write_text(ooii, "a little hungry.\r\n", -1);
					break;
				case 2:
					write_text(ooii, "hungry.\r\n", -1);
					break;
				case 3:
					write_text(ooii, "extremely hungry!\r\n", -1);
					break;
				case 4:
					term_setattr(ooii, 12);
					write_text(ooii, "DYING ", -1);
					term_setattr(ooii, 15);
					write_text(ooii, "of hunger!!\r\n", -1);
					break;
			}

			term_setattr(ooii, 14);
			write_text(ooii, "\r\n-[Q]  Quick Heal\r\n", -1);
			write_text(ooii, "-[U]  Use an Application\r\n", -1);

			getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
			zz = block_number(menuBlock);
			if (zz == 1)
				write_text(ooii, "-[R]  Use Rations\r\n", -1);

			write_text(ooii, "-[*]  Exit\r\n\r\n", -1);
			term_setattr(ooii, 9);
			write_text(ooii, "-<Medpacks>  \xe9 ", -1);
			break;
	}

	return codeStr - origCodeStr;
}

static void
checkStamp(st_ooii_t *ooii, int xx, int yy, char stampStr[]) // used w/ incomingCheck
{
	term_gotoxy(ooii, xx + 1, yy + 1);
	term_setattr(ooii, 11);
	write_text(ooii, stampStr, -1);
	term_setattr(ooii, 3);
	write_text(ooii, "\xe9", -1);
	return;
}

static ptrdiff_t
incomingCheckStatus(st_ooii_t *ooii, unsigned char *codeStr)
{
	int            who, zz;
	char           menuBlock[255];
	unsigned char *origCodeStr = codeStr;

	term_clearscreen(ooii);
	term_gotoxy(ooii, 1, 1);

	advance_code(ooii, &codeStr);
	getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
	who = block_number(menuBlock);

	term_setattr(ooii, who);
	write_text(ooii, "\r\n\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xbf\r\n", -1);
	term_setattr(ooii, 11);
	write_text(ooii, "Recruit   ", -1);
	term_setattr(ooii, 3);
	write_text(ooii, "\xe9", -1);
	term_setattr(ooii, who);
	term_gotoxy(ooii, 34, 3);
	write_text(ooii, " \xb3", -1);
	term_gotoxy(ooii, 1, 4);
	write_text(ooii,
	    "\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcf\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd",
	    -1);

	checkStamp(ooii, 0, 4, "Location  ");
	checkStamp(ooii, 27, 4, "Strength   ");
	checkStamp(ooii, 54, 4, "Radiation     ");
	checkStamp(ooii, 0, 5, "Training  ");
	checkStamp(ooii, 27, 5, "Dexterity  ");
	checkStamp(ooii, 54, 5, "Hit Accuracy  ");
	checkStamp(ooii, 0, 6, "Squadron  ");
	checkStamp(ooii, 27, 6, "Hit Pts    ");
	checkStamp(ooii, 54, 6, "Bravery       ");

	term_gotoxy(ooii, 1, 8);
	write_text(ooii,
	    "\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4",
	    -1);

	checkStamp(ooii, 0, 8, "Crystals  ");
	checkStamp(ooii, 27, 8, "Disease    ");
	checkStamp(ooii, 54, 8, "Total Kills   ");
	checkStamp(ooii, 0, 9, "Exp. Pts  ");
	checkStamp(ooii, 27, 9, "Level      ");
	checkStamp(ooii, 54, 9, "Hunger Level  ");

	term_gotoxy(ooii, 1, 11);
	term_setattr(ooii, who);
	write_text(ooii,
	    "\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd",
	    -1);

	checkStamp(ooii, 0, 11, "Weapon    ");
	checkStamp(ooii, 0, 12, "Weapon    ");

	term_gotoxy(ooii, 1, 14);
	term_setattr(ooii, who);
	write_text(ooii,
	    "\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4",
	    -1);

	checkStamp(ooii, 0, 14, "Armor     ");
	checkStamp(ooii, 0, 15, "Outfit    ");

	term_gotoxy(ooii, 1, 17);
	term_setattr(ooii, who);
	write_text(ooii,
	    "\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4",
	    -1);

	for (zz = 1; zz < 4; zz++)
		checkStamp(ooii, 0, 16 + zz, "Equipment ");

	term_gotoxy(ooii, 1, 21);
	term_setattr(ooii, who);
	write_text(ooii,
	    "\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd",
	    -1);

	term_gotoxy(ooii, 1, 23);

	return codeStr - origCodeStr;
}

static const char *
scanChar(unsigned char s, int where, int miniTrik)
{
	if (!miniTrik) {
		if ((s == 7) || ((s >= 9) && (s <= 11)) || ((s >= 26) && (s <= 28)) || ((s >= 31) && (s <= 33))
		    || (s == 38))
			return "??";
	}

	switch (s) {
		case 1:
			if (where == 4) // AFB
				return "\xb2\xb2\xb2";
			return "OO ";
		case 2:
			return "ww ";
		case 3:
			return ".. ";
		case 4:
			return "\"\" ";
		case 5:
			return "^^ ";
		case 6:
			return "-- ";
		case 7:
			return "UU ";
		case 8:
			return "@v ";
		case 9:
			return "\xae\xaf ";
		case 10:
			return "\xca\xca ";
		case 11:
			return "%% ";
		case 12:
			return "II ";
		case 13:
			return "## ";
		case 14:
			return "@^ ";
		case 15:
			return "\xf7\xf7 ";
		case 16:
			return "\xb0\xc4\xb0";
		case 17:
			return "\xb2\xb1\xb2";
		case 19:
			return "GG ";
		case 20:
			return "#_ ";
		case 21:
			return "\x15\x15 ";
		case 22:
			return "\xe1\xe1 ";
		case 23:
			return "\xd8\xd8 ";
		case 24:
			return " \xf9 ";
		case 25:
			if (where == 4) // AFB
				return "\xf3\xf2 ";
			return "\xc9\xbb ";
		case 26:
			return "ii ";
		case 27:
			return "\xe8\xe8 ";
		case 28:
			return "cc ";
		case 29:
			return "@@ ";
		case 30: {
			struct tm local = {0};
			(void)value_convert_localtime(time(NULL), &local);
			struct tm *tblock = &local;

			switch (tblock->tm_wday) {
                                // 0 : no storms on Sunday!
				case 1:
					return ")) ";
				case 2:
					return "## ";
				case 3:
					return "\xd5\xbe ";
				case 4:
					return "// ";
				case 5:
					return "/\\ ";
				case 6:
					return "00 ";
			}
		}
		break;

		case 31:
			return "YY ";
		case 32:
			return "\x9d\x9d ";
		case 33:
			return "\xeb\xeb ";
		case 34:
			return "\xe4\xaf ";
		case 35:
			return "[] ";
		case 37:
			return "** ";
		case 38:
			return "\x8f\x8f ";
		case 39:
			return "\xf0\xf0 ";
		case 99:
			return "[] ";
		default:
			return "?? ";
	}

	return "??";
}

static void
setScanCol(st_ooii_t *ooii, char s)
{
	switch ((char)s) {
		case '*':
			term_setattr(ooii, 4 + 128);
			break;
		case 'G':
			term_setattr(ooii, 1);
			break;
		case 'w':
			term_setattr(ooii, 2);
			break;
		case '"':
		case '\xb0':
			term_setattr(ooii, 3);
			break;
		case 'U':
		case '%':
			term_setattr(ooii, 4);
			break;

		case '^':
		case 'i':
		case '\xe1':
		case '\xf3':
			term_setattr(ooii, 6);
			break;

		case 'O':
		case '\xb2':
		case 'Q':
			term_setattr(ooii, 7);
			break;
		case '\xe4':
		case ' ':
			term_setattr(ooii, 8);
			break;
		case '\xf7':
			term_setattr(ooii, 9);
			break;
		case '-':
		case '\x15':
			term_setattr(ooii, 10);
			break;

		case '?':
		case '@':
		case '/':
		case '\x9d':
		case 'Y':
		case '\xeb':
			term_setattr(ooii, 12);
			break;

		case '#':
		case 'c':
		case '\xf0':
			term_setattr(ooii, 13);
			break;

		case '.':
		case '[':
		case '\xae':
		case '\xca':
		case '!':
		case '\xc9':
		case '\xd8':
			term_setattr(ooii, 14);
			break;

		case 'I':
		case '\xe8':
		case '\x8f':
			term_setattr(ooii, 15);
			break;
	}

	return;
}

static ptrdiff_t
incomingMapScanner(st_ooii_t *ooii, unsigned char *codeStr)
{
	int            zz, scanPtr, yy;
	unsigned char  scanVals[10];      // ScanVals  : ARRAY[1..9] OF BYTE;
	char           scan[30];
	char           menuBlock[255];
	unsigned char *origCodeStr = codeStr;
	int            where;
	int            miniTrik;

	advance_code(ooii, &codeStr);

	switch ((char)codeStr[0]) {
		case 'F':
			term_clearscreen(ooii);
			term_gotoxy(ooii, 1, 1);
			term_setattr(ooii, 1);
			write_text(ooii,
			    "\r\n\xda\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xbf\r\n",
			    -1);
			write_text(ooii, "\xb3 ", -1);
			term_setattr(ooii, 5);
			write_text(ooii, "\xd6\xc4", -1);
			term_setattr(ooii, 13);
			write_text(ooii, "Infrared", -1);
			term_setattr(ooii, 5);
			write_text(ooii, "\xc4\xb7", -1);
			term_setattr(ooii, 13);
			write_text(ooii,
			    "     Compass      Sector Monitor     System Monitor           ",
			    -1);
			term_setattr(ooii, 1);
			write_text(ooii, "\xb3\r\n", -1);
			write_text(ooii, "\xb3 ", -1);
			term_setattr(ooii, 5);
			write_text(ooii,
			    "\xc7          \xb6     ~~~~~~~      ~~~~~~~~~~~~~~     ~~~~~~~~~~~~~~           ",
			    -1);
			term_setattr(ooii, 1);
			write_text(ooii, "\xb3\r\n", -1);
			write_text(ooii, "\xb3 ", -1);
			term_setattr(ooii, 5);
			write_text(ooii, "\xc7          \xb6   [         ]    [ ]  ", -1);
			term_setattr(ooii, 13);
			write_text(ooii, "Items         Hit Points", -1);
			term_setattr(ooii, 5);
			write_text(ooii, " [:            ", -1);
			term_setattr(ooii, 1);
			write_text(ooii, "\xb3\r\n", -1);
			write_text(ooii, "\xb3 ", -1);
			term_setattr(ooii, 5);
			write_text(ooii, "\xc7          \xb6                  [ ]  ", -1);
			term_setattr(ooii, 13);
			write_text(ooii, "Campers       Radiation  ", -1);
			term_setattr(ooii, 5);
			write_text(ooii, "[:            ", -1);
			term_setattr(ooii, 1);
			write_text(ooii, "\xb3\r\n", -1);
			write_text(ooii, "\xb3 ", -1);
			term_setattr(ooii, 5);
			write_text(ooii, "\xd3\xc4\xc4\xc4", -1);
			term_setattr(ooii, 13);
			write_text(ooii, "scan", -1);
			term_setattr(ooii, 5);
			write_text(ooii, "\xc4\xc4\xc4\xbd    ", -1);
			term_setattr(ooii, 13);
			write_text(ooii, "Time :        ", -1);
			term_setattr(ooii, 5);
			write_text(ooii, "[ ]  ", -1);
			term_setattr(ooii, 13);
			write_text(ooii, "Bases         Condition  ", -1);
			term_setattr(ooii, 5);
			write_text(ooii, "[:            ", -1);
			term_setattr(ooii, 1);
			write_text(ooii, "\xb3\r\n", -1);
			write_text(ooii,
			    "\xc0\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xd9\r\n",
			    -1);
			break;

		case 'T':
			term_gotoxy(ooii, 1, 9);
			term_setattr(ooii, 1);
			write_text(ooii,
			    "\xda\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xbf\r\n",
			    -1);
			write_text(ooii,
			    "\xb3                                                                           \xb3\r\n",
			    -1);
			write_text(ooii,
			    "\xb3                                                                           \xb3\r\n",
			    -1);
			write_text(ooii,
			    "\xc0\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xd9\r\n",
			    -1);
			break;

		case 'M':
			advance_code(ooii, &codeStr);
			if (!*codeStr)
				break;

			if (codeStr[0] == 'A')
				where = 4;  // AFB
			else
				where = 1;  // wasteland

			advance_code(ooii, &codeStr);
			if (!*codeStr)
				break;

			if (codeStr[0] == 'T')
				miniTrik = 1;
			else
				miniTrik = 0;

			advance_code(ooii, &codeStr);

			for (scanPtr = 0; scanPtr < 9; scanPtr++) {
				getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
				scanVals[scanPtr] = (unsigned char)block_number(menuBlock);
			}

			for (zz = 0; zz < 9; zz++) {
				switch (zz) {
					case 0:
						term_gotoxy(ooii, 5, 4);
						break;
					case 1:
						term_gotoxy(ooii, 8, 4);
						break;
					case 2:
						term_gotoxy(ooii, 11, 4);
						break;
					case 3:
						term_gotoxy(ooii, 5, 5);
						break;
					case 4:
						term_gotoxy(ooii, 8, 5);
						break;
					case 5:
						term_gotoxy(ooii, 11, 5);
						break;
					case 6:
						term_gotoxy(ooii, 5, 6);
						break;
					case 7:
						term_gotoxy(ooii, 8, 6);
						break;
					case 8:
						term_gotoxy(ooii, 11, 6);
						break;
				}

				(void)strlcpy(scan, scanChar(scanVals[zz], where, miniTrik), sizeof(scan));

				if (scanVals[zz] == 30) {
                                        /* TODO: timer/tblock were not declared... */
					struct tm local = {0};
			(void)value_convert_localtime(time(NULL), &local);
			struct tm *tblock = &local;

					term_setattr(ooii, stormColors[tblock->tm_wday]);
				}
				else {
					setScanCol(ooii, scan[0]);
				}

				if (strlen(scan) == 2) {
					write_text(ooii, scan, -1);
					write_text(ooii, " ", -1);
				}
				else {
					write_text(ooii, scan, -1);
				}
			}

			term_gotoxy(ooii, 1, 14);
			break;

		case 'O':
			advance_code(ooii, &codeStr);

			for (zz = 1; zz < 9; zz++) {
				switch (zz) {
					case 1:
						term_gotoxy(ooii, 20, 5);
						term_setattr(ooii, 14);
						getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
						strlcat(menuBlock, ",", sizeof(menuBlock));
						write_text(ooii, menuBlock, -1);
						break;

					case 2:
						getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
						strlcat(menuBlock, ",", sizeof(menuBlock));
						write_text(ooii, menuBlock, -1);
						break;

					case 3:
						getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
						strlcat(menuBlock, " ", sizeof(menuBlock));
						write_text(ooii, menuBlock, -1);
						break;

					case 4:
						term_gotoxy(ooii, 26, 7);
						term_setattr(ooii, 15);
						getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
						strlcat(menuBlock, " ", sizeof(menuBlock));
						write_text(ooii, menuBlock, -1);
						break;

					case 5:
						term_gotoxy(ooii, 66, 5);
						term_setattr(ooii, 14);
						getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
						strlcat(menuBlock, " ", sizeof(menuBlock));
						write_text(ooii, menuBlock, -1);
						break;

					case 6:
						getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
						write_text(ooii, "(", 1);
						write_text(ooii, menuBlock, -1);
						write_text(ooii, ")", 1);
						break;

					case 7:
						term_gotoxy(ooii, 66, 6);
						getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
						yy = block_number(menuBlock);
						if (yy >= 40)
							term_setattr(ooii, 12);
						else
							term_setattr(ooii, 14);
						strlcat(menuBlock, "% ", sizeof(menuBlock));
						write_text(ooii, menuBlock, -1);
						break;

					case 8:
						term_gotoxy(ooii, 66, 7);
						term_setattr(ooii, 14);
						getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
						yy = block_number(menuBlock);

						switch (yy) {
							case 0:
								write_text(ooii, "Not Hungry", -1);
								break;
							case 1:
								write_text(ooii, "Bit Hungry", -1);
								break;
							case 2:
								write_text(ooii, "Hungry    ", -1);
								break;
							case 3:
								term_setattr(ooii, 12);
								write_text(ooii, "HUNGRY!   ", -1);
								break;
							case 4:
								term_setattr(ooii, 12 + 128);
								write_text(ooii, "STARVING!!", -1);
								break;
						}
						break;
				}
			}

			if (!*codeStr)
				break;
			advance_code(ooii, &codeStr);
			if (codeStr[0] == 'I') {
				if (!*codeStr)
					break;
				advance_code(ooii, &codeStr);
				term_gotoxy(ooii, 34, 5);

				if (codeStr[0] == '0') {
					write_text(ooii, " ", -1);
				}
				else {
					term_setattr(ooii, 12);
					write_text(ooii, "\xfb", -1);
				}

				if (!*codeStr)
					break;
				advance_code(ooii, &codeStr);
				if (!*codeStr)
					break;
				advance_code(ooii, &codeStr);
			}

			if (codeStr[0] == 'C') {
				if (!*codeStr)
					break;
				advance_code(ooii, &codeStr);
				term_gotoxy(ooii, 34, 6);

				if (codeStr[0] == '0') {
					write_text(ooii, " ", -1);
				}
				else {
					term_setattr(ooii, 12);
					write_text(ooii, "\xfb", -1);
				}

				if (!*codeStr)
					break;
				advance_code(ooii, &codeStr);
				if (!*codeStr)
					break;
				advance_code(ooii, &codeStr);
			}

			if (codeStr[0] == 'B') {
				if (!*codeStr)
					break;
				advance_code(ooii, &codeStr);
				term_gotoxy(ooii, 34, 7);

				if (codeStr[0] == '0') {
					write_text(ooii, " ", -1);
				}
				else {
					term_setattr(ooii, 12);
					write_text(ooii, "\xfb", -1);

					if (!*codeStr)
						break;
					advance_code(ooii, &codeStr);
					if (!*codeStr)
						break;
					advance_code(ooii, &codeStr);

					getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
					yy = block_number(menuBlock);
					term_gotoxy(ooii, 1, yy + 1);

					getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
					term_setattr(ooii, 14);
					strlcat(menuBlock, " is stationed nearby.", sizeof(menuBlock));
					write_text(ooii, menuBlock, -1);
				}
			}
			break;

		case 'D':
			advance_code(ooii, &codeStr);
			getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
			zz = block_number(menuBlock);
			term_gotoxy(ooii, 3, 10);
			write_text(ooii,
			    "                                                                          ",
			    -1);
			term_gotoxy(ooii, 3, 11);
			write_text(ooii,
			    "                                                                          ",
			    -1);
			term_gotoxy(ooii, 3, 10);

			term_setattr(ooii, 11);

			switch (zz) {
				case 2:
					write_text(ooii,
					    "Crumbling walls of ancient cities scatter the plains, and smoke lazily",
					    -1);
					term_gotoxy(ooii, 3, 11);
					write_text(ooii,
					    "rises from the destructed relics of the past.",
					    -1);
					break;

				case 3:
					write_text(ooii,
					    "Coldness creeps upon you like death in this vast range of empty desert.",
					    -1);
					term_gotoxy(ooii, 3, 11);
					write_text(ooii,
					    "Harsh winds rip sand against your body like tiny shards of glass.",
					    -1);
					break;

				case 4:
					write_text(ooii,
					    "The black muck from the swamp slithers between your toes as you make your",
					    -1);
					term_gotoxy(ooii, 3, 11);
					write_text(ooii, "way through the bubbling slime.", -1);
					break;

				case 5:
					write_text(ooii,
					    "High upon the scorched mountains, small blackening caves can be seen.",
					    -1);
					term_gotoxy(ooii, 3, 11);
					write_text(ooii,
					    "Possibly lurking within, horrible creatures wait for your trespassing.",
					    -1);
					break;

				case 6:
					write_text(ooii,
					    "A mist glides along the flat terrain and summons you forward.",
					    -1);
					break;

				case 7:
					;
					break; // pits

				case 8:
					write_text(ooii,
					    "A small hole in the ground leads down into a realm of darkness.",
					    -1);
					break;

				case 9:
					write_text(ooii,
					    "From the pre-war years, an Airforce Base lies half buried from nuclear",
					    -1);
					term_gotoxy(ooii, 3, 11);
					write_text(ooii, "ash and metallic scraps.", -1);
					break;

				case 10:
					write_text(ooii,
					    "A disabled missile silo remains intact from heavy bombing in this area.",
					    -1);
					break;

				case 11:
					write_text(ooii,
					    "In an effortless HeXonium attempt to clean the wastelands, radiation dumps",
					    -1);
					term_gotoxy(ooii, 3, 11);
					write_text(ooii,
					    "lay wretched with oozing radioactive substances forming.",
					    -1);
					break;

				case 12:
					write_text(ooii,
					    "This unmarked road is littered with dead shrubs and rotting vegatation.",
					    -1);
					break;

				case 13:
					write_text(ooii,
					    "Darkness swirls around the infamous and towering Hydrite Prison.",
					    -1);
					break;

				case 14:
					write_text(ooii,
					    "As you look up, you notice a small hole that leads upwards.",
					    -1);
					break;

				case 15:
					write_text(ooii,
					    "You cautiously wade through the black murky water of this shallow stream.",
					    -1);
					break;
			}
			break;

		case 'P':
			advance_code(ooii, &codeStr);
			term_gotoxy(ooii, 3, 10);
			write_text(ooii,
			    "                                                                          ",
			    -1);
			term_gotoxy(ooii, 3, 11);
			write_text(ooii,
			    "                                                                          ",
			    -1);
			term_gotoxy(ooii, 3, 10);
			term_setattr(ooii, 11);
			write_text(ooii, "20 foot pit!", -1);
			break;
	}

	return codeStr - origCodeStr;
}

static ptrdiff_t
incomingSoundVoc(st_ooii_t *ooii, unsigned char *codeStr)
{
	unsigned char *origCodeStr = codeStr;

	advance_code(ooii, &codeStr);
	switch (*codeStr) {
		case '1':
			play_sound(ooii, ooii_snd_welcome, sizeof(ooii_snd_welcome));
			break;
		case '3':
			play_sound(ooii, ooii_snd_countdn, sizeof(ooii_snd_countdn));

                /* Fallthrough */
		case '2':
			play_sound(ooii, ooii_snd_explode, sizeof(ooii_snd_explode));
			break;
		case '4':
			play_sound(ooii, ooii_snd_clone, sizeof(ooii_snd_clone));
			break;
		case '5':
			if (random_choice(ooii, 2))
				play_sound(ooii, ooii_snd_danger1, sizeof(ooii_snd_danger1));
			else
				play_sound(ooii, ooii_snd_danger2, sizeof(ooii_snd_danger2));
			break;
		case '6':
			play_sound(ooii, ooii_snd_steam, sizeof(ooii_snd_steam));
			break;
		case '7':
			play_sound(ooii, ooii_snd_scream1, sizeof(ooii_snd_scream1));
			break;
		case '8':
			play_sound(ooii, ooii_snd_watch, sizeof(ooii_snd_watch));
			break;
		case '9':
			play_sound(ooii, ooii_snd_levelup, sizeof(ooii_snd_levelup));
			break;
		case 'A':
			play_sound(ooii, ooii_snd_inflame, sizeof(ooii_snd_inflame));
			break;
		case 'B':
			switch (random_choice(ooii, 3)) {
				case 0:
					play_sound(ooii, ooii_snd_hit1, sizeof(ooii_snd_hit1));
					break;
				case 1:
					play_sound(ooii, ooii_snd_hit2, sizeof(ooii_snd_hit2));
					break;
				case 2:
					play_sound(ooii, ooii_snd_hit3, sizeof(ooii_snd_hit3));
					break;
			}
			break;
		case 'C':
			play_sound(ooii, ooii_snd_timeisup, sizeof(ooii_snd_timeisup));
			break;
		case 'D':
			play_sound(ooii, ooii_snd_healing, sizeof(ooii_snd_healing));
			break;
		case 'E':
			play_sound(ooii, ooii_snd_lrange2, sizeof(ooii_snd_lrange2));
			break;
		case 'F':
			play_sound(ooii, ooii_snd_cackle, sizeof(ooii_snd_cackle));
			break;
		case 'G':
			play_sound(ooii, ooii_snd_teleport, sizeof(ooii_snd_teleport));
			break;
		case 'H':
			play_sound(ooii, ooii_snd_genetics, sizeof(ooii_snd_genetics));
			break;
		case 'I':
			play_sound(ooii, ooii_snd_remote, sizeof(ooii_snd_remote));
			break;
		case 'J':
			play_sound(ooii, ooii_snd_afbdoor, sizeof(ooii_snd_afbdoor));
			break;
		case 'K':
			play_sound(ooii, ooii_snd_alarm, sizeof(ooii_snd_alarm));
			break;
		case 'L':
			play_sound(ooii, ooii_snd_reverse, sizeof(ooii_snd_reverse));
			break;
		case 'M':
			play_sound(ooii, ooii_snd_aerial, sizeof(ooii_snd_aerial));
			break;
		case 'N':
			play_sound(ooii, ooii_snd_phaser, sizeof(ooii_snd_phaser));
			break;
		case 'O':
			switch (random_choice(ooii, 3)) {
				case 0:
					play_sound(ooii, ooii_snd_miss1, sizeof(ooii_snd_miss1));
					break;
				case 1:
					play_sound(ooii, ooii_snd_miss2, sizeof(ooii_snd_miss2));
					break;
				case 2:
					play_sound(ooii, ooii_snd_miss3, sizeof(ooii_snd_miss3));
					break;
			}
			break;
		case 'P':
			if (random_choice(ooii, 2))
				play_sound(ooii, ooii_snd_music1, sizeof(ooii_snd_music1));
			else
				play_sound(ooii, ooii_snd_music2, sizeof(ooii_snd_music2));
			break;
		case 'Q':
			play_sound(ooii, ooii_snd_device, sizeof(ooii_snd_device));
			break;
		case 'R':
			play_sound(ooii, ooii_snd_death, sizeof(ooii_snd_death));
			break;
		case 'S':
			play_sound(ooii, ooii_snd_good, sizeof(ooii_snd_good));
			break;
		case 'T':
			play_sound(ooii, ooii_snd_yahoo, sizeof(ooii_snd_yahoo));
			break;
		case 'U':
			play_sound(ooii, ooii_snd_scream2, sizeof(ooii_snd_scream2));
			break;
		case 'V':
			play_sound(ooii, ooii_snd_wap, sizeof(ooii_snd_wap));
			break;
		case 'W':
			play_sound(ooii, ooii_snd_zip, sizeof(ooii_snd_zip));
			break;
		case 'X':
			play_sound(ooii, ooii_snd_lrange3, sizeof(ooii_snd_lrange3));
			break;
		case 'Y':
			play_sound(ooii, ooii_snd_snip, sizeof(ooii_snd_snip));
			break;
		case 'Z':
			play_sound(ooii, ooii_snd_pow, sizeof(ooii_snd_pow));
			break;
		default:

			break;
	}
	return codeStr - origCodeStr;
}

static bool
handle_ooii_code(st_ooii_t *ooii, unsigned char *codeStr, int *ooii_mode, unsigned char *retbuf, size_t retsize)
{
	bool quit = false;
	char menuBlock[255];
	int  zz;

	if (retbuf != NULL && retsize != 0)
		retbuf[0] = 0;

	advance_code(ooii, &codeStr); /* Skip intro char */

	for (; codeStr < ooii->end && *codeStr && *codeStr != '|'; advance_code(ooii, &codeStr)) {
		if ((codeStr[0] >= 'A') && (codeStr[0] <= 'Z')) {
                        /* This one never takes an extra char */
			readInPix(ooii, codeStr[0], *ooii_mode);
		}
		else if ((codeStr[0] >= '1') && (codeStr[0] <= '9')) {
			codeStr += readInText(ooii, codeStr);
		}
		else if ((codeStr[0] >= 'a') && (codeStr[0] <= 'z')) {
			codeStr += readSmallMenu(ooii, codeStr);
		}
		else {
			switch (codeStr[0]) {
				case '0':
                                        /* This one never takes an extra char */
					readInPix(ooii, codeStr[0], *ooii_mode);
					break;
				case '!':
					codeStr += incomingCheckStatus(ooii, codeStr);
					break;
				case '#':
					codeStr += incomingMapScanner(ooii, codeStr);
					break;
				case '$':
					codeStr += incomingSoundVoc(ooii, codeStr);
					break;
				case '\\':
					quit = true;

                                        // quitTerm=1;
					break;
				case '?':
					if (retbuf != NULL) {
						advance_code(ooii, &codeStr);
						getBlock(ooii, &codeStr, menuBlock, sizeof(menuBlock));
						zz = block_number(menuBlock);

                                                /* Highest we support is two */
						if (zz >= ST_OOII_MAX_MODE)
							zz = ST_OOII_MAX_MODE - 1;

                                                /* Old (1.22) versions don't include a number */
						if (zz < 1)
							zz = 1;
						*ooii_mode = zz + 1;
						if (strlen((char *)retbuf) + 3 < retsize)
							snprintf((char *)retbuf, retsize, "\xaf%d|", zz);
					}
					break;
			}
		}
	}
	return quit;
}

static const uint8_t init1[] =
    "\xdb\b \xdb\b \xdb\b[\xdb\b[\xdb\b \xdb\bM\xdb\ba\xdb\bi\xdb\bn\xdb\bt\xdb\be\xdb\bn\xdb\ba\xdb\bn\xdb\bc\xdb\be\xdb\b \xdb\bC\xdb\bo\xdb\bm\xdb\bp\xdb\bl\xdb\be\xdb\bt\xdb\be\xdb\b \xdb\b]\xdb\b]\xdb\b \b\r\n\r\n\r\n\x1b[0;0;36mDo you have the Overkill Ansiterm installed? (y/N)  \xe9 ";
static const uint8_t init2[] =
    "\xdb\b \xdb\b \xdb\b[\xdb\b[\xdb\b \xdb\bM\xdb\ba\xdb\bi\xdb\bn\xdb\bt\xdb\be\xdb\bn\xdb\ba\xdb\bn\xdb\bc\xdb\be\xdb\b \xdb\bC\xdb\bo\xdb\bm\xdb\bp\xdb\bl\xdb\be\xdb\bt\xdb\be\xdb\b \xdb\b]\xdb\b]\xdb\b \b\r\n\r\n\x1b[0m\x1b[2J\r\n\r\n\x1b[0;1;30mHX Force retinal scan in progress ... \x1b[0;0;30m";

int
st_ooii_create(st_ooii_t **result, const struct st_ooii_config *config)
{
	st_ooii_t *ooii;

	if (result == NULL || config == NULL || config->pass == NULL
	    || config->render == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	ooii = calloc(1, sizeof(*ooii));
	if (ooii == NULL) return IO_ERROR_OUT_OF_MEMORY;
	ooii->config = *config;
	ooii->attribute = 7;
	ooii->random = (uint32_t)time(NULL);
	if (ooii->random == 0) ooii->random = 1;
	*result = ooii;
	return 0;
}

void
st_ooii_destroy(st_ooii_t *ooii)
{
	free(ooii);
}

int
st_ooii_mode(const st_ooii_t *ooii)
{
	return ooii == NULL ? 0 : ooii->mode;
}

void
st_ooii_set_mode(st_ooii_t *ooii, int mode)
{
	if (ooii == NULL) return;
	++ooii->mode_generation;
	ooii->mode = mode < 0 ? 0 : mode > ST_OOII_MAX_MODE ? ST_OOII_MAX_MODE : mode;
	ooii->command_size = 0;
	ooii->discard = false;
	ooii->match[0] = ooii->match[1] = 0;
}

int
st_ooii_write(st_ooii_t *ooii, const void *data, size_t size)
{
	const uint8_t *bytes = data;
	const uint8_t *patterns[] = {init1, init2};
	const size_t lengths[] = {sizeof(init1) - 1, sizeof(init2) - 1};

	if (ooii == NULL || (data == NULL && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	ooii->error = 0;
	for (size_t i = 0; i < size;) {
		uint8_t byte = bytes[i];
		if (ooii->mode != 0 && (ooii->command_size != 0 || byte == 0xab)) {
			if (byte == 0 || ooii->command_size >= sizeof(ooii->command) - 1)
				ooii->discard = true;
			if (!ooii->discard) ooii->command[ooii->command_size++] = byte;
			if (byte == '|') {
				if (!ooii->discard) {
					uint8_t response[16];
					int mode = ooii->mode;
					size_t generation = ooii->mode_generation;
					ooii->command[ooii->command_size] = 0;
					ooii->end = ooii->command + ooii->command_size - 1;
					bool quit = handle_ooii_code(ooii, ooii->command, &mode,
					    response, sizeof(response));
					/* Text hooks may select another mode while a screen expands.
					 * Finish with a stable artwork index, then honor their choice. */
					if (generation == ooii->mode_generation)
						ooii->mode = quit ? 0 : mode;
					if (ooii->error == 0 && response[0] != 0
					    && ooii->config.reply != NULL)
						ooii->error = ooii->config.reply(ooii->config.context,
						    response, strlen((const char *)response));
				}
				ooii->command_size = 0;
				ooii->discard = false;
			}
			++i;
		} else {
			/* Preserve ordinary receive chunks, stopping at protocol boundaries.
			 * Deliver the banner before enabling the commands that follow it. */
			size_t start = i, generation = ooii->mode_generation;
			int detected = 0;
			do {
				byte = bytes[i++];
				if (ooii->mode == 0) {
					for (size_t p = 0; p < 2; ++p) {
						if (byte == patterns[p][ooii->match[p]]) ++ooii->match[p];
						else ooii->match[p] = byte == patterns[p][0] ? 1 : 0;
						if (ooii->match[p] == lengths[p]) {
							detected = (int)p + 1;
							break;
						}
					}
				}
			} while (i < size && detected == 0 && bytes[i] != 0xab);
			ooii->error = ooii->config.pass(ooii->config.context, bytes + start, i - start);
			if (detected != 0 && generation == ooii->mode_generation)
				st_ooii_set_mode(ooii, detected);
		}
		if (ooii->error != 0) return ooii->error;
	}
	return 0;
}
