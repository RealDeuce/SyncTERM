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
#include "st_theme.h"

#include <limits.h>
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
	FILE *stream = NULL;

	CHECK(fs_open(&stream, path, "wb") == 0);
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
test_transfer_timeout_settings(struct st_wren_host *host)
{
	interpret(host, "transfer_timeout_settings",
	    "import \"syncterm_menu\" for Menu\n"
	    "var snapshot = Menu.settings\n"
	    "var other = Menu.settings\n"
	    "if (snapshot.transferSuccessKeypressTimeout != 0 || snapshot.transferFailureKeypressTimeout != 60 || snapshot.dirty) Fiber.abort(\"transfer timeout defaults\")\n"
	    "snapshot.transferSuccessKeypressTimeout = 2147483647\n"
	    "if (snapshot.transferSuccessKeypressTimeout != 2147483647 || snapshot.transferFailureKeypressTimeout != 60 || !snapshot.dirty) Fiber.abort(\"independent success timeout\")\n"
	    "snapshot.transferSuccessKeypressTimeout = 0\n"
	    "snapshot.transferFailureKeypressTimeout = 2147483647\n"
	    "if (snapshot.transferSuccessKeypressTimeout != 0 || snapshot.transferFailureKeypressTimeout != 2147483647) Fiber.abort(\"independent failure timeout\")\n"
	    "snapshot.transferFailureKeypressTimeout = 0\n"
	    "if (snapshot.transferFailureKeypressTimeout != 0 || other.transferSuccessKeypressTimeout != 0 || other.transferFailureKeypressTimeout != 60 || Menu.settings.transferFailureKeypressTimeout != 60) Fiber.abort(\"transfer timeout snapshot isolation\")\n"
	    "if (snapshot.reload() != null || snapshot.dirty || snapshot.transferSuccessKeypressTimeout != 0 || snapshot.transferFailureKeypressTimeout != 60) Fiber.abort(\"transfer timeout reload\")\n"
	    "for (value in [-1, 0.5, 2147483648, 0/0, 1/0, -1/0, null, true, \"30\", []]) {\n"
	    "  for (assign in [Fn.new { snapshot.transferSuccessKeypressTimeout = value }, Fn.new { snapshot.transferFailureKeypressTimeout = value }]) {\n"
	    "    var bad = Fiber.new { assign.call() }\n"
	    "    bad.try()\n"
	    "    if (bad.error == null || snapshot.dirty || snapshot.transferSuccessKeypressTimeout != 0 || snapshot.transferFailureKeypressTimeout != 60) Fiber.abort(\"invalid transfer timeout changed snapshot\")\n"
	    "  }\n"
	    "}\n");
	interpret(host, "transfer_timeout_settings",
	    "snapshot.transferSuccessKeypressTimeout = 7\n"
	    "snapshot.transferFailureKeypressTimeout = 11\n"
	    "if (snapshot.apply() != null || snapshot.dirty || Menu.settings.transferSuccessKeypressTimeout != 7 || Menu.settings.transferFailureKeypressTimeout != 11) Fiber.abort(\"apply transfer timeouts\")\n"
	    "for (action in [Fn.new { other.transferSuccessKeypressTimeout }, Fn.new { other.transferFailureKeypressTimeout }, Fn.new { other.transferSuccessKeypressTimeout = 1 }, Fn.new { other.transferFailureKeypressTimeout = 1 }]) {\n"
	    "  var bad = Fiber.new { action.call() }\n"
	    "  bad.try()\n"
	    "  if (bad.error == null) Fiber.abort(\"stale transfer timeout snapshot accepted\")\n"
	    "}\n"
	    "snapshot.transferSuccessKeypressTimeout = 19\n"
	    "snapshot.transferFailureKeypressTimeout = 23\n"
	    "if (snapshot.reload() != null || snapshot.dirty || snapshot.transferSuccessKeypressTimeout != 7 || snapshot.transferFailureKeypressTimeout != 11) Fiber.abort(\"transfer timeout reload ignored running settings\")\n"
	    "snapshot.transferSuccessKeypressTimeout = 2147483647\n"
	    "snapshot.transferFailureKeypressTimeout = 0\n"
	    "if (snapshot.save() != null || snapshot.dirty) Fiber.abort(\"save transfer timeouts\")\n");
	CHECK(st_settings.transfer_success_keypress_timeout == INT_MAX
	    && st_settings.transfer_failure_keypress_timeout == 0);
	st_settings.transfer_success_keypress_timeout = 1;
	st_settings.transfer_failure_keypress_timeout = 1;
	CHECK(st_settings_reload() == 0);
	CHECK(st_settings.transfer_success_keypress_timeout == INT_MAX
	    && st_settings.transfer_failure_keypress_timeout == 0);
	interpret(host, "transfer_timeout_settings",
	    "var loaded = Menu.settings\n"
	    "if (loaded.transferSuccessKeypressTimeout != 2147483647 || loaded.transferFailureKeypressTimeout != 0) Fiber.abort(\"transfer timeout persistence\")\n"
	    "loaded.transferSuccessKeypressTimeout = 0\n"
	    "loaded.transferFailureKeypressTimeout = 60\n"
	    "if (loaded.save() != null) Fiber.abort(\"restore transfer timeout defaults\")\n");
}

static void
test_entry_provenance(struct st_wren_host *host, struct st_app_paths *paths,
	const char *personal)
{
	char shared[sizeof(ST_WREN_SETTINGS_TEST_DIRECTORY) + 64];
	char named[sizeof(ST_WREN_SETTINGS_TEST_DIRECTORY) + 64];
	char source[16384];
	FILE *stream;
	size_t size;

	CHECK(paths->shared_list == NULL && st_settings_web_download_count() == 0);
	CHECK(snprintf(shared, sizeof(shared), "%s/provenance-shared.lst",
	    ST_WREN_SETTINGS_TEST_DIRECTORY) < (int)sizeof(shared));
	CHECK(snprintf(named, sizeof(named), "%s/Caf\xc3\xa9.lst",
	    ST_WREN_SETTINGS_TEST_DIRECTORY) < (int)sizeof(named));
	write_text(personal, "[Personal]\nAddress=personal.example\n"
	    "[Overlap]\nAddress=personal.example\n");
	write_text(shared, "[Shared]\nAddress=shared.example\n"
	    "[Overlap]\nAddress=shared.example\n[Dual]\nAddress=shared-dual.example\n");
	write_text(paths->cached_system_list, "[SystemWeb]\nAddress=system.example\n"
	    "[Overlap]\nAddress=ignored-system.example\n[Dual]\nAddress=system-dual.example\n");
	write_text(named, "[Named]\nAddress=named.example\n"
	    "[Overlap]\nAddress=ignored-named.example\n[Dual]\nAddress=ignored-named.example\n");
	paths->shared_list = shared;
	CHECK(st_settings_web_download_append("Caf\xc3\xa9", "https://invalid/") == 0);
	stream = fopen(ST_WREN_SETTINGS_SOURCE_DIRECTORY
	    "/src/scripting/wren_host/tests/bbs_provenance.wren", "rb");
	CHECK(stream != NULL);
	size = fread(source, 1, sizeof(source) - 1, stream);
	CHECK(!ferror(stream) && feof(stream) && fclose(stream) == 0);
	source[size] = '\0';
	interpret(host, "bbs_provenance", source);
	struct st_wren_bbs_record *shared_record = NULL;
	for (size_t i = 0; i < host->bbs_model.count; ++i) {
		if (strcmp(host->bbs_model.entries[i]->profile.name, "Shared") == 0)
			shared_record = host->bbs_model.entries[i];
	}
	CHECK(shared_record != NULL);
	char *original_name = shared_record->source_name;
	char decomposed[] = "Cafe\xcc\x81";
	char malformed[] = "\xff";
	shared_record->source_name = decomposed;
	interpret(host, "bbs_provenance",
	    "var normalized = ProvenanceTest.shared.sourceName\n"
	    "if (normalized != \"Caf\\u00e9\") Fiber.abort(\"source name NFC\")\n");
	CHECK(strcmp(shared_record->source_name, "Cafe\xcc\x81") == 0);
	shared_record->source_name = malformed;
	interpret(host, "bbs_provenance",
	    "var invalidName = ProvenanceTest.shared.sourceName\n"
	    "if (!(invalidName is MenuError) || invalidName.code != IOErr.invalidEncoding || invalidName.nativeCode != IOErr.invalidEncoding || normalized != \"Caf\\u00e9\") Fiber.abort(\"source name conversion result\")\n");
	shared_record->source_name = original_name;
	st_wren_test_diagnostic_copy_status = IO_ERROR_OUT_OF_MEMORY;
	interpret(host, "bbs_provenance",
	    "var allocation = ProvenanceTest.shared.sourceName\n"
	    "if (!(allocation is MenuError) || allocation.code != IOErr.outOfMemory || allocation.nativeCode != IOErr.outOfMemory) Fiber.abort(\"source name allocation result\")\n"
	    "if (ProvenanceTest.shared.source != BBSListSource.shared) Fiber.abort(\"conversion invalidated source\")\n");
	st_wren_test_diagnostic_copy_status = 0;
	int *failures[] = {&st_wren_test_list_open_status,
	    &st_wren_test_list_sort_status, &st_wren_test_model_build_status};
	for (size_t i = 0; i < sizeof(failures) / sizeof(*failures); ++i) {
		*failures[i] = IO_ERROR_IO;
		interpret(host, "bbs_provenance",
		    "{\nvar failure = Menu.load(null)\n"
		    "if (!(failure is MenuError) || failure.code != IOErr.io) Fiber.abort(\"provenance failed-load fixture\")\n"
		    "ProvenanceTest.retained_()\n}\n");
		*failures[i] = 0;
	}
	/* Native source-name storage must not normalize eagerly during loading. */
	st_wren_test_diagnostic_copy_status = IO_ERROR_OUT_OF_MEMORY;
	interpret(host, "bbs_provenance",
	    "if (Menu.load(null) != null) Fiber.abort(\"load eagerly converted provenance\")\n");
	st_wren_test_diagnostic_copy_status = 0;
	interpret(host, "bbs_provenance",
	    "ProvenanceTest.replaced_()\n"
	    "var selectedHidden = ProvenanceTest.named_(\"Dual\").shadowed\n");
	struct st_profile selected;
	st_profile_init(&selected);
	wrenEnsureSlots(host->vm, 1);
	wrenGetVariable(host->vm, "bbs_provenance", "selectedHidden", 0);
	CHECK(st_wren_menu_copy_profile(host->vm, 0, &selected));
	CHECK(selected.source == ST_PROFILE_SOURCE_SYSTEM
	    && strcmp(selected.address, "system-dual.example") == 0);
	st_profile_dispose(&selected);
	CHECK(st_settings_web_download_remove(0) == 0);
	paths->shared_list = NULL;
	CHECK(fs_remove_file(shared) == 0 && fs_remove_file(named) == 0);
	write_text(paths->cached_system_list, "");
	write_text(personal, "[Existing]\nAddress=existing.example\n");
	interpret(host, "bbs_provenance", "if (Menu.load(null) != null) Fiber.abort(\"restore provenance fixture\")\n");
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
	interpret(host, "native_menu_results", "if (Menu.load(null) != null) Fiber.abort(\"conflict fixture reload\")\n");
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

static void
test_load_results(struct st_wren_host *host, struct st_app_paths *paths,
	const char *personal, st_console_t *console)
{
	static const char long_section[] =
	    "[1234567890123456789012345678901]\nAddress=skipped.example\n";
	char shared[sizeof(ST_WREN_SETTINGS_TEST_DIRECTORY) + 32];
	char named[sizeof(ST_WREN_SETTINGS_TEST_DIRECTORY) + 32];
	char script[1024];
	struct st_console_event pending;
	CHECK(snprintf(shared, sizeof(shared), "%s/shared.lst",
	    ST_WREN_SETTINGS_TEST_DIRECTORY) < (int)sizeof(shared));
	CHECK(snprintf(named, sizeof(named), "%s/Caf\xc3\xa9.lst",
	    ST_WREN_SETTINGS_TEST_DIRECTORY) < (int)sizeof(named));
	CHECK(st_settings_web_download_count() == 0);
	CHECK(snprintf(script, sizeof(script), "[Existing]\nAddress=existing.example\n%s",
	    long_section) < (int)sizeof(script));
	write_text(personal, script);
	write_text(shared, long_section);
	write_text(paths->cached_system_list, "\xff");
	write_text(named, long_section);
	paths->shared_list = shared;
	CHECK(st_settings_web_download_append("Caf\xc3\xa9", "https://invalid/") == 0);
	interpret(host, "load_diagnostics",
	    "import \"syncterm\" for IOErr\n"
	    "import \"syncterm_menu\" for Menu, MenuError, BBSListSource, BBSLoadDiagnostic\n"
	    "import \"main_menu\" for MainMenu\n"
	    "if (Menu.load(null) != null) Fiber.abort(\"warning load failed\")\n"
	    "var saved = Menu.loadDiagnostics\n"
	    "var oldEntry = Menu.entries.where {|entry| entry.name == \"Existing\" }.toList[0]\n"
	    "var oldDefaults = Menu.defaults\n"
	    "var oldCache = oldEntry.cache\n"
	    "oldCache.delete(\"load-result.tmp\")\n"
	    "var oldFile = oldCache.create(\"load-result.tmp\")\n"
	    "if (saved.count != 4) Fiber.abort(\"missing source warnings\")\n"
	    "var sources = [BBSListSource.personal, BBSListSource.shared, BBSListSource.webSystem, BBSListSource.webNamed]\n"
	    "var expected = [IOErr.nameTooLong, IOErr.nameTooLong, IOErr.invalidEncoding, IOErr.nameTooLong]\n"
	    "for (i in 0...saved.count) {\n"
	    "  var item = saved[i]\n"
	    "  var error = item.error\n"
	    "  if (!(item is BBSLoadDiagnostic) || item.source != sources[i] || sources[i] != i || !(error is MenuError) || error.code != expected[i] || error.nativeCode != expected[i]) Fiber.abort(\"diagnostic native mapping\")\n"
	    "  if (item.error == error) Fiber.abort(\"diagnostic cached error\")\n"
	    "}\n"
	    "if (saved[0].sourceName != null || saved[0].sectionName != \"1234567890123456789012345678901\" || saved[2].sourceName != \"System List\" || saved[2].sectionName != null || saved[3].sourceName != \"Caf\xc3\xa9\") Fiber.abort(\"nullable diagnostic names\")\n"
	    "var separate = Menu.loadDiagnostics\n"
	    "separate.clear()\n"
	    "if (saved.count != 4 || Menu.loadDiagnostics.count != 4) Fiber.abort(\"borrowed diagnostic list\")\n"
	    "for (action in [Fn.new { saved[0].source = 2 }, Fn.new { saved[0].sourceName = \"x\" }, Fn.new { saved[0].sectionName = \"x\" }, Fn.new { saved[0].error = null }, Fn.new { BBSLoadDiagnostic.new() }, Fn.new { Menu.statusMessage(0) }]) {\n"
	    "  var bad = Fiber.new { action.call() }\n"
	    "  bad.try()\n"
	    "  if (bad.error == null) Fiber.abort(\"diagnostic mutable/constructible or obsolete status method\")\n"
	    "}\n"
	    "var message = MainMenu.loadWarnings_()\n"
	    "if (!message.contains(\"Caf\xc3\xa9\") || !message.contains(\"System List\") || !message.contains(\"1234567890123456789012345678901\") || !message.contains(\"Invalid encoding\") || !message.contains(\"truncated\") || MainMenu.loadWarningsAutomatic_() != message) Fiber.abort(\"warning summary lost detail\")\n");
	CHECK(!wrenHasVariable(host->vm, "syncterm_menu", "MenuReadStatus"));
	st_bbs_list_t *original = host->bbs_list;
	uint64_t generation = host->bbs_model.generation;
	int *controls[] = {&st_wren_test_list_open_status,
	    &st_wren_test_list_sort_status, &st_wren_test_model_build_status};
	const int statuses[] = {IO_ERROR_PASSWORD_REQUIRED, IO_ERROR_DECRYPTION_FAILED,
	    IO_ERROR_INVALID_FORMAT, IO_ERROR_PERMISSION_DENIED};
	for (size_t stage = 0; stage < 3; ++stage) {
		for (size_t i = 0; i < sizeof(statuses) / sizeof(statuses[0]); ++i) {
			*controls[stage] = statuses[i];
			CHECK(snprintf(script, sizeof(script),
			    "{\nvar failure = Menu.load(null)\n"
			    "if (!(failure is MenuError) || failure.code != %d || failure.nativeCode != %d || !failure.message.contains(\"%s\") || Menu.loadDiagnostics.count != 4) Fiber.abort(\"load reason or retained warnings\")\n}\n",
			    statuses[i], statuses[i], stage == 0 ? "loaded" : stage == 1 ? "sorted" : "constructed") < (int)sizeof(script));
			interpret(host, "load_diagnostics", script);
			CHECK(host->bbs_list == original && host->bbs_model.generation == generation);
			*controls[stage] = 0;
		}
	}
	interpret(host, "load_diagnostics",
	    "if (oldEntry.name != \"Existing\" || oldDefaults.addr != \"\" || oldFile.open() != null || oldFile.close() != null) Fiber.abort(\"failed load revoked views/grants\")\n"
	    "var cacheEntries = oldCache.list\n"
	    "if (oldCache.delete(\"load-result.tmp\") != null) Fiber.abort(\"grant fixture cleanup\")\n");
	struct st_bbs_diagnostic injected = {
		.source = ST_BBS_SOURCE_WEB_NAMED,
		.source_name = "Cafe\xcc\x81", .section_name = "A\xcc\x8a",
		.status = IO_ERROR_PERMISSION_DENIED,
	};
	st_wren_test_diagnostic = &injected;
	interpret(host, "load_diagnostics",
	    "var normalized = Menu.loadDiagnostics[0]\n"
	    "if (normalized.sourceName != \"Caf\xc3\xa9\" || normalized.sectionName != \"\xc3\x85\" || normalized.error.code != IOErr.permissionDenied) Fiber.abort(\"diagnostic normalization\")\n");
	injected.source_name = "\xff";
	interpret(host, "load_diagnostics",
	    "var badText = Fiber.new { Menu.loadDiagnostics }.try()\n"
	    "if (!(badText is MenuError) || badText.code != IOErr.invalidEncoding) Fiber.abort(\"malformed diagnostic text\")\n"
	    "if (normalized.sourceName != \"Caf\xc3\xa9\") Fiber.abort(\"diagnostic borrowed names\")\n");
	/* Retrieval failure is a warning after a successful load, not load failure. */
	post_key(console, 13);
	interpret(host, "load_diagnostics",
	    "if (!MainMenu.prepare_(false)) Fiber.abort(\"warning retrieval failed the load\")\n");
	st_wren_test_diagnostic = NULL;
	st_wren_test_diagnostic_copy_status = IO_ERROR_OUT_OF_MEMORY;
	interpret(host, "load_diagnostics",
	    "if (Menu.load(null) != null) Fiber.abort(\"load eagerly copied warning text\")\n"
	    "var allocation = Fiber.new { Menu.loadDiagnostics }.try()\n"
	    "if (!(allocation is MenuError) || allocation.code != IOErr.outOfMemory || Menu.entries.where {|entry| entry.name == \"Existing\" }.count != 1) Fiber.abort(\"diagnostic allocation failure\")\n");
	st_wren_test_diagnostic_copy_status = 0;
	host->config.safe_mode = true;
	post_key(console, 0x5100);
	post_key(console, 13);
	interpret(host, "load_diagnostics",
	    "if (Menu.loadDiagnostics.count != 4 || !MainMenu.prepare_(false)) Fiber.abort(\"safe warning flow\")\n");
	host->config.safe_mode = false;
	post_key(console, 13);
	interpret(host, "load_diagnostics",
	    "if (!MainMenu.prepare_(false) || Menu.loadDiagnostics.count != 4 || MainMenu.loadWarnings_() == null) Fiber.abort(\"repeat load lost warnings\")\n");
	CHECK(st_console_event_try(console, &pending) == 0
	    && pending.type == ST_CONSOLE_EVENT_KEY_CODE && pending.data.key.code == 13);
	post_key(console, 27);
	interpret(host, "load_diagnostics",
	    "if (!MainMenu.prepare_(true)) Fiber.abort(\"repeat exit warning load failed\")\n");
	CHECK(st_console_event_try(console, &pending) == 0
	    && pending.type == ST_CONSOLE_EVENT_KEY_CODE && pending.data.key.code == 27);
	/* Retrieval failures still show every time, even after acknowledging warnings. */
	st_wren_test_diagnostic_copy_status = IO_ERROR_OUT_OF_MEMORY;
	for (unsigned attempt = 0; attempt < 2; ++attempt) {
		post_key(console, 13);
		interpret(host, "load_diagnostics",
		    "if (!MainMenu.prepare_(false)) Fiber.abort(\"repeat retrieval failure failed load\")\n");
		CHECK(st_console_event_try(console, &pending) == IO_ERROR_WOULD_BLOCK);
	}
	st_wren_test_diagnostic_copy_status = 0;
	/* Changed warnings still open a dialog, including through the exit path. */
	write_text(shared, "[Changed warning 12345678901234567890]\nAddress=changed.example\n");
	post_key(console, 27);
	interpret(host, "load_diagnostics",
	    "if (!MainMenu.prepare_(true)) Fiber.abort(\"exit warning flow\")\n"
	    "import \"syncterm\" for Key, KeyEvent\n"
	    "var pane = MainMenu.loadWarningsPane_(MainMenu.loadWarnings_() * 10, true, 0)\n"
	    "if (!pane.atExit || pane.title != \"Directory Warnings\" || pane.messageScrollTop != 0) Fiber.abort(\"warning pane configuration\")\n"
	    "pane.handle(KeyEvent.new(Key.pageDown))\n"
	    "if (pane.messageScrollTop <= 0) Fiber.abort(\"warnings do not scroll\")\n"
	    "pane.handle(KeyEvent.new(Key.pageUp))\n"
	    "if (pane.messageScrollTop != 0) Fiber.abort(\"warnings cannot scroll back\")\n"
	    "pane.handle(KeyEvent.new(Key.down))\n"
	    "if (pane.messageScrollTop != 1) Fiber.abort(\"warnings Down must scroll one line\")\n"
	    "pane.handle(KeyEvent.new(Key.up))\n"
	    "if (pane.messageScrollTop != 0) Fiber.abort(\"warnings Up must scroll one line\")\n"
	    "pane.handle(KeyEvent.new(Key.end))\n"
	    "var bottom = pane.messageScrollTop\n"
	    "if (bottom <= 1) Fiber.abort(\"warnings End must reach bottom\")\n"
	    "pane.handle(KeyEvent.new(Key.down))\n"
	    "if (pane.messageScrollTop != bottom) Fiber.abort(\"warnings Down must clamp at bottom\")\n"
	    "pane.handle(KeyEvent.new(Key.home))\n"
	    "pane.handle(KeyEvent.new(Key.up))\n"
	    "if (pane.messageScrollTop != 0) Fiber.abort(\"warnings Home must reach top and Up must clamp\")\n");
	CHECK(st_console_event_try(console, &pending) == IO_ERROR_WOULD_BLOCK);
	/* Both native password failures enter the real prompt; Escape cancels. */
	for (size_t i = 0; i < 2; ++i) {
		st_wren_test_list_open_status = statuses[i];
		post_key(console, 27);
		interpret(host, "load_diagnostics",
		    "if (MainMenu.prepare_(false)) Fiber.abort(\"password cancellation\")\n");
	}
	st_wren_test_list_open_status = IO_ERROR_PERMISSION_DENIED;
	post_key(console, 13);
	interpret(host, "load_diagnostics",
	    "if (MainMenu.prepare_(true)) Fiber.abort(\"fatal error reported success\")\n");
	st_wren_test_list_open_status = 0;
	if (st_list_crypto_encryption_available()) {
		interpret(host, "load_diagnostics",
		    "if (Menu.setEncryption(6, 128, \"secret\") != null) Fiber.abort(\"password load fixture\")\n"
		    "MainMenu.password = null\n");
		const char *answers = "wrong\rsecret\r";
		for (const char *key = answers; *key != '\0'; ++key)
			post_key(console, (uint16_t)*key);
		interpret(host, "load_diagnostics",
		    "if (!MainMenu.prepare_(false) || MainMenu.password != \"secret\") Fiber.abort(\"password retry flow\")\n"
		    "if (Menu.setEncryption(0, 0, null) != null) Fiber.abort(\"password fixture reset\")\n"
		    "MainMenu.password = null\n");
	}
	/* Web warnings remain available without opening a modal or consuming input. */
	write_text(personal, "[Existing]\nAddress=existing.example\n");
	write_text(shared, "");
	post_key(console, 13);
	interpret(host, "load_diagnostics",
	    "if (!MainMenu.prepare_(false) || Menu.loadDiagnostics.count != 2 || MainMenu.loadWarningsAutomatic_() != null) Fiber.abort(\"web warnings opened dialog\")\n"
	    "if (!MainMenu.loadWarnings_().contains(\"truncated\") || !MainMenu.loadWarnings_().contains(\"System List\")) Fiber.abort(\"web warnings hidden\")\n");
	CHECK(st_console_event_try(console, &pending) == 0
	    && pending.type == ST_CONSOLE_EVENT_KEY_CODE && pending.data.key.code == 13);
	/* An unrelated failure must still open a dialog with only web warnings. */
	st_wren_test_list_open_status = IO_ERROR_PERMISSION_DENIED;
	post_key(console, 13);
	interpret(host, "load_diagnostics",
	    "if (MainMenu.prepare_(false)) Fiber.abort(\"web warnings hid fatal load error\")\n");
	CHECK(st_console_event_try(console, &pending) == IO_ERROR_WOULD_BLOCK);
	st_wren_test_list_open_status = 0;
	st_wren_test_diagnostic_copy_status = IO_ERROR_OUT_OF_MEMORY;
	post_key(console, 13);
	interpret(host, "load_diagnostics",
	    "if (!MainMenu.prepare_(false)) Fiber.abort(\"web warning retrieval failed the load\")\n");
	CHECK(st_console_event_try(console, &pending) == IO_ERROR_WOULD_BLOCK);
	st_wren_test_diagnostic_copy_status = 0;
	CHECK(st_settings_web_download_remove(0) == 0);
	paths->shared_list = NULL;
	CHECK(fs_remove_file(shared) == 0);
	CHECK(fs_remove_file(named) == 0);
	CHECK(fs_remove_file(paths->cached_system_list) == 0);
	write_text(personal, "[Existing]\nAddress=existing.example\n");
	interpret(host, "load_diagnostics",
	    "if (Menu.load(null) != null || Menu.loadDiagnostics.count != 0 || MainMenu.loadWarnings_() != null) Fiber.abort(\"missing optional lists warned\")\n"
	    "if (!MainMenu.prepare_(false)) Fiber.abort(\"clean warning load failed\")\n"
	    "System.gc()\nSystem.gc()\n"
	    "if (saved[2].error.code != IOErr.invalidEncoding || saved[3].sourceName != \"Caf\xc3\xa9\" || normalized.sectionName != \"\xc3\x85\") Fiber.abort(\"diagnostic snapshot lifetime\")\n");
	/* Once cleared, the same warning is new again. */
	for (unsigned recurrence = 0; recurrence < 2; ++recurrence) {
		write_text(personal, long_section);
		post_key(console, 13);
		interpret(host, "load_diagnostics",
		    "if (!MainMenu.prepare_(false) || Menu.loadDiagnostics.count != 1) Fiber.abort(\"recurring warning after clean load\")\n");
		CHECK(st_console_event_try(console, &pending) == IO_ERROR_WOULD_BLOCK);
		write_text(personal, "[Existing]\nAddress=existing.example\n");
		interpret(host, "load_diagnostics",
		    "if (!MainMenu.prepare_(false)) Fiber.abort(\"restore clean directory\")\n");
	}
}

static void
test_settings_reload_running(struct st_wren_host *host, const char *settings)
{
	char backup[sizeof(ST_WREN_SETTINGS_TEST_DIRECTORY) + 64];
	char contents[128];
	bool running = st_settings.confirm_close;

	CHECK(snprintf(backup, sizeof(backup), "%s.reload-backup", settings)
	    < (int)sizeof(backup));
	CHECK(rename(settings, backup) == 0);
	CHECK(snprintf(contents, sizeof(contents),
	    "[SyncTERM]\nAudioModes=0\nConfirmClose=%s\n",
	    running ? "false" : "true") < (int)sizeof(contents));
	write_text(settings, contents);
	bool passed = wrenInterpret(host->vm, "settings_reload_external",
	    "import \"syncterm_menu\" for Menu\n"
	    "var snapshot = Menu.settings\n"
	    "var before = snapshot.confirmClose\n"
	    "snapshot.confirmClose = !before\n"
	    "if (!snapshot.dirty || snapshot.reload() != null || snapshot.confirmClose != before || snapshot.dirty || Menu.settings.confirmClose != before) Fiber.abort(\"snapshot reload read external settings\")\n")
	    == WREN_RESULT_SUCCESS;
	CHECK(fs_remove_file(settings) == 0);
	CHECK(rename(backup, settings) == 0);
	CHECK(passed && st_settings.confirm_close == running);
}

static void
test_theme_results(struct st_wren_host *host, const char *directory)
{
	char path[sizeof(ST_WREN_SETTINGS_TEST_DIRECTORY) + 64];
	char other[sizeof(path)];
	char source[1024];
	CHECK(snprintf(path, sizeof(path), "%s/theme-result.ini", directory) < (int)sizeof(path));
	CHECK(snprintf(other, sizeof(other), "%s/theme-other.ini", directory) < (int)sizeof(other));
	(void)fs_remove_file(path);
	(void)fs_remove_file(other);
	interpret(host, "theme_results",
	    "import \"syncterm\" for IOErr, Key, KeyEvent, Host\n"
	    "import \"syncterm_menu\" for Menu, MenuError, ThemeDocument, ThemeSaveErr\n"
	    "import \"menu_theme_editor\" for ThemeEditor\n"
	    "import \"menu_settings_ui\" for SettingsMenu\n"
	    "import \"menu_settings_ui_test\" for SettingsTestApp, SettingsMenuTest\n"
	    "var missing = Menu.openThemeDocument(\"absent-theme.ini\")\n"
	    "if (!(missing is MenuError) || missing.code != IOErr.notFound || missing.nativeCode != IOErr.notFound) Fiber.abort(\"theme open lost native reason\")\n"
	    "var empty = Fiber.new { Menu.openThemeDocument(\"\") }\n"
	    "empty.try()\n"
	    "if (empty.error == null) Fiber.abort(\"empty theme open accepted\")\n"
	    "var doc = Menu.newThemeDocument()\n"
	    "if (!(doc is ThemeDocument)) Fiber.abort(\"theme creation\")\n"
	    "var semantic = doc.setMetadata(0, \"\")\n"
	    "if (!(semantic is MenuError) || semantic.code != IOErr.invalidArgument || semantic.nativeCode != IOErr.invalidArgument) Fiber.abort(\"theme semantic result\")\n"
	    "for (action in [Fn.new { doc.setStyle(\"missing\", 2, 2, 0) }, Fn.new { doc.removeStyle(\"missing\") }, Fn.new { doc.setGlyph(\"missing\", 0, 2, 1) }, Fn.new { doc.removeGlyph(\"missing\") }, Fn.new { doc.importTheme(\"absent-theme.ini\") }]) {\n"
	    "  var failure = action.call()\n"
	    "  if (!(failure is MenuError) || failure.code != IOErr.notFound || failure.nativeCode != failure.code) Fiber.abort(\"theme action lost native reason\")\n"
	    "}\n"
	    "for (action in [Fn.new { doc.setStyle(\"default\", 100, 2, 0) }, Fn.new { doc.setMetadata(0, String.fromByte(255)) }, Fn.new { doc.setMetadata(0, \"a\\0b\") }]) {\n"
	    "  var bad = Fiber.new { action.call() }\n"
	    "  bad.try()\n"
	    "  if (bad.error == null) Fiber.abort(\"invalid theme arguments accepted\")\n"
	    "}\n");
	CHECK(snprintf(source, sizeof(source),
	    "if ([ThemeSaveErr.ok,ThemeSaveErr.invalid,ThemeSaveErr.conflict,ThemeSaveErr.exists,ThemeSaveErr.readOnly,ThemeSaveErr.io].join() != [%d,%d,%d,%d,%d,%d].join()) Fiber.abort(\"theme save enum drift\")\n",
	    ST_THEME_DOCUMENT_OK, ST_THEME_DOCUMENT_INVALID, ST_THEME_DOCUMENT_CONFLICT,
	    ST_THEME_DOCUMENT_EXISTS, ST_THEME_DOCUMENT_READ_ONLY, ST_THEME_DOCUMENT_IO) < (int)sizeof(source));
	interpret(host, "theme_results", source);
	for (int result = ST_THEME_DOCUMENT_INVALID; result <= ST_THEME_DOCUMENT_IO; ++result) {
		st_wren_test_theme_save_result = result;
		CHECK(snprintf(source, sizeof(source),
		    "{\nvar result = doc.save(\"theme-result.ini\", false)\n"
		    "if (!(result is MenuError) || result.code != null || result.nativeCode != %d || !doc.dirty) Fiber.abort(\"document enum was classified as portable I/O\")\n}\n", result) < (int)sizeof(source));
		interpret(host, "theme_results", source);
	}
	st_wren_test_theme_save_result = 0;
	host->config.safe_mode = true;
	unsigned saves = st_wren_test_theme_save_calls;
	interpret(host, "theme_results",
	    "var blocked = doc.save(\"theme-result.ini\", true)\n"
	    "if (!(blocked is MenuError) || blocked.code != IOErr.readOnly || blocked.nativeCode != null) Fiber.abort(\"theme safe save result\")\n"
	    "for (failure in [Menu.deleteTheme(\"theme-result.ini\"),Menu.deleteCloudTheme(\"official/missing\")]) if (!(failure is MenuError) || failure.code != IOErr.readOnly || failure.nativeCode != null) Fiber.abort(\"theme safe deletion\")\n");
	CHECK(st_wren_test_theme_save_calls == saves);
	host->config.safe_mode = false;
	st_wren_test_theme_catalog_status = IO_ERROR_PERMISSION_DENIED;
	interpret(host, "theme_results",
	    "var catalog = Menu.themes\n"
	    "if (!(catalog is MenuError) || catalog.code != IOErr.permissionDenied) Fiber.abort(\"catalog result\")\n"
	    "var catalogApp = SettingsTestApp.new([])\n"
	    "SettingsMenu.themes_(catalogApp, null)\n"
	    "if (catalogApp.alerts != 1) Fiber.abort(\"browser used failed catalog\")\n");
	st_wren_test_theme_catalog_status = 0;
	st_wren_test_theme_create_status = IO_ERROR_OUT_OF_MEMORY;
	st_wren_test_theme_open_status = IO_ERROR_IO;
	st_wren_test_theme_copy_status = IO_ERROR_PROTOCOL;
	interpret(host, "theme_results",
	    "var constructors = [Menu.newThemeDocument(),Menu.openThemeDocument(\"theme-result.ini\"),Menu.copyCloudTheme(\"official/missing\")]\n"
	    "var reasons = [IOErr.outOfMemory,IOErr.io,IOErr.protocol]\n"
	    "for (i in 0...constructors.count) if (!(constructors[i] is MenuError) || constructors[i].code != reasons[i] || constructors[i].nativeCode != reasons[i]) Fiber.abort(\"constructor result\")\n"
	    "var newApp = SettingsTestApp.new([Fn.new {|pane| pane.handle(KeyEvent.new(Key.insert)) },SettingsMenuTest.cancel_])\n"
	    "SettingsMenu.themes_(newApp, null)\n"
	    "if (newApp.alerts != 1 || newApp.remaining != 0) Fiber.abort(\"browser used failed constructor\")\n");
	st_wren_test_theme_create_status = 0;
	st_wren_test_theme_open_status = 0;
	st_wren_test_theme_copy_status = 0;
	st_wren_test_theme_settings_copy_failure = true;
	interpret(host, "theme_results",
	    "var copyFailure = Menu.selectTheme(\"theme-result.ini\")\n"
	    "if (!(copyFailure is MenuError) || copyFailure.code != null || copyFailure.nativeCode != null) Fiber.abort(\"theme Boolean failure invented native status\")\n");
	st_wren_test_theme_settings_copy_failure = false;
	st_wren_test_theme_warning = "cached manifest retained";
	interpret(host, "theme_results",
	    "if (Menu.refreshCloudThemes() != \"cached manifest retained\") Fiber.abort(\"success warning became failure\")\n"
	    "var warningApp = SettingsTestApp.new([])\n"
	    "SettingsMenu.cloudThemes_(warningApp, null)\n"
	    "if (warningApp.alerts != 1) Fiber.abort(\"cloud warning not displayed\")\n");
	st_wren_test_theme_refresh_status = IO_ERROR_PROTOCOL;
	interpret(host, "theme_results",
	    "var refresh = Menu.refreshCloudThemes()\n"
	    "if (!(refresh is MenuError) || refresh.code != IOErr.protocol || refresh.nativeCode != IOErr.protocol || !refresh.message.contains(\"cached manifest retained\")) Fiber.abort(\"cloud refresh failure became warning\")\n"
	    "var refreshApp = SettingsTestApp.new([])\n"
	    "SettingsMenu.cloudThemes_(refreshApp, null)\n"
	    "if (refreshApp.alerts != 1) Fiber.abort(\"cloud failure display\")\n");
	st_wren_test_theme_refresh_status = 0;
	st_wren_test_theme_warning = "";
	interpret(host, "theme_results",
	    "if (Menu.refreshCloudThemes() != null) Fiber.abort(\"clean refresh result\")\n"
	    "if (doc.save(\"theme-result.ini\", false) != null || doc.dirty || doc.canUndo) Fiber.abort(\"save result\")\n"
	    "if (Menu.selectTheme(doc.filename) != null || doc.setMetadata(0, \"Edited Theme\") != null || doc.save(null, false) != null) Fiber.abort(\"selected save setup\")\n");
	CHECK(strcmp(st_theme_active(host->themes)->name, "New Theme") == 0);
	CHECK(fs_remove_file(path) == 0);
	host->config.safe_mode = true;
	uint64_t theme_generation = st_theme_generation(host->themes);
	uint64_t settings_generation = host->settings_generation;
	interpret(host, "theme_results", "if (doc.activate() != null) Fiber.abort(\"activation reread file or rejected safe mode\")\n");
	CHECK(st_theme_generation(host->themes) == theme_generation + 1);
	CHECK(host->settings_generation == settings_generation);
	CHECK(strcmp(st_theme_active(host->themes)->name, "Edited Theme") == 0);
	host->config.safe_mode = false;
	interpret(host, "theme_results", "if (doc.save(null, true) != null || doc.setMetadata(2, \"Editor save\") != null) Fiber.abort(\"editor setup\")\n");
	st_wren_test_theme_activate_status = IO_ERROR_OUT_OF_MEMORY;
	saves = st_wren_test_theme_save_calls;
	unsigned activations = st_wren_test_theme_activate_calls;
	interpret(host, "theme_results",
	    "var editorApp = SettingsTestApp.new([])\n"
	    "if (!ThemeEditor.save_(editorApp, doc, false) || editorApp.alerts != 1 || doc.dirty) Fiber.abort(\"activation failure repeated persistence\")\n");
	CHECK(st_wren_test_theme_save_calls == saves + 1 && st_wren_test_theme_activate_calls == activations + 1);
	st_wren_test_theme_activate_status = 0;
	write_text(path, "Name=External change\n");
	saves = st_wren_test_theme_save_calls;
	interpret(host, "theme_results",
	    "doc.setMetadata(1, \"Author\")\n"
	    "var declined = SettingsTestApp.new([SettingsMenuTest.answer_(false)])\n"
	    "if (ThemeEditor.save_(declined, doc, false) || !doc.dirty || declined.alerts != 0 || declined.remaining != 0) Fiber.abort(\"conflict confirmation\")\n");
	CHECK(st_wren_test_theme_save_calls == saves + 1);
	saves = st_wren_test_theme_save_calls;
	interpret(host, "theme_results",
	    "var confirmed = SettingsTestApp.new([SettingsMenuTest.answer_(true)])\n"
	    "if (!ThemeEditor.save_(confirmed, doc, false) || doc.dirty || confirmed.remaining != 0) Fiber.abort(\"explicit force save\")\n");
	CHECK(st_wren_test_theme_save_calls == saves + 2);
	write_text(other, "Name=Other\n");
	interpret(host, "theme_results",
	    "var saveAs = SettingsTestApp.new([SettingsMenuTest.answer_(\"theme-other.ini\"),SettingsMenuTest.answer_(false)])\n"
	    "if (ThemeEditor.save_(saveAs, doc, true) || doc.filename != \"theme-result.ini\") Fiber.abort(\"exists confirmation\")\n");
	theme_generation = st_theme_generation(host->themes);
	saves = st_wren_test_theme_save_calls;
	interpret(host, "theme_results",
	    "var replaceOther = SettingsTestApp.new([SettingsMenuTest.answer_(\"theme-other.ini\"),SettingsMenuTest.answer_(true)])\n"
	    "if (!ThemeEditor.save_(replaceOther, doc, true) || doc.dirty || doc.filename != \"theme-other.ini\" || Menu.selectedThemeFile != \"theme-result.ini\" || replaceOther.alerts != 0) Fiber.abort(\"save-as changed selection or failed explicit replacement\")\n");
	CHECK(st_wren_test_theme_save_calls == saves + 2);
	CHECK(st_theme_generation(host->themes) == theme_generation);
	st_wren_test_settings_save_status = IO_ERROR_NO_SPACE;
	st_wren_test_theme_restore_status = IO_ERROR_IO;
	interpret(host, "theme_results",
	    "var restoration = Menu.selectTheme(\"theme-result.ini\")\n"
	    "if (!(restoration is MenuError) || restoration.code != IOErr.noSpace || restoration.nativeCode != IOErr.noSpace || !restoration.message.contains(\"Restoration failed\")) Fiber.abort(\"restoration hid primary status\")\n");
	st_wren_test_settings_save_status = 0;
	st_wren_test_theme_restore_status = 0;
	interpret(host, "theme_results",
	    "System.gc()\nSystem.gc()\n"
	    "if (constructors[0].code != IOErr.outOfMemory || !constructors[0].message.contains(\"theme\")) Fiber.abort(\"theme error lifetime\")\n"
	    "if (Menu.selectTheme(\"\") != null || Menu.deleteTheme(\"theme-result.ini\") != null || Menu.deleteTheme(\"theme-other.ini\") != null) Fiber.abort(\"theme fixture cleanup\")\n");
	st_wren_test_theme_warning = NULL;
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
	write_text(cached, "");
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
	interpret(host, "load_result_contract",
	    "import \"syncterm_menu\" for Menu\n"
	    "if (Menu.loadDiagnostics.count != 0) Fiber.abort(\"unloaded warnings\")\n"
	    "if (Menu.load(null) != null) Fiber.abort(\"successful load must return null\")\n"
	    "if (Menu.loadDiagnostics.count != 0) Fiber.abort(\"clean load has warnings\")\n");
	interpret(host, "settings_setup",
	    "import \"syncterm_menu\" for Menu\n"
	    "if (Menu.load(null) != null) Fiber.abort(\"load fixture\")\n");
	test_transfer_timeout_settings(host);
	test_entry_provenance(host, &paths, personal);
	test_load_results(host, &paths, personal, console);
	interpret(host, "theme_result_regression",
	    "import \"syncterm\" for IOErr\n"
	    "import \"syncterm_menu\" for Menu, MenuError\n"
	    "var failure = Menu.previewTheme(\"absent-theme.ini\")\n"
	    "if (!(failure is MenuError) || failure.code != IOErr.notFound || failure.nativeCode != IOErr.notFound) Fiber.abort(\"theme failures must preserve the native MenuError\")\n");
	test_settings_reload_running(host, settings);
	test_menu_results(host);
	test_menu_native_failures(host, personal);
	stream = fopen(ST_WREN_SETTINGS_SOURCE_DIRECTORY "/scripts/menu_settings_ui_test.wren", "rb");
	CHECK(stream != NULL);
	size = fread(source, 1, sizeof(source) - 1, stream);
	CHECK(!ferror(stream) && feof(stream));
	CHECK(fclose(stream) == 0);
	source[size] = '\0';
	interpret(host, "menu_settings_ui_test", source);
	test_theme_results(host, directory);
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
