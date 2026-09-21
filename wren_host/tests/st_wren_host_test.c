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
#include "st_audio_runtime.h"
#include "st_console.h"
#include "st_profile.h"
#include "st_settings.h"
#include "st_session_history.h"
#include "st_theme.h"
#include "st_threads.h"
#include "st_time.h"

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

static bool
file_contains(const char *path, const char *text)
{
	FILE *stream = fopen(path, "rb");
	char buffer[8192];

	if (stream == NULL)
		return false;
	size_t size = fread(buffer, 1, sizeof(buffer) - 1, stream);
	bool okay = !ferror(stream) && fclose(stream) == 0;
	buffer[size] = '\0';
	return okay && strstr(buffer, text) != NULL;
}

static bool
file_equals(const char *path, const char *expected)
{
	FILE *stream = fopen(path, "rb");
	char buffer[8192];
	size_t expected_size = strlen(expected);
	size_t size;
	bool okay;

	if (stream == NULL || expected_size >= sizeof(buffer)) {
		if (stream != NULL)
			(void)fclose(stream);
		return false;
	}
	size = fread(buffer, 1, sizeof(buffer), stream);
	okay = !ferror(stream) && fclose(stream) == 0;
	return okay && size == expected_size
	    && memcmp(buffer, expected, expected_size) == 0;
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
	    "if (Menu.currentScreenMode != 0) Fiber.abort(\"current mode\")\n"
	    "if (!Menu.setScreenMode(3) || Menu.currentScreenMode != 3 || !Menu.setScreenMode(1)) Fiber.abort(\"set mode\")\n"
	    "if (Menu.entries.count != 2 || Menu.entries[0].name != \"Zulu\") Fiber.abort(\"entries/sort\")\n"
	    "var q = Menu.quickConnect(\"ssh://guest:secret@[::1]:2222\")\n"
	    "if (q == null || q.connType != 5 || q.addr != \"::1\" || q.port != 2222 || q.user != \"guest\" || q.password != \"secret\") Fiber.abort(\"quick\")\n"
	    "if (Menu.fileLocations[\"configuration\"].count == 0) Fiber.abort(\"paths\")\n"
	    "if (Menu.paletteDefaults(1)[1].count != 16) Fiber.abort(\"palette\")\n"
	    "var scalers = Menu.scalingModes\n"
	    "if (scalers.count != 3 || scalers[0] != \"Blocky\" || scalers[1] != \"Pointy\" || scalers[2] != \"External\") Fiber.abort(\"scaling catalog\")\n"
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
	    "s.listPath = s.listPath\n"
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
	    "if (s.scalingMode != 1) Fiber.abort(\"scaling setting\")\n"
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
exercise_session_history_contract(st_wren_host_t *public_host)
{
	struct st_wren_host *host = public_host;
	static const char source[] =
	    "import \"syncterm_menu\" for Menu\n"
	    "var history = Menu.offlineScrollback\n"
	    "if (history == null || history.width != 2 || history.height != 2) Fiber.abort(\"history surface\")\n"
	    "if (history.cellAt(0, 0).ch != \"A\" || history.cellAt(1, 1).ch != \"D\") Fiber.abort(\"history contents\")\n"
	    "if (!Menu.offlineScrollbackHasStatus || !Menu.prepareOfflineScrollback()) Fiber.abort(\"history metadata\")\n";

	return wrenInterpret(host->vm, "session_history_contract", source)
	    == WREN_RESULT_SUCCESS ? 0 : -1;
}

static int
exercise_settings_save_contract(st_wren_host_t *public_host)
{
	struct st_wren_host *host = public_host;
	static const char source[] =
	    "import \"syncterm_menu\" for Menu\n"
	    "var old = Menu.settings\n"
	    "var current = Menu.settings\n"
	    "current.promptSave = !current.promptSave\n"
	    "if (!current.save() || current.dirty) Fiber.abort(\"save failed\")\n"
	    "var stale = Fiber.new { old.promptSave }.try()\n"
	    "if (!(stale is String)) Fiber.abort(\"save did not invalidate snapshot\")\n";

	return wrenInterpret(host->vm, "settings_save_contract", source)
	    == WREN_RESULT_SUCCESS ? 0 : -1;
}

static int
interpret_bbs_contract(struct st_wren_host *host, const char *module,
    const char *source)
{
	if (wrenInterpret(host->vm, module, source) == WREN_RESULT_SUCCESS)
		return 0;
	fprintf(stderr, "BBS model contract failed in %s: %s\n", module,
	    host->last_error);
	return -1;
}

static int
exercise_bbs_model_contract(st_wren_host_t *public_host,
    struct st_app_paths *paths, const char *personal, const char *shared)
{
	static const char personal_text[] =
	    "[Alpha]\nAddress=alpha.example\nAdded=10\nLastConnected=100\n"
	    "TotalCalls=9\n"
	    "[Zulu]\nAddress=zulu.example\nLastConnected=200\n";
	static const char state_source[] =
	    "import \"syncterm_menu\" for Menu\n"
	    "class BbsContractState {\n"
	    "  static entry { __entry }\n"
	    "  static entry=(value) { __entry = value }\n"
	    "  static transient { __transient }\n"
	    "  static transient=(value) { __transient = value }\n"
	    "  static persisted { __persisted }\n"
	    "  static persisted=(value) { __persisted = value }\n"
	    "}\n"
	    "if (Menu.load(null) != 0) Fiber.abort(\"initial load\")\n"
	    "BbsContractState.entry = Menu.entries[0]\n"
	    "BbsContractState.transient = Menu.quickConnect(\"raw://temporary.example:23\")\n";
	static const char failed_load_source[] =
	    "import \"syncterm_menu\" for Menu\n"
	    "import \"bbs_contract_state\" for BbsContractState\n"
	    "if (Menu.load(null) == 0) Fiber.abort(\"failed load succeeded\")\n"
	    "if (BbsContractState.entry.name != \"Zulu\" || "
	    "BbsContractState.transient.addr != \"temporary.example\") "
	    "Fiber.abort(\"failed load changed model\")\n";
	static const char successful_load_source[] =
	    "import \"syncterm_menu\" for Menu\n"
	    "import \"bbs_contract_state\" for BbsContractState\n"
	    "if (Menu.load(null) != 0) Fiber.abort(\"reload\")\n"
	    "var oldEntry = Fiber.new { BbsContractState.entry.name }.try()\n"
	    "var oldTransient = Fiber.new { BbsContractState.transient.name }.try()\n"
	    "if (!(oldEntry is String) || !(oldTransient is String)) "
	    "Fiber.abort(\"successful load retained old handle\")\n";
	static const char categories_source[] =
	    "import \"syncterm_menu\" for Menu\n"
	    "class BbsLookup {\n"
	    "  static named(name) {\n"
	    "    for (entry in Menu.entries) {\n"
	    "      if (entry.name == name) return entry\n"
	    "    }\n"
	    "    return null\n"
	    "  }\n"
	    "}\n"
	    "if (Menu.load(null) != 0) Fiber.abort(\"category load\")\n"
	    "var system = BbsLookup.named(\"System\")\n"
	    "if (system == null || system.type != 1) Fiber.abort(\"system entry\")\n"
	    "var readOnly = Fiber.new { system.comment = \"forbidden\" }.try()\n"
	    "if (!(readOnly is String) || system.rename(\"Other\") || "
	    "system.save() || system.delete()) Fiber.abort(\"system mutability\")\n"
	    "if (!Menu.nameAvailable(\"System\") || Menu.nameAvailable(\"Alpha\") "
	    "|| Menu.nameAvailable(\"\") || Menu.nameAvailable(\"bad[name\") || "
	    "Menu.nameAvailable(\"syncterm-system-cache\")) "
	    "Fiber.abort(\"name admission\")\n"
	    "if (Menu.create(\"Alpha\") != null || Menu.create(\"\") != null || "
	    "Menu.create(\"bad[name\") != null) Fiber.abort(\"create admission\")\n"
	    "var defaults = Menu.defaults\n"
	    "var defaultsAgain = Menu.defaults\n"
	    "defaults.comment = \"working defaults\"\n"
	    "if (defaultsAgain.comment != \"working defaults\" || !defaults.save() "
	    "|| defaults.dirty) Fiber.abort(\"defaults sharing\")\n"
	    "var transient = Menu.quickConnect(\"raw://quick.example:23\")\n"
	    "transient.comment = \"allowed\"\n"
	    "if (transient.dirty || transient.rename(\"Transient\") || "
	    "transient.save() || transient.delete()) Fiber.abort(\"transient rules\")\n"
	    "var count = Menu.entries.count\n"
	    "var shadow = Menu.create(\"System\")\n"
	    "if (shadow == null || shadow.type != 0 || shadow.calls != 0 || "
	    "shadow.connected != 0 || Menu.entries.count != count) "
	    "Fiber.abort(\"system shadow\")\n"
	    "var transientStale = Fiber.new { transient.name }.try()\n"
	    "var systemStale = Fiber.new { system.name }.try()\n"
	    "var defaultsStale = Fiber.new { defaults.comment }.try()\n"
	    "if (!(transientStale is String) || !(systemStale is String) || "
	    "!(defaultsStale is String)) Fiber.abort(\"create retained old handle\")\n"
	    "var sameShadow = BbsLookup.named(\"System\")\n"
	    "var unrelated = BbsLookup.named(\"Alpha\")\n"
	    "shadow.comment = \"shared record\"\n"
	    "if (sameShadow.comment != \"shared record\") Fiber.abort(\"record sharing\")\n"
	    "if (!shadow.delete()) Fiber.abort(\"discard shadow\")\n"
	    "var shadowStale = Fiber.new { shadow.name }.try()\n"
	    "var unrelatedStale = Fiber.new { unrelated.name }.try()\n"
	    "var restored = BbsLookup.named(\"System\")\n"
	    "if (!(shadowStale is String) || !(unrelatedStale is String) || "
	    "restored == null || restored.type != 1 "
	    "|| restored.addr != \"system.example\") Fiber.abort(\"restore system\")\n"
	    "var scratch = Menu.create(\"Scratch\")\n"
	    "if (scratch == null || !scratch.delete()) Fiber.abort(\"discard draft\")\n"
	    "if (!(Fiber.new { scratch.name }.try() is String)) "
	    "Fiber.abort(\"deleted draft remained usable\")\n";
	static const char save_shadow_source[] =
	    "import \"syncterm_menu\" for Menu\n"
	    "import \"bbs_contract_state\" for BbsContractState\n"
	    "var shadow = Menu.create(\"System\")\n"
	    "shadow.comment = \"persisted shadow\"\n"
	    "if (!shadow.save() || shadow.dirty || shadow.comment != "
	    "\"persisted shadow\") Fiber.abort(\"save shadow\")\n"
	    "BbsContractState.persisted = shadow\n";
	static const char delete_shadow_source[] =
	    "import \"syncterm_menu\" for Menu\n"
	    "import \"bbs_contract_state\" for BbsContractState\n"
	    "var shadow = BbsContractState.persisted\n"
	    "if (!shadow.delete()) Fiber.abort(\"delete shadow\")\n"
	    "if (!(Fiber.new { shadow.name }.try() is String)) "
	    "Fiber.abort(\"deleted shadow remained usable\")\n"
	    "var restored = null\n"
	    "for (entry in Menu.entries) {\n"
	    "  if (entry.name == \"System\") restored = entry\n"
	    "}\n"
	    "if (restored == null || restored.type != 1) Fiber.abort(\"system not restored\")\n";
	static const char save_copy_source[] =
	    "import \"syncterm_menu\" for Menu\n"
	    "import \"bbs_contract_state\" for BbsContractState\n"
	    "var alpha = null\n"
	    "for (entry in Menu.entries) {\n"
	    "  if (entry.name == \"Alpha\") alpha = entry\n"
	    "}\n"
	    "if (alpha == null || alpha.calls != 9 || alpha.connected != 100) "
	    "Fiber.abort(\"copy source\")\n"
	    "var copy = Menu.copy(alpha, \"Copy\")\n"
	    "if (copy == null || copy.calls != 0 || copy.connected != 0 || "
	    "copy.added <= 0) Fiber.abort(\"copy reset\")\n"
	    "var same = null\n"
	    "for (entry in Menu.entries) {\n"
	    "  if (entry.name == \"Copy\") same = entry\n"
	    "}\n"
	    "copy.comment = \"shared copy\"\n"
	    "if (same.comment != \"shared copy\" || !copy.rename(\"Aardvark\") "
	    "|| same.name != \"Aardvark\" || !Menu.sort() || "
	    "same.name != \"Aardvark\" || !copy.save() || copy.dirty) "
	    "Fiber.abort(\"copy lifecycle\")\n"
	    "BbsContractState.persisted = copy\n";
	static const char delete_copy_source[] =
	    "import \"bbs_contract_state\" for BbsContractState\n"
	    "var copy = BbsContractState.persisted\n"
	    "if (copy.name != \"Aardvark\" || !copy.delete()) "
	    "Fiber.abort(\"delete persisted copy\")\n"
	    "if (!(Fiber.new { copy.name }.try() is String)) "
	    "Fiber.abort(\"persisted delete retained handle\")\n";
	struct st_wren_host *host = public_host;
	char *saved_personal = paths->personal_list;
	char *saved_list_path = st_settings.list_path;
	bool saved_download_system_list = st_settings.download_system_list;

	st_settings.list_path = NULL;
	st_settings.download_system_list = false;
	CHECK(write_file(personal, personal_text) == 0);
	CHECK(write_file(shared,
	    "[System]\nAddress=system.example\nUserName=ignored\n") == 0);
	CHECK(interpret_bbs_contract(host, "bbs_contract_state", state_source) == 0);
	paths->personal_list = ST_WREN_TEST_DIRECTORY;
	CHECK(interpret_bbs_contract(host, "bbs_failed_load", failed_load_source)
	    == 0);
	paths->personal_list = saved_personal;
	CHECK(interpret_bbs_contract(host, "bbs_successful_load",
	    successful_load_source) == 0);
	CHECK(interpret_bbs_contract(host, "bbs_categories", categories_source) == 0);
	CHECK(!file_contains(personal, "[Scratch]"));
	CHECK(interpret_bbs_contract(host, "bbs_save_shadow", save_shadow_source)
	    == 0);
	CHECK(file_contains(personal, "[System]"));
	CHECK(file_contains(personal, "Comment=persisted shadow"));
	CHECK(interpret_bbs_contract(host, "bbs_delete_shadow",
	    delete_shadow_source) == 0);
	CHECK(!file_contains(personal, "[System]"));
	CHECK(interpret_bbs_contract(host, "bbs_save_copy", save_copy_source) == 0);
	CHECK(file_contains(personal, "[Aardvark]"));
	CHECK(interpret_bbs_contract(host, "bbs_delete_copy", delete_copy_source)
	    == 0);
	CHECK(!file_contains(personal, "[Aardvark]"));

	CHECK(write_file(personal,
	    "[Alpha]\nAddress=alpha.example\nLastConnected=100\n"
	    "[Zulu]\nAddress=zulu.example\nLastConnected=200\n") == 0);
	CHECK(write_file(shared, "") == 0);
	CHECK(interpret_bbs_contract(host, "bbs_contract_restore",
	    "import \"syncterm_menu\" for Menu\n"
	    "if (Menu.load(null) != 0 || Menu.entries.count != 2) "
	    "Fiber.abort(\"restore\")\n") == 0);
	st_settings.list_path = saved_list_path;
	st_settings.download_system_list = saved_download_system_list;
	return 0;
}

static int
exercise_build_options_contract(st_wren_host_t *public_host)
{
	struct st_wren_host *host = public_host;
	static const char source[] =
	    "import \"syncterm\" for PixelBuffer\n"
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
	    "if (options[\"JPEG XL support\"] != PixelBuffer.jxlSupported) "
	    "Fiber.abort(\"JPEG XL build option\")\n"
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
exercise_pixel_contract(st_wren_host_t *public_host)
{
	struct st_wren_host *host = public_host;
	static const char source[] =
	    "import \"pixel_vm_test\" for PixelVmTest\n"
	    "var result = PixelVmTest.run()\n"
	    "if (result[1] != 0) Fiber.abort(\"pixel contract failures: %(result[1])\")\n";
	WrenVM *vms[] = {host->vm, host->picker_vm};

	for (size_t i = 0; i < sizeof(vms) / sizeof(vms[0]); ++i) {
		char module[48];

		(void)snprintf(module, sizeof(module), "pixel_contract_%zu", i);
		if (wrenInterpret(vms[i], module, source) != WREN_RESULT_SUCCESS) {
			fprintf(stderr, "pixel contract failed in VM %zu: %s\n", i,
			    host->last_error);
			return -1;
		}
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
exercise_rectangle_contract(st_wren_host_t *public_host)
{
	struct st_wren_host *host = public_host;
	static const char copy_source[] =
	    "import \"syncterm\" for Cell, Screen, Surface\n"
	    "import \"ui_widget\" for Rect\n"
	    "class RectangleContract {\n"
	    "  static row(s, y) {\n"
	    "    var text = \"\"\n"
	    "    for (x in 0...s.width) text = text + s.cellAt(x, y).ch\n"
	    "    return text\n"
	    "  }\n"
	    "  static setRow(s, y, text) {\n"
	    "    for (x in 0...text.count) s.cellAt(x, y).ch = text[x]\n"
	    "  }\n"
	    "  static expect(s, y, text, label) {\n"
	    "    if (row(s, y) != text) Fiber.abort(label)\n"
	    "  }\n"
	    "}\n"
	    "var s = Surface.new(5, 3)\n"
	    "RectangleContract.setRow(s, 0, \"ABCDE\")\n"
	    "s.putRect(s, 0, 0)\n"
	    "RectangleContract.expect(s, 0, \"ABCDE\", \"identity copy\")\n"
	    "s.putRect(s, Rect.new(0, 0, 4, 1), 1, 0)\n"
	    "RectangleContract.expect(s, 0, \"AABCD\", \"right overlap\")\n"
	    "RectangleContract.setRow(s, 0, \"ABCDE\")\n"
	    "s.putRect(s, Rect.new(1, 0, 4, 1), 0, 0)\n"
	    "RectangleContract.expect(s, 0, \"BCDEE\", \"left overlap\")\n"
	    "RectangleContract.setRow(s, 0, \"AAAAA\")\n"
	    "RectangleContract.setRow(s, 1, \"BBBBB\")\n"
	    "RectangleContract.setRow(s, 2, \"CCCCC\")\n"
	    "s.putRect(s, Rect.new(0, 0, 5, 2), 0, 1)\n"
	    "RectangleContract.expect(s, 0, \"AAAAA\", \"down row zero\")\n"
	    "RectangleContract.expect(s, 1, \"AAAAA\", \"down row one\")\n"
	    "RectangleContract.expect(s, 2, \"BBBBB\", \"down row two\")\n"
	    "RectangleContract.setRow(s, 0, \"AAAAA\")\n"
	    "RectangleContract.setRow(s, 1, \"BBBBB\")\n"
	    "RectangleContract.setRow(s, 2, \"CCCCC\")\n"
	    "s.putRect(s, Rect.new(0, 1, 5, 2), 0, 0)\n"
	    "RectangleContract.expect(s, 0, \"BBBBB\", \"up row zero\")\n"
	    "RectangleContract.expect(s, 1, \"CCCCC\", \"up row one\")\n"
	    "RectangleContract.expect(s, 2, \"CCCCC\", \"up row two\")\n"
	    "var src = Surface.new(3, 1)\n"
	    "var dst = Surface.new(4, 1)\n"
	    "RectangleContract.setRow(src, 0, \"ABC\")\n"
	    "RectangleContract.setRow(dst, 0, \"zzzz\")\n"
	    "dst.putRect(src, Rect.new(-1, 0, 3, 1), 0, 0)\n"
	    "RectangleContract.expect(dst, 0, \"zABz\", \"source clipping\")\n"
	    "RectangleContract.setRow(dst, 0, \"zzzz\")\n"
	    "dst.putRect(src, -1, 0)\n"
	    "RectangleContract.expect(dst, 0, \"BCzz\", \"destination clipping\")\n"
	    "var wide = Surface.new(4, 1)\n"
	    "wide.cellAt(1, 0).legacyAttr = 0x1e\n"
	    "wide.cellAt(1, 0).ch = \"\\u4e00\"\n"
	    "RectangleContract.setRow(dst, 0, \"abcd\")\n"
	    "dst.putRect(wide, Rect.new(2, 0, 1, 1), 1, 0)\n"
	    "if (dst.cellAt(1, 0).ch != \" \" || dst.cellAt(1, 0).legacyAttr != 0x1e) Fiber.abort(\"clipped continuation\")\n"
	    "if (dst.cellAt(0, 0).ch != \"a\" || dst.cellAt(2, 0).ch != \"c\") Fiber.abort(\"continuation changed outside\")\n"
	    "RectangleContract.setRow(dst, 0, \"abcd\")\n"
	    "dst.putRect(wide, Rect.new(1, 0, 1, 1), 1, 0)\n"
	    "if (dst.cellAt(1, 0).ch != \" \" || dst.cellAt(1, 0).legacyAttr != 0x1e) Fiber.abort(\"clipped wide lead\")\n"
	    "if (dst.cellAt(0, 0).ch != \"a\" || dst.cellAt(2, 0).ch != \"c\") Fiber.abort(\"wide lead changed outside\")\n"
	    "var narrow = Surface.new(1, 1)\n"
	    "narrow[0].ch = \"X\"\n"
	    "RectangleContract.setRow(dst, 0, \"abcd\")\n"
	    "dst.cellAt(1, 0).ch = \"\\u4e00\"\n"
	    "dst.putRect(narrow, 2, 0)\n"
	    "if (dst.cellAt(1, 0).ch != \" \" || dst.cellAt(2, 0).ch != \"X\") Fiber.abort(\"overwrite continuation\")\n"
	    "RectangleContract.setRow(dst, 0, \"abcd\")\n"
	    "dst.cellAt(1, 0).ch = \"\\u4e00\"\n"
	    "dst.putRect(narrow, 1, 0)\n"
	    "if (dst.cellAt(1, 0).ch != \"X\" || dst.cellAt(2, 0).ch != \" \") Fiber.abort(\"overwrite wide lead\")\n";
	static const char rectangle_source[] =
	    "import \"syncterm\" for Cell, Screen, Surface\n"
	    "import \"ui_widget\" for Rect\n"
	    "class FillContract {\n"
	    "  static row(s, y) {\n"
	    "    var text = \"\"\n"
	    "    for (x in 0...s.width) text = text + s.cellAt(x, y).ch\n"
	    "    return text\n"
	    "  }\n"
	    "}\n"
	    "var fill = Surface.new(4, 3)\n"
	    "var dot = Cell.new()\n"
	    "dot.ch = \".\"\n"
	    "var xcell = Cell.new()\n"
	    "xcell.ch = \"X\"\n"
	    "fill.fill(Rect.new(0, 0, 4, 3), dot)\n"
	    "fill.fill(Rect.new(-1, 0, 2, 1), xcell)\n"
	    "if (FillContract.row(fill, 0) != \"X...\") Fiber.abort(\"negative x fill\")\n"
	    "fill.fill(Rect.new(0, -1, 1, 2), xcell)\n"
	    "if (fill.cellAt(0, 0).ch != \"X\" || fill.cellAt(0, 1).ch != \".\") Fiber.abort(\"negative y fill\")\n"
	    "fill.fill(Rect.new(3, 2, 3, 3), xcell)\n"
	    "if (fill.cellAt(3, 2).ch != \"X\" || fill.cellAt(2, 2).ch != \".\") Fiber.abort(\"far edge fill\")\n"
	    "fill.fill(Rect.new(0, 0, 0, 2), xcell)\n"
	    "fill.fill(Rect.new(0, 0, -1, 2), xcell)\n"
	    "fill.fill(Rect.new(-4, -4, 2, 2), xcell)\n"
	    "if (fill.cellAt(1, 0).ch != \".\" || fill.cellAt(0, 1).ch != \".\") Fiber.abort(\"empty fill\")\n"
	    "var read = Screen.readRect(1, 1, 1, 1)\n"
	    "if (read == null || read.width != 1 || read.height != 1) Fiber.abort(\"valid readRect\")\n"
	    "if (Screen.readRect(0, 1, 1, 1) != null || Screen.readRect(-1, 1, 1, 1) != null) Fiber.abort(\"invalid read origin\")\n"
	    "if (Screen.readRect(2, 1, 1, 1) != null || Screen.readRect(1, 2, 1, 1) != null) Fiber.abort(\"inverted read\")\n"
	    "if (Screen.readRect(81, 1, 81, 1) != null || Screen.readRect(1, 26, 1, 26) != null) Fiber.abort(\"out-of-bounds read\")\n"
	    "if (Screen.readRect(1, 1, 4294967296, 1) != null) Fiber.abort(\"unrepresentable read\")\n"
	    "var wrongType = Fiber.new { Screen.readRect(\"1\", 1, 1, 1) }\n"
	    "wrongType.try()\n"
	    "if (wrongType.error == null) Fiber.abort(\"readRect type error\")\n"
	    "var fractional = Fiber.new { Screen.readRect(1.5, 1, 1, 1) }\n"
	    "fractional.try()\n"
	    "if (fractional.error == null) Fiber.abort(\"readRect fractional error\")\n";
	WrenVM *vms[] = {host->vm, host->picker_vm};

	for (size_t i = 0; i < sizeof(vms) / sizeof(vms[0]); ++i) {
		char module[48];

		(void)snprintf(module, sizeof(module), "surface_copy_contract_%zu", i);
		if (wrenInterpret(vms[i], module, copy_source)
		    != WREN_RESULT_SUCCESS) {
			fprintf(stderr, "Surface copy contract failed in VM %zu: %s\n",
			    i, host->last_error);
			return -1;
		}
		(void)snprintf(module, sizeof(module), "rectangle_contract_%zu", i);
		if (wrenInterpret(vms[i], module, rectangle_source)
		    != WREN_RESULT_SUCCESS) {
			fprintf(stderr, "rectangle contract failed in VM %zu: %s\n",
			    i, host->last_error);
			return -1;
		}
	}
	return 0;
}

static int
exercise_core_value_contracts(st_wren_host_t *public_host,
	st_console_t *console)
{
	struct st_wren_host *host = public_host;
	static const char source[] =
	    "import \"syncterm\" for Cell, Format, Screen, Surface\n"
	    "var left = Cell.new()\n"
	    "var right = Cell.new()\n"
	    "if (!left.eqContent(right)) Fiber.abort(\"equal cells\")\n"
	    "if (left.eqContent(null) || left.eqContent(1) || left.eqContent(\"cell\") || left.eqContent(Surface.new(1, 1))) Fiber.abort(\"non-Cell equality\")\n"
	    "right.ch = \"A\"\n"
	    "if (left.eqContent(right)) Fiber.abort(\"unequal cells\")\n"
	    "var pixel = Screen.readRect(1, 1, 1, 1).cellAt(0, 0)\n"
	    "if (!pixel.pixelGraphics || pixel.eqContent(pixel) || left.eqContent(pixel)) Fiber.abort(\"pixel cell equality\")\n"
	    "var surface = Surface.new(1, 1)\n"
	    "var below = Fiber.new { surface.iterate(9223372036854774784) }\n"
	    "var belowResult = below.try()\n"
	    "if (below.error != null || belowResult != false) Fiber.abort(\"integer below 2^63\")\n"
	    "var at = Fiber.new { surface.iterate(9223372036854775808) }\n"
	    "at.try()\n"
	    "if (at.error == null) Fiber.abort(\"integer at 2^63\")\n"
	    "var above = Fiber.new { surface.iterate(18446744073709551616) }\n"
	    "above.try()\n"
	    "if (above.error == null) Fiber.abort(\"integer above 2^63\")\n"
	    "var fractional = Fiber.new { surface.iterate(1.5) }\n"
	    "fractional.try()\n"
	    "if (fractional.error == null) Fiber.abort(\"fractional integer\")\n"
	    "var nan = Fiber.new { surface.iterate(Num.nan) }\n"
	    "nan.try()\n"
	    "if (nan.error == null) Fiber.abort(\"NaN integer\")\n"
	    "var infinity = Fiber.new { surface.iterate(Num.infinity) }\n"
	    "infinity.try()\n"
	    "if (infinity.error == null) Fiber.abort(\"infinite integer\")\n"
	    "if (Format.bytes(-1) != \"0\" || Format.bytes(42) != \"42\" || Format.bytes(1536) != \"1.5K\" || Format.bytes(1023.9) != \"1023\") Fiber.abort(\"byte format\")\n"
	    "if (Format.bytes(Num.nan) != \"0\" || Format.bytes(Num.infinity) != \"16384.0P\") Fiber.abort(\"non-finite byte format\")\n"
	    "if (Format.duration(-1) != \"0s\" || Format.duration(8.25) != \"8.2s\" || Format.duration(90) != \"1.5m\" || Format.duration(10800) != \"3.0h\") Fiber.abort(\"duration format\")\n"
	    "if (Format.duration(Num.nan) != \"0s\" || Format.duration(Num.infinity) != \"infy\") Fiber.abort(\"non-finite duration format\")\n"
	    "var byteType = Fiber.new { Format.bytes(\"1\") }\n"
	    "byteType.try()\n"
	    "if (byteType.error == null) Fiber.abort(\"byte type\")\n"
	    "var durationType = Fiber.new { Format.duration(null) }\n"
	    "durationType.try()\n"
	    "if (durationType.error == null) Fiber.abort(\"duration type\")\n";

	CHECK(st_console_set_pixel(console, 0, 0,
	    ST_PIXEL_COLOR_RGB_VALUE(0x12, 0x34, 0x56)) == 0);
	if (wrenInterpret(host->vm, "core_value_contract", source)
	    != WREN_RESULT_SUCCESS) {
		fprintf(stderr, "core value contract failed: %s\n", host->last_error);
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
	    "var saved = CustomCursor.current\n"
	    "var normal = CustomCursor.normal\n"
	    "if (normal.startLine != 14 || normal.endLine != 15 || normal.range != 16 || !normal.visible || !normal.blink) Fiber.abort(\"normal cursor\")\n"
	    "var solid = CustomCursor.solid\n"
	    "if (solid.startLine != 0 || solid.endLine != 15 || solid.range != 16 || !solid.visible || solid.blink) Fiber.abort(\"solid cursor\")\n"
	    "CustomCursor.startLine = 1\n"
	    "CustomCursor.endLine = 3\n"
	    "CustomCursor.range = 32\n"
	    "CustomCursor.blink = false\n"
	    "CustomCursor.visible = false\n"
	    "if (CustomCursor.startLine != 1 || CustomCursor.endLine != 3 || CustomCursor.range != 32 || CustomCursor.blink || CustomCursor.visible) Fiber.abort(\"live cursor\")\n"
	    "var staged = CustomCursor.current\n"
	    "staged.startLine = 2\n"
	    "staged.endLine = 5\n"
	    "staged.range = 16\n"
	    "staged.blink = true\n"
	    "staged.visible = true\n"
	    "staged.apply()\n"
	    "if (CustomCursor.startLine != 2 || CustomCursor.endLine != 5 || !CustomCursor.blink || !CustomCursor.visible) Fiber.abort(\"staged cursor\")\n"
	    "var invalid = CustomCursor.current\n"
	    "invalid.range = 0\n"
	    "var rejected = Fiber.new { invalid.apply() }\n"
	    "rejected.try()\n"
	    "if (rejected.error == null || CustomCursor.range != 16) Fiber.abort(\"invalid cursor\")\n"
	    "var wrongType = Fiber.new { CustomCursor.visible = 1 }\n"
	    "wrongType.try()\n"
	    "if (wrongType.error == null) Fiber.abort(\"cursor Boolean type\")\n"
	    "saved.apply()\n";

	if (wrenInterpret(host->vm, "cursor_contract", source)
	    != WREN_RESULT_SUCCESS) {
		fprintf(stderr, "cursor contract failed: %s\n", host->last_error);
		return -1;
	}
	return 0;
}

static int
exercise_video_colour_contract(st_wren_host_t *public_host)
{
	struct st_wren_host *host = public_host;
	static const char source[] =
	    "import \"syncterm\" for Cell, Color, Screen, ScreenSupports, VideoFlags\n"
	    "var saved = VideoFlags.current\n"
	    "var savedScreen = Screen.save()\n"
	    "var expansion = VideoFlags.expand\n"
	    "VideoFlags.altChars = false\n"
	    "VideoFlags.noBright = false\n"
	    "VideoFlags.bgBright = false\n"
	    "VideoFlags.blinkAltChars = false\n"
	    "VideoFlags.noBlink = false\n"
	    "VideoFlags.lineGraphicsExpand = false\n"
	    "if (VideoFlags.altChars || VideoFlags.noBright || VideoFlags.bgBright || VideoFlags.blinkAltChars || VideoFlags.noBlink || VideoFlags.lineGraphicsExpand || VideoFlags.expand != expansion) Fiber.abort(\"live video flags\")\n"
	    "Screen.font[2] = 65535\n"
	    "VideoFlags.altChars = true\n"
	    "VideoFlags.altChars = false\n"
	    "Screen.font[1] = 0\n"
	    "Screen.font[2] = 1\n"
	    "Screen.font[3] = 2\n"
	    "Screen.font[4] = 3\n"
	    "VideoFlags.altChars = true\n"
	    "VideoFlags.blinkAltChars = true\n"
	    "Screen.window.position = [1, 1]\n"
	    "Screen.attr = 0x08\n"
	    "Screen.window.putChar(65)\n"
	    "Screen.attr = 0x80\n"
	    "Screen.window.putChar(65)\n"
	    "Screen.attr = 0x88\n"
	    "Screen.window.putChar(65)\n"
	    "var selected = Screen.readRect(1, 1, 3, 1)\n"
	    "if (selected.cellAt(0, 0).font != 1 || selected.cellAt(1, 0).font != 2 || selected.cellAt(2, 0).font != 3) Fiber.abort(\"attribute font selection\")\n"
	    "VideoFlags.altChars = false\n"
	    "VideoFlags.blinkAltChars = false\n"
	    "if (Screen.readRect(1, 1, 1, 1).cellAt(0, 0).font != 1) Fiber.abort(\"stored font identity\")\n"
	    "Screen.restore(savedScreen)\n"
	    "if (Screen.font[1] != 0 || Screen.font[2] != 0 || Screen.font[3] != 0 || Screen.font[4] != 0 || VideoFlags.altChars || VideoFlags.blinkAltChars) Fiber.abort(\"screen display-state restore\")\n"
	    "var staged = VideoFlags.current\n"
	    "staged.noBright = true\n"
	    "staged.bgBright = true\n"
	    "staged.noBlink = true\n"
	    "staged.apply()\n"
	    "if (!VideoFlags.noBright || !VideoFlags.bgBright || !VideoFlags.noBlink || VideoFlags.expand != expansion) Fiber.abort(\"staged video flags\")\n"
	    "VideoFlags.noBright = false\n"
	    "VideoFlags.bgBright = false\n"
	    "VideoFlags.noBlink = false\n"
	    "var colours = Color.fromAttr(0x1f)\n"
	    "if (colours[0] != 0xffffff || colours[1] != 0x0000a8) Fiber.abort(\"attribute RGB conversion\")\n"
	    "if (Color.toLegacyAttr(0xffffff, 0) != 0x0f) Fiber.abort(\"RGB attribute conversion\")\n"
	    "var old4 = Screen.palette[4]\n"
	    "var old12 = Screen.palette[12]\n"
	    "Screen.palette[4] = 0x123456\n"
	    "Screen.palette[12] = 0x654321\n"
	    "if (Color.fromAttr(0x01)[0] != 0x123456 || Color.toLegacyAttr(0x123456, 0) != 0x01) Fiber.abort(\"active foreground palette conversion\")\n"
	    "VideoFlags.bgBright = true\n"
	    "if (Color.fromAttr(0x90)[1] != 0x654321 || Color.toLegacyAttr(0, 0x654321) != 0x90) Fiber.abort(\"active background palette conversion\")\n"
	    "VideoFlags.bgBright = false\n"
	    "Screen.palette[4] = old4\n"
	    "Screen.palette[12] = old12\n"
	    "var cell = Cell.new()\n"
	    "cell.legacyAttr = 0x9f\n"
	    "if (cell.legacyAttr != 0x9f || !cell.bright || !cell.blink) Fiber.abort(\"legacy cell state\")\n"
	    "cell.fgRgb = 0xa8a8a8\n"
	    "if (!cell.bright || !cell.blink || cell.fgRgb != 0xa8a8a8) Fiber.abort(\"explicit RGB state\")\n"
	    "if (!ScreenSupports.altBlinkFont || !ScreenSupports.altBoldFont || !ScreenSupports.brightBackground) Fiber.abort(\"screen capabilities\")\n"
	    "var wrongType = Fiber.new { VideoFlags.noBlink = 1 }\n"
	    "wrongType.try()\n"
	    "if (wrongType.error == null) Fiber.abort(\"video Boolean type\")\n"
	    "saved.apply()\n"
	    "Screen.font[1] = 0\n"
	    "Screen.font[2] = 0\n"
	    "Screen.font[3] = 0\n"
	    "Screen.font[4] = 0\n";

	if (wrenInterpret(host->vm, "video_colour_contract", source)
	    != WREN_RESULT_SUCCESS) {
		fprintf(stderr, "video/colour contract failed: %s\n",
		    host->last_error);
		return -1;
	}
	return 0;
}

static int
exercise_font_slot_contract(st_wren_host_t *public_host)
{
	struct st_wren_host *host = public_host;
	static const char source[] =
	    "import \"syncterm\" for Screen, Surface, VideoFlags\n"
	    "var saved = Screen.save()\n"
	    "VideoFlags.altChars = false\n"
	    "VideoFlags.blinkAltChars = false\n"
	    "Screen.window.bounds = [1, 2, 5, 6]\n"
	    "Screen.font[1] = 0\n"
	    "Screen.attr = 0x07\n"
	    "Screen.font[1] = 4\n"
	    "Screen.window.clear()\n"
	    "if (Screen.readRect(1, 2, 1, 2).cellAt(0, 0).font != 4) Fiber.abort(\"clear font selection\")\n"
	    "VideoFlags.altChars = true\n"
	    "Screen.font[2] = 0\n"
	    "Screen.attr = 0x08\n"
	    "Screen.font[2] = 5\n"
	    "Screen.window.position = [2, 3]\n"
	    "Screen.window.clearToLineEnd()\n"
	    "if (Screen.readRect(2, 3, 2, 3).cellAt(0, 0).font != 5) Fiber.abort(\"clear-line font selection\")\n"
	    "VideoFlags.blinkAltChars = true\n"
	    "Screen.font[3] = 0\n"
	    "Screen.attr = 0x80\n"
	    "Screen.font[3] = 6\n"
	    "Screen.window.position = [1, 4]\n"
	    "Screen.window.insertLine()\n"
	    "if (Screen.readRect(1, 4, 1, 4).cellAt(0, 0).font != 6) Fiber.abort(\"insert-line font selection\")\n"
	    "Screen.font[4] = 0\n"
	    "Screen.attr = 0x88\n"
	    "Screen.font[4] = 7\n"
	    "Screen.window.position = [1, 4]\n"
	    "Screen.window.deleteLine()\n"
	    "if (Screen.readRect(1, 6, 1, 6).cellAt(0, 0).font != 7) Fiber.abort(\"delete-line font selection\")\n"
	    "VideoFlags.altChars = false\n"
	    "VideoFlags.blinkAltChars = false\n"
	    "Screen.font[1] = 0\n"
	    "Screen.attr = 0x07\n"
	    "Screen.font[1] = 8\n"
	    "Screen.window.scroll()\n"
	    "if (Screen.readRect(1, 6, 1, 6).cellAt(0, 0).font != 8) Fiber.abort(\"scroll font selection\")\n"
	    "Screen.font[1] = 10\n"
	    "if (Screen.readRect(1, 6, 1, 6).cellAt(0, 0).font != 8) Fiber.abort(\"retained font identity\")\n"
	    "VideoFlags.altChars = true\n"
	    "VideoFlags.blinkAltChars = true\n"
	    "var explicit = Surface.new(1, 1)\n"
	    "explicit.cellAt(0, 0).ch = \"A\"\n"
	    "explicit.cellAt(0, 0).font = 9\n"
	    "explicit.cellAt(0, 0).legacyAttr = 0x88\n"
	    "Screen.putRect(explicit, 5, 2)\n"
	    "Screen.font[4] = 10\n"
	    "if (Screen.readRect(5, 2, 5, 2).cellAt(0, 0).font != 9) Fiber.abort(\"explicit font identity\")\n"
	    "Screen.restore(saved)\n";

	if (wrenInterpret(host->vm, "font_slot_contract", source)
	    != WREN_RESULT_SUCCESS) {
		fprintf(stderr, "font slot contract failed: %s\n",
		    host->last_error);
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
	    "var duplicate = Fiber.new { customFont.name = \"configured\" }\n"
	    "duplicate.try()\n"
	    "if (duplicate.error == null || customFont.name != \"Temporary\") Fiber.abort(\"font duplicate\")\n"
	    "customFont.name = \"Renamed\"\n"
	    "if (customFont.name != \"Renamed\" || !customFont.clearFile(2)) Fiber.abort(\"font edit\")\n"
	    "if (!customFont.delete() || Menu.fonts.count != 1 || !Menu.saveFonts()) Fiber.abort(\"font delete\")\n"
	    "var unsaved = Menu.createFont(\"Unsaved\", 1)\n"
	    "if (unsaved == null || !Menu.reloadFonts() || Menu.fonts.count != 1 || Menu.fontsDirty) Fiber.abort(\"font reload\")\n"
	    "var stale = Fiber.new { unsaved.name }\n"
	    "stale.try()\n"
	    "if (stale.error == null) Fiber.abort(\"font reload handle\")\n";

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
	    "if (saved[0] != 0 || saved[1] != null || document.dirty || document.canUndo || document.canRedo) Fiber.abort(\"document save\")\n"
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
exercise_selected_local_theme_delete_failure(st_wren_host_t *public_host,
	const char *path, const char *settings_path)
{
	struct st_wren_host *host = public_host;
	const struct st_theme *active;
	static const char select_source[] =
	    "import \"syncterm_menu\" for Menu\n"
	    "var oldSettings = Menu.settings\n"
	    "if (Menu.selectTheme(\"host-theme.ini\") != null || "
	    "Menu.selectedThemeFile != \"host-theme.ini\") "
	    "Fiber.abort(\"select local theme\")\n"
	    "var stale = Fiber.new { oldSettings.confirmClose }.try()\n"
	    "if (!(stale is String)) Fiber.abort(\"theme selection did not invalidate settings\")\n";
	static const char delete_source[] =
	    "import \"syncterm_menu\" for Menu\n"
	    "if (Menu.deleteTheme(\"host-theme.ini\") == null) "
	    "Fiber.abort(\"local delete unexpectedly succeeded\")\n"
	    "if (Menu.selectedThemeFile != \"\" || "
	    "Menu.selectedThemePackage != \"\") "
	    "Fiber.abort(\"local delete restored stale selection\")\n";

	CHECK(wrenInterpret(host->vm, "select_local_delete_failure", select_source)
	    == WREN_RESULT_SUCCESS);
	CHECK(fs_remove_file(path) == 0);
	CHECK(fs_create_directories(path) == 0);
	CHECK(wrenInterpret(host->vm, "selected_local_delete_failure",
	    delete_source) == WREN_RESULT_SUCCESS);
	active = st_theme_active(host->themes);
	CHECK(active != NULL && active->filename != NULL
	    && active->filename[0] == '\0' && active->package == NULL);
	CHECK(st_settings.theme_file == NULL && st_settings.theme_package == NULL);
	CHECK(!file_contains(settings_path, "ThemeFile=host-theme.ini"));
	CHECK(fs_remove_directory(path) == 0);
	CHECK(write_file(path,
	    "Name=Host Theme\n[Style:frame]\nForeground=0x123456\n") == 0);
	return 0;
}

static int
exercise_selected_cloud_theme_delete_failure(st_wren_host_t *public_host,
	const char *directory, const char *manifest, const char *path,
	const char *settings_path)
{
	struct st_wren_host *host = public_host;
	const struct st_theme *active;
	char warning[256] = "";
	static const char select_source[] =
	    "import \"syncterm_menu\" for Menu\n"
	    "if (Menu.selectCloudTheme(\"official/blue\") != null || "
	    "Menu.selectedThemePackage != \"official/blue\") "
	    "Fiber.abort(\"select cloud theme\")\n";
	static const char delete_source[] =
	    "import \"syncterm_menu\" for Menu\n"
	    "if (Menu.deleteCloudTheme(\"official/blue\") == null) "
	    "Fiber.abort(\"cloud delete unexpectedly succeeded\")\n"
	    "if (Menu.selectedThemeFile != \"\" || "
	    "Menu.selectedThemePackage != \"\") "
	    "Fiber.abort(\"cloud delete restored stale selection\")\n";

	CHECK(fs_create_directories(directory) == 0);
	CHECK(write_file(path,
	    "Name=Cloud Theme\n[Style:frame]\nForeground=0x654321\n") == 0);
	CHECK(write_file(manifest,
	    "Repository=official\n\n[Theme:blue]\nName=Cloud Theme\n"
	    "URI=https://example.invalid/blue.ini\n"
	    "SHA256=1891d225f61ef1aaa8df82e47f540561a4829fb5cc62535a34b1454f9bb0bfd5\n")
	    == 0);
	CHECK(st_theme_cloud_refresh(host->themes, false, "", warning,
	    sizeof(warning)) == 0);
	CHECK(wrenInterpret(host->vm, "select_cloud_delete_failure", select_source)
	    == WREN_RESULT_SUCCESS);
	CHECK(fs_remove_file(path) == 0);
	CHECK(fs_create_directories(path) == 0);
	CHECK(wrenInterpret(host->vm, "selected_cloud_delete_failure",
	    delete_source) == WREN_RESULT_SUCCESS);
	active = st_theme_active(host->themes);
	CHECK(active != NULL && active->filename != NULL
	    && active->filename[0] == '\0' && active->package == NULL);
	CHECK(st_settings.theme_file == NULL && st_settings.theme_package == NULL);
	CHECK(!file_contains(settings_path, "ThemePackage=official/blue"));
	CHECK(fs_remove_directory(path) == 0);
	CHECK(write_file(path,
	    "Name=Cloud Theme\n[Style:frame]\nForeground=0x654321\n") == 0);
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
	    "Input.enableMouseEvent(Mouse.wheelDown)\n"
	    "if (Input.mouseEvents != 140737488355328) Fiber.abort(\"wide mouse mask\")\n"
	    "Input.disableMouseEvent(Mouse.wheelDown)\n"
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
	CHECK(!host->menu.mouse_gestures[0].pressed);
	CHECK(!host->menu.mouse_gestures[0].dragging);
	return 0;
}

static int
exercise_input_value_contract(struct st_wren_host *host,
	st_console_t *console)
{
	static const char values[] =
	    "import \"syncterm\" for Input, KeyEvent, Mouse, MouseEvent, PhysicalKeyEvent\n"
	    "var key = KeyEvent.new(4660)\n"
	    "Input.unget(key)\n"
	    "var gotKey = Input.poll()\n"
	    "if (!(gotKey is KeyEvent) || gotKey.code != 4660) Fiber.abort(\"key unget\")\n"
	    "var physical = PhysicalKeyEvent.new(77, true, 5, true)\n"
	    "Input.unget(physical)\n"
	    "var gotPhysical = Input.poll()\n"
	    "if (!(gotPhysical is PhysicalKeyEvent) || gotPhysical.evdev != 77 || !gotPhysical.pressed || gotPhysical.modifiers != 5 || !gotPhysical.repeat) Fiber.abort(\"physical unget\")\n"
	    "var mouse = MouseEvent.withPixelsAndWheel(Mouse.wheelLeft, 2, 3, 4, 5, 6, 9, 10, 11, 12, 13, -3, 0)\n"
	    "Input.unget(mouse)\n"
	    "var gotMouse = Input.poll()\n"
	    "if (!(gotMouse is MouseEvent) || gotMouse.event != Mouse.wheelLeft || gotMouse.startX != 2 || gotMouse.startY != 3 || gotMouse.endX != 4 || gotMouse.endY != 5 || gotMouse.modifiers != 6 || gotMouse.bstate != 9 || gotMouse.startPixelX != 10 || gotMouse.startPixelY != 11 || gotMouse.endPixelX != 12 || gotMouse.endPixelY != 13 || gotMouse.wheelX != -3 || gotMouse.wheelY != 0) Fiber.abort(\"mouse unget\")\n"
	    "var invalid = Fiber.new { Input.unget(42) }\n"
	    "invalid.try()\n"
	    "if (invalid.error == null) Fiber.abort(\"invalid unget\")\n"
	    "invalid = Fiber.new { Input.enableMouseEvent(50) }\n"
	    "invalid.try()\n"
	    "if (invalid.error == null) Fiber.abort(\"invalid mouse event\")\n"
	    "invalid = Fiber.new { Input.mouseEvents = 1125899906842624 }\n"
	    "invalid.try()\n"
	    "if (invalid.error == null) Fiber.abort(\"invalid mouse mask\")\n"
	    "Input.synthesizePhysicalKey(88, true)\n"
	    "Input.synthesizePhysicalKey(88, true)\n"
	    "var press = Input.poll()\n"
	    "if (!(press is PhysicalKeyEvent) || press.evdev != 88 || !press.pressed || press.modifiers != 0 || press.repeat || Input.poll() != null) Fiber.abort(\"physical press synthesis\")\n"
	    "Input.synthesizePhysicalKey(88, false)\n"
	    "Input.synthesizePhysicalKey(88, false)\n"
	    "var release = Input.poll()\n"
	    "if (!(release is PhysicalKeyEvent) || release.evdev != 88 || release.pressed || Input.poll() != null) Fiber.abort(\"physical release synthesis\")\n";
	const struct st_console_event press = {
		.type = ST_CONSOLE_EVENT_MOUSE,
		.data.mouse = {
			.x = 2, .y = 3, .pixel_x = 20, .pixel_y = 30,
			.buttons = ST_CONSOLE_MOUSE_BUTTON_LEFT
			    | ST_CONSOLE_MOUSE_BUTTON_X1,
			.modifiers = ST_CONSOLE_MODIFIER_ALT,
			.action = ST_CONSOLE_MOUSE_BUTTON,
		},
	};
	const struct st_console_event move = {
		.type = ST_CONSOLE_EVENT_MOUSE,
		.data.mouse = {
			.x = 4, .y = 5, .pixel_x = 40, .pixel_y = 50,
			.buttons = ST_CONSOLE_MOUSE_BUTTON_LEFT
			    | ST_CONSOLE_MOUSE_BUTTON_X1,
			.modifiers = ST_CONSOLE_MODIFIER_CONTROL,
			.action = ST_CONSOLE_MOUSE_MOVE,
		},
	};
	const struct st_console_event release = {
		.type = ST_CONSOLE_EVENT_MOUSE,
		.data.mouse = {
			.x = 6, .y = 7, .pixel_x = 60, .pixel_y = 70,
			.modifiers = ST_CONSOLE_MODIFIER_SHIFT,
			.action = ST_CONSOLE_MOUSE_BUTTON,
		},
	};
	const struct st_console_event wheel = {
		.type = ST_CONSOLE_EVENT_MOUSE,
		.data.mouse = {
			.x = 8, .y = 9, .pixel_x = 80, .pixel_y = 90,
			.wheel_x = -3, .wheel_y = 2,
			.modifiers = ST_CONSOLE_MODIFIER_GUI,
			.action = ST_CONSOLE_MOUSE_WHEEL,
		},
	};
	static const char mouse_source[] =
	    "import \"syncterm\" for Input, Mouse, MouseEvent\n"
	    "Input.mouseEvents = 0\n"
	    "for (event in [Mouse.move, Mouse.button1Press, Mouse.button1Release, Mouse.button1DragStart, Mouse.button1DragEnd, Mouse.button4Press, Mouse.button4Release, Mouse.button4DragStart, Mouse.button4DragEnd, Mouse.wheelUp, Mouse.wheelLeft]) Input.enableMouseEvent(event)\n"
	    "var events = []\n"
	    "for (i in 0...11) events.add(Input.poll())\n"
	    "if (events[0].event != Mouse.button1Press || events[1].event != Mouse.button4Press || events[0].bstate != 9 || events[1].bstate != 9 || events[0].modifiers != 4) Fiber.abort(\"button presses\")\n"
	    "if (events[2].event != Mouse.move || events[3].event != Mouse.button1DragStart || events[4].event != Mouse.button4DragStart || events[3].startX != 3 || events[3].startY != 4 || events[3].endX != 5 || events[3].endY != 6 || events[3].modifiers != 4) Fiber.abort(\"drag expansion\")\n"
	    "if (events[5].event != Mouse.button1Release || events[5].modifiers != 1 || events[6].event != Mouse.button1DragEnd || events[6].modifiers != 4 || events[7].event != Mouse.button4Release || events[8].event != Mouse.button4DragEnd) Fiber.abort(\"button releases\")\n"
	    "if (events[9].event != Mouse.wheelUp || events[9].wheelY != 2 || events[9].wheelX != 0 || events[9].modifiers != 8 || events[10].event != Mouse.wheelLeft || events[10].wheelX != -3 || events[10].wheelY != 0) Fiber.abort(\"wheel axes\")\n"
	    "if (Input.poll() != null) Fiber.abort(\"extra mouse event\")\n";

	CHECK(wrenInterpret(host->vm, "input_value_contract", values)
	    == WREN_RESULT_SUCCESS);
	CHECK(st_console_post_event(console, &press) == 0);
	CHECK(st_console_post_event(console, &move) == 0);
	CHECK(st_console_post_event(console, &release) == 0);
	CHECK(st_console_post_event(console, &wheel) == 0);
	CHECK(wrenInterpret(host->vm, "mouse_input_contract", mouse_source)
	    == WREN_RESULT_SUCCESS);
	return 0;
}

static int
exercise_claim_wake_contract(struct st_wren_host *host)
{
	static const char source[] =
	    "import \"syncterm\" for Hook, Input, KeyEvent, REPL, Wake\n"
	    "class ClaimProbe {\n"
	    "  static start() {\n"
	    "    __seen = []\n"
	    "    __worker = Fiber.new {\n"
	    "      __stale = Input.pushClaim(Fn.new {|event|\n"
	    "        __seen.add(\"old\")\n"
	    "        return false\n"
	    "      })\n"
	    "      Fiber.yield()\n"
	    "      __current = Input.pushClaim(Fn.new {|event|\n"
	    "        __seen.add(event.code)\n"
	    "        return event.code == 2\n"
	    "      })\n"
	    "      Fiber.yield()\n"
	    "    }\n"
	    "    __worker.call()\n"
	    "  }\n"
	    "  static replace() { __worker.call() }\n"
	    "  static stalePop() { __stale.pop() }\n"
	    "  static currentPop() { __current.pop() }\n"
	    "  static finish() { __worker.call() }\n"
	    "  static dead() {\n"
	    "    __deadWorker = Fiber.new {\n"
	    "      __deadHandle = Input.pushClaim(Fn.new {|event| true })\n"
	    "      Fiber.yield()\n"
	    "    }\n"
	    "    __deadWorker.call()\n"
	    "    __deadWorker.call()\n"
	    "  }\n"
	    "  static deadPop() { __deadHandle.pop() }\n"
	    "  static seen { __seen }\n"
	    "}\n"
	    "class WakeProbe {\n"
	    "  static start() {\n"
	    "    __values = []\n"
	    "    __worker = Fiber.new {\n"
	    "      var value = Fiber.yield()\n"
	    "      __values.add(value)\n"
	    "      value = Fiber.yield()\n"
	    "      __values.add(value)\n"
	    "    }\n"
	    "    __worker.call()\n"
	    "    Wake.post(__worker, \"one\")\n"
	    "    Wake.post(__worker, \"two\")\n"
	    "  }\n"
	    "  static values { __values }\n"
	    "}\n"
	    "class BadText {\n"
	    "  construct new() {}\n"
	    "  toString { Fiber.abort(\"formatting failed\") }\n"
	    "}\n"
	    "for (value in [\"string\", 42, KeyEvent.new(1), BadText.new()]) {\n"
	    "  var result = Hook.dispatch_(Fn.new { Fiber.abort(value) })\n"
	    "  if (result != null) Fiber.abort(\"hook abort escaped\")\n"
	    "}\n"
	    "if (Hook.dispatch_(Fn.new { null }) != null) Fiber.abort(\"hook null result\")\n"
	    "var yielded = Hook.dispatch_(Fn.new { Fiber.yield() })\n"
	    "if (yielded != null) Fiber.abort(\"hook yield escaped\")\n"
	    "ClaimProbe.start()\n"
	    "WakeProbe.start()\n";
	struct st_wren_input_record event = {
		.type = ST_WREN_INPUT_KEY,
		.data.key.code = 1,
	};
	bool consumed = true;

	CHECK(wrenInterpret(host->vm, "input_async_contract", source)
	    == WREN_RESULT_SUCCESS);
	CHECK(st_wren_input_dispatch(host->vm, &event, &consumed) == 0);
	CHECK(!consumed);
	CHECK(wrenInterpret(host->vm, "input_async_contract",
	    "ClaimProbe.replace()\n"
	    "if (ClaimProbe.stalePop()) Fiber.abort(\"stale claim popped\")\n")
	    == WREN_RESULT_SUCCESS);
	event.data.key.code = 2;
	CHECK(st_wren_input_dispatch(host->vm, &event, &consumed) == 0);
	CHECK(consumed);
	CHECK(wrenInterpret(host->vm, "input_async_contract",
	    "if (ClaimProbe.seen.count != 2 || ClaimProbe.seen[0] != \"old\" || ClaimProbe.seen[1] != 2) Fiber.abort(\"claim ordering\")\n"
	    "if (!ClaimProbe.currentPop() || ClaimProbe.currentPop()) Fiber.abort(\"claim pop\")\n"
	    "ClaimProbe.finish()\n") == WREN_RESULT_SUCCESS);
	consumed = true;
	CHECK(st_wren_input_dispatch(host->vm, &event, &consumed) == 0);
	CHECK(!consumed);
	CHECK(wrenInterpret(host->vm, "input_async_contract",
	    "ClaimProbe.dead()\n") == WREN_RESULT_SUCCESS);
	consumed = true;
	CHECK(st_wren_input_dispatch(host->vm, &event, &consumed) == 0);
	CHECK(!consumed);
	CHECK(wrenInterpret(host->vm, "input_async_contract",
	    "if (ClaimProbe.deadPop()) Fiber.abort(\"dead claim survived\")\n")
	    == WREN_RESULT_SUCCESS);
	CHECK(st_wren_wake_drain(host->vm) == 0);
	CHECK(wrenInterpret(host->vm, "input_async_contract",
	    "if (WakeProbe.values.count != 2 || WakeProbe.values[0] != \"one\" || WakeProbe.values[1] != \"two\") Fiber.abort(\"wake order\")\n")
	    == WREN_RESULT_SUCCESS);
	CHECK(st_wren_wake_drain(host->vm) == 0);
	return 0;
}

struct ignored_input_poster {
	st_console_t *console;
};

static int
post_ignored_input(void *argument)
{
	struct ignored_input_poster *poster = argument;
	const struct timespec interval = {0, 20000000};
	const struct st_console_event event = {
		.type = ST_CONSOLE_EVENT_FOCUS,
		.data.focus.focused = true,
	};

	for (unsigned int i = 0; i < 10; ++i) {
		(void)st_thread_sleep(&interval, NULL);
		if (st_console_post_event(poster->console, &event) != 0)
			return 1;
	}
	return 0;
}

static int
exercise_input_deadline_contract(struct st_wren_host *host,
	st_console_t *console)
{
	struct ignored_input_poster poster = {.console = console};
	st_thread_t thread;
	uint64_t start;
	uint64_t end;
	int thread_result;

	CHECK(st_thread_create(&thread, post_ignored_input, &poster)
	    == ST_THREAD_SUCCESS);
	CHECK(st_time_monotonic_ms(&start) == 0);
	CHECK(wrenInterpret(host->vm, "input_deadline_contract",
	    "import \"syncterm\" for Input\n"
	    "if (Input.next(100) != null) Fiber.abort(\"unexpected input\")\n")
	    == WREN_RESULT_SUCCESS);
	CHECK(st_time_monotonic_ms(&end) == 0);
	CHECK(st_thread_join(thread, &thread_result) == ST_THREAD_SUCCESS);
	CHECK(thread_result == 0);
	CHECK(end - start < 250);
	CHECK(st_console_events_clear(console) == 0);
	return 0;
}

static int
exercise_persistent_quit_contract(struct st_wren_host *host,
	st_console_t *console)
{
	const struct st_console_event close = {.type = ST_CONSOLE_EVENT_CLOSE};

	CHECK(st_console_post_event(console, &close) == 0);
	CHECK(wrenInterpret(host->vm, "persistent_quit_contract",
	    "import \"syncterm\" for Input, Key\n"
	    "for (i in 0...3) {\n"
	    "  var event = Input.poll()\n"
	    "  if (event == null || event.code != Key.quit) Fiber.abort(\"quit did not persist\")\n"
	    "}\n") == WREN_RESULT_SUCCESS);
	return 0;
}

static int
exercise_safe_mode_persistence_contract(st_wren_host_t *public_host,
	const char *personal, const char *settings, const char *cache)
{
	static const char personal_contents[] =
	    "[Alpha]\nAddress=alpha.example\nLastConnected=100\n";
	static const char settings_contents[] =
	    "[SyncTERM]\nConfirmClose=false\n"
	    "[WebLists]\nAudit=https://example.invalid/original\n";
	static const char source[] =
	    "import \"syncterm\" for Host\n"
	    "import \"syncterm_menu\" for Menu\n"
	    "if (!Host.safeMode || Menu.load(null) != 0) Fiber.abort(\"safe setup\")\n"
	    "var count = Menu.entries.count\n"
	    "var entry = Menu.entries[0]\n"
	    "entry.comment = \"unsaved edit\"\n"
	    "if (!entry.rename(\"Renamed\") || entry.save() || !entry.dirty) Fiber.abort(\"entry save\")\n"
	    "if (entry.delete() || Menu.entries.count != count || Menu.entries[0].name != \"Renamed\") Fiber.abort(\"entry delete\")\n"
	    "var fresh = Menu.create(\"Fresh\")\n"
	    "fresh.comment = \"unsaved new entry\"\n"
	    "if (fresh.save() || !fresh.dirty || Menu.entries.count != count + 1) Fiber.abort(\"new entry save\")\n"
	    "if (!fresh.delete() || Menu.entries.count != count) Fiber.abort(\"new entry discard\")\n"
	    "var defaults = Menu.defaults\n"
	    "defaults.comment = \"unsaved defaults\"\n"
	    "if (defaults.save() || !defaults.dirty || Menu.defaults.comment != \"unsaved defaults\") Fiber.abort(\"defaults save\")\n"
	    "var snapshot = Menu.settings\n"
	    "var confirm = snapshot.confirmClose\n"
	    "snapshot.confirmClose = !confirm\n"
	    "if (snapshot.save() || !snapshot.dirty || Menu.settings.confirmClose != confirm) Fiber.abort(\"settings save\")\n"
	    "if (!Menu.updateWebList(0, \"https://example.invalid/changed\") || !Menu.webListsDirty) Fiber.abort(\"web edit\")\n"
	    "if (Menu.saveWebLists() || !Menu.webListsDirty) Fiber.abort(\"web save\")\n"
	    "if (Menu.canCreateFont || Menu.createFont(\"Blocked\", 0) != null || Menu.saveFonts()) Fiber.abort(\"font safe mode\")\n"
	    "if (!Menu.reloadFonts() || Menu.fontsDirty) Fiber.abort(\"font safe reload\")\n"
	    "var algorithm = Menu.encryptionAlgorithm\n"
	    "var keySize = Menu.encryptionKeySize\n"
	    "if (Menu.setEncryption(6, 128, \"secret\")) Fiber.abort(\"encryption save\")\n"
	    "if (Menu.encryptionAlgorithm != algorithm || Menu.encryptionKeySize != keySize) Fiber.abort(\"encryption state\")\n";
	struct st_wren_host *host = public_host;
	struct fs_info info;
	char cache_directory[sizeof(ST_WREN_TEST_DIRECTORY) + 48];
	char cache_sentinel[sizeof(ST_WREN_TEST_DIRECTORY) + 64];

	CHECK(snprintf(cache_directory, sizeof(cache_directory), "%s/Alpha", cache)
	    < (int)sizeof(cache_directory));
	CHECK(snprintf(cache_sentinel, sizeof(cache_sentinel), "%s/sentinel",
	    cache_directory) < (int)sizeof(cache_sentinel));
	CHECK(fs_create_directories(cache_directory) == 0);
	CHECK(write_file(cache_sentinel, "preserve") == 0);
	CHECK(file_equals(personal, personal_contents));
	CHECK(file_equals(settings, settings_contents));
	CHECK(wrenInterpret(host->vm, "safe_mode_persistence_contract", source)
	    == WREN_RESULT_SUCCESS);
	CHECK(file_equals(personal, personal_contents));
	CHECK(file_equals(settings, settings_contents));
	CHECK(fs_get_info(&info, cache_sentinel) == 0
	    && info.kind == FS_ENTRY_FILE);
	return 0;
}

int
main(void)
{
	static const uint8_t default_bitmap[32] = {0};
	static const uint8_t default_wide_bitmap[32] = {0};
	static const struct st_console_font_mapping default_mapping[] = {
		{'?', 0},
		{'A', 1},
		{UINT32_C(0x4e00), 2},
	};
	static const struct st_console_font default_font = {
		.bitmap = default_bitmap,
		.bitmap_size = sizeof(default_bitmap),
		.width = 8,
		.height = 16,
		.glyph_count = 2,
		.row_stride = 1,
		.glyph_stride = 16,
		.wide_bitmap = default_wide_bitmap,
		.wide_bitmap_size = sizeof(default_wide_bitmap),
		.wide_glyph_count = 1,
		.wide_row_stride = 2,
		.wide_glyph_stride = 32,
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
	char cloud_directory[sizeof(ST_WREN_TEST_DIRECTORY) + 64];
	char cloud_manifest[sizeof(ST_WREN_TEST_DIRECTORY) + 80];
	char cloud_theme[sizeof(ST_WREN_TEST_DIRECTORY) + 80];
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
	st_session_history_t *session_history = NULL;
	st_audio_runtime_t *audio_runtime = NULL;
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
	(void)snprintf(cloud_directory, sizeof(cloud_directory),
	    "%s/themes/official", system_cache);
	(void)snprintf(cloud_manifest, sizeof(cloud_manifest), "%s/manifest.lst",
	    cloud_directory);
	(void)snprintf(cloud_theme, sizeof(cloud_theme), "%s/blue.ini",
	    cloud_directory);
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
	struct st_console_cell history_cells[4] = {
		{.codepoint = 'A'}, {.codepoint = 'B'},
		{.codepoint = 'C'}, {.codepoint = 'D'},
	};
	CHECK(st_session_history_create(&session_history, 4) == 0);
	CHECK(st_audio_runtime_create(&audio_runtime, UINT32_MAX) == 0);
	CHECK(st_session_history_publish(session_history, history_cells, 2, 2,
	    ST_SCREEN_MODE_CURRENT, true) == 0);
	host_config = (struct st_wren_host_config){
		.console = console,
		.paths = &paths,
		.application_title = "SyncTERM test",
		.session_history = session_history,
		.audio_runtime = audio_runtime,
	};
	CHECK(st_wren_host_create(&host, &host_config) == 0);
	{
		enum st_screen_mode mode = ST_SCREEN_MODE_80X25;

		CHECK(st_wren_host_get_screen_mode(host, &mode) == 0);
		CHECK(mode == (enum st_screen_mode)st_settings.startup_mode);
	}
	CHECK(exercise_session_history_contract(host) == 0);
	CHECK(exercise_repl_style(host, console) == 0);
	CHECK(exercise_mouse_drag_selection(host, console) == 0);
	CHECK(exercise_input_value_contract(host, console) == 0);
	CHECK(exercise_claim_wake_contract(host) == 0);
	CHECK(exercise_input_deadline_contract(host, console) == 0);
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
	    "import \"syncterm\" for Input, Key, PhysicalKeyEvent\n"
	    "var physical = Input.poll()\n"
	    "var event = Input.poll()\n"
	    "if (!(physical is PhysicalKeyEvent) || physical.evdev != 41 || "
	    "!physical.pressed || physical.modifiers != 2 || physical.repeat || "
	    "event == null || event.code != Key.wrenConsole) "
	    "Fiber.abort(\"Ctrl+grave conversion failed\")\n")
	    == WREN_RESULT_SUCCESS);
	for (uint16_t selector = 0; selector < 45; ++selector)
		CHECK(st_wren_font_identifier(&((struct st_wren_host *)host)->menu,
		    selector, &configured_identifier));
	CHECK(!st_wren_font_identifier(&((struct st_wren_host *)host)->menu, 45,
	    &configured_identifier));
	CHECK(exercise_unicode_fallback_contract(host) == 0);
	CHECK(exercise_surface_cell_retarget_contract(host) == 0);
	CHECK(exercise_rectangle_contract(host) == 0);
	CHECK(exercise_core_value_contracts(host, console) == 0);
	CHECK(exercise_pixel_contract(host) == 0);
	CHECK(exercise_web_refresh_overlay(host, console) == 0);
	CHECK(exercise_cursor_contract(host) == 0);
	CHECK(exercise_video_colour_contract(host) == 0);
	CHECK(exercise_font_slot_contract(host) == 0);
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
	CHECK(st_audio_runtime_allowed(audio_runtime) == 0);
	CHECK(st_session_history_capacity(session_history) == 100);
	CHECK(exercise_settings_save_contract(host) == 0);
	CHECK(exercise_bbs_model_contract(host, &paths, personal, shared) == 0);
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
	CHECK(exercise_selected_local_theme_delete_failure(host, theme, settings)
	    == 0);
	CHECK(exercise_selected_cloud_theme_delete_failure(host, cloud_directory,
	    cloud_manifest, cloud_theme, settings) == 0);
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
	CHECK(exercise_persistent_quit_contract(host, console) == 0);
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
	    "import \"syncterm\" for Host, PixelBlit, PixelBuffer, Screen\n"
	    "if (!Host.textTerminal || !Host.localControlKeys) "
	    "Fiber.abort(\"ANSI host capabilities\")\n"
	    "if (Screen.supports.pixels || Screen.pixelSize != null || "
	    "Screen.cellPixelSize != null) Fiber.abort(\"ANSI pixel capability\")\n"
	    "if (Screen.setPixel(0, 0, 0) || "
	    "Screen.blitPixels(PixelBuffer.new(1, 1), PixelBlit.new())) "
	    "Fiber.abort(\"ANSI pixel mutation\")\n")
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

	static const char safe_personal_contents[] =
	    "[Alpha]\nAddress=alpha.example\nLastConnected=100\n";
	static const char safe_settings_contents[] =
	    "[SyncTERM]\nConfirmClose=false\n"
	    "[WebLists]\nAudit=https://example.invalid/original\n";
	CHECK(write_file(personal, safe_personal_contents) == 0);
	CHECK(write_file(settings, safe_settings_contents) == 0);
	CHECK(st_settings_reload() == 0);
	console = NULL;
	host = NULL;
	console_config_display.backend = ST_CONSOLE_BACKEND_HEADLESS;
	console_config_display.ansi_io = NULL;
	CHECK(st_console_create(&console, &console_config_display,
	    &console_config) == 0);
	CHECK(st_console_register_static_font(console, &default_font,
	    &default_identifier) == 0);
	host_config.console = console;
	host_config.safe_mode = true;
	CHECK(st_wren_host_create(&host, &host_config) == 0);
	CHECK(exercise_safe_mode_persistence_contract(host, personal, settings,
	    cache) == 0);
	st_wren_host_destroy(host);
	st_console_destroy(console);
	st_session_history_destroy(session_history);
	st_audio_runtime_destroy(audio_runtime);
	st_settings_shutdown();
	return 0;
}
