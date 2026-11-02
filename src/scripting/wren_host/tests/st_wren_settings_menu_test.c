/* Settings host application, private failure injection, and save policy. */
/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "wren_host_internal.h"
#include "st_wren_menu_test_bindings.h"
#include "filesystem.h"
#include "io_error.h"
#include "st_app_paths.h"
#include "st_audio_runtime.h"
#include "st_builtin_fonts.h"
#include "st_bbs_list.h"
#include "st_presentation_font.h"
#include "st_session_history.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", \
		    __FILE__, __LINE__, #expression); \
		exit(EXIT_FAILURE); \
	} \
} while (false)

static void
write_text(const char *path, const char *text)
{
	FILE *stream = fopen(path, "wb");

	CHECK(stream != NULL);
	CHECK(fputs(text, stream) >= 0);
	CHECK(fclose(stream) == 0);
}

static void
interpret(struct st_wren_host *host, const char *module, const char *source)
{
	if (wrenInterpret(host->vm, module, source) != WREN_RESULT_SUCCESS) {
		fprintf(stderr, "%s: %s\n", module, host->last_error);
		exit(EXIT_FAILURE);
	}
}

static void
test_menu_results(struct st_wren_host *host)
{
	interpret(host, "menu_results",
	    "import \"syncterm\" for Error, IOErr\n"
	    "import \"syncterm_menu\" for Menu, MenuError, DirectoryEntry\n"
	    "var failure = Menu.create(\"Existing\")\n"
	    "if (!(failure is MenuError) || !(failure is Error) || failure.code != null || failure.nativeCode != null || !(failure.message is String) || !failure.toString.startsWith(\"MenuError: \")) Fiber.abort(\"pointer failure invented a reason\")\n"
	    "for (action in [Fn.new { failure.code = 1 }, Fn.new { failure.nativeCode = 1 }, Fn.new { failure.message = \"changed\" }, Fn.new { MenuError.new() }]) {\n"
	    "  var bad = Fiber.new { action.call() }\n"
	    "  bad.try()\n"
	    "  if (bad.error == null) Fiber.abort(\"mutable or constructible menu error\")\n"
	    "}\n"
	    "var draft = Menu.create(\"Result Draft\")\n"
	    "if (!(draft is DirectoryEntry) || draft.rename(\"Result Renamed\") != null || draft.delete() != null) Fiber.abort(\"draft result contract\")\n");
	char constants[4096];
	CHECK(snprintf(constants, sizeof(constants),
	    "var codes = [IOErr.invalidArgument,IOErr.invalidEncoding,IOErr.notFound,IOErr.permissionDenied,IOErr.alreadyExists,IOErr.notDirectory,IOErr.isDirectory,IOErr.directoryNotEmpty,IOErr.outOfMemory,IOErr.noSpace,IOErr.nameTooLong,IOErr.outOfRange,IOErr.busy,IOErr.readOnly,IOErr.crossDevice,IOErr.tooManyLinks,IOErr.interrupted,IOErr.notSupported,IOErr.io,IOErr.timedOut,IOErr.cancelled,IOErr.wouldBlock,IOErr.inProgress,IOErr.nameResolution,IOErr.connectionRefused,IOErr.connectionReset,IOErr.connectionAborted,IOErr.notConnected,IOErr.networkUnreachable,IOErr.hostUnreachable,IOErr.addressInUse,IOErr.addressNotAvailable,IOErr.messageTooLarge,IOErr.protocol,IOErr.passwordRequired,IOErr.decryptionFailed,IOErr.invalidFormat,IOErr.conflict]\n"
	    "var expected = [%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d]\n"
	    "if (codes.count != expected.count) Fiber.abort(\"portable codes differ from C\")\n"
	    "for (i in 0...codes.count) if (codes[i] != expected[i]) Fiber.abort(\"portable code value\")\n",
	    IO_ERROR_INVALID_ARGUMENT, IO_ERROR_INVALID_ENCODING, IO_ERROR_NOT_FOUND, IO_ERROR_PERMISSION_DENIED, IO_ERROR_ALREADY_EXISTS, IO_ERROR_NOT_DIRECTORY, IO_ERROR_IS_DIRECTORY, IO_ERROR_DIRECTORY_NOT_EMPTY, IO_ERROR_OUT_OF_MEMORY, IO_ERROR_NO_SPACE, IO_ERROR_NAME_TOO_LONG, IO_ERROR_OUT_OF_RANGE, IO_ERROR_BUSY, IO_ERROR_READ_ONLY, IO_ERROR_CROSS_DEVICE, IO_ERROR_TOO_MANY_LINKS, IO_ERROR_INTERRUPTED, IO_ERROR_NOT_SUPPORTED, IO_ERROR_IO, IO_ERROR_TIMED_OUT, IO_ERROR_CANCELLED, IO_ERROR_WOULD_BLOCK, IO_ERROR_IN_PROGRESS, IO_ERROR_NAME_RESOLUTION, IO_ERROR_CONNECTION_REFUSED, IO_ERROR_CONNECTION_RESET, IO_ERROR_CONNECTION_ABORTED, IO_ERROR_NOT_CONNECTED, IO_ERROR_NETWORK_UNREACHABLE, IO_ERROR_HOST_UNREACHABLE, IO_ERROR_ADDRESS_IN_USE, IO_ERROR_ADDRESS_NOT_AVAILABLE, IO_ERROR_MESSAGE_TOO_LARGE, IO_ERROR_PROTOCOL, IO_ERROR_PASSWORD_REQUIRED, IO_ERROR_DECRYPTION_FAILED, IO_ERROR_INVALID_FORMAT, IO_ERROR_CONFLICT) < (int)sizeof(constants));
	interpret(host, "menu_results", constants);
	CHECK(!wrenHasVariable(host->vm, "syncterm", "FileErr"));
	host->config.safe_mode = true;
	interpret(host, "menu_results",
	    "var blocked = Menu.saveSortProfiles()\n"
	    "if (!(blocked is MenuError) || blocked.code != IOErr.readOnly || blocked.nativeCode != null) Fiber.abort(\"blocked persistence reported success\")\n"
	    "System.gc()\nSystem.gc()\n"
	    "if (!(failure.message is String) || failure.code != null) Fiber.abort(\"menu error lifetime\")\n");
	host->config.safe_mode = false;
}

static void
test_menu_native_failures(struct st_wren_host *host, const char *personal)
{
	st_wren_test_font_insert_status = -1;
	interpret(host, "native_menu_results",
	    "import \"syncterm\" for IOErr\n"
	    "import \"syncterm_menu\" for Menu, MenuError\n"
	    "var generic = Menu.createFont(\"Generic Failure\", 0)\n"
	    "if (!(generic is MenuError) || generic.code != null || generic.nativeCode != -1 || Menu.fonts.count != 0) Fiber.abort(\"generic -1 was classified as invalidArgument\")\n"
	    "var existing = Menu.entries[0]\n"
	    "var rename = existing.rename(\"\")\n"
	    "if (!(rename is MenuError) || rename.code != null || rename.nativeCode != null) Fiber.abort(\"Boolean failure invented native status\")\n");
	st_wren_test_prepare_save_failure = true;
	interpret(host, "native_menu_results",
	    "existing.profile.comment = \"preflight test\"\n"
	    "var preflight = existing.save()\n"
	    "if (!(preflight is MenuError) || preflight.code != null || preflight.nativeCode != null || !existing.dirty) Fiber.abort(\"Boolean preflight invented integer status\")\n");
	st_wren_test_prepare_save_failure = false;
	st_wren_test_font_insert_status = 0;
	unsigned calls = st_wren_test_activation_calls;
	interpret(host, "native_menu_results",
	    "var font = Menu.createFont(\"Result Font\", 0)\n"
	    "if (Menu.saveFonts() != null || Menu.fontsDirty) Fiber.abort(\"font persistence\")\n");
	st_wren_test_font_rename_status = -1;
	interpret(host, "native_menu_results",
	    "var renameError = Fiber.new { font.name = \"Unpublished Name\" }.try()\n"
	    "if (!(renameError is MenuError) || renameError.code != null || renameError.nativeCode != -1 || font.name != \"Result Font\") Fiber.abort(\"setter lost native failure\")\n"
	    "var unauthorized = font.setFile(2, \"not a File grant\")\n"
	    "if (!(unauthorized is MenuError) || unauthorized.code != IOErr.permissionDenied || unauthorized.nativeCode != null || font.path(2) != null) Fiber.abort(\"font grant bypass\")\n");
	st_wren_test_font_rename_status = 0;
	interpret(host, "native_menu_results", "var grants = []\n");
	wrenEnsureSlots(host->vm, 2);
	wrenGetVariable(host->vm, "native_menu_results", "grants", 0);
	CHECK(st_wren_file_push(host->vm, 1, personal,
	    ST_WREN_PICKER_DISPOSITION_READ, true));
	wrenInsertInList(host->vm, 0, -1, 1);
	interpret(host, "native_menu_results",
	    "if (font.setFile(2, grants[0]) != null || font.path(2) != grants[0].path || !Menu.fontsDirty) Fiber.abort(\"readable font grant was rejected\")\n"
	    "if (font.clearFile(2) != null || font.path(2) != null || Menu.saveFonts() != null) Fiber.abort(\"font grant cleanup\")\n");
	CHECK(st_wren_test_activation_calls == calls && host->menu.fonts.loaded_count == 0);
	st_wren_test_font_activation_status = IO_ERROR_OUT_OF_MEMORY;
	interpret(host, "native_menu_results",
	    "var activation = Menu.activateFonts()\n"
	    "if (!(activation is MenuError) || activation.code != IOErr.outOfMemory || activation.nativeCode != IOErr.outOfMemory || Menu.fontsDirty || Menu.fonts.count != 1) Fiber.abort(\"font activation lost native failure or saved draft\")\n");
	CHECK(host->menu.fonts.loaded_count == 0 && host->picker.fonts.loaded_count == 0);
	st_wren_test_font_activation_status = 0;
	interpret(host, "native_menu_results",
	    "if (Menu.activateFonts() != null) Fiber.abort(\"font activation\")\n"
	    "if (font.delete() != null || Menu.saveFonts() != null || Menu.activateFonts() != null) Fiber.abort(\"font result cleanup\")\n");
	interpret(host, "native_menu_results",
	    "var sortIndex = Menu.sortProfiles.count\n"
	    "if (Menu.addSortProfile(sortIndex, \"Partial Profile\", [1]) != null) Fiber.abort(\"sort fixture\")\n");
	st_wren_test_sort_update_failure = true;
	interpret(host, "native_menu_results",
	    "var sortError = Menu.updateSortProfile(sortIndex, \"Changed Profile\", [1])\n"
	    "if (!(sortError is MenuError) || sortError.code != null || sortError.nativeCode != -1 || Menu.sortProfiles[sortIndex][0] != \"Changed Profile\") Fiber.abort(\"partial native collection result\")\n");
	st_wren_test_sort_update_failure = false;
	interpret(host, "native_menu_results", "if (Menu.deleteSortProfile(sortIndex) != null) Fiber.abort(\"sort cleanup\")\n");
	st_wren_test_emoji_status = IO_ERROR_IO;
	interpret(host, "native_menu_results",
	    "var snapshot = Menu.settings\n"
	    "snapshot.confirmClose = !snapshot.confirmClose\n"
	    "var runtime = snapshot.apply()\n"
	    "if (!(runtime is MenuError) || runtime.code != IOErr.io || runtime.nativeCode != IOErr.io || snapshot.dirty || Menu.settings.confirmClose != snapshot.confirmClose || !runtime.message.contains(\"installed\")) Fiber.abort(\"runtime failure hid installed settings\")\n");
	st_wren_test_emoji_status = 0;
	interpret(host, "native_menu_results",
	    "if (snapshot.apply() != null) Fiber.abort(\"runtime recovery\")\n"
	    "existing.profile.comment = \"local\"\n");
	write_text(personal, "[Existing]\nAddress=existing.example\nComment=external\n");
	interpret(host, "native_menu_results",
	    "var conflict = existing.save()\n"
	    "if (!(conflict is MenuError) || conflict.code != IOErr.conflict || conflict.nativeCode != IOErr.conflict || !existing.dirty) Fiber.abort(\"native conflict was discarded\")\n");
	write_text(personal, "[Existing]\nAddress=existing.example\n");
	interpret(host, "native_menu_results", "if (Menu.load(null) != 0) Fiber.abort(\"conflict fixture reload\")\n");
	if (!st_list_crypto_encryption_available())
		return;
	interpret(host, "native_menu_results",
	    "var policy = Menu.settings\npolicy.kdfShift = 8\n"
	    "if (policy.apply() != null || Menu.setEncryption(6, 128, \"result password\") != null) Fiber.abort(\"partial rewrite setup\")\n"
	    "var pending = Menu.settings\npending.kdfShift = 9\n");
	st_wren_test_settings_save_status = IO_ERROR_NO_SPACE;
	interpret(host, "native_menu_results",
	    "var partial = pending.save()\n"
	    "if (!(partial is MenuError) || partial.code != IOErr.noSpace || partial.nativeCode != IOErr.noSpace || !partial.message.contains(\"KDF was rewritten\") || Menu.encryptionInfo.costLog2 != 9 || Menu.settings.kdfShift != 8 || !pending.dirty) Fiber.abort(\"partial rewrite result\")\n"
	    "System.gc()\nSystem.gc()\n"
	    "if (generic.nativeCode != -1 || conflict.code != IOErr.conflict) Fiber.abort(\"retained error lifetime\")\n");
	st_wren_test_settings_save_status = 0;
	interpret(host, "native_menu_results", "if (Menu.setEncryption(0, 0, null) != null) Fiber.abort(\"partial rewrite cleanup\")\n");
}

static void
test_default_cursor(struct st_wren_host *host, st_console_t *console)
{
	for (size_t index = 0; index < st_screen_mode_count(); ++index) {
		const struct st_screen_mode_descriptor *mode = st_screen_mode_at(index);
		struct st_console_custom_cursor actual;
		char module[40];

		if (mode->mode == ST_SCREEN_MODE_CURRENT
		    || mode->mode == ST_SCREEN_MODE_CUSTOM)
			continue;
		CHECK(st_wren_host_set_screen_mode(host, mode->mode) == 0);
		CHECK(snprintf(module, sizeof(module), "cursor_apply_%zu", index)
		    < (int)sizeof(module));
		interpret(host, module,
		    "import \"syncterm_menu\" for Menu\n"
		    "var s = Menu.settings\n"
		    "s.cursorStyle = 0\n"
		    "s.confirmClose = !s.confirmClose\n"
		    "if (!(s.apply() == null)) Fiber.abort(\"cursor apply\")\n");
		CHECK(st_console_get_default_cursor(console, &actual) == 0);
		CHECK(actual.start == mode->console.default_cursor.start);
		CHECK(actual.end == mode->console.default_cursor.end);
		CHECK(actual.range == mode->console.default_cursor.range);
	}
	CHECK(st_wren_host_set_screen_mode(host, ST_SCREEN_MODE_80X25) == 0);
}

static void
test_safe_kdf(struct st_wren_host *host)
{
	if (!st_list_crypto_encryption_available())
		return;
	interpret(host, "kdf_setup",
	    "import \"syncterm_menu\" for Menu\n"
	    "if (!(Menu.setEncryption(6, 128, \"audit password\") == null)) Fiber.abort(\"encrypt fixture\")\n");
	unsigned before = st_bbs_list_crypto_info(host->bbs_list)->cost_log2;
	CHECK(before == 8);
	char old_kdf[sizeof(st_settings.key_derivation_iterations)];
	strcpy(old_kdf, st_settings.key_derivation_iterations);
	host->config.safe_mode = true;
	interpret(host, "safe_kdf",
	    "import \"syncterm_menu\" for Menu\n"
	    "var s = Menu.settings\n"
	    "s.kdfShift = s.kdfShift == 8 ? 9 : 8\n"
	    "if ((s.apply() == null) || !s.dirty) Fiber.abort(\"safe mode rewrote encrypted list\")\n");
	CHECK(strcmp(old_kdf, st_settings.key_derivation_iterations) == 0);
	CHECK(st_bbs_list_reload(host->bbs_list) == 0);
	CHECK(st_bbs_list_crypto_info(host->bbs_list)->cost_log2 == before);
	host->config.safe_mode = false;
	interpret(host, "kdf_cleanup",
	    "import \"syncterm_menu\" for Menu\n"
	    "if (!(Menu.setEncryption(0, 0, null) == null)) Fiber.abort(\"decrypt fixture\")\n");
}

/* Exercise native metadata independently of optional legacy cipher providers. */
static void
test_encryption_metadata(struct st_wren_host *host)
{
	struct st_list_crypto_info *info = (struct st_list_crypto_info *)
	    st_bbs_list_crypto_info(host->bbs_list);
	const struct st_list_crypto_info saved = *info;
	static const struct {
		enum st_list_cipher cipher;
		uint32_t bits;
		const char *name;
		bool writable;
	} cases[] = {
		{ST_LIST_CIPHER_3DES, 192, "3DES", false},
		{ST_LIST_CIPHER_IDEA, 128, "IDEA", false},
		{ST_LIST_CIPHER_CAST, 40, "CAST-40", false},
		{ST_LIST_CIPHER_RC2, 256, "RC2-256", false},
		{ST_LIST_CIPHER_RC4, 128, "RC4-128", false},
		{ST_LIST_CIPHER_AES, 128, "AES-128", true},
		{ST_LIST_CIPHER_AES, 192, "AES-192", false},
		{ST_LIST_CIPHER_AES, 256, "AES-256", true},
		{ST_LIST_CIPHER_CHACHA20, 256, "ChaCha20", true},
	};
	char source[2048], module[64];

	interpret(host, "metadata_plain",
	    "import \"syncterm_menu\" for Menu, ListEncryptionInfo, ListEncryptionFormat, ListEncryptionKdf, MenuEncryption\n"
	    "var plain = Menu.encryptionInfo\n"
	    "if (!(plain is ListEncryptionInfo) || plain.format != ListEncryptionFormat.plain || plain.algorithm != MenuEncryption.none || plain.keyBits != 0 || plain.name != \"Not Encrypted\" || plain.kdf != ListEncryptionKdf.none || plain.rewriteRecommended) Fiber.abort(\"plain metadata\")\n"
	    "if (plain.iterations != null || plain.costLog2 != null || plain.blockSize != null || plain.parallelism != null) Fiber.abort(\"plain KDF\")\n"
	    "if (plain == Menu.encryptionInfo) Fiber.abort(\"snapshot reused\")\n"
	    "var bad = Fiber.new { plain.keyBits = 128 }\n"
	    "bad.try()\n"
	    "if (bad.error == null) Fiber.abort(\"mutable snapshot\")\n"
	    "bad = Fiber.new { ListEncryptionInfo.new() }\n"
	    "bad.try()\n"
	    "if (bad.error == null) Fiber.abort(\"public constructor\")\n"
	    "for (fn in [Fn.new { Menu.encryptionAlgorithm }, Fn.new { Menu.encryptionKeySize }, Fn.new { Menu.encryptionName }]) {\n"
	    "  bad = Fiber.new(fn)\n"
	    "  bad.try()\n"
	    "  if (bad.error == null) Fiber.abort(\"old getter retained\")\n"
	    "}\n"
	    "if (MenuEncryption.tripleDes != 1 || MenuEncryption.idea != 2 || MenuEncryption.cast != 3 || MenuEncryption.rc2 != 4 || MenuEncryption.rc4 != 5) Fiber.abort(\"cipher enum\")\n");
	struct st_bbs_list *list = host->bbs_list;
	host->bbs_list = NULL;
	interpret(host, "metadata_absent",
	    "import \"syncterm_menu\" for Menu\n"
	    "import \"menu_settings_ui\" for SettingsMenu\n"
	    "import \"menu_settings_ui_test\" for SettingsTestApp\n"
	    "if (Menu.encryptionInfo != null) Fiber.abort(\"absent list\")\n"
	    "var app = SettingsTestApp.new([])\n"
	    "if (SettingsMenu.encryption_(app) || app.alerts != 1) Fiber.abort(\"absent list UI\")\n");
	host->bbs_list = list;
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
		*info = (struct st_list_crypto_info){
			.format = ST_LIST_CRYPTO_V1, .cipher = cases[i].cipher,
			.kdf = ST_LIST_KDF_PBKDF2_SHA256, .key_bits = cases[i].bits,
			.iterations = 50001, .rewrite_recommended = true,
		};
		CHECK(snprintf(module, sizeof(module), "metadata_cipher_%zu", i) < (int)sizeof(module));
		CHECK(snprintf(source, sizeof(source),
		    "import \"syncterm_menu\" for Menu, ListEncryptionFormat, ListEncryptionKdf\n"
		    "import \"menu_settings_ui_test\" for SettingsMenuTest\n"
		    "var info = Menu.encryptionInfo\n"
		    "if (info.format != ListEncryptionFormat.v1 || info.algorithm != %d || info.keyBits != %u || info.name != \"%s\" || info.kdf != ListEncryptionKdf.pbkdf2Sha256 || info.iterations != 50001 || !info.rewriteRecommended) Fiber.abort(\"legacy metadata\")\n"
		    "if (info.costLog2 != null || info.blockSize != null || info.parallelism != null) Fiber.abort(\"PBKDF fields\")\n"
		    "SettingsMenuTest.inspectEncryption_(\"Currently %s\", %s)\n",
		    (int)cases[i].cipher, cases[i].bits, cases[i].name,
		    cases[i].name, cases[i].writable ? "Menu.encryptionAvailable" : "false") < (int)sizeof(source));
		interpret(host, module, source);
	}
	*info = (struct st_list_crypto_info){
		.format = ST_LIST_CRYPTO_V2, .cipher = ST_LIST_CIPHER_AES,
		.kdf = ST_LIST_KDF_SCRYPT, .key_bits = 256,
		.cost_log2 = 10, .block_size = 4, .parallelism = 2,
	};
	interpret(host, "metadata_scrypt",
	    "import \"syncterm_menu\" for Menu, ListEncryptionFormat, ListEncryptionKdf\n"
	    "var snapshot = Menu.encryptionInfo\n"
	    "if (snapshot.format != ListEncryptionFormat.v2 || snapshot.kdf != ListEncryptionKdf.scrypt || snapshot.iterations != null || snapshot.costLog2 != 10 || snapshot.blockSize != 4 || snapshot.parallelism != 2 || snapshot.rewriteRecommended) Fiber.abort(\"scrypt metadata\")\n"
	    "if (Menu.settings.kdfShift != 8) Fiber.abort(\"configured policy confused with loaded KDF\")\n");
	info->iterations = ST_WREN_MAX_INTEGER;
	info->kdf = ST_LIST_KDF_PBKDF2_SHA256;
	interpret(host, "metadata_exact_max",
	    "import \"syncterm_menu\" for Menu\n"
	    "if (Menu.encryptionInfo.iterations != 9007199254740991) Fiber.abort(\"exact native integer\")\n");
	info->iterations = UINT64_MAX;
	info->kdf = ST_LIST_KDF_PBKDF2_SHA256;
	interpret(host, "metadata_inexact",
	    "import \"syncterm_menu\" for Menu\n"
	    "var bad = Fiber.new { Menu.encryptionInfo }\n"
	    "bad.try()\n"
	    "if (bad.error == null) Fiber.abort(\"rounded native integer\")\n");
	info->kdf = (enum st_list_kdf)99;
	interpret(host, "metadata_bad_kdf",
	    "import \"syncterm_menu\" for Menu\n"
	    "var bad = Fiber.new { Menu.encryptionInfo }\n"
	    "bad.try()\n"
	    "if (bad.error == null) Fiber.abort(\"unknown KDF\")\n");
	*info = saved;
	info->cipher = (enum st_list_cipher)99;
	interpret(host, "metadata_bad_cipher",
	    "import \"syncterm_menu\" for Menu\n"
	    "var bad = Fiber.new { Menu.encryptionInfo }\n"
	    "bad.try()\n"
	    "if (bad.error == null) Fiber.abort(\"unknown cipher\")\n");
	*info = saved;
	CHECK(st_bbs_list_reload(host->bbs_list) == 0);
	interpret(host, "metadata_retained",
	    "import \"metadata_plain\" for plain\n"
	    "import \"metadata_scrypt\" for snapshot\n"
	    "if (plain.keyBits != 0 || snapshot.keyBits != 256 || snapshot.costLog2 != 10 || snapshot.blockSize != 4 || snapshot.parallelism != 2) Fiber.abort(\"snapshot changed\")\n");
	char old_kdf[sizeof(st_settings.key_derivation_iterations)];
	strcpy(old_kdf, st_settings.key_derivation_iterations);
	strcpy(st_settings.key_derivation_iterations, "legacy unsupported");
	interpret(host, "metadata_effective_policy",
	    "import \"syncterm_menu\" for Menu\n"
	    "if (Menu.settings.kdfShift != 15) Fiber.abort(\"effective KDF default\")\n");
	CHECK(strcmp(st_settings.key_derivation_iterations, "legacy unsupported") == 0);
	strcpy(st_settings.key_derivation_iterations, old_kdf);
}

static void
test_encryption_lifecycle(struct st_wren_host *host, const char *personal)
{
	char backup[sizeof(ST_WREN_SETTINGS_TEST_DIRECTORY) + 64];

	interpret(host, "metadata_before_rewrite",
	    "import \"syncterm_menu\" for Menu\n"
	    "var before = Menu.encryptionInfo\n"
	    "if (Menu.encryptionAvailable && !(Menu.setEncryption(6, 128, \"metadata password\") == null)) Fiber.abort(\"metadata encrypt\")\n"
	    "var after = Menu.encryptionInfo\n"
	    "if (before.algorithm != 0 || (Menu.encryptionAvailable && (after.algorithm != 6 || after.keyBits != 128 || after.costLog2 != 8))) Fiber.abort(\"rewrite snapshots\")\n");
	CHECK(st_bbs_list_reload(host->bbs_list) == 0);
	CHECK(snprintf(backup, sizeof(backup), "%s.metadata-backup", personal) < (int)sizeof(backup));
	CHECK(rename(personal, backup) == 0);
	CHECK(fs_create_directories(personal) == 0);
	CHECK(st_bbs_list_reload(host->bbs_list) != 0);
	interpret(host, "metadata_failed_rewrite",
	    "import \"syncterm_menu\" for Menu\n"
	    "import \"metadata_before_rewrite\" for before, after\n"
	    "if ((Menu.setEncryption(0, 0, null) == null)) Fiber.abort(\"rewrite unexpectedly succeeded\")\n"
	    "var current = Menu.encryptionInfo\n"
	    "if (current.algorithm != after.algorithm || current.keyBits != after.keyBits || current.kdf != after.kdf || current.costLog2 != after.costLog2 || before.algorithm != 0) Fiber.abort(\"failure changed metadata\")\n");
	CHECK(fs_remove_directory(personal) == 0);
	CHECK(rename(backup, personal) == 0);
	if (st_list_crypto_encryption_available()) {
		host->config.safe_mode = true;
		interpret(host, "metadata_safe_ui",
		    "import \"syncterm_menu\" for Menu\n"
		    "import \"menu_settings_ui\" for SettingsMenu\n"
		    "import \"menu_settings_ui_test\" for SettingsTestApp, SettingsMenuTest\n"
		    "var app = SettingsTestApp.new([SettingsMenuTest.row_(\"Decrypt\")])\n"
		    "if (SettingsMenu.encryption_(app) || Menu.encryptionInfo.algorithm != 6) Fiber.abort(\"safe dialog changed encryption\")\n");
		host->config.safe_mode = false;
	}
	interpret(host, "metadata_after_rewrite",
	    "import \"syncterm_menu\" for Menu\n"
	    "import \"metadata_before_rewrite\" for before, after\n"
	    "if (!(Menu.setEncryption(0, 0, null) == null)) Fiber.abort(\"metadata decrypt\")\n"
	    "if (before.algorithm != 0 || (Menu.encryptionAvailable && after.algorithm != 6) || Menu.encryptionInfo.algorithm != 0) Fiber.abort(\"retained rewrite snapshot\")\n");
}

static void
test_custom_mode(struct st_wren_host *host, st_console_t *console)
{
	uint32_t columns, rows, width, height;

	CHECK(st_wren_host_set_screen_mode(host, ST_SCREEN_MODE_CUSTOM) == 0);
	interpret(host, "custom_apply",
	    "import \"syncterm_menu\" for Menu\n"
	    "var s = Menu.settings\n"
	    "s.customRows = 31\n"
	    "s.customColumns = 101\n"
	    "s.customFontHeight = 14\n"
	    "s.customAspectWidth = 4\n"
	    "s.customAspectHeight = 3\n"
	    "if (!(s.apply() == null)) Fiber.abort(\"custom apply\")\n");
	CHECK(st_console_size(console, &columns, &rows) == 0);
	CHECK(columns == 101 && rows == 31);
	CHECK(st_console_cell_pixel_size(console, &width, &height) == 0);
	CHECK(width == 8 && height == 14);
	CHECK(st_console_get_aspect_ratio(console, &width, &height) == 0);
	CHECK(width == 4 && height == 3);
	CHECK(st_wren_host_set_screen_mode(host, ST_SCREEN_MODE_80X25) == 0);
}

static void
test_save_retry(struct st_wren_host *host, const char *settings)
{
	char backup[sizeof(ST_WREN_SETTINGS_TEST_DIRECTORY) + 64];

	CHECK(snprintf(backup, sizeof(backup), "%s.backup", settings) < (int)sizeof(backup));
	CHECK(rename(settings, backup) == 0);
	CHECK(fs_create_directories(settings) == 0);
	interpret(host, "save_retry",
	    "import \"menu_settings_ui\" for SettingsMenu\n"
	    "import \"menu_settings_ui_test\" for SettingsTestApp, SettingsMenuTest\n"
	    "var app = SettingsTestApp.new([\n"
	    "  SettingsMenuTest.row_(\"Confirm Program Exit\"),\n"
	    "  SettingsMenuTest.cancel_,\n"
	    "  Fn.new {|pane| Fiber.abort(\"retry offered\") }\n"
	    "])\n"
	    "var result = Fiber.new { SettingsMenu.program_(app, false) }.try()\n"
	    "if (result != \"retry offered\" || app.alerts != 1) "
	    "Fiber.abort(\"failed save closed the settings editor\")\n");
	CHECK(fs_remove_directory(settings) == 0);
	CHECK(rename(backup, settings) == 0);
	CHECK(st_settings_save() == 0);
}

static void
post_key(st_console_t *console, uint16_t key)
{
	const struct st_console_event event = key >= 32 && key < 127
	    ? (struct st_console_event){.type = ST_CONSOLE_EVENT_TEXT,
		.data.text.codepoint = key}
	    : (struct st_console_event){.type = ST_CONSOLE_EVENT_KEY_CODE,
		.data.key.code = key};
	CHECK(st_console_post_event(console, &event) == 0);
}

static void
test_offer_save(struct st_wren_host *host, st_console_t *console)
{
	struct st_profile source;
	bool saved;
	bool old_prompt = st_settings.prompt_save;

	st_profile_init(&source);
	strcpy(source.name, "Offered");
	strcpy(source.address, "offered.example");
	strcpy(source.username, "guest");
	source.identifier = -1;
	source.connection_type = ST_CONNECTION_RAW;
	source.port = 23;
	source.screen_mode = ST_SCREEN_MODE_80X25;
	source.encoding = ST_PROFILE_ENCODING_UTF8;
	source.calls = 3;
	st_settings.prompt_save = false;
	CHECK(st_wren_host_offer_save_profile(host, &source, &saved) == 0 && !saved);
	st_settings.prompt_save = true;
	host->config.safe_mode = true;
	CHECK(st_wren_host_offer_save_profile(host, &source, &saved) == 0 && !saved);
	host->config.safe_mode = false;
	source.hide_popups = true;
	CHECK(st_wren_host_offer_save_profile(host, &source, &saved) == 0 && !saved);
	source.hide_popups = false;
	source.identifier = 1;
	CHECK(st_wren_host_offer_save_profile(host, &source, &saved) == 0 && !saved);
	source.identifier = -1;
	post_key(console, 27);
	CHECK(st_wren_host_offer_save_profile(host, &source, &saved) == 0 && !saved);
	CHECK(st_bbs_list_find(host->bbs_list, "Offered") == NULL);
	post_key(console, 'y');
	post_key(console, 27);
	CHECK(st_wren_host_offer_save_profile(host, &source, &saved) == 0 && saved);
	const struct st_bbs_entry *entry = st_bbs_list_find(host->bbs_list, "Offered");
	CHECK(entry != NULL && strcmp(entry->profile->username, "guest") == 0
	    && entry->profile->calls == 0 && source.calls == 3);
	st_settings.prompt_save = old_prompt;
	st_profile_dispose(&source);
}

int
main(void)
{
	static const char directory[] = ST_WREN_SETTINGS_TEST_DIRECTORY;
	char settings[sizeof(directory) + 32];
	char personal[sizeof(directory) + 32];
	char cached[sizeof(directory) + 32];
	struct st_app_paths paths = {0};
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config geometry = {
		.columns = 80, .rows = 25, .cell_width = 8, .cell_height = 16,
	};
	st_console_t *console = NULL;
	st_console_t *picker = NULL;
	st_console_display_t *console_display = NULL;
	st_wren_host_t *host = NULL;
	st_hyperlink_registry_t *hyperlinks = NULL;
	st_session_history_t *history = NULL;
	st_audio_runtime_t *audio = NULL;
	struct st_console_font builtin;
	struct st_presentation_font presentation = {0};
	uint16_t font;
	char source[32768];
	FILE *stream;
	size_t size;

	CHECK(fs_create_directories(directory) == 0);
	CHECK(snprintf(settings, sizeof(settings), "%s/settings.ini", directory) < (int)sizeof(settings));
	CHECK(snprintf(personal, sizeof(personal), "%s/personal.lst", directory) < (int)sizeof(personal));
	CHECK(snprintf(cached, sizeof(cached), "%s/system.lst", directory) < (int)sizeof(cached));
	struct fs_info info;
	if (fs_get_info(&info, settings) == 0 && info.kind == FS_ENTRY_DIRECTORY)
		CHECK(fs_remove_directory(settings) == 0);
	write_text(settings, "[SyncTERM]\nAudioModes=0\n");
	write_text(personal, "[Existing]\nAddress=existing.example\n");
	paths.settings_file = settings;
	paths.personal_list = personal;
	paths.cached_system_list = cached;
	paths.scripts_directory = (char *)directory;
	paths.themes_directory = (char *)directory;
	paths.cache_directory = (char *)directory;
	paths.system_cache_directory = (char *)directory;
	paths.download_directory = (char *)directory;
	CHECK(st_settings_load(&paths) == 0);
	CHECK(st_settings.color_emoji);
	CHECK(st_console_display_create(&console_display, &display) == 0);
	CHECK(st_console_create_on_display(&console, console_display, &geometry) == 0);
	CHECK(st_console_create_on_display(&picker, console_display, &geometry) == 0);
	CHECK(st_builtin_font_get(0, 8, 16, &builtin));
	CHECK(st_presentation_font_create(&presentation, &builtin) == 0);
	CHECK(st_console_register_static_font(console, &presentation.font, &font) == 0 && font == 0);
	CHECK(st_console_register_static_font(picker, &presentation.font, &font) == 0 && font == 0);
	CHECK(st_session_history_create(&history, 2000) == 0);
	CHECK(st_hyperlink_registry_create(&hyperlinks) == 0);
	CHECK(st_audio_runtime_create(&audio, 0) == 0);
	const struct st_wren_host_config config = {
		.display = console_display, .console = console,
		.picker_console = picker, .paths = &paths, .session_history = history,
		.hyperlinks = hyperlinks,
		.audio_runtime = audio,
	};
	CHECK(st_wren_host_create(&host, &config) == 0);
	interpret(host, "settings_setup",
	    "import \"syncterm_menu\" for Menu\n"
	    "if (Menu.load(null) != 0) Fiber.abort(\"load fixture\")\n");
	test_menu_results(host);
	test_menu_native_failures(host, personal);
	stream = fopen(ST_WREN_SETTINGS_SOURCE_DIRECTORY "/scripts/menu_settings_ui_test.wren", "rb");
	CHECK(stream != NULL);
	size = fread(source, 1, sizeof(source) - 1, stream);
	CHECK(!ferror(stream) && feof(stream));
	CHECK(fclose(stream) == 0);
	source[size] = '\0';
	interpret(host, "menu_settings_ui_test", source);
	st_wren_test_emoji_status = IO_ERROR_IO;
	interpret(host, "settings_editor_partial",
	    "import \"menu_settings_ui\" for SettingsMenu\n"
	    "import \"menu_settings_ui_test\" for SettingsTestApp\n"
	    "import \"syncterm_menu\" for Menu\n"
	    "var state = Menu.settings\n"
	    "state.confirmClose = !state.confirmClose\n"
	    "var app = SettingsTestApp.new([])\n"
	    "if (!SettingsMenu.applySettings_(app, state) || app.alerts != 1 || state.dirty || Menu.settings.confirmClose != state.confirmClose) Fiber.abort(\"editor dropped installed settings after runtime failure\")\n");
	st_wren_test_emoji_status = 0;
	st_wren_test_font_activation_status = IO_ERROR_OUT_OF_MEMORY;
	unsigned saved_calls = st_wren_test_font_save_calls;
	unsigned activation_calls = st_wren_test_activation_calls;
	interpret(host, "font_editor_result",
	    "import \"menu_settings_ui\" for SettingsMenu\n"
	    "import \"menu_settings_ui_test\" for SettingsTestApp, SettingsMenuTest\n"
	    "import \"syncterm_menu\" for Menu\n"
	    "var font = Menu.createFont(\"Saved UI Font\", 0)\n"
	    "var app = SettingsTestApp.new([SettingsMenuTest.cancel_])\n"
	    "SettingsMenu.fonts_(app)\n"
	    "if (app.alerts != 1 || app.remaining != 0 || Menu.fontsDirty) Fiber.abort(\"font editor result handling\")\n");
	CHECK(st_wren_test_font_save_calls == saved_calls + 1
	    && st_wren_test_activation_calls == activation_calls + 1);
	st_wren_test_font_activation_status = 0;
	interpret(host, "font_editor_result",
	    "if (font.delete() != null || Menu.saveFonts() != null || Menu.activateFonts() != null) Fiber.abort(\"editor fixture cleanup\")\n");
	interpret(host, "settings_apply",
	    "import \"syncterm_menu\" for Menu\n"
	    "var s = Menu.settings\n"
	    "s.colorEmoji = false\n"
	    "s.scrollbackLines = 123\n"
	    "s.customRows = 30\n"
	    "s.customColumns = 100\n"
	    "s.frameColor = 1\n"
	    "s.kdfShift = 8\n"
	    "if (!(s.save() == null)) Fiber.abort(\"settings runtime apply\")\n");
	bool color = true;
	CHECK(st_console_get_color_emoji(console, &color) == 0 && !color);
	CHECK(st_console_get_color_emoji(picker, &color) == 0 && !color);
	CHECK(st_session_history_capacity(history) == 123);
	CHECK(st_audio_runtime_allowed(audio) == st_settings.audio_output_modes);
	CHECK(st_settings_reload() == 0);
	CHECK(!st_settings.color_emoji);
	CHECK(st_settings.scrollback_lines == 123 && st_settings.custom_rows == 30
	    && st_settings.custom_columns == 100 && st_settings.theme_frame_color == 1);
	host->config.safe_mode = true;
	interpret(host, "settings_safe_licenses",
	    "import \"menu_settings_ui_test\" for SettingsMenuTest\n"
	    "SettingsMenuTest.testLicenses_()\n");
	interpret(host, "settings_colour_safe",
	    "import \"syncterm_menu\" for Menu\n"
	    "var s = Menu.settings\n"
	    "s.colorEmoji = true\n"
	    "if (!(s.apply() == null)) Fiber.abort(\"apply colour in safe mode\")\n");
	CHECK(st_console_get_color_emoji(console, &color) == 0 && color);
	CHECK(st_console_get_color_emoji(picker, &color) == 0 && color);
	host->config.safe_mode = false;
	test_default_cursor(host, console);
	test_custom_mode(host, console);
	CHECK(st_console_get_color_emoji(console, &color) == 0 && color);
	test_save_retry(host, settings);
	test_encryption_metadata(host);
	test_encryption_lifecycle(host, personal);
	test_safe_kdf(host);
	test_offer_save(host, console);
	stream = fopen(ST_WREN_SETTINGS_SOURCE_DIRECTORY "/scripts/menu_bbs_editor_test.wren", "rb");
	CHECK(stream != NULL);
	size = fread(source, 1, sizeof(source) - 1, stream);
	CHECK(!ferror(stream) && feof(stream) && fclose(stream) == 0);
	source[size] = '\0';
	interpret(host, "menu_bbs_editor_test", source);
	interpret(host, "bbs_editor_results",
	    "import \"menu_bbs_editor_test\" for MenuTest\n"
	    "if (MenuTest.run()[1] != 0) Fiber.abort(\"BBS editor regressions\")\n");
	CHECK(wrenInterpret(host->picker_vm, "forbidden_menu_error",
	    "import \"syncterm_menu\" for MenuError\n") != WREN_RESULT_SUCCESS);
	st_wren_host_destroy(host);
	st_audio_runtime_destroy(audio);
	st_session_history_destroy(history);
	st_console_destroy(console);
	st_console_destroy(picker);
	st_console_display_destroy(console_display);
	st_presentation_font_destroy(&presentation);
	st_hyperlink_registry_destroy(hyperlinks);
	st_settings_shutdown();
	return EXIT_SUCCESS;
}
