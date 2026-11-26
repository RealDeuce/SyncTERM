/* Independent RIP TERMINFO replies and nonfatal reference recovery. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "io_error.h"
#include "st_rip3_host_internal.h"
#include "st_rip3_host_variables.h"

#define CHECK(expression)                                                                        \
	do {                                                                                     \
		if (!(expression)) {                                                             \
			fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
			return 1;                                                                \
		}                                                                                \
	} while (0)

struct observations {
	const char                  *expected;
	size_t                       replies, diagnostics;
	bool                         mismatch;
	enum st_rip3_host_diagnostic last;
};

static void
reply(void *context, const uint8_t *text, size_t length)
{
	struct observations *observed = context;
	observed->replies++;
	observed->mismatch = observed->expected == NULL || strlen(observed->expected) != length
	                     || memcmp(observed->expected, text, length) != 0;
}

static void
diagnostic(void *context, enum st_rip3_host_diagnostic kind)
{
	struct observations *observed = context;
	observed->diagnostics++;
	observed->last = kind;
}

static int
run(const struct st_rip3_host_variables *host, const char *command, const char *expected, int error)
{
	struct observations            observed = {.expected = expected};
	const struct st_rip3_host_sink sink     = {&observed, reply, diagnostic};

	st_rip3_host_variables_process(host, &sink, (const uint8_t *)command, strlen(command));
	CHECK(!observed.mismatch);
	if (error >= 0)
		CHECK(observed.replies == 0 && observed.diagnostics == 1 && (int)observed.last == error);
	else
		CHECK(observed.diagnostics == 0 && observed.replies == (expected[0] != 0 ? 1u : 0u));
	return 0;
}

int
main(void)
{
	struct st_rip3_terminal_identity identity = {"SyncTERM", "Test vendor", "1.2.3"};
	struct st_rip3_host_variables    host = {.identity = &identity}, absent = {0};

	static const struct {
		const char *command, *expected;
	} cases[] = {
	    {"$TERMINFO$",                       "SyncTERM"                },
	    {"$TERMINFO()$",                     "SyncTERM"                },
	    {"$TERMINFO( )$",                    "NONE"                    },
	    {"$TERMINFO(NAME)$",                 "SyncTERM"                },
	    {"$TERMINFO(,)$",                    "SyncTERM"                },
	    {"$tErMiNfO(vEnDoR)$",               "Test vendor"             },
	    {"$TERMINFO( VERSION )$",            "1.2.3"                   },
	    {"$TERMINFO(LIST)$",                 "LIST,NAME,VENDOR,VERSION"},
	    {"$TERMINFO(GOOSE)$",                "NONE"                    },
	    {"$TERMINFO(ABCDEFGHIJKL)$",         "NONE"                    },
	    {"$TERMINFO(ABCDEFGHIJKLM)$",        "NONE"                    },
	    {"$TERMINFO(ABCDEFGHIJKLMNOPQRST)$", "NONE"                    },
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
		CHECK(run(&host, cases[i].command, cases[i].expected, -1) == 0);
	static const char *const invalid[] = {"$TERMINFO(NAME,VENDOR)$", "$TERMINFO(ABCDEFGHIJKLMNOPQRSTU)$",
	    "$TERMINFO(NAME$", "$TERMINFO((NAME))$"};
	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		CHECK(run(&host, invalid[i], NULL, ST_RIP3_HOST_INVALID_REFERENCE) == 0);
		CHECK(run(&host, "$TERMINFO(NAME)$", "SyncTERM", -1) == 0);
	}
	CHECK(run(&absent, "$TERMINFO$", NULL, ST_RIP3_HOST_UNSUPPORTED_REFERENCE) == 0);
	CHECK(run(&absent, "$TERMINFO(LIST)$", NULL, ST_RIP3_HOST_UNSUPPORTED_REFERENCE) == 0);
	CHECK(run(&absent, "$TERMINFO(NAME,VERSION)$", NULL, ST_RIP3_HOST_INVALID_REFERENCE) == 0);
	struct st_rip3_terminal_identity incomplete[] = {
	    {NULL,   "Vendor", "1" },
            {"Name", NULL,     "1" },
            {"Name", "Vendor", NULL}
        };
	for (size_t i = 0; i < sizeof(incomplete) / sizeof(incomplete[0]); i++) {
		host.identity = &incomplete[i];
		CHECK(run(&host, "$TERMINFO(LIST)$", NULL, ST_RIP3_HOST_UNSUPPORTED_REFERENCE) == 0);
	}
	host.identity   = &identity;
	identity.vendor = NULL;
	CHECK(run(&host, "$TERMINFO(NAME)$", NULL, ST_RIP3_HOST_UNSUPPORTED_REFERENCE) == 0);
	identity.vendor = "";
	CHECK(run(&host, "$TERMINFO(VENDOR)$", "", -1) == 0);
	identity.vendor = "Literal $NAME$ <<MACRO>> \303\251 \202";
	CHECK(run(&host, "$TERMINFO(VENDOR)$", identity.vendor, -1) == 0);
	CHECK(run(&host, "$TERMINFO(\202)$", "NONE", -1) == 0);
	CHECK(run(&host, "$TERMINFO(\303\251)$", "NONE", -1) == 0);
	/* Borrowed text is emitted whole, beyond the numeric/table scratch size. */
	char long_name[5001];
	memset(long_name, 'N', sizeof(long_name) - 1);
	long_name[5000] = 0;
	identity.name   = long_name;
	CHECK(run(&host, "$TERMINFO$", long_name, -1) == 0);
	struct st_rip3_builtin_reference reference;
	char                             output[8] = "KEEP";
	CHECK(st_rip3_builtin_reference_parse(&reference, (const uint8_t *)"$TERMINFO$", 10) == 0);
	CHECK(st_rip3_host_variables_execute(&host, &reference, output, sizeof(output)) == IO_ERROR_OUT_OF_RANGE);
	CHECK(strcmp(output, "KEEP") == 0);
	identity.name = "SyncTERM";
	char exact[9] = "KEEP";
	CHECK(st_rip3_host_variables_execute(&host, &reference, exact, 8) == IO_ERROR_OUT_OF_RANGE);
	CHECK(strcmp(exact, "KEEP") == 0);
	CHECK(st_rip3_host_variables_execute(&host, &reference, exact, 9) == 0 && strcmp(exact, "SyncTERM") == 0);
	CHECK(st_rip3_host_variables_execute(&host, &reference, exact, 0) == IO_ERROR_OUT_OF_RANGE);
	CHECK(strcmp(exact, "SyncTERM") == 0);
	st_rip3_host_variables_process(&host, NULL, (const uint8_t *)"$TERMINFO$", 10);
	st_rip3_host_variables_process(&host, NULL, (const uint8_t *)"$TERMINFO(,)$", 13);
	CHECK(run(&host, "$TERMINFO$", "SyncTERM", -1) == 0);
	return 0;
}
