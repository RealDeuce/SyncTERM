/* Isolated byte and mapped-file digest contracts, including provider=none. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "wren_host_internal.h"
#include "filesystem.h"
#include "st_app_paths.h"
#include "st_crypto.h"
#include "io_error.h"
#include "string_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); \
	exit(EXIT_FAILURE); } } while (false)

extern unsigned st_wren_test_digest_map_opens, st_wren_test_digest_map_closes;

extern int st_wren_test_digest_status, st_wren_test_digest_map_status;
extern unsigned st_wren_test_digest_calls;
extern unsigned st_wren_test_digest_map_calls, st_wren_test_digest_flush_calls;
extern bool st_wren_test_digest_direct_mapping, st_wren_test_digest_flush_fail;
void st_wren_test_digest_push_grant(WrenVM *vm, const char *path, bool stale, bool save);

static char *
read_source(void)
{
	FILE *stream = fopen(ST_WREN_DIGEST_TEST_SOURCE, "rb");
	CHECK(stream != NULL);
	CHECK(fseek(stream, 0, SEEK_END) == 0);
	long length = ftell(stream);
	CHECK(length >= 0 && length < 65536);
	char *source = malloc((size_t)length + 1);
	CHECK(source != NULL && fseek(stream, 0, SEEK_SET) == 0);
	CHECK(fread(source, 1, (size_t)length, stream) == (size_t)length);
	source[length] = '\0';
	CHECK(fclose(stream) == 0);
	return source;
}

static bool
interpret(WrenVM *vm, st_wren_connected_t *connected, const char *source)
{
	return connected != NULL
	    ? st_wren_connected_interpret(connected, "digest_test", source) == 0
	    : wrenInterpret(vm, "digest_test", source) == WREN_RESULT_SUCCESS;
}

static void
install_grant(WrenVM *vm, const char *path, bool stale, bool save)
{
	wrenEnsureSlots(vm, 3);
	st_wren_test_digest_push_grant(vm, path, stale, save);
	wrenGetVariable(vm, "digest_test", "DigestChecks", 0);
	WrenHandle *setter = wrenMakeCallHandle(vm, "granted=(_)");
	CHECK(wrenCall(vm, setter) == WREN_RESULT_SUCCESS);
	wrenReleaseHandle(vm, setter);
}

static void
file_boundaries(WrenVM *vm)
{
	unsigned calls = st_wren_test_digest_calls;
	unsigned maps = st_wren_test_digest_map_calls;
	unsigned flushes = st_wren_test_digest_flush_calls;
	CHECK(interpret(vm, NULL,
	    "DigestChecks.file.open()\nDigestChecks.file.writeBytes(\"pending\")\n"
	    "DigestChecks.rejects(Fn.new { DigestChecks.file.digest(null) })\n"));
	CHECK(st_wren_test_digest_calls == calls && st_wren_test_digest_map_calls == maps
	    && st_wren_test_digest_flush_calls == flushes);
	st_wren_test_digest_flush_fail = true;
	CHECK(interpret(vm, NULL,
	    "var flush = DigestChecks.file.digest(Digest.sha256)\n"
	    "if (!(flush is FileError) || flush.code != IOErr.noSpace || flush.errno == 0) Fiber.abort(\"digest flush failure\")\n"));
	st_wren_test_digest_flush_fail = false;
	CHECK(st_wren_test_digest_calls == calls && st_wren_test_digest_map_calls == maps);
	CHECK(interpret(vm, NULL,
	    "if (!(DigestChecks.file.digest(Digest.sha256) is String)) Fiber.abort(\"flush recovery\")\n"
	    "DigestChecks.file.close()\n"));
	calls = st_wren_test_digest_calls;
	st_wren_test_digest_map_status = IO_ERROR_NOT_SUPPORTED;
	CHECK(interpret(vm, NULL,
	    "var mapped = DigestChecks.file.digest(Digest.sha256)\n"
	    "if (!(mapped is FileError) || mapped.code != IOErr.notSupported || mapped.errno != 0) Fiber.abort(\"mapping status\")\n"));
	st_wren_test_digest_map_status = 0;
	CHECK(st_wren_test_digest_calls == calls);
	const char *path = ST_WREN_DIGEST_TEST_DIRECTORY "/digest.bin";
	install_grant(vm, path, false, false);
	CHECK(interpret(vm, NULL,
	    "if (!(DigestChecks.granted.digest(Digest.sha256) is String)) Fiber.abort(\"picker read grant\")\n"));
	install_grant(vm, path, false, true);
	calls = st_wren_test_digest_calls;
	CHECK(interpret(vm, NULL,
	    "DigestChecks.rejects(Fn.new { DigestChecks.granted.digest(Digest.sha256) })\n"
	    "DigestChecks.granted.open()\n"
	    "DigestChecks.rejects(Fn.new { DigestChecks.granted.digest(Digest.sha256) })\n"
	    "DigestChecks.granted.close()\n"));
	install_grant(vm, path, true, false);
	CHECK(interpret(vm, NULL,
	    "DigestChecks.rejects(Fn.new { DigestChecks.granted.digest(Digest.sha256) })\n"));
	CHECK(st_wren_test_digest_calls == calls);
}

static void
write_binary_file(void)
{
	unsigned char bytes[256];
	for (size_t i = 0; i < sizeof(bytes); ++i) bytes[i] = (unsigned char)i;
	FILE *stream = fopen(ST_WREN_DIGEST_TEST_DIRECTORY "/digest.bin", "wb");
	CHECK(stream != NULL);
	for (unsigned i = 0; i < 256; ++i)
		CHECK(fwrite(bytes, 1, sizeof(bytes), stream) == sizeof(bytes));
	CHECK(fclose(stream) == 0);
}

static void
sftp_status(st_wren_connected_t *connected)
{
	CHECK(st_wren_connected_interpret(connected, "digest_sftp",
	    "import \"syncterm\" for Digest, Download, Error\n"
	    "import \"sftp_app\" for SftpFmt\n"
	    "class DigestSftp {\n"
	    "  static hash(hex) {\n"
	    "    var bytes = \"\"\n"
	    "    var digits = \"0123456789abcdef\"\n"
	    "    for (i in 0...hex.count) {\n"
	    "      if (i % 2 == 0) bytes = bytes + String.fromByte(digits.indexOf(hex[i]) * 16 + digits.indexOf(hex[i+1]))\n"
	    "    }\n"
	    "    return bytes\n"
	    "  }\n"
	    "  static chip(hash) { SftpFmt.statusChip(false, false, \"digest.bin\", \"/digest.bin\", 3, 0, hash) }\n"
	    "  static run(expected) {\n"
	    "    Download.delete(\"digest.bin\")\n"
	    "    var file = Download.create(\"digest.bin\")\n"
	    "    if (file.open() is Error || file.writeBytes(\"abc\") is Error || file.close() is Error) Fiber.abort(\"SFTP digest setup\")\n"
	    "    for (hex in [\"a9993e364706816aba3e25717850c26c9cd0d89d\", \"900150983cd24fb0d6963f7d28e17f72\"]) {\n"
	    "      var digest = hash(hex)\n"
	    "      if (digest.count == digest.bytes.count) Fiber.abort(\"fixture must distinguish scalar and byte lengths\")\n"
	    "      if (chip(digest) != expected) Fiber.abort(\"SFTP digest match: %(chip(digest))\")\n"
	    "      if (chip(String.fromByte(0) * digest.bytes.count) != (expected == \"[==]\" ? \"[<>]\" : expected)) Fiber.abort(\"SFTP digest mismatch\")\n"
	    "    }\n"
	    "  }\n"
	    "}\n") == 0);
	CHECK(st_wren_connected_interpret(connected, "digest_sftp",
	    strcmp(st_crypto_provider_name(), "none") == 0
	    ? "DigestSftp.run(\"[er]\")\n" : "DigestSftp.run(\"[==]\")\n") == 0);
	st_wren_test_digest_status = ST_CRYPTO_ERR_DISABLED;
	CHECK(st_wren_connected_interpret(connected, "digest_sftp", "DigestSftp.run(\"[er]\")\n") == 0);
	st_wren_test_digest_status = 0;
	st_wren_test_digest_map_status = IO_ERROR_NOT_SUPPORTED;
	CHECK(st_wren_connected_interpret(connected, "digest_sftp", "DigestSftp.run(\"[er]\")\n") == 0);
	st_wren_test_digest_map_status = 0;
}

int
main(void)
{
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config config = {.columns = 80, .rows = 25, .wrap = true};
	struct st_app_paths paths = {0};
	st_console_t *console = NULL;
	st_wren_host_t *host = NULL;
	st_hyperlink_registry_t *hyperlinks = NULL;
	CHECK(fs_create_directories(ST_WREN_DIGEST_TEST_DIRECTORY) == 0);
	paths.cache_directory = ST_WREN_DIGEST_TEST_DIRECTORY;
	paths.system_cache_directory = ST_WREN_DIGEST_TEST_DIRECTORY;
	paths.scripts_directory = ST_WREN_DIGEST_TEST_DIRECTORY;
	paths.themes_directory = ST_WREN_DIGEST_TEST_DIRECTORY;
	paths.download_directory = ST_WREN_DIGEST_TEST_DIRECTORY;
	CHECK(st_console_create(&console, &display, &config) == 0);
	CHECK(st_hyperlink_registry_create(&hyperlinks) == 0);
	const struct st_wren_host_config host_config = {
		.console = console, .paths = &paths, .safe_mode = true,
		.application_title = "Digest tests",
		.hyperlinks = hyperlinks,
	};
	CHECK(st_wren_host_create(&host, &host_config) == 0);
	free(host->selected.download_directory);
	host->selected.download_directory = strdup(ST_WREN_DIGEST_TEST_DIRECTORY);
	CHECK(host->selected.download_directory != NULL);
	host->selected_valid = true;
	struct st_profile profile;
	st_profile_init(&profile);
	free(profile.download_directory);
	profile.download_directory = strdup(ST_WREN_DIGEST_TEST_DIRECTORY);
	CHECK(profile.download_directory != NULL);
	const struct st_wren_connected_config connected_config = {
		.host = host, .console = console, .profile = &profile,
	};
	st_wren_connected_t *connected = NULL;
	CHECK(st_wren_connected_create(&connected, &connected_config) == 0);
	char *source = read_source();
	for (unsigned owner = 0; owner < 3; ++owner) {
		WrenVM *vm = owner == 0 ? host->vm : host->picker_vm;
		st_wren_connected_t *session = owner == 2 ? connected : NULL;
		CHECK(interpret(vm, session, source));
		CHECK(interpret(vm, session, strcmp(st_crypto_provider_name(), "none") == 0
		    ? "DigestChecks.full = false\nDigestChecks.bytes()\n"
		    : "DigestChecks.full = true\nDigestChecks.bytes()\n"));
		if (owner != 1) {
			CHECK(interpret(vm, session, "DigestChecks.files()\n"));
			write_binary_file();
			CHECK(interpret(vm, session, "DigestChecks.binaryFile()\n"));
			CHECK(st_wren_test_digest_map_opens > 0);
			CHECK(st_wren_test_digest_map_opens == st_wren_test_digest_map_closes);
		}
		const int statuses[] = {-1, -2, -3, -4, -5, -6, -7, -8, -9, -700, 701};
		for (size_t i = 0; i < sizeof(statuses) / sizeof(statuses[0]); ++i) {
			char probe[256];
			st_wren_test_digest_status = statuses[i];
			unsigned calls = st_wren_test_digest_calls;
			(void)snprintf(probe, sizeof(probe),
			    "DigestChecks.fail(Digest.hash(Digest.sha256, \"abc\"), %d)\n", statuses[i]);
			CHECK(interpret(vm, session, probe));
			CHECK(st_wren_test_digest_calls == calls + 1);
			if (owner != 1) {
				unsigned closed = st_wren_test_digest_map_closes;
				(void)snprintf(probe, sizeof(probe),
				    "DigestChecks.fail(DigestChecks.file.digest(Digest.sha256), %d)\n", statuses[i]);
				CHECK(interpret(vm, session, probe));
				CHECK(st_wren_test_digest_calls == calls + 2);
				CHECK(st_wren_test_digest_map_closes == closed + 1);
			}
		}
		st_wren_test_digest_status = 0;
		CHECK(interpret(vm, session, "DigestChecks.lifetime()\n"));
		if (owner == 0) file_boundaries(vm);
		CHECK(st_wren_test_digest_direct_mapping);
		CHECK(st_wren_test_digest_map_opens == st_wren_test_digest_map_closes);
	}
	sftp_status(connected);
	CHECK(st_wren_test_digest_map_opens == st_wren_test_digest_map_closes);
	free(source);
	st_wren_connected_destroy(connected);
	st_profile_dispose(&profile);
	st_wren_host_destroy(host);
	st_hyperlink_registry_destroy(hyperlinks);
	st_console_destroy(console);
	return EXIT_SUCCESS;
}
