/* Integration test for the embedded Wren menu graph. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_wren_host.h"
#include "wren_host_internal.h"

#include "filesystem.h"
#include "io_error.h"
#include "st_app_paths.h"
#include "st_console.h"
#include "st_profile.h"
#include "st_settings.h"
#include "st_theme.h"
#include "st_threads.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef ST_WREN_TEST_DIRECTORY
#error ST_WREN_TEST_DIRECTORY is required
#endif

#ifdef ST_HAVE_UNIFONT
#define ST_WREN_TEST_UNIFONT_ENABLED "true"
#else
#define ST_WREN_TEST_UNIFONT_ENABLED "false"
#endif

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, \
		    __LINE__, #expression); \
		return 1; \
	} \
} while (0)

static int
write_file(const char *path, const char *contents)
{
	FILE *stream = fopen(path, "wb");
	size_t length = strlen(contents);

	if (stream == NULL)
		return -1;
	if (fwrite(contents, 1, length, stream) != length) {
		fclose(stream);
		return -1;
	}
	return fclose(stream);
}

static int
write_zero_file(const char *path, size_t size)
{
	static const unsigned char zeros[256] = {0};
	FILE *stream = fopen(path, "wb");

	if (stream == NULL)
		return -1;
	while (size != 0) {
		size_t chunk = size < sizeof(zeros) ? size : sizeof(zeros);

		if (fwrite(zeros, 1, chunk, stream) != chunk) {
			(void)fclose(stream);
			return -1;
		}
		size -= chunk;
	}
	return fclose(stream);
}

static int
discard_ansi_output(void *context, const void *data, size_t size)
{
	(void)context;
	(void)data;
	(void)size;
	return 0;
}

static bool
console_has_text(st_console_t *console, uint32_t x, uint32_t y,
	const char *text)
{
	struct st_console_cell cells[64];
	size_t length = strlen(text);
	struct st_console_rect area = {x, y, (uint32_t)length, 1};

	if (length > sizeof(cells) / sizeof(cells[0])
	    || st_console_read_cells(console, &area, cells, length) != 0)
		return false;
	for (size_t i = 0; i < length; ++i)
		if (cells[i].codepoint != (unsigned char)text[i])
			return false;
	return true;
}

static int
exercise_web_refresh_overlay(st_wren_host_t *public_host,
	st_console_t *console)
{
	struct st_wren_host *host = public_host;
	struct st_console_rect area = {10, 10, 1, 1};
	struct st_console_cell before;
	struct st_console_cell after;
	uint32_t palette_before[2];
	uint32_t palette_after[2];
	bool cursor_before;
	bool cursor_after;
	enum st_console_cursor_shape shape_before;
	enum st_console_cursor_shape shape_after;
	int status;

	CHECK(host->menu_ui_class != NULL && host->menu_status != NULL
	    && host->menu_status_clear != NULL);
	CHECK(st_console_read_cells(console, &area, &before, 1) == 0);
	CHECK(st_console_get_palette(console, 0, palette_before, 2) == 0);
	CHECK(st_console_get_cursor(console, &cursor_before, &shape_before) == 0);
	CHECK(st_settings_web_download_append("Startup Test", "invalid") == 0);
	status = st_wren_host_refresh_web_lists(public_host);
	CHECK(st_settings_web_download_remove(0) == 0);
	CHECK(status == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_console_read_cells(console, &area, &after, 1) == 0);
	CHECK(st_console_get_palette(console, 0, palette_after, 2) == 0);
	CHECK(st_console_get_cursor(console, &cursor_after, &shape_after) == 0);
	CHECK(before.codepoint == after.codepoint
	    && before.foreground == after.foreground
	    && before.background == after.background
	    && before.flags == after.flags && before.font == after.font
	    && before.hyperlink == after.hyperlink);
	CHECK(memcmp(palette_before, palette_after, sizeof(palette_before)) == 0);
	CHECK(cursor_before == cursor_after && shape_before == shape_after);
	CHECK(st_wren_host_last_error(public_host) == NULL);
	return 0;
}

static int
exercise_theme_contract(st_wren_host_t *public_host, bool recolored)
{
	struct st_wren_host *host = public_host;
	static const char initial[] =
	    "import \"syncterm\" for Host\n"
	    "var styles = {}\n"
	    "for (row in Host.themeData[0]) styles[row[0]] = row[2]\n"
	    "if (Host.defaultThemeData[0].count != 48 || Host.themeData[0].count != 55) Fiber.abort(\"theme roles\")\n"
	    "if (styles[\"default\"] != 0x1f || styles[\"frame\"] != 0x1e || styles[\"list.item.focused\"] != 0x71 || styles[\"classic.backdrop\"] != 0x13 || styles[\"classic.header\"] != 0x31 || styles[\"classic.console\"] != 0x3e) Fiber.abort(\"classic theme\")\n";
	static const char changed[] =
	    "import \"syncterm\" for Host\n"
	    "var styles = {}\n"
	    "for (row in Host.themeData[0]) styles[row[0]] = row[2]\n"
	    "if (styles[\"default\"] != 0x07 || styles[\"frame\"] != 0x01 || styles[\"classic.header\"] != 0x70 || styles[\"classic.console\"] != 0x7e) Fiber.abort(\"classic settings\")\n";

	if (wrenInterpret(host->vm, recolored ? "theme_changed" :
	    "theme_initial", recolored ? changed : initial)
	    != WREN_RESULT_SUCCESS) {
		fprintf(stderr, "theme contract failed: %s\n", host->last_error);
		return -1;
	}
	return 0;
}

static int
exercise_menu_contract(st_wren_host_t *public_host)
{
	struct st_wren_host *host = public_host;
	static const char source[] =
	    "import \"syncterm_menu\" for Menu, MenuReadStatus\n"
	    "if (Menu.load(null) != MenuReadStatus.ok) Fiber.abort(\"load\")\n"
	    "if (Menu.applicationTitle != \"SyncTERM test - Headless\") Fiber.abort(\"application title\")\n"
	    "if (Menu.currentScreenMode != 1) Fiber.abort(\"current mode\")\n"
	    "if (!Menu.setScreenMode(3) || Menu.currentScreenMode != 3 || !Menu.setScreenMode(1)) Fiber.abort(\"set mode\")\n"
	    "if (Menu.entries.count != 2 || Menu.entries[0].name != \"Zulu\") Fiber.abort(\"entries/sort\")\n"
	    "var q = Menu.quickConnect(\"ssh://guest:secret@[::1]:2222\")\n"
	    "if (q == null || q.connType != 5 || q.addr != \"::1\" || q.port != 2222 || q.user != \"guest\" || q.password != \"secret\") Fiber.abort(\"quick\")\n"
	    "if (Menu.fileLocations[\"configuration\"].count == 0) Fiber.abort(\"paths\")\n"
	    "if (Menu.paletteDefaults(1)[1].count != 16) Fiber.abort(\"palette\")\n"
	    "if (Menu.fontsCatalog.count == 0) Fiber.abort(\"fonts\")\n"
	    "if (Menu.wrenScriptsMaxLength < 1 || Menu.maxPathLength < 1) Fiber.abort(\"limits\")\n"
	    "if (Menu.addWebList(\"Test\", \"https://example.invalid/list\", 0) != null || !Menu.webListsDirty || Menu.webLists.count != 1) Fiber.abort(\"web add\")\n"
	    "if (!Menu.updateWebList(0, \"https://example.invalid/next\") || Menu.webLists[0][1] != \"https://example.invalid/next\") Fiber.abort(\"web update\")\n"
	    "if (!Menu.deleteWebList(0) || Menu.webLists.count != 0) Fiber.abort(\"web delete\")\n"
	    "var s = Menu.settings\n"
	    "s.confirmClose = true\n"
	    "s.promptSave = true\n"
	    "s.invertWheel = true\n"
	    "s.modemDevice = \"modem\"\n"
	    "s.modemInit = \"ATZ\"\n"
	    "s.modemDial = \"ATDT\"\n"
	    "s.listPath = \"list.lst\"\n"
	    "s.shellTerm = \"ansi\"\n"
	    "s.tlsClientCert = \"client.pem\"\n"
	    "s.tlsClientKey = \"client.key\"\n"
	    "s.startupMode = 1\n"
	    "s.outputMode = 0\n"
	    "s.cursorStyle = 1\n"
	    "s.audioModes = 0\n"
	    "s.scrollbackLines = 100\n"
	    "s.modemRate = 9600\n"
	    "s.scalingMode = 1\n"
	    "s.kdfShift = 15\n"
	    "s.customRows = 30\n"
	    "s.customColumns = 80\n"
	    "s.customFontHeight = 16\n"
	    "s.customAspectWidth = 1\n"
	    "s.customAspectHeight = 1\n"
	    "s.frameColor = 1\n"
	    "s.textColor = 7\n"
	    "s.backgroundColor = 0\n"
	    "s.inverseColor = 7\n"
	    "s.lightbarColor = 15\n"
	    "s.lightbarBackgroundColor = 1\n"
	    "if (!s.dirty || !s.apply()) Fiber.abort(\"settings\")\n"
	    "var b = Menu.entries[0]\n"
	    "b.addr = \"alpha.example\"\n"
	    "b.port = 2323\n"
	    "b.connType = 4\n"
	    "b.user = \"guest\"\n"
	    "b.password = \"password\"\n"
	    "b.syspass = \"system\"\n"
	    "b.music = 0\n"
	    "b.rip = 0\n"
	    "b.comment = \"comment\"\n"
	    "b.addressFamily = 0\n"
	    "b.termName = \"ansi\"\n"
	    "b.screenMode = 1\n"
	    "b.bpsRate = 0\n"
	    "b.font = \"Codepage 437 English\"\n"
	    "b.noStatus = false\n"
	    "b.hidePopups = false\n"
	    "b.yellowIsYellow = true\n"
	    "b.forceLcf = false\n"
	    "b.dlDir = \"download\"\n"
	    "b.ulDir = \"upload\"\n"
	    "b.logFile = \"session.log\"\n"
	    "b.appendLogFile = true\n"
	    "b.xferLogLevel = 6\n"
	    "b.protocolLogLevel = 6\n"
	    "b.stopBits = 1\n"
	    "b.dataBits = 8\n"
	    "b.parity = 0\n"
	    "b.flowControl = 0\n"
	    "b.telnetNoBinary = false\n"
	    "b.deferTelnetNegotiation = false\n"
	    "b.tlsTrustWebPki = true\n"
	    "b.tlsTrustedCert = \"server.pem\"\n"
	    "b.tlsClientCert = \"client.pem\"\n"
	    "b.tlsClientKey = \"client.key\"\n"
	    "b.tlsPskIdentity = \"identity\"\n"
	    "b.tlsPsk = \"secret\"\n"
	    "b.tlsPskVersion = 1\n"
	    "b.tlsVersionFloor = 1\n"
	    "b.ghostProgram = \"ghost\"\n"
	    "b.sftpPublicKey = true\n"
	    "b.sshAllowAes128Cbc = false\n"
	    "b.sshAcceptEarlyData = false\n"
	    "b.palette = [0, 11184810]\n"
	    "b.wrenScripts = [\"ui_list\", \"menu_bbs_editor\"]\n"
	    "b.sortOrder = 4\n"
	    "b.lfExpand = false\n"
	    "if (!b.dirty || b.connTypeName.count == 0 || b.palette.count != 2 || b.wrenScripts.count != 2) Fiber.abort(\"bbs\")\n"
	    "if (Menu.encryptionAvailable) {\n"
	    "  if (!Menu.setEncryption(6, 128, \"secret\") || Menu.encryptionAlgorithm != 6 || Menu.encryptionKeySize != 128) Fiber.abort(\"encrypt\")\n"
	    "  if (!Menu.setEncryption(7, 256, null) || Menu.encryptionAlgorithm != 7) Fiber.abort(\"reencrypt\")\n"
	    "  if (!Menu.setEncryption(0, 0, null) || Menu.encryptionAlgorithm != 0) Fiber.abort(\"decrypt\")\n"
	    "}\n";

	if (wrenInterpret(host->vm, "menu_contract", source)
	    != WREN_RESULT_SUCCESS) {
		fprintf(stderr, "menu contract failed: %s\n", host->last_error);
		return -1;
	}
	return 0;
}

static int
exercise_build_options_contract(st_wren_host_t *public_host)
{
	struct st_wren_host *host = public_host;
	static const char source[] =
	    "import \"syncterm_menu\" for Menu\n"
	    "var options = {}\n"
	    "for (row in Menu.buildOptions) options[row[1]] = row[2]\n"
	    "if (Menu.buildOptions.count != 28 "
	    "|| !options.containsKey(\"DeuceSSH (SSH)\") "
	    "|| !options.containsKey(\"OpenSSL (TLS, symmetric ciphers)\") "
	    "|| !options.containsKey(\"Operation Overkill ][ Terminal\") "
	    "|| !options.containsKey(\"X11 Support\") "
	    "|| !options.containsKey(\"Wayland\") "
	    "|| !options.containsKey(\"Core Audio\") "
	    "|| !options.containsKey(\"libsndfile\")) "
	    "Fiber.abort(\"build options\")\n"
	    "if (options[\"GPLv2+ binary (GNU Unifont-derived data)\"] != "
	    ST_WREN_TEST_UNIFONT_ENABLED
	    ") Fiber.abort(\"Unifont license option\")\n";

	if (wrenInterpret(host->vm, "build_options_contract", source)
	    != WREN_RESULT_SUCCESS) {
		fprintf(stderr, "build options contract failed: %s\n",
		    host->last_error);
		return -1;
	}
	return 0;
}

static int
exercise_unicode_fallback_contract(st_wren_host_t *public_host)
{
	struct st_wren_host *host = public_host;
	static const char source[] =
	    "import \"syncterm\" for Cell\n"
	    "var cell = Cell.new()\n"
	    "cell.font = 46\n"
	    "cell.ch = \"\\u4e00\"\n"
	    "if (cell.columns != 2) Fiber.abort(\"Unicode fallback width\")\n";

	if (wrenInterpret(host->vm, "unicode_fallback_contract", source)
	    != WREN_RESULT_SUCCESS) {
		fprintf(stderr, "Unicode fallback contract failed: %s\n",
		    host->last_error);
		return -1;
	}
	return 0;
}

static int
exercise_surface_cell_retarget_contract(st_wren_host_t *public_host)
{
	struct st_wren_host *host = public_host;
	static const char source[] =
	    "import \"syncterm\" for Cell, Surface\n"
	    "var first = Surface.new(2, 1)\n"
	    "var cell = Cell.new()\n"
	    "cell.ch = \"S\"\n"
	    "if (first.cellAt(-1, 0, cell) || cell.ch != \"S\") "
	    "Fiber.abort(\"out-of-bounds retarget\")\n"
	    "if (!first.cellAt(0, 0, cell)) Fiber.abort(\"first retarget\")\n"
	    "cell.ch = \"A\"\n"
	    "if (!first.cellAt(1, 0, cell)) Fiber.abort(\"same storage\")\n"
	    "cell.ch = \"B\"\n"
	    "if (first.cellAt(2, 0, cell) || cell.ch != \"B\") "
	    "Fiber.abort(\"failed retarget changed cell\")\n"
	    "if (first.cellAt(0, 0).ch != \"A\" || "
	    "first.cellAt(1, 0).ch != \"B\") Fiber.abort(\"cell writes\")\n"
	    "var second = Surface.new(1, 1)\n"
	    "if (!second.cellAt(0, 0, cell)) Fiber.abort(\"cross storage\")\n"
	    "second = null\n"
	    "System.gc()\n"
	    "cell.ch = \"C\"\n"
	    "if (cell.ch != \"C\") Fiber.abort(\"retarget lifetime\")\n";

	if (wrenInterpret(host->vm, "surface_cell_retarget_contract", source)
	    != WREN_RESULT_SUCCESS) {
		fprintf(stderr, "Surface cell retarget contract failed: %s\n",
		    host->last_error);
		return -1;
	}
	return 0;
}

static int
exercise_cursor_contract(st_wren_host_t *public_host)
{
	struct st_wren_host *host = public_host;
	static const char source[] =
	    "import \"syncterm\" for CustomCursor\n"
	    "var normal = CustomCursor.normal\n"
	    "if (normal.startLine != 14 || normal.endLine != 15 || normal.range != 16 || !normal.visible || !normal.blink) Fiber.abort(\"normal cursor\")\n"
	    "var solid = CustomCursor.solid\n"
	    "if (solid.startLine != 0 || solid.endLine != 15 || solid.range != 16 || !solid.visible || solid.blink) Fiber.abort(\"solid cursor\")\n";

	if (wrenInterpret(host->vm, "cursor_contract", source)
	    != WREN_RESULT_SUCCESS) {
		fprintf(stderr, "cursor contract failed: %s\n", host->last_error);
		return -1;
	}
	return 0;
}

static int
exercise_codepage_contract(st_wren_host_t *public_host)
{
	struct st_wren_host *host = public_host;
	static const char source[] =
	    "import \"syncterm\" for Codepage, Font, Screen\n"
	    "if (!Codepage.encodes_(\"A\") || Codepage.encodes_(\"🙂\")) Fiber.abort(\"codepage encoding\")\n"
	    "if (Font.name(0) != \"Codepage 437 English\" || Font.name(43) != \"Prestel\" || Font.name(200) != \"\") Fiber.abort(\"font names\")\n"
	    "if (Font.count != 47 || !Font.available(0) || Font.available(45)) Fiber.abort(\"font catalog\")\n"
	    "Screen.font[0] = Font.commodore64Upper\n"
	    "Screen.font[1] = Font.prestel\n"
	    "Screen.font[2] = 46\n"
	    "if (Font.codepage != Codepage.petsciiUpper || Font.codepageOf(1) != Codepage.prestel || Font.codepageOf(2) != Codepage.cp437) Fiber.abort(\"font codepages\")\n"
	    "Screen.font[0] = 0\n"
	    "Screen.font[1] = 0\n"
	    "Screen.font[2] = 0\n";

	if (wrenInterpret(host->vm, "codepage_contract", source)
	    != WREN_RESULT_SUCCESS) {
		fprintf(stderr, "codepage contract failed: %s\n", host->last_error);
		return -1;
	}
	return 0;
}

static int
exercise_configuration_collections(st_wren_host_t *public_host)
{
	struct st_wren_host *host = public_host;
	static const char source[] =
	    "import \"syncterm_menu\" for Menu\n"
	    "if (Menu.sortProfiles.count != 2 || Menu.sortProfiles[1][1][1] != 3 || Menu.activeSortProfile != 1) Fiber.abort(\"sort load\")\n"
	    "if (!Menu.sortFields[2][2]) Fiber.abort(\"sort direction\")\n"
	    "if (!Menu.setActiveSortProfile(0) || Menu.activeSortProfile != 0) Fiber.abort(\"sort select\")\n"
	    "if (!Menu.addSortProfile(2, \"Calls\", [29, 4, 1])) Fiber.abort(\"sort add\")\n"
	    "if (!Menu.updateSortProfile(2, \"Call Count\", [29, -4, 1])) Fiber.abort(\"sort update\")\n"
	    "if (!Menu.deleteSortProfile(2) || !Menu.saveSortProfiles()) Fiber.abort(\"sort delete\")\n"
	    "if (Menu.fonts.count != 1 || Menu.fonts[0].name != \"Configured\" "
	    "|| Menu.fonts[0].path(2) == null || Menu.fontsCatalog.count != 47 "
	    "|| Menu.fontsCatalog[0][1] != \"Codepage 437 English\" "
	    "|| Menu.fontsCatalog[43][1] != \"Prestel\" "
	    "|| Menu.fontsCatalog[45][1] != \"RIPterm\" "
	    "|| Menu.fontsCatalog[46][0] != 46) Fiber.abort(\"font load\")\n"
	    "var customFont = Menu.createFont(\"Temporary\", 1)\n"
	    "if (customFont == null) Fiber.abort(\"font create\")\n"
	    "customFont.name = \"Renamed\"\n"
	    "if (customFont.name != \"Renamed\" || !customFont.clearFile(2)) Fiber.abort(\"font edit\")\n"
	    "if (!customFont.delete() || Menu.fonts.count != 1 || !Menu.saveFonts()) Fiber.abort(\"font delete\")\n";

	if (wrenInterpret(host->vm, "configuration_collections", source)
	    != WREN_RESULT_SUCCESS) {
		fprintf(stderr, "configuration collection contract failed: %s\n",
		    host->last_error);
		return -1;
	}
	return 0;
}

static int
exercise_url_selection(st_wren_host_t *host)
{
	struct st_profile profile;

	st_profile_init(&profile);
	CHECK(st_wren_host_select_url(host, "example.com",
	    ST_CONNECTION_RLOGIN, &profile) == 0);
	CHECK(profile.connection_type == ST_CONNECTION_RLOGIN);
	CHECK(profile.port == 513);
	CHECK(strcmp(profile.address, "example.com") == 0);
	CHECK(st_wren_host_select_url(host, "ssh://example.com",
	    ST_CONNECTION_RLOGIN, &profile) == 0);
	CHECK(profile.connection_type == ST_CONNECTION_SSH);
	CHECK(profile.port == 22);
	st_profile_dispose(&profile);
	return 0;
}

static int
exercise_theme_menu_contract(st_wren_host_t *public_host)
{
	struct st_wren_host *host = public_host;
	static const char source[] =
	    "import \"syncterm\" for Host\n"
	    "import \"syncterm_menu\" for Menu\n"
	    "if (Menu.themes.count < 2) Fiber.abort(\"theme catalog\")\n"
	    "var generation = Host.themeGeneration\n"
	    "if (Menu.previewTheme(\"host-theme.ini\") != null) Fiber.abort(\"preview\")\n"
	    "var foreground = null\n"
	    "for (row in Host.themeData[0]) if (row[0] == \"frame\") foreground = row[3]\n"
	    "if (foreground != 0x123456 || Host.themeGeneration <= generation) Fiber.abort(\"preview data\")\n"
	    "Menu.cancelThemePreview()\n"
	    "var document = Menu.newThemeDocument()\n"
	    "if (document == null || document.metadata(0) != \"New Theme\") Fiber.abort(\"new document\")\n"
	    "if (document.setMetadata(0, \"Draft Theme\") != null || document.addStyle(\"custom.notice\") != null || document.setStyle(\"custom.notice\", 2, 2, 0xabcdef) != null) Fiber.abort(\"document edit\")\n"
	    "if (!document.dirty || !document.canUndo || !document.undo() || !document.canRedo || !document.redo()) Fiber.abort(\"history\")\n"
	    "var saved = document.save(\"host-draft.ini\", false)\n"
	    "if (saved[0] != 0 || saved[1] != null || document.dirty) Fiber.abort(\"document save\")\n"
	    "var reopened = Menu.openThemeDocument(\"host-draft.ini\")\n"
	    "if (reopened.metadata(0) != \"Draft Theme\" || reopened.styles.count < 49) Fiber.abort(\"document reopen\")\n"
	    "if (Menu.deleteTheme(\"host-draft.ini\") != null) Fiber.abort(\"document delete\")\n";

	if (wrenInterpret(host->vm, "theme_menu_contract", source)
	    != WREN_RESULT_SUCCESS) {
		fprintf(stderr, "theme menu contract failed: %s\n", host->last_error);
		return -1;
	}
	return 0;
}

static int
exercise_repl_contract(struct st_wren_host *host)
{
	static const char source[] =
	    "import \"syncterm\" for Console, Host, REPL, LogSource\n"
	    "Console.clear()\nConsole.markSeen()\n"
	    "if (Host.logUnread || Host.logUnreadError) Fiber.abort(\"seen\")\n"
	    "System.write(\"log marker\")\n"
	    "var seq = Console.total - 1\n"
	    "var entry = Console[seq]\n"
	    "if (entry[1] != LogSource.print || entry[2] != \"log marker\" || !Host.logUnread || Host.logUnreadError) Fiber.abort(\"print log\")\n"
	    "if (Console[seq] != entry) Fiber.abort(\"cached entry\")\n"
	    "REPL.eval(\"repl_contract\", \"var replValue = 21\")\n"
	    "if (REPL.eval(\"repl_contract\", \"replValue * 2\")[0] != 42) Fiber.abort(\"eval\")\n"
	    "if (!REPL.hasModule(\"repl_contract\") || !REPL.modules.contains(\"repl_contract\")) Fiber.abort(\"modules\")\n"
	    "REPL.eval(\"repl_contract\", \"pwd\")\n"
	    "if (!Host.logUnreadError || Console.total <= seq + 1) Fiber.abort(\"compile diagnostics\")\n"
	    "var failed = Fiber.new { REPL.eval(\"repl_contract\", \"Fiber.abort(\\\"caught repl error\\\")\") }\n"
	    "failed.try()\n"
	    "if (failed.error != \"caught repl error\") Fiber.abort(\"catch\")\n"
	    "REPL.printTrace_(failed)\n"
	    "if (REPL.eval(\"repl_contract\", \"replValue\")[0] != 21) Fiber.abort(\"recovery\")\n"
	    "REPL.captureStart_()\n"
	    "REPL.compile_(\"repl_contract\", \"unknownCapturedName\", true, true)\n"
	    "if (!REPL.captureContains_(\"unknownCapturedName\")) Fiber.abort(\"capture\")\n"
	    "REPL.captureClear_()\n"
	    "for (i in 0...1100) System.write(\"wrap %(i)\")\n"
	    "if (Console.count != 1024 || Console[seq] != null || Console[-1] != null || Console[0.5] != null) Fiber.abort(\"ring eviction\")\n"
	    "if (entry[2] != \"log marker\" || Console.entries.toList.count != 1024) Fiber.abort(\"retained entry\")\n"
	    "var total = Console.total\nConsole.clear()\n"
	    "if (Console.total != total || Console.count != 0 || Console.iterate(null) != false) Fiber.abort(\"clear\")\n"
	    "Console.markSeen()\n";

	CHECK(wrenInterpret(host->vm, "repl_contract", source) == WREN_RESULT_SUCCESS);
	uint64_t menu_total = host->menu.log.total;
	CHECK(wrenInterpret(host->picker_vm, "repl_contract", source) == WREN_RESULT_SUCCESS);
	CHECK(host->menu.log.total == menu_total);
	return 0;
}

static int
queue_text(st_console_t *console, const char *text)
{
	for (; *text != '\0'; ++text) {
		struct st_console_event event = {
			.type = ST_CONSOLE_EVENT_TEXT,
			.data.text.codepoint = (unsigned char)*text,
		};
		CHECK(st_console_post_event(console, &event) == 0);
	}
	return 0;
}

static int
exercise_repl_style(struct st_wren_host *host, st_console_t *console)
{
	static const char source[] =
	    "import \"syncterm\" for Cell, Screen\n"
	    "import \"wren_console\" for WrenConsole\n"
	    "var saved = Screen.save()\n"
	    "var correct = false\n"
	    "WrenConsole.register(\"style-test\", \"test\", Fn.new {|args|\n"
	    "  var size = Screen.size\n"
	    "  var cells = Screen.readRect(1, 1, size[0], size[1])\n"
	    "  correct = Screen.attr == 7 && cells.all {|c| c.fgPalette == 7 && c.bgPalette == 0 }\n"
	    "})\n"
	    "var mapped = Cell.new()\n"
	    "mapped.legacyAttr = 0x41\n"
	    "if (mapped.fgPalette != 4 || mapped.bgPalette != 1 || mapped.legacyAttr != 0x41) Fiber.abort(\"legacy colour mapping failed\")\n"
	    "Screen.attr = 0x1f\n"
	    "WrenConsole.run(\"syncterm\")\n"
	    "if (Screen.attr != 0x1f) Fiber.abort(\"REPL did not restore caller attribute\")\n"
	    "if (!correct) Fiber.abort(\"REPL inherited editor attribute\")\n"
	    "Screen.restore(saved)\n"
	    "WrenConsole.unregister(\"style-test\")\n";
	struct st_console_event enter = {
		.type = ST_CONSOLE_EVENT_TEXT,
		.data.text.codepoint = '\r',
	};
	WrenVM *vms[] = {host->vm, host->picker_vm};

	for (size_t i = 0; i < sizeof(vms) / sizeof(vms[0]); ++i) {
		CHECK(queue_text(console, "/style-test") == 0);
		CHECK(st_console_post_event(console, &enter) == 0);
		CHECK(queue_text(console, "/quit") == 0);
		CHECK(st_console_post_event(console, &enter) == 0);
		CHECK(wrenInterpret(vms[i], "repl_style", source) == WREN_RESULT_SUCCESS);
	}
	return 0;
}

struct selection_observer {
	st_console_t *console;
	struct st_console_rect area;
	struct st_console_event release;
	struct st_console_cell highlighted[6];
};

static int
observe_selection(void *argument)
{
	struct selection_observer *observer = argument;
	static const uint32_t expected_backgrounds[6] = {3, 4, 4, 4, 4, 4};
	const struct timespec interval = {0, 1000000};
	int result = 1;

	for (unsigned int attempt = 0; attempt < 5000; ++attempt) {
		bool highlighted = true;

		if (st_console_read_cells(observer->console, &observer->area,
		    observer->highlighted, observer->area.width) != 0)
			break;
		for (size_t index = 0; index < 6; ++index) {
			if (observer->highlighted[index].background
			    != expected_backgrounds[index]) {
				highlighted = false;
				break;
			}
		}
		if (highlighted) {
			result = 0;
			break;
		}
		(void)st_thread_sleep(&interval, NULL);
	}
	if (st_console_post_event(observer->console, &observer->release) != 0)
		result = 1;
	return result;
}

static int
exercise_mouse_drag_selection(struct st_wren_host *host,
	st_console_t *console)
{
	const uint32_t selection_blue = UINT32_C(0x0000a8);
	const struct st_console_rect area = {1, 1, 3, 2};
	struct st_console_cell cells[6] = {
		{.codepoint = 'A',
		    .foreground = ST_CONSOLE_COLOR_RGB_VALUE(255, 255, 255),
		    .background = 500},
		{.codepoint = UINT32_C(0x03a9),
		    .foreground = ST_CONSOLE_COLOR_RGB_VALUE(0, 0, 168)},
		{.codepoint = ' ', .foreground = 7, .background = 1},
		{.codepoint = 'B', .foreground = 7},
		{.codepoint = 'C', .foreground = 0, .background = 7,
		    .flags = ST_CONSOLE_CELL_REVERSE},
		{.codepoint = 'D', .foreground = 1},
	};
	struct st_console_cell before[6];
	struct st_console_cell after[6];
	struct selection_observer observer;
	st_thread_t observer_thread;
	int observer_result;
	WrenInterpretResult interpret_result;
	const struct st_console_event press = {
		.type = ST_CONSOLE_EVENT_MOUSE,
		.data.mouse = {
			.x = 1, .y = 1,
			.pixel_x = 8, .pixel_y = 16,
			.buttons = ST_CONSOLE_MOUSE_BUTTON_LEFT,
			.action = ST_CONSOLE_MOUSE_BUTTON,
		},
	};
	const struct st_console_event move = {
		.type = ST_CONSOLE_EVENT_MOUSE,
		.data.mouse = {
			.x = 3, .y = 2,
			.pixel_x = 24, .pixel_y = 32,
			.buttons = ST_CONSOLE_MOUSE_BUTTON_LEFT,
			.action = ST_CONSOLE_MOUSE_MOVE,
		},
	};
	const struct st_console_event release = {
		.type = ST_CONSOLE_EVENT_MOUSE,
		.data.mouse = {
			.x = 3, .y = 2,
			.pixel_x = 24, .pixel_y = 32,
			.action = ST_CONSOLE_MOUSE_BUTTON,
		},
	};
	static const char source[] =
	    "import \"syncterm\" for Input, Mouse, MouseEvent\n"
	    "Input.mouseEvents = 0\n"
	    "Input.enableMouseEvent(Mouse.wheelDownPress)\n"
	    "if (Input.mouseEvents != 137438953472) Fiber.abort(\"wide mouse mask\")\n"
	    "Input.disableMouseEvent(Mouse.wheelDownPress)\n"
	    "Input.enableMouseEvent(Mouse.button1Press)\n"
	    "Input.enableMouseEvent(Mouse.button1DragStart)\n"
	    "var press = Input.next()\n"
	    "if (!(press is MouseEvent) || press.event != Mouse.button1Press || press.startX != 2 || press.startY != 2 || press.startPixelX != 8 || press.startPixelY != 16) Fiber.abort(\"mouse press\")\n"
	    "var drag = Input.next()\n"
	    "if (!(drag is MouseEvent) || drag.event != Mouse.button1DragStart || drag.startX != 2 || drag.startY != 2 || drag.endX != 4 || drag.endY != 3 || drag.startPixelX != 8 || drag.startPixelY != 16 || drag.endPixelX != 24 || drag.endPixelY != 32) Fiber.abort(\"mouse drag\")\n"
	    "Input.mousedrag(true)\n";

	CHECK(st_console_set_palette(console, 500, &selection_blue, 1) == 0);
	CHECK(st_console_write_cells(console, &area, cells, 3) == 0);
	CHECK(st_console_present(console) == 0);
	CHECK(st_console_read_cells(console, &area, before, 3) == 0);
	CHECK(st_console_post_event(console, &press) == 0);
	CHECK(st_console_post_event(console, &move) == 0);
	observer = (struct selection_observer){
		.console = console,
		.area = area,
		.release = release,
	};
	CHECK(st_thread_create(&observer_thread, observe_selection, &observer)
	    == ST_THREAD_SUCCESS);
	interpret_result = wrenInterpret(host->vm, "mouse_drag_selection", source);
	CHECK(st_thread_join(observer_thread, &observer_result)
	    == ST_THREAD_SUCCESS);
	CHECK(interpret_result == WREN_RESULT_SUCCESS);
	CHECK(observer_result == 0);
	CHECK(observer.highlighted[0].foreground
	    == ST_CONSOLE_COLOR_RGB_VALUE(255, 255, 255));
	CHECK(observer.highlighted[1].foreground == 12);
	CHECK(observer.highlighted[4].foreground == 7);
	CHECK((observer.highlighted[4].flags & ST_CONSOLE_CELL_REVERSE) == 0);
	CHECK(observer.highlighted[5].foreground == 1);
	CHECK(st_console_read_cells(console, &area, after, 3) == 0);
	for (size_t index = 0; index < 6; ++index) {
		CHECK(before[index].codepoint == after[index].codepoint);
		CHECK(before[index].glyph == after[index].glyph);
		CHECK(before[index].foreground == after[index].foreground);
		CHECK(before[index].background == after[index].background);
		CHECK((before[index].flags & ST_CONSOLE_CELL_PUBLIC_FLAGS)
		    == (after[index].flags & ST_CONSOLE_CELL_PUBLIC_FLAGS));
		CHECK(before[index].font == after[index].font);
		CHECK(before[index].hyperlink == after[index].hyperlink);
	}
	CHECK(!host->menu.mouse_left_pressed);
	CHECK(!host->menu.mouse_left_dragging);
	return 0;
}

int
main(void)
{
	static const uint8_t default_bitmap[16] = {0};
	static const uint8_t default_wide_bitmap[16] = {0};
	static const struct st_console_font_mapping default_mapping[] = {
		{'?', 0},
		{'A', 1},
		{UINT32_C(0x4e00), 2},
	};
	static const struct st_console_font default_font = {
		.bitmap = default_bitmap,
		.bitmap_size = sizeof(default_bitmap),
		.width = 8,
		.height = 8,
		.glyph_count = 2,
		.row_stride = 1,
		.glyph_stride = 8,
		.wide_bitmap = default_wide_bitmap,
		.wide_bitmap_size = sizeof(default_wide_bitmap),
		.wide_glyph_count = 1,
		.wide_row_stride = 2,
		.wide_glyph_stride = 16,
		.mapping = default_mapping,
		.mapping_count = sizeof(default_mapping) / sizeof(default_mapping[0]),
	};
	char personal[sizeof(ST_WREN_TEST_DIRECTORY) + 32];
	char shared[sizeof(ST_WREN_TEST_DIRECTORY) + 32];
	char cache[sizeof(ST_WREN_TEST_DIRECTORY) + 32];
	char system_cache[sizeof(ST_WREN_TEST_DIRECTORY) + 48];
	char system_list[sizeof(ST_WREN_TEST_DIRECTORY) + 64];
	char settings[sizeof(ST_WREN_TEST_DIRECTORY) + 32];
	char scripts[sizeof(ST_WREN_TEST_DIRECTORY) + 32];
	char theme[sizeof(ST_WREN_TEST_DIRECTORY) + 32];
	char font[sizeof(ST_WREN_TEST_DIRECTORY) + 32];
	char settings_contents[sizeof(font) + 192];
	struct st_app_paths paths = {0};
	struct st_console_display_config console_config_display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	struct st_console_config console_config = {
		.columns = 80,
		.rows = 25,
		.cell_width = 8,
		.cell_height = 16,
		.wrap = true,
	};
	const struct st_console_ansi_io ansi_io = {
		.write = discard_ansi_output,
		.indexed_colours = 16,
	};
	struct st_wren_host_config host_config;
	struct st_console_event enter = {
		.type = ST_CONSOLE_EVENT_KEY_CODE,
		.data.key.code = 13,
	};
	struct st_console_event ctrl_q = {
		.type = ST_CONSOLE_EVENT_KEY_CODE,
		.data.key.code = 0x11,
	};
	struct st_console_event ctrl_s = {
		.type = ST_CONSOLE_EVENT_KEY_CODE,
		.data.key.code = 0x13,
	};
	struct st_console_event doorway = {
		.type = ST_CONSOLE_EVENT_DOORWAY_KEY,
		.data.doorway_key = {.scan_code = 0x2e},
	};
	struct st_console_event doorway_enhanced = {
		.type = ST_CONSOLE_EVENT_DOORWAY_KEY,
		.data.doorway_key = {.scan_code = 0x1c, .enhanced = true},
	};
	struct st_console_event wren_console_key = {
		.type = ST_CONSOLE_EVENT_PHYSICAL_KEY,
		.data.physical_key = {
			.evdev = 41,
			.modifiers = ST_CONSOLE_MODIFIER_CONTROL,
			.pressed = true,
		},
	};
	struct st_profile selected;
	st_console_t *console = NULL;
	st_wren_host_t *host = NULL;
	bool has_selection = false;
	bool font_registered = false;
	uint16_t default_identifier;
	uint16_t configured_identifier;

	CHECK(fs_create_directories(ST_WREN_TEST_DIRECTORY) == 0);
	(void)snprintf(personal, sizeof(personal), "%s/personal.lst",
	    ST_WREN_TEST_DIRECTORY);
	(void)snprintf(shared, sizeof(shared), "%s/shared.lst",
	    ST_WREN_TEST_DIRECTORY);
	(void)snprintf(cache, sizeof(cache), "%s/cache", ST_WREN_TEST_DIRECTORY);
	(void)snprintf(system_cache, sizeof(system_cache), "%s/cache/system",
	    ST_WREN_TEST_DIRECTORY);
	(void)snprintf(system_list, sizeof(system_list), "%s/system.lst",
	    system_cache);
	(void)snprintf(settings, sizeof(settings), "%s/syncterm.ini",
	    ST_WREN_TEST_DIRECTORY);
	(void)snprintf(scripts, sizeof(scripts), "%s/scripts",
	    ST_WREN_TEST_DIRECTORY);
	(void)snprintf(theme, sizeof(theme), "%s/host-theme.ini",
	    ST_WREN_TEST_DIRECTORY);
	(void)snprintf(font, sizeof(font), "%s/configured.f16",
	    ST_WREN_TEST_DIRECTORY);
	CHECK(write_file(personal,
	    "[Alpha]\nAddress=alpha.example\nLastConnected=100\n"
	    "[Zulu]\nAddress=zulu.example\nLastConnected=200\n") == 0);
	CHECK(write_file(shared, "") == 0);
	CHECK(write_zero_file(font, 4096) == 0);
	CHECK(snprintf(settings_contents, sizeof(settings_contents),
	    "[SyncTERM]\nSortOrder=29,3,1\nActiveSortProfile=Recent\n"
	    "[SortProfiles]\nName=29,1\nRecent=29,3,1\n"
	    "[Font:Configured]\nPath8x16=%s\n", font)
	    < (int)sizeof(settings_contents));
	CHECK(write_file(settings, settings_contents) == 0);
	CHECK(write_file(theme,
	    "Name=Host Theme\n[Style:frame]\nForeground=0x123456\n") == 0);
	paths.settings_file = settings;
	paths.personal_list = personal;
	paths.shared_list = shared;
	paths.cache_directory = cache;
	paths.system_cache_directory = system_cache;
	paths.cached_system_list = system_list;
	paths.download_directory = ST_WREN_TEST_DIRECTORY;
	paths.scripts_directory = scripts;
	paths.themes_directory = ST_WREN_TEST_DIRECTORY;
	paths.keys_file = settings;
	CHECK(st_settings_load(&paths) == 0);
	CHECK(st_console_create(&console, &console_config_display, &console_config) == 0);
	CHECK(st_console_register_static_font(console, &default_font,
	    &default_identifier) == 0);
	CHECK(default_identifier == 0);
	host_config = (struct st_wren_host_config){
		.console = console,
		.paths = &paths,
		.application_title = "SyncTERM test",
	};
	CHECK(st_wren_host_create(&host, &host_config) == 0);
	CHECK(exercise_repl_style(host, console) == 0);
	CHECK(exercise_mouse_drag_selection(host, console) == 0);
	CHECK(exercise_repl_contract(host) == 0);
	CHECK(wrenInterpret(((struct st_wren_host *)host)->vm,
	    "headless_host_capabilities",
	    "import \"syncterm\" for Host\n"
	    "if (Host.textTerminal || Host.localControlKeys) "
	    "Fiber.abort(\"headless host capabilities\")\n")
	    == WREN_RESULT_SUCCESS);
	CHECK(st_console_post_event(console, &doorway) == 0);
	CHECK(st_console_post_event(console, &doorway_enhanced) == 0);
	CHECK(wrenInterpret(((struct st_wren_host *)host)->vm,
	    "doorway_input_contract",
	    "import \"syncterm\" for Input\n"
	    "var ordinary = Input.poll()\n"
	    "var enhanced = Input.poll()\n"
	    "if (ordinary == null || ordinary.code != 11776 || "
	    "enhanced == null || enhanced.code != 7168) "
	    "Fiber.abort(\"DoorWay key conversion failed\")\n")
	    == WREN_RESULT_SUCCESS);
	CHECK(st_console_post_event(console, &wren_console_key) == 0);
	CHECK(wrenInterpret(((struct st_wren_host *)host)->vm,
	    "wren_console_key_contract",
	    "import \"syncterm\" for Input, Key\n"
	    "var event = Input.poll()\n"
	    "if (event == null || event.code != Key.wrenConsole) "
	    "Fiber.abort(\"Ctrl+grave conversion failed\")\n")
	    == WREN_RESULT_SUCCESS);
	for (uint16_t selector = 0; selector < 45; ++selector)
		CHECK(st_wren_font_identifier(&((struct st_wren_host *)host)->menu,
		    selector, &configured_identifier));
	CHECK(!st_wren_font_identifier(&((struct st_wren_host *)host)->menu, 45,
	    &configured_identifier));
	CHECK(exercise_unicode_fallback_contract(host) == 0);
	CHECK(exercise_surface_cell_retarget_contract(host) == 0);
	CHECK(exercise_web_refresh_overlay(host, console) == 0);
	CHECK(exercise_cursor_contract(host) == 0);
	CHECK(exercise_codepage_contract(host) == 0);
	CHECK(st_wren_font_identifier(&((struct st_wren_host *)host)->menu, 46,
	    &configured_identifier));
	CHECK(st_console_font_registered(console, configured_identifier,
	    &font_registered) == 0);
	CHECK(font_registered);
	wrenEnsureSlots(((struct st_wren_host *)host)->vm, 1);
	st_wren_push_theme_data(((struct st_wren_host *)host)->vm, NULL);
	CHECK(wrenGetSlotType(((struct st_wren_host *)host)->vm, 0)
	    == WREN_TYPE_LIST);
	CHECK(wrenGetListCount(((struct st_wren_host *)host)->vm, 0) == 2);
	CHECK(exercise_theme_contract(host, false) == 0);
	uint64_t theme_generation = st_theme_generation(
	    ((struct st_wren_host *)host)->themes);
	CHECK(exercise_menu_contract(host) == 0);
	CHECK(exercise_build_options_contract(host) == 0);
	CHECK(exercise_configuration_collections(host) == 0);
	CHECK(st_wren_font_identifier(&((struct st_wren_host *)host)->menu, 46,
	    &configured_identifier));
	CHECK(st_console_font_registered(console, configured_identifier,
	    &font_registered) == 0);
	CHECK(font_registered);
	CHECK(st_theme_generation(((struct st_wren_host *)host)->themes)
	    > theme_generation);
	CHECK(exercise_theme_contract(host, true) == 0);
	CHECK(exercise_theme_menu_contract(host) == 0);
	CHECK(exercise_url_selection(host) == 0);
	CHECK(wrenInterpret(((struct st_wren_host *)host)->vm, "repl_screen_probe",
	    "import \"syncterm\" for Screen\n"
	    "import \"wren_console\" for WrenConsole\n"
	    "var observed = false\n"
	    "WrenConsole.register(\"screen-probe\", \"test\", Fn.new {|args|\n"
	    "  var size = Screen.size\n"
	    "  var cells = Screen.readRect(1, 1, size[0], size[1])\n"
	    "  var text = cells.map {|c| c.ch }.join()\n"
	    "  observed = text.contains(\"42\") && text.contains(\"pwd\")\n"
	    "})\n") == WREN_RESULT_SUCCESS);
	CHECK(st_console_post_event(console, &wren_console_key) == 0);
	CHECK(queue_text(console, "pwd") == 0);
	CHECK(st_console_post_event(console, &enter) == 0);
	CHECK(queue_text(console, "Fiber.abort(\"recoverable console error\")") == 0);
	CHECK(st_console_post_event(console, &enter) == 0);
	CHECK(queue_text(console, "var consoleAnswer = 6 * 7") == 0);
	CHECK(st_console_post_event(console, &enter) == 0);
	CHECK(queue_text(console, "consoleAnswer") == 0);
	CHECK(st_console_post_event(console, &enter) == 0);
	CHECK(queue_text(console, "/screen-probe") == 0);
	CHECK(st_console_post_event(console, &enter) == 0);
	CHECK(st_console_post_event(console, &ctrl_q) == 0);
	CHECK(st_console_post_event(console, &ctrl_s) == 0);
	CHECK(st_console_post_event(console, &wren_console_key) == 0);
	CHECK(st_console_post_event(console, &enter) == 0);
	st_profile_init(&selected);
	CHECK(st_wren_host_run_menu(host, &selected, &has_selection) == 0);
	CHECK(has_selection);
	CHECK(wrenInterpret(((struct st_wren_host *)host)->vm, "repl_ui_result",
	    "import \"syncterm\" for Console, REPL\n"
	    "if (REPL.eval(\"main_menu\", \"consoleAnswer\")[0] != 42) Fiber.abort(\"UI eval failed\")\n"
	    "if (!Console.entries.any {|e| e[2] == \"42\" }) Fiber.abort(\"missing visible result\")\n")
	    == WREN_RESULT_SUCCESS);
	CHECK(wrenInterpret(((struct st_wren_host *)host)->vm, "repl_screen_probe",
	    "if (!observed) Fiber.abort(\"REPL output was not painted\")\n")
	    == WREN_RESULT_SUCCESS);
	CHECK(console_has_text(console, 2, 0, "SyncTERM test - Headless"));
	struct st_console_cell title_cell;
	struct st_console_rect title_area = {2, 0, 1, 1};
	CHECK(st_console_read_cells(console, &title_area, &title_cell, 1) == 0);
	CHECK(title_cell.foreground == ST_CONSOLE_COLOR_RGB_VALUE(0, 0, 0));
	CHECK(title_cell.background == ST_CONSOLE_COLOR_RGB_VALUE(0xaa, 0xaa,
	    0xaa));
	CHECK(strcmp(selected.name, "Alpha") == 0);
	st_profile_dispose(&selected);
	CHECK(wrenInterpret(((struct st_wren_host *)host)->vm,
	    "alt_shortcut_contract",
	    "import \"syncterm\" for Key\n"
	    "import \"main_menu\" for MainMenu\n"
	    "MainMenu.registerCommand(Key.altY, \"Alt-Y\", \"Test\", "
	    "Fn.new {|app, connected| app.quit() })\n")
	    == WREN_RESULT_SUCCESS);
	struct st_console_event alt_y = {
		.type = ST_CONSOLE_EVENT_PHYSICAL_KEY,
		.data.physical_key = {
			.evdev = 21,
			.modifiers = ST_CONSOLE_MODIFIER_ALT,
			.pressed = true,
		},
	};
	CHECK(st_console_post_event(console, &alt_y) == 0);
	st_profile_init(&selected);
	has_selection = true;
	CHECK(st_wren_host_run_menu(host, &selected, &has_selection) == 0);
	CHECK(!has_selection);
	st_profile_dispose(&selected);
	st_wren_host_destroy(host);
	st_console_destroy(console);

	console = NULL;
	host = NULL;
	console_config_display.backend = ST_CONSOLE_BACKEND_ANSI;
	console_config_display.ansi_io = &ansi_io;
	CHECK(st_console_create(&console, &console_config_display, &console_config) == 0);
	uint32_t text_capabilities;
	CHECK(st_console_capabilities(console, &text_capabilities) == 0);
	CHECK((text_capabilities & ST_CONSOLE_CAP_TEXT_TERMINAL) != 0);
	CHECK((text_capabilities & ST_CONSOLE_CAP_LOCAL_CONTROL_KEYS) != 0);
	host_config.console = console;
	CHECK(st_wren_host_create(&host, &host_config) == 0);
	CHECK(wrenInterpret(((struct st_wren_host *)host)->vm,
	    "ansi_host_capabilities",
	    "import \"syncterm\" for Host\n"
	    "if (!Host.textTerminal || !Host.localControlKeys) "
	    "Fiber.abort(\"ANSI host capabilities\")\n")
	    == WREN_RESULT_SUCCESS);
	CHECK(st_console_post_event(console, &ctrl_q) == 0);
	CHECK(st_console_post_event(console, &ctrl_s) == 0);
	CHECK(st_console_post_event(console, &enter) == 0);
	st_profile_init(&selected);
	has_selection = false;
	CHECK(st_wren_host_run_menu(host, &selected, &has_selection) == 0);
	CHECK(has_selection);
	st_profile_dispose(&selected);
	CHECK(st_console_post_event(console, &ctrl_q) == 0);
	CHECK(st_console_post_event(console, &ctrl_q) == 0);
	CHECK(st_console_post_event(console, &enter) == 0);
	st_profile_init(&selected);
	has_selection = false;
	CHECK(st_wren_host_run_menu(host, &selected, &has_selection) == 0);
	CHECK(has_selection);
	st_profile_dispose(&selected);
	st_wren_host_destroy(host);
	st_console_destroy(console);
	st_settings_shutdown();
	return 0;
}
