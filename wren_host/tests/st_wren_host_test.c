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
#include "console_internal.h"
#include "st_cterm.h"
#include "st_font_catalog.h"
#include "st_profile.h"
#include "st_settings.h"
#include "st_session_history.h"
#include "st_theme.h"
#include "st_term.h"
#include "st_threads.h"
#include "st_time.h"
#include "string_compat.h"
#include "syncterm_config.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if ST_WREN_TEST_HAVE_PTY
#include <fcntl.h>
#include <unistd.h>
#endif

#if SYNCTERM_PLATFORM_POSIX
#include <sys/utsname.h>
#endif

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

struct fake_sftp_response {
	struct st_sftp_response response;
	unsigned char bytes[32];
};

struct fake_sftp {
	struct fake_sftp_response responses[128];
	size_t head;
	size_t count;
	uint64_t next_id;
	const unsigned char *remote;
	size_t remote_size;
	int64_t modified;
	bool close_error;
	bool transient_read;
	bool transient_sent;
	uint64_t transient_offset;
};

static bool fake_sftp_available(void *context)
{
	(void)context;
	return true;
}

static const char *fake_sftp_public_directory(void *context)
{
	(void)context;
	return "/";
}

static int
fake_sftp_submit(void *context, const struct st_sftp_request *request,
	uint64_t *request_id)
{
	struct fake_sftp *fake = context;
	struct fake_sftp_response *stored;
	size_t index;

	if (fake->count == sizeof(fake->responses) / sizeof(fake->responses[0]))
		return IO_ERROR_OUT_OF_RANGE;
	index = (fake->head + fake->count) %
	    (sizeof(fake->responses) / sizeof(fake->responses[0]));
	stored = &fake->responses[index];
	memset(stored, 0, sizeof(*stored));
	stored->response.request_id = ++fake->next_id;
	*request_id = stored->response.request_id;
	switch (request->operation) {
	case ST_SFTP_START:
		stored->response.kind = ST_SFTP_RESULT_STARTED;
		break;
	case ST_SFTP_STAT:
		stored->response.kind = ST_SFTP_RESULT_STAT;
		stored->response.stat.size = fake->remote_size;
		stored->response.stat.modified_time = fake->modified;
		break;
	case ST_SFTP_OPEN:
		stored->response.kind = ST_SFTP_RESULT_HANDLE;
		stored->response.handle = 1;
		break;
	case ST_SFTP_READ: {
		if (fake->transient_read && !fake->transient_sent
		    && request->offset >= fake->transient_offset) {
			fake->transient_sent = true;
			stored->response.kind = ST_SFTP_RESULT_ERROR;
			stored->response.error_code = IO_ERROR_IO;
			stored->response.message = "injected transport loss";
			stored->response.transient = true;
			break;
		}
		size_t available = request->offset < fake->remote_size
		    ? fake->remote_size - (size_t)request->offset : 0;
		size_t amount = request->size < available ? request->size : available;

		/* Deliberately exercise successful short reads. */
		if (amount > 7)
			amount = 7;
		stored->response.kind = amount == 0 ? ST_SFTP_RESULT_EOF
		    : ST_SFTP_RESULT_BYTES;
		if (amount != 0) {
			memcpy(stored->bytes, fake->remote + request->offset, amount);
			stored->response.data = stored->bytes;
			stored->response.size = amount;
		}
		break;
	}
	case ST_SFTP_CLOSE:
		if (fake->close_error) {
			stored->response.kind = ST_SFTP_RESULT_ERROR;
			stored->response.error_code = IO_ERROR_IO;
			stored->response.message = "injected close failure";
			break;
		}
		stored->response.kind = ST_SFTP_RESULT_OK;
		break;
	case ST_SFTP_REMOVE:
	case ST_SFTP_SET_MTIME:
		stored->response.kind = ST_SFTP_RESULT_OK;
		break;
	default:
		stored->response.kind = ST_SFTP_RESULT_ERROR;
		stored->response.error_code = IO_ERROR_NOT_SUPPORTED;
		stored->response.message = "fake operation is not supported";
		break;
	}
	fake->count++;
	return 0;
}

static int
fake_sftp_poll(void *context, struct st_sftp_response *response,
	uint32_t timeout_ms)
{
	struct fake_sftp *fake = context;
	(void)timeout_ms;

	if (fake->count == 0)
		return 0;
	*response = fake->responses[fake->head].response;
	fake->head = (fake->head + 1) %
	    (sizeof(fake->responses) / sizeof(fake->responses[0]));
	fake->count--;
	return 1;
}

static int
exercise_filesystem_path_handles(st_wren_connected_t *connected)
{
	char external_file[sizeof(ST_WREN_TEST_DIRECTORY) + 40];
	char external_directory[sizeof(ST_WREN_TEST_DIRECTORY) + 40];
	char external_child[sizeof(ST_WREN_TEST_DIRECTORY) + 56];

	CHECK(snprintf(external_file, sizeof(external_file),
	    "%s/wren-external-file.tmp", ST_WREN_TEST_DIRECTORY)
	    < (int)sizeof(external_file));
	CHECK(snprintf(external_directory, sizeof(external_directory),
	    "%s/wren-external-dir", ST_WREN_TEST_DIRECTORY)
	    < (int)sizeof(external_directory));
	CHECK(snprintf(external_child, sizeof(external_child),
	    "%s/child.tmp", external_directory) < (int)sizeof(external_child));
	(void)fs_remove_file(external_file);
	(void)fs_remove_file(external_child);
	(void)fs_remove_directory(external_directory);

	CHECK(st_wren_connected_interpret(connected, "filesystem_path_handles",
	    "import \"syncterm\" for Cache, Download, File, FileError, FileErr\n"
	    "class PathHandles {\n"
	    "  static file { __file }\n"
	    "  static directory { __directory }\n"
	    "  static prepare() {\n"
	    "    __file = Download.create(\"wren-external-file.tmp\")\n"
	    "    __directory = Download.createDir(\"wren-external-dir\")\n"
	    "    if (!(__file is File) || __directory == null) Fiber.abort(\"path setup\")\n"
	    "  }\n"
	    "  static checkMissing() {\n"
	    "    if (!(__file.path is String) || __file.token != null) Fiber.abort(\"missing file metadata\")\n"
	    "    var missing = __file.open()\n"
	    "    if (!(missing is FileError) || missing.code != FileErr.vanished) Fiber.abort(\"missing file result\")\n"
	    "    var unavailable = Fiber.new { __directory.contains(\"child.tmp\") }.try()\n"
	    "    if (!(unavailable is String)) Fiber.abort(\"missing directory result\")\n"
	    "    if (!(__directory.toString is String)) Fiber.abort(\"missing directory metadata\")\n"
	    "  }\n"
	    "  static checkRestored() {\n"
	    "    var opened = __file.open()\n"
	    "    if (opened is FileError || __file.read() != \"external-restored\") Fiber.abort(\"restored file\")\n"
	    "    if (__file.close() is FileError) Fiber.abort(\"restored close\")\n"
	    "    var child = __directory.create(\"child.tmp\")\n"
	    "    if (!(child is File) || !__directory.delete(\"child.tmp\")) Fiber.abort(\"restored directory\")\n"
	    "  }\n"
	    "}\n"
	    "PathHandles.prepare()\n") == 0);
	CHECK(fs_remove_file(external_file) == 0);
	CHECK(fs_remove_directory(external_directory) == 0);
	CHECK(st_wren_connected_interpret(connected, "filesystem_path_missing",
	    "import \"filesystem_path_handles\" for PathHandles\n"
	    "PathHandles.checkMissing()\n") == 0);
	CHECK(write_file(external_file, "external-restored") == 0);
	CHECK(fs_create_directory(external_directory) == 0);
	CHECK(st_wren_connected_interpret(connected, "filesystem_path_restored",
	    "import \"filesystem_path_handles\" for PathHandles\n"
	    "PathHandles.checkRestored()\n") == 0);

	CHECK(st_wren_connected_interpret(connected, "filesystem_delete_reuse",
	    "import \"syncterm\" for Cache, Download, File, FileError, FileErr\n"
	    "var oldFile = Download.create(\"wren-delete-file.tmp\")\n"
	    "if (!(oldFile is File) || !Download.delete(\"wren-delete-file.tmp\")) Fiber.abort(\"file delete\")\n"
	    "var missing = Fiber.new { oldFile.open() }.try()\n"
	    "if (!(missing is FileError) || missing.code != FileErr.vanished) Fiber.abort(\"deleted file result\")\n"
	    "var newFile = Download.create(\"wren-delete-file.tmp\")\n"
	    "if (!(newFile is File) || (newFile.open() is FileError) || (newFile.write(\"recreated\") is FileError) || (newFile.close() is FileError)) Fiber.abort(\"file recreate\")\n"
	    "if ((oldFile.open() is FileError) || oldFile.read() != \"recreated\" || (oldFile.close() is FileError)) Fiber.abort(\"file handle reuse\")\n"
	    "if (!Download.delete(\"wren-delete-file.tmp\")) Fiber.abort(\"file cleanup\")\n"
	    "var oldDir = Download.createDir(\"wren-delete-dir\")\n"
	    "if (oldDir == null || !Download.delete(\"wren-delete-dir\")) Fiber.abort(\"directory delete\")\n"
	    "var unavailable = Fiber.new { oldDir.contains(\"child.tmp\") }.try()\n"
	    "if (!(unavailable is String)) Fiber.abort(\"deleted directory result\")\n"
	    "var newDir = Download.createDir(\"wren-delete-dir\")\n"
	    "if (newDir == null || oldDir.contains(\"child.tmp\")) Fiber.abort(\"directory handle reuse\")\n"
	    "if (!Download.delete(\"wren-delete-dir\")) Fiber.abort(\"directory cleanup\")\n"
	    "var replacement = Download.create(\"wren-replace.tmp\")\n"
	    "if (!(replacement is File) || (Download.replace(\"wren-replace.tmp\", \"replacement\") is FileError)) Fiber.abort(\"replace setup\")\n"
	    "if ((replacement.open() is FileError) || replacement.read() != \"replacement\" || (replacement.close() is FileError)) Fiber.abort(\"replace handle\")\n"
	    "if (!Download.delete(\"wren-replace.tmp\")) Fiber.abort(\"replace cleanup\")\n"
	    "var relaxed = Download.create(\"file with spaces.tmp\")\n"
	    "if (!(relaxed is File) || !Download.delete(\"file with spaces.tmp\")) Fiber.abort(\"download names\")\n"
	    "if (Download.create(\"../escape\") != null || Download.create(\"nul\\u0000suffix\") != null || Cache.create(\".hidden\") != null) Fiber.abort(\"name policies\")\n") == 0);

	CHECK(fs_remove_file(external_file) == 0);
	CHECK(fs_remove_directory(external_directory) == 0);
	return 0;
}

static int
exercise_connected_profile_scripts(st_wren_host_t *host,
	st_console_t *console, const struct st_profile *profile,
	const char *scripts_directory)
{
	struct st_profile selected = *profile;
	struct st_wren_connected_config config = {
		.host = host,
		.console = console,
		.profile = &selected,
	};
	st_wren_connected_t *connected = NULL;
	char first[sizeof(ST_WREN_TEST_DIRECTORY) + 56];
	char second[sizeof(ST_WREN_TEST_DIRECTORY) + 56];

	CHECK(fs_create_directories(scripts_directory) == 0);
	CHECK(snprintf(first, sizeof(first), "%s/profile_first.wren",
	    scripts_directory) < (int)sizeof(first));
	CHECK(snprintf(second, sizeof(second), "%s/profile_second.wren",
	    scripts_directory) < (int)sizeof(second));
	CHECK(write_file(first,
	    "import \"syncterm\" for Status\n"
	    "Status.details = Fn.new { \"first\" }\n") == 0);
	CHECK(write_file(second,
	    "import \"syncterm\" for Status\n"
	    "if (Status.details.call() != \"first\") Fiber.abort(\"script order\")\n"
	    "Status.details = Fn.new { \"second\" }\n") == 0);
	(void)snprintf(selected.wren_scripts, sizeof(selected.wren_scripts),
	    "profile_first,profile_second");
	CHECK(st_wren_connected_create(&connected, &config) == 0);
	CHECK(st_wren_connected_interpret(connected, "profile_script_assert",
	    "import \"syncterm\" for Status, BBS\n"
	    "if (Status.details.call() != \"second\") Fiber.abort(\"not loaded\")\n"
	    "if (BBS.wrenScripts.count != 2 || BBS.wrenScripts[0] != \"profile_first\" || BBS.wrenScripts[1] != \"profile_second\") Fiber.abort(\"script order view\")\n") == 0);
	st_wren_connected_destroy(connected);
	connected = NULL;
	(void)snprintf(selected.wren_scripts, sizeof(selected.wren_scripts),
	    "../profile_first");
	CHECK(st_wren_connected_create(&connected, &config)
	    == IO_ERROR_INVALID_FORMAT);
	CHECK(connected == NULL);
	CHECK(fs_remove_file(first) == 0);
	CHECK(fs_remove_file(second) == 0);
	return 0;
}

/* Transfer diagnostics are separate from the REPL's source/error counters. */
static void
count_transfer_logs(void *context, int level, const char *message)
{
	(void)level;
	(void)message;
	++*(size_t *)context;
}

static int
exercise_connected_without_sftp(st_wren_host_t *host,
	st_console_t *console, struct st_profile *profile)
{
	size_t transfer_logs = 0;
	const struct st_wren_connected_config config = {
		.host = host,
		.console = console,
		.profile = profile,
		.status_enabled = true,
		.transfer_log = count_transfer_logs,
		.transfer_log_context = &transfer_logs,
	};
	st_wren_connected_t *connected = NULL;
	struct st_console_cell cells[3];
	struct st_console_rect area;
	uint32_t width, height;
	bool consumed;

	CHECK(st_wren_connected_create(&connected, &config) == 0);
	st_wren_connected_log_transfer(connected, ST_PROFILE_LOG_INFO, "info");
	st_wren_connected_log_transfer(connected, ST_PROFILE_LOG_DEBUG, "debug");
	CHECK(transfer_logs == 1);
	CHECK(st_wren_connected_render_status(connected, true) == 0);
	char *details = st_wren_connected_status_details(connected);
	CHECK(details != NULL && strstr(details, "BBS: Alpha") != NULL);
	free(details);
	CHECK(st_wren_connected_last_error(connected) == NULL);
	CHECK(st_wren_connected_interpret(connected, "live_log_level",
	    "import \"syncterm\" for Host, BBS\n"
	    "if (Host.logLevel != BBS.xferLogLevel || Host.logLevelNames.count != 8 || Host.logLevelNames[7] != \"Debug\") Fiber.abort(\"initial log level\")\n"
	    "Host.logLevel = 99\n"
	    "if (Host.logLevel != 7 || BBS.xferLogLevel == 7) Fiber.abort(\"clamp/persistence\")\n"
	    "Host.logLevel = -4\n"
	    "if (Host.logLevel != 0) Fiber.abort(\"negative clamp\")\n") == 0);
	st_wren_connected_log_transfer(connected, ST_PROFILE_LOG_INFO, "hidden");
	st_wren_connected_log_transfer(connected, ST_PROFILE_LOG_EMERGENCY, "visible");
	CHECK(transfer_logs == 2);
	CHECK(st_wren_connected_interpret(connected, "no_sftp_provider",
	    "import \"syncterm\" for SFTP, SFTPError, Host, CTerm, Status, Conn, ConnError, ConnErr, Scrollback, Surface, BBS\n"
	    "if (SFTP.available || CTerm.sftpActive || Conn.connected) Fiber.abort(\"unexpected provider\")\n"
	    "if (Conn.sessionLog != null) Fiber.abort(\"unexpected session log provider\")\n"
	    "if (Host.sshPublicKey != null || !(SFTP.start(Fiber.current) is SFTPError)) Fiber.abort(\"unexpected SSH capability\")\n"
	    "if (BBS.name != \"Alpha\" || BBS.addr != \"alpha.example\" || BBS.connected != 100 || BBS.palette.count != BBS.paletteSize || BBS.sshFingerprint.count != BBS.sshFingerprintLen) Fiber.abort(\"connected profile view\")\n"
	    "if (Scrollback.width != 0 || Scrollback.height != 0 || Scrollback.copyRowsTo(Surface.new(2, 2), 0, 2, 0) != 0) Fiber.abort(\"empty scrollback\")\n"
	    "CTerm.suspended = true\n"
	    "if (!CTerm.suspended) Fiber.abort(\"suspend state\")\n"
	    "CTerm.suspended = false\n"
	    "CTerm.mouseDisabled = true\n"
	    "if (!CTerm.mouseDisabled) Fiber.abort(\"mouse state\")\n"
	    "CTerm.mouseDisabled = false\n"
	    "var sent = Conn.send(\"text\")\n"
	    "if (!(sent is ConnError) || sent.code != ConnErr.notConnected || sent.bytesSent != 0) Fiber.abort(\"text send error\")\n"
	    "sent = Conn.sendRaw(\"raw\")\n"
	    "if (!(sent is ConnError) || sent.code != ConnErr.notConnected) Fiber.abort(\"raw send error\")\n"
	    "if (Conn.paste() != null) Fiber.abort(\"disconnected paste\")\n")
	    == 0);
	CHECK(st_wren_connected_interpret(connected, "connected_key_test",
	    "import \"syncterm\" for Hook\n"
	    "class KeyHookState {\n"
	    "  static handle { __hook }\n"
	    "  static handle=(h) { __hook = h }\n"
	    "}\n"
	    "KeyHookState.handle = Hook.onKey(65, Fn.new {|k| k == 65 })\n"
	    "Hook.onKey(90, Fn.new {|k| k == 90 })\n"
	    "Hook.onMouse(1, Fn.new {|m| m.event == 1 })\n"
	    "Hook.onPhysicalKey(30, Fn.new {|p| p.evdev == 30 })\n"
	    "if (KeyHookState.handle == null || KeyHookState.handle.callCount != 0) Fiber.abort(\"hook handle\")\n") == 0);
	CHECK(st_wren_connected_interpret(connected, "connected_wire_test",
	    "import \"syncterm\" for Hook\n"
	    "class WireState {\n"
	    "  static calls { __calls }\n"
	    "  static calls=(n) { __calls = n }\n"
	    "}\n"
	    "WireState.calls = 0\n"
	    "Hook.onWire(65, Fn.new {|b| \"XY\" })\n"
	    "Hook.onWire(66, Fn.new {|b| true })\n"
	    "Hook.onWire(Fn.new {|b| WireState.calls = WireState.calls + 1 })\n") == 0);
	uint8_t wire_replacement[256];
	size_t wire_replacement_size;
	CHECK(st_wren_connected_filter_wire_byte(connected, 'A', &consumed,
	    wire_replacement, sizeof(wire_replacement),
	    &wire_replacement_size) == 0);
	CHECK(consumed && wire_replacement_size == 2
	    && memcmp(wire_replacement, "XY", 2) == 0);
	CHECK(st_wren_connected_filter_wire_byte(connected, 'B', &consumed,
	    wire_replacement, sizeof(wire_replacement),
	    &wire_replacement_size) == 0);
	CHECK(consumed && wire_replacement_size == 0);
	CHECK(st_wren_connected_filter_wire_byte(connected, 'C', &consumed,
	    wire_replacement, sizeof(wire_replacement),
	    &wire_replacement_size) == 0);
	CHECK(!consumed && wire_replacement_size == 0);
	CHECK(st_wren_connected_interpret(connected, "connected_wire_verify",
	    "import \"connected_wire_test\" for WireState\n"
	    "if (WireState.calls != 1) Fiber.abort(\"wire hook order\")\n") == 0);
	CHECK(st_wren_connected_interpret(connected, "connected_input_test",
	    "import \"syncterm\" for Hook\n"
	    "class InputState {\n"
	    "  static calls { __calls }\n"
	    "  static calls=(n) { __calls = n }\n"
	    "}\n"
	    "InputState.calls = 0\n"
	    "Hook.onInput(233, Fn.new {|c| String.fromCodePoint(101) + String.fromCodePoint(769) })\n"
	    "Hook.onInput(10, Fn.new {|c| \"\\r\\n\" })\n"
	    "Hook.onInput(81, Fn.new {|c| true })\n"
	    "Hook.onInput(Fn.new {|c| InputState.calls = InputState.calls + 1 })\n") == 0);
	char input_replacement[256];
	size_t input_replacement_size;
	CHECK(st_wren_connected_has_input_hooks(connected));
	CHECK(st_wren_connected_filter_input_scalar(connected, 233,
	    &consumed, input_replacement, sizeof(input_replacement),
	    &input_replacement_size) == 0);
	CHECK(consumed && input_replacement_size == 2
	    && memcmp(input_replacement, "\xc3\xa9", 2) == 0);
	CHECK(st_wren_connected_filter_input_scalar(connected, 10,
	    &consumed, input_replacement, sizeof(input_replacement),
	    &input_replacement_size) == 0);
	CHECK(consumed && input_replacement_size == 2
	    && memcmp(input_replacement, "\r\n", 2) == 0);
	CHECK(st_wren_connected_filter_input_scalar(connected, 'Q',
	    &consumed, input_replacement, sizeof(input_replacement),
	    &input_replacement_size) == 0);
	CHECK(consumed && input_replacement_size == 0);
	CHECK(st_wren_connected_filter_input_scalar(connected, 'C',
	    &consumed, input_replacement, sizeof(input_replacement),
	    &input_replacement_size) == 0);
	CHECK(!consumed && input_replacement_size == 0);
	CHECK(st_wren_connected_interpret(connected, "connected_input_verify",
	    "import \"connected_input_test\" for InputState\n"
	    "if (InputState.calls != 1) Fiber.abort(\"input hook order\")\n") == 0);
	CHECK(st_wren_connected_handle_key(connected, 66, &consumed) == 0);
	CHECK(!consumed);
	CHECK(st_wren_connected_handle_key(connected, 0x1800, &consumed) == 0);
	CHECK(consumed);
	CHECK(st_wren_connected_interpret(connected, "mouse_toggle_on",
	    "import \"syncterm\" for CTerm\n"
	    "if (!CTerm.mouseDisabled) Fiber.abort(\"Alt-O did not disable mouse\")\n") == 0);
	CHECK(st_wren_connected_handle_key(connected, 0x1800, &consumed) == 0);
	CHECK(consumed);
	struct st_console_event text_z = {
		.type = ST_CONSOLE_EVENT_TEXT,
		.data.text = {.codepoint = 'Z'},
	};
	CHECK(st_wren_connected_handle_event(connected, &text_z,
	    &consumed) == 0);
	CHECK(consumed);
	CHECK(st_wren_connected_interpret(connected, "modified_key_hook",
	    "import \"syncterm\" for Hook\n"
	    "Hook.onKey(10, Fn.new {|key| true })\n") == 0);
	const struct st_console_event ctrl_enter = {
		.type = ST_CONSOLE_EVENT_KEY_CODE,
		.data.key = {.code = 13, .modifiers = ST_CONSOLE_MODIFIER_CONTROL},
	};
	CHECK(st_wren_connected_handle_event(connected, &ctrl_enter, &consumed) == 0);
	CHECK(consumed);
	const struct st_console_event reveal = {
		.type = ST_CONSOLE_EVENT_KEY_CODE, .data.key.code = ST_CONSOLE_KEY_F3,
	};
	CHECK(st_wren_connected_handle_event(connected, &reveal, &consumed) == 0);
	CHECK(!consumed);
	struct st_console_event physical_a = {
		.type = ST_CONSOLE_EVENT_PHYSICAL_KEY,
		.data.physical_key = {.evdev = 30, .pressed = true},
	};
	CHECK(st_wren_connected_handle_event(connected, &physical_a,
	    &consumed) == 0);
	CHECK(consumed);
	struct st_console_event alt_l = {
		.type = ST_CONSOLE_EVENT_PHYSICAL_KEY,
		.data.physical_key = {
			.evdev = 38,
			.modifiers = ST_CONSOLE_MODIFIER_ALT,
			.pressed = true,
		},
	};
	CHECK(st_wren_connected_handle_event(connected, &alt_l,
	    &consumed) == 0);
	CHECK(consumed);
	struct st_console_event mouse_press = {
		.type = ST_CONSOLE_EVENT_MOUSE,
		.data.mouse = {
			.action = ST_CONSOLE_MOUSE_BUTTON,
			.buttons = ST_CONSOLE_MOUSE_BUTTON_LEFT,
			.x = 1,
			.y = 1,
		},
	};
	CHECK(st_wren_connected_handle_event(connected, &mouse_press,
	    &consumed) == 0);
	CHECK(consumed);
	struct st_console_event escape = {
		.type = ST_CONSOLE_EVENT_KEY_CODE,
		.data.key = {.code = 27},
	};
	struct st_console_event alt_h = {
		.type = ST_CONSOLE_EVENT_PHYSICAL_KEY,
		.data.physical_key = {
			.evdev = 35,
			.modifiers = ST_CONSOLE_MODIFIER_ALT,
			.pressed = true,
		},
	};
	CHECK(st_wren_connected_handle_event(connected, &alt_h,
	    &consumed) == 0);
	CHECK(consumed);
	CHECK(st_console_post_event(console, &escape) == 0);
	CHECK(st_console_event_try(console, &escape) == 0);
	CHECK(st_wren_connected_handle_event(connected, &escape, &consumed) == 0);
	CHECK(consumed);
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_last_error(connected) == NULL);
	CHECK(st_wren_connected_handle_key(connected, 65, &consumed) == 0);
	CHECK(consumed);
	CHECK(st_wren_connected_interpret(connected, "connected_key_remove",
	    "import \"connected_key_test\" for KeyHookState\n"
	    "var h = KeyHookState.handle\n"
	    "if (h.callCount != 1 || h.totalRuntime < 0) Fiber.abort(\"hook metrics\")\n"
	    "h.remove()\n"
	    "if (h.callCount != 0) Fiber.abort(\"hook removal\")\n") == 0);
	CHECK(st_wren_connected_handle_key(connected, 65, &consumed) == 0);
	CHECK(!consumed);
	CHECK(st_wren_connected_interpret(connected, "status_render_test",
	    "import \"syncterm\" for Status\n"
	    "if (!Status.enabled) Fiber.abort(\"status disabled\")\n"
	    "Status.callable = Fn.new {|s|\n"
	    "  s[0].chByte = 65\n"
	    "  s[1].chByte = 66\n"
	    "  s[2].chByte = 67\n"
	    "}\n"
	    "Status.details = Fn.new { \"A B C\" }\n"
	    "if (Status.callable == null || Status.details == null) Fiber.abort(\"status handles\")\n")
	    == 0);
	CHECK(st_wren_connected_render_status(connected, true) == 0);
	details = st_wren_connected_status_details(connected);
	CHECK(details != NULL && strcmp(details, "A B C") == 0);
	CHECK(st_wren_connected_interpret(connected, "status_details_clear",
	    "import \"syncterm\" for Status\nStatus.details = null\n") == 0);
	CHECK(st_wren_connected_status_details(connected) == NULL);
	CHECK(strcmp(details, "A B C") == 0);
	free(details);
	CHECK(st_console_size(console, &width, &height) == 0);
	area = (struct st_console_rect){0, height - 1, 3, 1};
	CHECK(st_console_read_cells(console, &area, cells, 3) == 0);
	CHECK(cells[0].glyph == 'A' && cells[1].glyph == 'B'
	    && cells[2].glyph == 'C');
	CHECK(cells[0].background == ST_CONSOLE_COLOR_RGB_VALUE(0, 0, 168));
	CHECK(st_wren_connected_render_status(connected, false) == 0);
	CHECK(st_wren_connected_interpret(connected, "status_disabled_test",
	    "import \"syncterm\" for Status\n"
	    "if (Status.enabled) Fiber.abort(\"status still enabled\")\n") == 0);
	CHECK(st_wren_connected_interpret(connected, "connected_interval_test",
	    "import \"syncterm\" for Hook\n"
	    "class IntervalState {\n"
	    "  static fires { __fires }\n"
	    "  static fires=(n) { __fires = n }\n"
	    "  static handle { __handle }\n"
	    "  static handle=(h) { __handle = h }\n"
	    "}\n"
	    "IntervalState.fires = 0\n"
	    "IntervalState.handle = Hook.every(1, Fn.new { IntervalState.fires = IntervalState.fires + 1 })\n") == 0);
	struct timespec interval_delay = {.tv_nsec = 15000000L};
	(void)st_thread_sleep(&interval_delay, NULL);
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	(void)st_thread_sleep(&interval_delay, NULL);
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "connected_interval_verify",
	    "import \"connected_interval_test\" for IntervalState\n"
	    "if (IntervalState.fires != 2 || IntervalState.handle.callCount != 2) Fiber.abort(\"timer cadence\")\n"
	    "IntervalState.handle.remove()\n") == 0);
	(void)st_thread_sleep(&interval_delay, NULL);
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "connected_interval_removed",
	    "import \"connected_interval_test\" for IntervalState\n"
	    "if (IntervalState.fires != 2) Fiber.abort(\"removed timer fired\")\n") == 0);
	CHECK(st_wren_connected_interpret(connected, "shell_close_hook_test",
	    "import \"syncterm\" for Hook\n"
	    "class ShellState {\n"
	    "  static closed { __closed }\n"
	    "  static closed=(v) { __closed = v }\n"
	    "}\n"
	    "ShellState.closed = false\n"
	    "Hook.onShellClose(Fn.new { ShellState.closed = true })\n") == 0);
	CHECK(st_wren_connected_shell_closed(connected) == 0);
	CHECK(st_wren_connected_interpret(connected, "shell_close_hook_verify",
	    "import \"shell_close_hook_test\" for ShellState\n"
	    "if (!ShellState.closed) Fiber.abort(\"shell hook not called\")\n") == 0);
	st_wren_connected_destroy(connected);
	return 0;
}

static bool
connected_input_active(void *context)
{
	return st_wren_connected_has_input_hooks(
	    *(st_wren_connected_t **)context);
}

struct session_log_fixture {
	const char *text;
	bool fail;
};

static int
session_log_snapshot(void *context, char **text)
{
	struct session_log_fixture *fixture = context;
	if (fixture->fail) return IO_ERROR_OUT_OF_MEMORY;
	*text = strdup(fixture->text);
	return *text == NULL ? IO_ERROR_OUT_OF_MEMORY : 0;
}

static int
session_log_key(st_wren_connected_t *connected, uint16_t code)
{
	const struct st_console_event event = {
		.type = ST_CONSOLE_EVENT_KEY_CODE, .data.key.code = code,
	};
	bool claimed;
	CHECK(st_wren_connected_handle_event(connected, &event, &claimed) == 0 && claimed);
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	return 0;
}

static int
exercise_connected_session_log(st_wren_host_t *host, st_console_t *console,
	struct st_profile *profile)
{
	char text[4096] = "OLDEST LOG RECORD\n";
	size_t used = strlen(text);
	for (unsigned i = 0; i < 40; ++i) {
		int count = snprintf(text + used, sizeof(text) - used, "CTerm Debug: record %u\n", i);
		CHECK(count > 0 && (size_t)count < sizeof(text) - used);
		used += (size_t)count;
	}
	CHECK(snprintf(text + used, sizeof(text) - used, "LATEST LOG RECORD\n") > 0);
	struct session_log_fixture fixture = {.text = text};
	st_font_catalog_t *fonts = NULL;
	st_cterm_t *cterm = NULL;
	st_wren_connected_t *connected = NULL;
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	const struct st_cterm_config terminal_config = {
		.console = console, .fonts = fonts,
		.emulation = ST_CTERM_ANSI_BBS, .encoding = ST_CTERM_ENCODING_UTF8,
		.codepage = ST_CODEPAGE_CP437, .quiet = true,
	};
	CHECK(st_cterm_create(&cterm, &terminal_config) == 0);
	const struct st_wren_connected_config config = {
		.host = host, .console = console, .profile = profile, .cterm = cterm,
		.session_log = session_log_snapshot, .session_log_context = &fixture,
	};
	CHECK(st_wren_connected_create(&connected, &config) == 0);
	CHECK(st_wren_connected_interpret(connected, "session_log_contract",
	    "import \"syncterm\" for Conn, Input\n"
	    "import \"session_log\" for SessionLogView\n"
	    "import \"ui_markdown\" for Markdown, MdBlockKind, MdRole\n"
	    "var SavedMouse = Input.mouseEvents\n"
	    "if (!Conn.sessionLog.startsWith(\"OLDEST LOG RECORD\")) Fiber.abort(\"log snapshot\")\n"
	    "var literal = \"**literal** `code` \\\\path\\n# heading\\n: definition\"\n"
	    "var doc = Markdown.parse(SessionLogView.body_(literal))\n"
	    "for (block in doc) {\n"
	    "  if (block.kind != MdBlockKind.bullet) Fiber.abort(\"log block markup\")\n"
	    "  for (run in block.runs) if (run.role != MdRole.text) Fiber.abort(\"log inline markup\")\n"
	    "}\n"
	    "if (doc.map {|b| b.runs.map {|r| r.text }.join() }.join(\"\\n\") != literal) Fiber.abort(\"log literal text\")\n") == 0);
	uint32_t width, height;
	CHECK(st_console_size(console, &width, &height) == 0);
	const struct st_console_rect full = {0, 0, width, height};
	size_t bytes = (size_t)width * height * sizeof(struct st_console_cell);
	struct st_console_cell *before = malloc(bytes), *after = malloc(bytes);
	CHECK(before != NULL && after != NULL);
	CHECK(st_console_read_cells(console, &full, before, width) == 0);
	bool claimed;
	CHECK(st_wren_connected_handle_key(connected, 0x2f00, &claimed) == 0 && claimed);
	CHECK(st_wren_connected_interpret(connected, "session_log_visible",
	    "import \"syncterm\" for CTerm, Screen\n"
	    "var size = Screen.size\n"
	    "var text = Screen.readRect(1, 1, size[0], size[1]).map {|c| c.ch }.join()\n"
	    "if (!CTerm.suspended || !text.contains(\"Session Log\") || !text.contains(\"LATEST LOG RECORD\")) Fiber.abort(\"Alt-V log viewer\")\n") == 0);
	CHECK(session_log_key(connected, ST_CONSOLE_KEY_HOME) == 0);
	CHECK(st_wren_connected_interpret(connected, "session_log_oldest",
	    "import \"syncterm\" for Screen\n"
	    "var size = Screen.size\n"
	    "if (!Screen.readRect(1, 1, size[0], size[1]).map {|c| c.ch }.join().contains(\"OLDEST LOG RECORD\")) Fiber.abort(\"log Home\")\n") == 0);
	CHECK(session_log_key(connected, ST_CONSOLE_KEY_END) == 0);
	CHECK(session_log_key(connected, 27) == 0);
	CHECK(st_console_read_cells(console, &full, after, width) == 0);
	CHECK(memcmp(before, after, bytes) == 0);
	CHECK(st_wren_connected_interpret(connected, "session_log_restored",
	    "import \"syncterm\" for Input, CTerm\n"
	    "import \"session_log_contract\" for SavedMouse\n"
	    "if (CTerm.suspended || Input.mouseEvents != SavedMouse) Fiber.abort(\"log restoration\")\n") == 0);
	/* Select the advertised entry through the actual Alt-Z menu. */
	CHECK(st_wren_connected_interpret(connected, "session_log_menu",
	    "import \"online_menu\" for OnlineMenu\n"
	    "var labels = OnlineMenu.entries_().map {|e| e[0] }.toList\n"
	    "var entries = labels.where {|label| label.startsWith(\"View Session Log\") }.toList\n"
	    "if (entries.count != 1) Fiber.abort(\"online log entry\")\n") == 0);
	CHECK(st_wren_connected_handle_key(connected, 0x2c00, &claimed) == 0 && claimed);
	const struct st_console_event select_log = {
		.type = ST_CONSOLE_EVENT_TEXT, .data.text.codepoint = 'v',
	};
	CHECK(st_wren_connected_handle_event(connected, &select_log, &claimed) == 0 && claimed);
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(session_log_key(connected, 13) == 0);
	CHECK(st_wren_connected_interpret(connected, "session_log_menu_visible",
	    "import \"syncterm\" for Screen\n"
	    "var size = Screen.size\n"
	    "if (!Screen.readRect(1, 1, size[0], size[1]).map {|c| c.ch }.join().contains(\"LATEST LOG RECORD\")) Fiber.abort(\"online log viewer\")\n") == 0);
	CHECK(session_log_key(connected, 27) == 0);
	fixture.text = "";
	CHECK(st_wren_connected_handle_key(connected, 0x2f00, &claimed) == 0 && claimed);
	CHECK(st_wren_connected_interpret(connected, "session_log_empty",
	    "import \"syncterm\" for Screen\n"
	    "var size = Screen.size\n"
	    "if (!Screen.readRect(1, 1, size[0], size[1]).map {|c| c.ch }.join().contains(\"No session log messages\")) Fiber.abort(\"empty log viewer\")\n") == 0);
	CHECK(session_log_key(connected, 27) == 0);
	fixture.fail = true;
	CHECK(st_wren_connected_interpret(connected, "session_log_failure",
	    "import \"syncterm\" for Conn\nvar snapshot = Conn.sessionLog\n") != 0);
	free(before);
	free(after);
	st_wren_connected_destroy(connected);
	st_cterm_destroy(&cterm);
	st_font_catalog_destroy(fonts);
	return 0;
}

#if !SYNCTERM_PLATFORM_WINDOWS
static char opened_uri[4096], copied_uri[4096];
static unsigned uri_open_count;
static bool uri_open_fails;

static int
record_link_present(st_console_t *console)
{
	(void)console;
	return 0;
}

static int
record_open_uri(st_console_display_t *display, const char *uri)
{
	(void)display;
	++uri_open_count;
	(void)snprintf(opened_uri, sizeof(opened_uri), "%s", uri);
	return uri_open_fails ? IO_ERROR_NOT_SUPPORTED : 0;
}

static int
record_copy_uri(st_console_display_t *display, const char *uri)
{
	(void)display;
	(void)snprintf(copied_uri, sizeof(copied_uri), "%s", uri);
	return 0;
}

static int
record_link_pointer(st_console_display_t *display, enum st_console_pointer_shape shape)
{
	(void)display;
	(void)shape;
	return 0;
}

static int
link_click(st_wren_connected_t *connected, int32_t x, int32_t y,
	uint32_t modifiers, bool expected_claimed)
{
	struct st_console_event event = {
		.type = ST_CONSOLE_EVENT_MOUSE,
		.data.mouse = {.action = ST_CONSOLE_MOUSE_BUTTON, .x = x, .y = y,
		    .buttons = ST_CONSOLE_MOUSE_BUTTON_LEFT, .modifiers = modifiers},
	};
	bool claimed;
	CHECK(st_wren_connected_handle_event(connected, &event, &claimed) == 0);
	CHECK(claimed == expected_claimed);
	event.data.mouse.buttons = 0;
	CHECK(st_wren_connected_handle_event(connected, &event, &claimed) == 0);
	CHECK(claimed == expected_claimed);
	return 0;
}

static int
link_text(st_cterm_t *cterm, const char *uri)
{
	char wire[4096];
	size_t consumed;
	int count = snprintf(wire, sizeof(wire), "\033[H\033]8;;%s\033\\link\033]8;;\033\\", uri);
	CHECK(count > 0 && (size_t)count < sizeof(wire));
	CHECK(st_cterm_write(cterm, wire, (size_t)count, &consumed) == 0);
	CHECK(consumed == (size_t)count);
	return 0;
}

static int
exercise_connected_link_hover(st_wren_host_t *host, st_console_t *console,
	struct st_profile *profile)
{
	struct st_profile session_profile;
	st_profile_init(&session_profile);
	CHECK(st_profile_copy(&session_profile, profile));
	session_profile.hide_popups = true;
	st_font_catalog_t *fonts = NULL;
	st_cterm_t *cterm = NULL;
	st_wren_connected_t *connected = NULL;
	size_t consumed;
	bool claimed;
	uint32_t columns, rows;
	const char text[] = "\033[1$~\033[H\033]8;;https://hover.invalid/path\033\\link\033]8;;\033\\";

	CHECK(st_font_catalog_create(&fonts, console) == 0);
	const struct st_cterm_config terminal_config = {
		.console = console, .fonts = fonts,
		.hyperlinks = host->config.hyperlinks,
		.emulation = ST_CTERM_ANSI_BBS,
		.encoding = ST_CTERM_ENCODING_UTF8,
		.codepage = ST_CODEPAGE_CP437, .quiet = true,
	};
	CHECK(st_cterm_create(&cterm, &terminal_config) == 0);
	CHECK(st_cterm_write(cterm, text, sizeof(text) - 1, &consumed) == 0);
	CHECK(consumed == sizeof(text) - 1);
	const struct st_wren_connected_config config = {
		.host = host, .console = console, .profile = &session_profile,
		.cterm = cterm, .status_enabled = true,
	};
	CHECK(st_wren_connected_create(&connected, &config) == 0);
	const struct st_console_event move = {
		.type = ST_CONSOLE_EVENT_MOUSE,
		.data.mouse = {.action = ST_CONSOLE_MOUSE_MOVE, .x = 1, .y = 0},
	};
	CHECK(st_wren_connected_handle_event(connected, &move, &claimed) == 0);
	CHECK(!claimed);
	CHECK(st_wren_connected_render_status(connected, true) == 0);
	CHECK(st_console_size(console, &columns, &rows) == 0);
	CHECK(st_wren_connected_interpret(connected, "hover_status_test",
	    "import \"syncterm\" for Screen\n"
	    "var row = Screen.readRect(1, Screen.size[1], Screen.size[0], Screen.size[1])\n"
	    "var text = row.map {|cell| cell.ch }.join()\n"
	    "if (text.trim() != \"https://hover.invalid/path\") Fiber.abort(\"missing hover URI\")\n") == 0);
	/* Destination persists across refreshes and clears when the pointer leaves. */
	CHECK(st_wren_connected_render_status(connected, true) == 0);
	struct st_console_event away = move;
	away.data.mouse.x = 10;
	CHECK(st_wren_connected_handle_event(connected, &away, &claimed) == 0 && !claimed);
	CHECK(st_wren_connected_render_status(connected, true) == 0);
	CHECK(st_wren_connected_interpret(connected, "hover_status_clear",
	    "import \"syncterm\" for Screen, Status\n"
	    "var size = Screen.size\n"
	    "var text = Screen.readRect(1, size[1], size[0], size[1]).map {|c| c.ch }.join()\n"
	    "if (Status.hoverUri != null || !text.contains(\"Alpha\")) Fiber.abort(\"hover restoration\")\n") == 0);
	CHECK(st_wren_connected_handle_event(connected, &move, &claimed) == 0);
	CHECK(st_cterm_write(cterm, "\033[HXX", 5, &consumed) == 0);
	CHECK(st_wren_connected_interpret(connected, "hover_cell_replaced",
	    "import \"syncterm\" for Status\n"
	    "if (Status.hoverUri != null) Fiber.abort(\"stale hover\")\n") == 0);

	char long_uri[256];
	memset(long_uri, 'a', sizeof(long_uri) - 1);
	memcpy(long_uri, "https://long.invalid/", 21);
	long_uri[sizeof(long_uri) - 1] = '\0';
	CHECK(link_text(cterm, long_uri) == 0);
	CHECK(st_wren_connected_render_status(connected, true) == 0);
	CHECK(st_wren_connected_interpret(connected, "hover_long_uri",
	    "import \"syncterm\" for Screen\n"
	    "var size = Screen.size\n"
	    "var row = Screen.readRect(1, size[1], size[0], size[1])\n"
	    "var text = row.map {|c| c.ch }.join()\n"
	    "if (!text.startsWith(\" https://long.invalid/\") || !text.endsWith(\"... \")) Fiber.abort(\"long URI clipping\")\n") == 0);
	/* Registry destinations can be Unicode even though wire OSC strings
	 * retain the legacy ASCII framing rules. */
	CHECK(st_wren_connected_interpret(connected, "unicode_link_cells",
	    "import \"syncterm\" for Screen, Hyperlinks\n"
	    "var id = Hyperlinks.add(\"https://unicode.invalid/é\", null)\n"
	    "var label = Screen.readRect(1, 1, 4, 1)\n"
	    "for (cell in label) cell.hyperlinkId = id\n"
	    "Screen.putRect(label, 1, 1)\n") == 0);
	CHECK(st_wren_connected_render_status(connected, true) == 0);
	CHECK(st_wren_connected_interpret(connected, "hover_unicode_uri",
	    "import \"syncterm\" for Screen\n"
	    "var size = Screen.size\n"
	    "var text = Screen.readRect(1, size[1], size[0], size[1]).map {|c| c.ch }.join()\n"
	    "if (text.trim() != \"https://unicode.invalid/é\") Fiber.abort(\"Unicode URI preview\")\n") == 0);
	const char host_status[] = "\033[2$~\033[1$}HOST STATUS\033[0$}";
	CHECK(st_cterm_write(cterm, host_status, sizeof(host_status) - 1, &consumed) == 0);
	CHECK(st_wren_connected_render_status(connected, st_cterm_status_display(cterm) == 1) == 0);
	CHECK(st_wren_connected_interpret(connected, "hover_host_status",
	    "import \"syncterm\" for Screen\n"
	    "var row = Screen.readRect(1, Screen.size[1], 11, Screen.size[1])\n"
	    "if (row.map {|c| c.ch }.join() != \"HOST STATUS\") Fiber.abort(\"host status overwritten\")\n") == 0);
	CHECK(st_cterm_write(cterm, "\033[1$~", 5, &consumed) == 0);

	const struct st_console_backend_ops recorder = {
		.present = record_link_present,
		.open_url = record_open_uri, .set_clipboard_text = record_copy_uri,
		.set_pointer_shape = record_link_pointer,
	};
	const struct st_console_backend_ops *original = console->display->backend_ops;
	console->display->backend_ops = &recorder;
	uri_open_count = 0;
	const char *uris[] = {"https://open.invalid/a?q='&x=$z", "HTTPS://open.invalid/", "ftp://open.invalid/file", "ftps://open.invalid/file"};
	for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); ++i) {
		CHECK(link_text(cterm, uris[i]) == 0);
		CHECK(link_click(connected, 1, 0, 0, true) == 0);
		CHECK(uri_open_count == i + 1 && strcmp(opened_uri, uris[i]) == 0);
	}
	CHECK(st_cterm_write(cterm, "\033[?1000h", 8, &consumed) == 0);
	CHECK(link_click(connected, 1, 0, 0, false) == 0);
	CHECK(uri_open_count == 4);
	CHECK(link_click(connected, 1, 0, ST_CONSOLE_MODIFIER_CONTROL, true) == 0);
	CHECK(uri_open_count == 5 && strcmp(opened_uri, uris[3]) == 0);
	const char plain[] = "\033[2;1HSee (www.example.invalid/a_(b)). ";
	CHECK(st_cterm_write(cterm, plain, sizeof(plain) - 1, &consumed) == 0);
	CHECK(link_click(connected, 10, 1, ST_CONSOLE_MODIFIER_CONTROL, true) == 0);
	CHECK(uri_open_count == 6 && strcmp(opened_uri, "https://www.example.invalid/a_(b)") == 0);
	CHECK(st_cterm_write(cterm, "\033[?1000l", 8, &consumed) == 0);
	CHECK(link_click(connected, 10, 1, 0, false) == 0);
	CHECK(uri_open_count == 6);
	CHECK(st_wren_connected_interpret(connected, "link_input_claim",
	    "import \"syncterm\" for Input\n"
	    "class LinkClaim {\n"
	    "  static start() {\n"
	    "    __worker = Fiber.new {\n"
	    "      __handle = Input.pushClaim(Fn.new {|event| true })\n"
	    "      Fiber.yield()\n"
	    "    }\n"
	    "    __worker.call()\n"
	    "  }\n"
	    "  static stop() {\n"
	    "    __handle.pop()\n"
	    "    __worker.call()\n"
	    "  }\n"
	    "}\nLinkClaim.start()\n") == 0);
	CHECK(link_click(connected, 1, 0, 0, true) == 0);
	CHECK(uri_open_count == 6);
	CHECK(st_wren_connected_interpret(connected, "link_input_release",
	    "import \"link_input_claim\" for LinkClaim\nLinkClaim.stop()\n") == 0);
	uri_open_fails = true;
	CHECK(link_click(connected, 1, 0, 0, true) == 0);
	CHECK(uri_open_count == 7 && strcmp(copied_uri, uris[3]) == 0);
	uri_open_fails = false;
	CHECK(link_text(cterm, "mailto:test@example.invalid") == 0);
	CHECK(link_click(connected, 1, 0, 0, true) == 0);
	CHECK(uri_open_count == 7 && strcmp(copied_uri, "mailto:test@example.invalid") == 0);
	CHECK(link_text(cterm, uris[0]) == 0);
	host->config.safe_mode = true;
	CHECK(link_click(connected, 1, 0, 0, true) == 0);
	CHECK(uri_open_count == 7 && strcmp(copied_uri, uris[0]) == 0);
	host->config.safe_mode = false;
	console->display->backend_ops = original;
	CHECK(st_wren_connected_interpret(connected, "detected_surface_uri",
	    "import \"syncterm\" for Surface\n"
	    "var surface = Surface.new(10, 4)\n"
	    "for (c in surface) c.ch = \" \"\n"
	    "var url = \"HTTP://example.invalid/a_(b).\"\n"
	    "for (i in 0...url.count) surface[i].ch = url[i]\n"
	    "if (surface.urlAt(1, 1) != \"HTTP://example.invalid/a_(b)\" || surface.urlAt(-1, 0) != null || surface.urlAt(9, 3) != null) Fiber.abort(\"URL detection\")\n"
	    "var large = Surface.new(32, 64)\n"
	    "for (c in large) c.ch = \"a\"\n"
	    "var prefix = \"https://\"\n"
	    "for (i in 0...prefix.count) large[i].ch = prefix[i]\n"
	    "if (large.urlAt(31, 63) != null || large.urlAt(0, 0) != null) Fiber.abort(\"oversized URL prefix\")\n"
	    "large[2047].ch = \" \"\n"
	    "if (large.urlAt(30, 63).count != 2047 || large.urlAt(0, 0).count != 2047) Fiber.abort(\"URL length boundary\")\n") == 0);
	st_wren_connected_destroy(connected);
	st_cterm_destroy(&cterm);
	st_font_catalog_destroy(fonts);
	st_profile_dispose(&session_profile);
	return 0;
}

static int
exercise_connected_throttle(st_wren_host_t *host, st_console_t *console)
{
	struct st_profile profile;
	st_connection_t *connection = NULL;
	st_font_catalog_t *fonts = NULL;
	st_term_session_t *session = NULL;
	st_wren_connected_t *connected = NULL;

	CHECK(st_font_catalog_create(&fonts, console) == 0);
	st_profile_init(&profile);
	profile.connection_type = ST_CONNECTION_SHELL;
	profile.screen_mode = ST_SCREEN_MODE_80X25;
	strcpy(profile.address, "printf ready; sleep 1");
	CHECK(st_term_connect_profile(&connection, &profile, console, NULL,
	    NULL) == 0);
	const struct st_term_config term_config = {
		.console = console, .connection = connection, .fonts = fonts,
		.screen_mode = ST_SCREEN_MODE_80X25,
		.encoding = ST_CTERM_ENCODING_UTF8,
		.codepage = ST_CODEPAGE_CP437,
		.hide_status = true,
		.throttle_supported = true,
		.initial_throttle_bps = 300,
	};
	CHECK(st_term_session_create(&session, &term_config) == 0);
	const struct st_wren_connected_config wren_config = {
		.host = host, .console = console, .profile = &profile,
		.connection = connection, .session = session,
		.cterm = st_term_session_cterm(session),
	};
	CHECK(st_wren_connected_create(&connected, &wren_config) == 0);
	CHECK(st_wren_connected_interpret(connected, "throttle_binding_test",
	    "import \"syncterm\" for CTerm, Host\n"
	    "var rates = Host.outputRates\n"
	    "var names = Host.outputRateNames\n"
	    "if (rates.count != 12 || names.count != 12 || rates[0] != 300 || rates[10] != 115200 || rates[11] != 0 || names[11] != \"Current\") Fiber.abort(\"rate ladder\")\n"
	    "if (CTerm.throttleSpeed != 300) Fiber.abort(\"initial rate\")\n"
	    "CTerm.throttleSpeedDown()\n"
	    "if (CTerm.throttleSpeed != 0) Fiber.abort(\"rate down\")\n"
	    "CTerm.throttleSpeedUp()\n"
	    "if (CTerm.throttleSpeed != 300) Fiber.abort(\"rate up\")\n"
	    "CTerm.throttleSpeed = 9600\n"
	    "CTerm.throttleSpeedUp()\n"
	    "if (CTerm.throttleSpeed != 19200) Fiber.abort(\"rate step up\")\n"
	    "CTerm.throttleSpeedDown()\n"
	    "if (CTerm.throttleSpeed != 9600) Fiber.abort(\"rate step down\")\n") == 0);
	CHECK(st_term_session_throttle_speed(session) == 9600);
	CHECK(st_wren_connected_interpret(connected, "throttle_hotkey_reset",
	    "import \"syncterm\" for CTerm\n"
	    "CTerm.throttleSpeed = 0\n") == 0);
	bool consumed = false;
	CHECK(st_wren_connected_handle_key(connected, 0x9800,
	    &consumed) == 0 && consumed);
	CHECK(st_term_session_throttle_speed(session) == 300);
	consumed = false;
	CHECK(st_wren_connected_handle_key(connected, 0xa000,
	    &consumed) == 0 && consumed);
	CHECK(st_term_session_throttle_speed(session) == 0);
	st_wren_connected_destroy(connected);
	st_term_session_destroy(&session);
	CHECK(st_connection_close(connection) == 0);
	st_profile_dispose(&profile);
	st_font_catalog_destroy(fonts);
	return 0;
}
#endif

static int
connected_input_filter(void *context, uint32_t scalar, bool *consumed,
	char *replacement, size_t capacity, size_t *replacement_size)
{
	return st_wren_connected_filter_input_scalar(
	    *(st_wren_connected_t **)context, scalar, consumed, replacement,
	    capacity, replacement_size);
}

static bool
connected_capture_active(void *context)
{
	return st_wren_connected_capture_active(
	    *(st_wren_connected_t **)context);
}

static int
connected_capture_input(void *context, const char *bytes, size_t size,
	bool protocol)
{
	return st_wren_connected_capture_input(
	    *(st_wren_connected_t **)context, bytes, size, protocol);
}

static int
exercise_connected_copy_paste_mouse(st_wren_connected_t *connected,
	st_console_t *console, st_cterm_t *cterm)
{
	const struct st_console_event left_press = {
		.type = ST_CONSOLE_EVENT_MOUSE,
		.data.mouse = {
			.action = ST_CONSOLE_MOUSE_BUTTON,
			.buttons = ST_CONSOLE_MOUSE_BUTTON_LEFT,
			.x = 1, .y = 1,
		},
	};
	const struct st_console_event left_move = {
		.type = ST_CONSOLE_EVENT_MOUSE,
		.data.mouse = {
			.action = ST_CONSOLE_MOUSE_MOVE,
			.buttons = ST_CONSOLE_MOUSE_BUTTON_LEFT,
			.x = 3, .y = 1,
		},
	};
	const struct st_console_event left_release = {
		.type = ST_CONSOLE_EVENT_MOUSE,
		.data.mouse = {
			.action = ST_CONSOLE_MOUSE_BUTTON,
			.x = 3, .y = 1,
		},
	};
	struct st_console_event button = {
		.type = ST_CONSOLE_EVENT_MOUSE,
		.data.mouse = {
			.action = ST_CONSOLE_MOUSE_BUTTON,
			.x = 1, .y = 1,
		},
	};
	bool consumed;
	size_t written;

	CHECK(st_cterm_mouse_mode(cterm) == 0);
	consumed = false;
	CHECK(st_wren_connected_handle_key(connected, 0x0500,
	    &consumed) == 0 && consumed);
	consumed = true;
	CHECK(st_wren_connected_handle_event(connected, &left_press,
	    &consumed) == 0 && !consumed);
	CHECK(st_console_post_event(console, &left_release) == 0);
	consumed = false;
	CHECK(st_wren_connected_handle_event(connected, &left_move,
	    &consumed) == 0 && consumed);
	CHECK(st_wren_connected_last_error(connected) == NULL);
	for (uint32_t mask = ST_CONSOLE_MOUSE_BUTTON_MIDDLE;
	    mask <= ST_CONSOLE_MOUSE_BUTTON_RIGHT; mask <<= 1) {
		button.data.mouse.buttons = mask;
		consumed = true;
		CHECK(st_wren_connected_handle_event(connected, &button,
		    &consumed) == 0 && !consumed);
		CHECK(!st_cterm_handle_mouse(cterm, &button));
		button.data.mouse.buttons = 0;
		consumed = false;
		CHECK(st_wren_connected_handle_event(connected, &button,
		    &consumed) == 0 && consumed);
	}
	CHECK(st_cterm_write(cterm, "\x1b[?1000h", 8, &written) == 0
	    && written == 8 && st_cterm_mouse_mode(cterm) == 1000);
	button.data.mouse.buttons = ST_CONSOLE_MOUSE_BUTTON_MIDDLE;
	consumed = false;
	CHECK(st_wren_connected_handle_event(connected, &button,
	    &consumed) == 0 && !consumed);
	CHECK(st_cterm_handle_mouse(cterm, &button));
	(void)st_cterm_handle_mouse(cterm, &left_release);
	button.data.mouse.buttons = 0;
	CHECK(st_wren_connected_handle_event(connected, &button,
	    &consumed) == 0 && !consumed);
	CHECK(st_wren_connected_handle_event(connected, &left_press,
	    &consumed) == 0 && !consumed);
	CHECK(st_wren_connected_handle_event(connected, &left_move,
	    &consumed) == 0 && !consumed);
	CHECK(st_wren_connected_handle_event(connected, &left_release,
	    &consumed) == 0 && !consumed);
	CHECK(st_wren_connected_interpret(connected, "copy_paste_mouse_off",
	    "import \"syncterm\" for CTerm\n"
	    "CTerm.mouseDisabled = true\n") == 0);
	button.data.mouse.buttons = ST_CONSOLE_MOUSE_BUTTON_MIDDLE;
	CHECK(st_wren_connected_handle_event(connected, &button,
	    &consumed) == 0 && !consumed);
	button.data.mouse.buttons = 0;
	CHECK(st_wren_connected_handle_event(connected, &button,
	    &consumed) == 0 && consumed);
	CHECK(st_wren_connected_interpret(connected, "copy_paste_mouse_on",
	    "import \"syncterm\" for CTerm\n"
	    "CTerm.mouseDisabled = false\n") == 0);
	CHECK(st_cterm_write(cterm, "\x1b[?1000l\x1b[?9h", 13,
	    &written) == 0 && written == 13 && st_cterm_mouse_mode(cterm) == 9);
	CHECK(st_wren_connected_handle_event(connected, &left_press,
	    &consumed) == 0 && !consumed);
	CHECK(st_cterm_handle_mouse(cterm, &left_press));
	CHECK(st_console_post_event(console, &left_release) == 0);
	consumed = false;
	CHECK(st_wren_connected_handle_event(connected, &left_move,
	    &consumed) == 0 && consumed);
	CHECK(st_cterm_handle_mouse(cterm, &left_press));
	(void)st_cterm_handle_mouse(cterm, &left_release);
	button.data.mouse.buttons = ST_CONSOLE_MOUSE_BUTTON_RIGHT;
	CHECK(st_wren_connected_handle_event(connected, &button,
	    &consumed) == 0 && !consumed);
	button.data.mouse.buttons = 0;
	CHECK(st_wren_connected_handle_event(connected, &button,
	    &consumed) == 0 && !consumed);
	CHECK(st_cterm_write(cterm, "\x1b[?9l", 5, &written) == 0
	    && written == 5 && st_cterm_mouse_mode(cterm) == 0);
	return 0;
}

static int
exercise_match_stream(st_wren_connected_t *connected, st_cterm_t *cterm,
	bool utf8)
{
	static const char *const sequences[] = {
		"\033[1;33m", "\033]0;hidden\007", "\033]0;hidden\033\\",
		"\033Phidden\033\\", "\033_hidden\033\\", "\033Xhidden\033\\",
		"\033^hidden\033\\",
	};
	size_t matches = 0;
	size_t consumed;
	char source[256], module[48], input[128];

	CHECK(st_wren_connected_interpret(connected, "match_stream",
	    "import \"syncterm\" for Hook\n"
	    "class MatchStreamProbe {\n"
	    "  static reset() {\n"
	    "    __clean = 0\n    __raw = 0\n    __hidden = 0\n  }\n"
	    "  static clean(m) {\n"
	    "    if (m.count == 2 && m[0] == \"AéZ\" && m[1] == \"é\") "
	    "__clean = __clean + 1\n  }\n"
	    "  static raw() { __raw = __raw + 1 }\n"
	    "  static hidden() { __hidden = __hidden + 1 }\n"
	    "  static check(n) {\n"
	    "    if (__clean != n || __raw != 0 || __hidden != 0) "
	    "Fiber.abort(\"match stream %(n): clean=%(__clean), raw=%(__raw), hidden=%(__hidden)\")\n  }\n}\n"
	    "MatchStreamProbe.reset()\n"
	    "Hook.onMatchClean(\"A(é)Z\", Fn.new {|m| MatchStreamProbe.clean(m) })\n"
	    "Hook.onMatch(\"AéZ\", Fn.new {|m| MatchStreamProbe.raw() })\n"
	    "Hook.onMatchClean(\"hidden\", Fn.new {|m| MatchStreamProbe.hidden() })\n") == 0);
	for (size_t index = 0; index < sizeof(sequences) / sizeof(sequences[0]); ++index) {
		int count = snprintf(input, sizeof(input), "A%s%sZ", sequences[index],
		    utf8 ? "\xc3\xa9" : "\x82");
		CHECK(count > 0 && (size_t)count < sizeof(input));
		size_t length = (size_t)count;

		/* Every split, then single-byte writes, including split UTF-8. */
		for (size_t split = 0; split <= length + 1; ++split) {
			if (split <= length) {
				CHECK(st_cterm_write(cterm, input, split, &consumed) == 0
				    && consumed == split);
				CHECK(st_cterm_write(cterm, input + split, length - split,
				    &consumed) == 0 && consumed == length - split);
			}
			else {
				for (size_t byte = 0; byte < length; ++byte)
					CHECK(st_cterm_write(cterm, input + byte, 1,
					    &consumed) == 0 && consumed == 1);
			}
			++matches;
			CHECK(snprintf(module, sizeof(module), "match_stream_check_%zu",
			    matches) < (int)sizeof(module));
			CHECK(snprintf(source, sizeof(source),
			    "import \"match_stream\" for MatchStreamProbe\n"
			    "MatchStreamProbe.check(%zu)\n", matches) < (int)sizeof(source));
			CHECK(st_wren_connected_interpret(connected, module, source) == 0);
		}
	}
	return 0;
}

static int
exercise_connected_scrollback(st_wren_host_t *host,
	st_console_t *console, struct st_profile *profile)
{
	st_font_catalog_t *fonts = NULL;
	st_cterm_t *cterm = NULL;
	st_wren_connected_t *connected = NULL;
	struct st_profile session_profile;
	char font_path[sizeof(ST_WREN_TEST_DIRECTORY) + 40];
	struct st_console_event escape = {
		.type = ST_CONSOLE_EVENT_KEY_CODE,
		.data.key = {.code = 27},
	};
	size_t consumed;
	uint32_t width, height;

	CHECK(st_font_catalog_create(&fonts, console) == 0);
	st_profile_init(&session_profile);
	CHECK(st_profile_copy(&session_profile, profile));
	free(session_profile.download_directory);
	session_profile.download_directory = strdup(ST_WREN_TEST_DIRECTORY);
	CHECK(session_profile.download_directory != NULL);
	CHECK(snprintf(font_path, sizeof(font_path),
	    "%s/wren-session-font.f16", ST_WREN_TEST_DIRECTORY)
	    < (int)sizeof(font_path));
	(void)fs_remove_file(font_path);
	struct st_cterm_config terminal_config = {
		.console = console,
		.fonts = fonts,
		.emulation = ST_CTERM_ANSI_BBS,
		.encoding = ST_CTERM_ENCODING_UTF8,
		.rip_version = ST_CTERM_RIP_1,
		.codepage = ST_CODEPAGE_CP437,
		.scrollback_lines = 2,
		.quiet = true,
		.input_filter = connected_input_filter,
		.input_filter_active = connected_input_active,
		.input_filter_context = &connected,
		.capture = connected_capture_input,
		.capture_active = connected_capture_active,
		.capture_context = &connected,
	};
	CHECK(st_cterm_create(&cterm, &terminal_config) == 0);
	CHECK(st_cterm_write(cterm, "!|$SBARON$\r", 11, &consumed) == 0);
	CHECK(consumed == 11 && st_cterm_status_display(cterm) == 1);
	CHECK(st_cterm_size(cterm, &width, &height) == 0);
	for (uint32_t row = 0; row < height + 3; ++row) {
		CHECK(st_cterm_write(cterm, "X\r\n", 3, &consumed) == 0);
		CHECK(consumed == 3);
	}
	struct st_wren_connected_config connected_config = {
		.host = host,
		.console = console,
		.profile = &session_profile,
		.cterm = cterm,
	};
	CHECK(st_wren_connected_create(&connected, &connected_config) == 0);
	CHECK(exercise_match_stream(connected, cterm, true) == 0);
	CHECK(exercise_connected_copy_paste_mouse(connected, console, cterm) == 0);
	CHECK(st_wren_connected_interpret(connected, "scrollback_paint_cursor",
	    "import \"syncterm\" for Scrollback, Surface, Screen, CTerm\n"
	    "import \"scrollback_view\" for ScrollbackView\n"
	    "var saved = Screen.save()\n"
	    "var x = CTerm.originX\n"
	    "var y = CTerm.originY\n"
	    "var width = Scrollback.width\n"
	    "var height = CTerm.height\n"
	    "var live = Screen.readRect(x, y, x + width - 1, y + height - 1)\n"
	    "var viewport = Surface.new(width, height)\n"
	    "ScrollbackView.blankSurroundings_()\n"
	    "Screen.cursor.visible = false\n"
	    "ScrollbackView.paint_(viewport, live, Scrollback.height, Scrollback.height, x, y)\n"
	    "if (Screen.window.position[0] != x || Screen.window.position[1] != y) Fiber.abort(\"scrollback cursor position\")\n"
	    "if (Screen.cursor.visible) Fiber.abort(\"scrollback hidden cursor\")\n"
	    "Screen.cursor.visible = true\n"
	    "ScrollbackView.paint_(viewport, live, Scrollback.height, Scrollback.height, x, y)\n"
	    "if (!Screen.cursor.visible) Fiber.abort(\"scrollback visible cursor\")\n"
	    "Screen.restore(saved)\n") == 0);
	CHECK(st_wren_connected_render_status(connected, true) == 0);
	CHECK(st_wren_connected_last_error(connected) == NULL);
	CHECK(st_wren_connected_interpret(connected, "font_grant",
	    "import \"syncterm\" for Download\n"
	    "class FontGrant {\n"
	    "  static file { __file }\n"
	    "  static prepare() {\n"
	    "    __file = Download.create(\"wren-session-font.f16\")\n"
	    "  }\n"
	    "}\n"
	    "FontGrant.prepare()\n") == 0);
	{
		static const uint8_t font_image[4096] = {0};
		FILE *stream = fopen(font_path, "wb");

		CHECK(stream != NULL);
		CHECK(fwrite(font_image, 1, sizeof(font_image), stream)
		    == sizeof(font_image));
		CHECK(fclose(stream) == 0);
	}
	CHECK(st_wren_connected_interpret(connected, "font_load_consent",
	    "import \"syncterm\" for CTerm, Font, ScreenSupports\n"
	    "import \"font_grant\" for FontGrant\n"
	    "if (ScreenSupports.fontSelection) {\n"
	    "  if (FontGrant.file == null || !Font.load(FontGrant.file)) Fiber.abort(\"font load\")\n"
	    "  var selector = Font.count - 1\n"
	    "  if (!Font.available(selector) || Font.name(selector) == \"\") Fiber.abort(\"loaded font listing\")\n"
	    "  CTerm.altFont = selector\n"
	    "  if (CTerm.altFont != selector) Fiber.abort(\"loaded font select\")\n"
	    "  CTerm.altFont = 0\n"
	    "}\n") == 0);
	CHECK(st_wren_connected_interpret(connected, "cterm_state_view",
	    "import \"syncterm\" for CTerm, Font, ScreenSupports\n"
	    "if (CTerm.x < 1 || CTerm.y < 1 || CTerm.width < 1 || CTerm.height < 1) Fiber.abort(\"cursor\")\n"
	    "if (CTerm.topMargin < 1 || CTerm.bottomMargin > CTerm.height || CTerm.leftMargin < 1 || CTerm.rightMargin > CTerm.width) Fiber.abort(\"margins\")\n"
	    "if (CTerm.attr < 0 || CTerm.fgColor < 0 || CTerm.bgColor < 0) Fiber.abort(\"colors\")\n"
	    "if (CTerm.altFonts.count != 4 || CTerm.fontSlot < 0 || CTerm.altFont != CTerm.altFonts[0]) Fiber.abort(\"fonts\")\n"
	    "if (Font.load(\"not a File\")) Fiber.abort(\"font consent\")\n"
	    "if (!(Fiber.new { CTerm.saveScreenshot(\"not a File\", true) }.try() is String)) Fiber.abort(\"screenshot consent\")\n"
	    "if (ScreenSupports.fontSelection) {\n"
	    "  var before = CTerm.altFont\n"
	    "  CTerm.altFont = before\n"
	    "  if (CTerm.altFont != before) Fiber.abort(\"font setter\")\n"
	    "}\n"
	    "if (CTerm.hasPaletteOverride || CTerm.paletteOverride.count != 0) Fiber.abort(\"palette\")\n"
	    "if (CTerm.scrollbackLines != 2 || CTerm.scrollbackWidth != CTerm.width) Fiber.abort(\"scrollback\")\n"
	    "if (CTerm.emulation != 0 || !CTerm.started || CTerm.skypix) Fiber.abort(\"mode\")\n"
	    "var ea = CTerm.extAttr\n"
	    "var lc = CTerm.lastColumnFlag\n"
	    "if ((ea.autoWrap != true && ea.autoWrap != false) || (ea.alternateKeypad != true && ea.alternateKeypad != false)) Fiber.abort(\"ext attr\")\n"
	    "if ((lc.set != true && lc.set != false) || (lc.enabled != true && lc.enabled != false) || (lc.forced != true && lc.forced != false)) Fiber.abort(\"last column\")\n") == 0);
	CHECK(st_console_post_event(console, &escape) == 0);
	CHECK(st_wren_connected_interpret(connected, "connected_menu_handoff",
	    "import \"syncterm\" for Host, CTerm\n"
	    "Host.editBBSList()\n"
	    "if (CTerm.suspended) Fiber.abort(\"menu left terminal suspended\")\n") == 0);
	CHECK(st_wren_connected_interpret(connected, "font_picker_cancel",
	    "import \"font_pick\" for FontApp\n"
	    "FontApp.run()\n") == 0);
	bool font_claimed = false;
	CHECK(st_wren_connected_handle_event(connected, &escape,
	    &font_claimed) == 0 && font_claimed);
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_last_error(connected) == NULL);
	CHECK(st_wren_connected_interpret(connected, "font_modal_state",
	    "import \"syncterm\" for CTerm\n"
	    "if (CTerm.suspended) Fiber.abort(\"font modal did not restore\")\n") == 0);
	CHECK(st_wren_connected_interpret(connected, "rip_input_filter",
	    "import \"syncterm\" for Hook\n"
	    "Hook.onInput(233, Fn.new {|scalar| \"X\" })\n") == 0);
	CHECK(st_cterm_write(cterm, "\x1b[H\xc3\xa9", 5,
	    &consumed) == 0 && consumed == 5);
	struct st_console_rect filtered_cell = {0, 0, 1, 1};
	struct st_console_cell filtered_value;
	CHECK(st_console_read_cells(console, &filtered_cell,
	    &filtered_value, 1) == 0 && filtered_value.glyph == 'X');
	CHECK(st_wren_connected_interpret(connected, "scrollback_contract",
	    "import \"syncterm\" for Scrollback, Surface, CTerm, Conn\n"
	    "if (Scrollback.width != CTerm.width || Scrollback.height != 2) Fiber.abort(\"ring geometry\")\n"
	    "var surface = Surface.new(CTerm.width, 2)\n"
	    "if (Scrollback.copyRowsTo(surface, 0, 2, 0) != 2) Fiber.abort(\"ring copy\")\n"
	    "Conn.scrollback()\n") == 0);
	CHECK(st_console_post_event(console, &escape) == 0);
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_last_error(connected) == NULL);
	bool claimed = false;
	CHECK(st_console_post_event(console, &escape) == 0);
	CHECK(st_wren_connected_handle_key(connected, 0x3000,
	    &claimed) == 0);
	CHECK(claimed && st_wren_connected_last_error(connected) == NULL);
	CHECK(st_wren_connected_interpret(connected, "music_menu_probe",
	    "import \"music_menu\" for MusicMenu\n"
	    "MusicMenu.run()\n") == 0);
	CHECK(st_wren_connected_last_error(connected) == NULL);
	claimed = false;
	CHECK(st_wren_connected_handle_event(connected, &escape,
	    &claimed) == 0);
	CHECK(claimed);
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_last_error(connected) == NULL);
	claimed = false;
	CHECK(st_wren_connected_handle_key(connected, 0x2e00,
	    &claimed) == 0 && claimed);
	claimed = false;
	CHECK(st_wren_connected_handle_event(connected, &escape,
	    &claimed) == 0 && claimed);
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_last_error(connected) == NULL);
	CHECK(st_wren_connected_interpret(connected, "capture_modal_state",
	    "import \"syncterm\" for CTerm\n"
	    "if (CTerm.suspended) Fiber.abort(\"capture modal did not restore\")\n") == 0);
	claimed = false;
	CHECK(st_wren_connected_handle_key(connected, 0x2c00, &claimed) == 0);
	CHECK(claimed);
	claimed = false;
	CHECK(st_wren_connected_handle_event(connected, &escape,
	    &claimed) == 0);
	CHECK(claimed);
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_last_error(connected) == NULL);
	claimed = false;
	CHECK(st_console_post_event(console, &escape) == 0);
	CHECK(st_wren_connected_handle_key(connected, 0x29e0,
	    &claimed) == 0);
	CHECK(claimed && st_wren_connected_last_error(connected) == NULL);
	CHECK(st_wren_connected_interpret(connected, "cterm_write_probe",
	    "import \"syncterm\" for CTerm\n"
	    "CTerm.write(String.fromCodePoint(27) + \"[HXY\")\n") == 0);
	struct st_console_rect written = {0, 0, 2, 1};
	struct st_console_cell written_cells[2];
	CHECK(st_console_read_cells(console, &written, written_cells, 2) == 0);
	CHECK(written_cells[0].glyph == 'X' && written_cells[1].glyph == 'Y');
	CHECK(st_wren_connected_interpret(connected, "match_registration",
	    "import \"syncterm\" for Hook, CTerm\n"
	    "Hook.onMatchClean(\"W(e)lcome\", Fn.new {|m| CTerm.music = (m.count == 2 && m[0] == \"Welcome\" && m[1] == \"e\") ? 2 : 0 })\n"
	    "Hook.onMatch(\"W(e)lcome\", Fn.new {|m| CTerm.music = 3 })\n"
	    "Hook.onMatch(\"foo\", Fn.new {|m| CTerm.music = 1 })\n"
	    "var bad = Fiber.new { Hook.onMatch(\".*bad\", Fn.new {}) }\n"
	    "bad.try()\n"
	    "if (bad.error == null) Fiber.abort(\"unanchored pattern accepted\")\n") == 0);
	const char *coloured_match = "W\x1b[1;33melcome";
	CHECK(st_cterm_write(cterm, coloured_match, strlen(coloured_match),
	    &consumed) == 0 && consumed == strlen(coloured_match));
	CHECK(st_cterm_music_mode(cterm) == 2);
	CHECK(st_cterm_write(cterm, "foo", 3, &consumed) == 0 && consumed == 3);
	CHECK(st_cterm_music_mode(cterm) == 1);
	CHECK(st_wren_connected_interpret(connected, "match_unicode",
	    "import \"syncterm\" for Hook, CTerm\n"
	    "Hook.onMatch(\"世\", Fn.new {|m| CTerm.music = 2 })\n"
	    "var self = null\n"
	    "self = Hook.onMatch(\"bar\", Fn.new {|m|\n"
	    "  self.remove()\n"
	    "  CTerm.music = 1\n"
	    "})\n") == 0);
	CHECK(st_cterm_write(cterm, "\xe4\xb8", 2, &consumed) == 0 && consumed == 2);
	CHECK(st_cterm_music_mode(cterm) == 1);
	CHECK(st_cterm_write(cterm, "\x96", 1, &consumed) == 0 && consumed == 1);
	CHECK(st_cterm_music_mode(cterm) == 2);
	CHECK(st_cterm_write(cterm, "bar", 3, &consumed) == 0 && consumed == 3);
	CHECK(st_cterm_music_mode(cterm) == 1);
	CHECK(st_wren_connected_interpret(connected, "match_reset_music",
	    "import \"syncterm\" for CTerm\nCTerm.music = 0\n") == 0);
	CHECK(st_cterm_write(cterm, "bar", 3, &consumed) == 0 && consumed == 3);
	CHECK(st_cterm_music_mode(cterm) == 0);
	FILE *plain = tmpfile();
	char captured[32] = {0};
	CHECK(plain != NULL);
	CHECK(st_wren_connected_capture_start(connected, plain, false) == 0);
	CHECK(st_wren_connected_interpret(connected, "capture_plain_active",
	    "import \"syncterm\" for Capture\n"
	    "if (!Capture.active || Capture.paused) Fiber.abort(\"capture start\")\n"
	    "Capture.pause()\n"
	    "if (!Capture.paused) Fiber.abort(\"capture pause\")\n"
	    "Capture.resume()\n") == 0);
	CHECK(st_cterm_write(cterm, "A\x1b[31m\xc3\xa9", 8,
	    &consumed) == 0 && consumed == 8);
	CHECK(st_wren_connected_interpret(connected, "capture_pause",
	    "import \"syncterm\" for Capture\nCapture.pause()\n") == 0);
	CHECK(st_cterm_write(cterm, "B", 1, &consumed) == 0);
	CHECK(st_wren_connected_interpret(connected, "capture_resume",
	    "import \"syncterm\" for Capture\nCapture.resume()\n") == 0);
	CHECK(st_cterm_write(cterm, "C", 1, &consumed) == 0);
	CHECK(fflush(plain) == 0 && fseek(plain, 0, SEEK_SET) == 0);
	CHECK(fread(captured, 1, 3, plain) == 3);
	CHECK(memcmp(captured, "AXC", 3) == 0);
	CHECK(st_wren_connected_interpret(connected, "capture_stop",
	    "import \"syncterm\" for Capture\n"
	    "Capture.stop()\nCapture.stop()\n"
	    "if (Capture.active || Capture.paused) Fiber.abort(\"capture stop\")\n") == 0);
	FILE *raw = tmpfile();
	CHECK(raw != NULL);
	CHECK(st_wren_connected_capture_start(connected, raw, true) == 0);
	CHECK(st_cterm_write(cterm, "D\x1b[32mE", 7,
	    &consumed) == 0 && consumed == 7);
	CHECK(fflush(raw) == 0 && fseek(raw, 0, SEEK_SET) == 0);
	CHECK(fread(captured, 1, 7, raw) == 7);
	CHECK(memcmp(captured, "D\x1b[32mE", 7) == 0);
	st_wren_connected_destroy(connected);
	connected = NULL;
	st_cterm_destroy(&cterm);
	terminal_config.encoding = ST_CTERM_ENCODING_CODEPAGE;
	terminal_config.rip_version = ST_CTERM_RIP_OFF;
	CHECK(st_cterm_create(&cterm, &terminal_config) == 0);
	connected_config.cterm = cterm;
	CHECK(st_wren_connected_create(&connected, &connected_config) == 0);
	CHECK(exercise_match_stream(connected, cterm, false) == 0);
	CHECK(st_wren_connected_interpret(connected, "match_codepage",
	    "import \"syncterm\" for Hook, CTerm\n"
	    "Hook.onMatch(\"£\", Fn.new {|m| CTerm.music = 2 })\n") == 0);
	CHECK(st_cterm_write(cterm, "\x9c", 1, &consumed) == 0 && consumed == 1);
	CHECK(st_cterm_music_mode(cterm) == 2);
	CHECK(st_cterm_size(cterm, &width, &height) == 0);
	for (uint32_t row = 0; row < height + 3; ++row)
		CHECK(st_cterm_write(cterm, "\x82 history\r\n", 11, &consumed) == 0
		    && consumed == 11);
	CHECK(st_wren_connected_interpret(connected, "scrollback_codepage",
	    "import \"syncterm\" for Scrollback, Surface, Screen, CTerm\n"
	    "import \"scrollback_view\" for ScrollbackView\n"
	    "var rows = Surface.new(Scrollback.width, 2)\n"
	    "if (Scrollback.copyRowsTo(rows, 0, 2, 0) != 2 || "
	    "rows.cellAt(0, 1).ch != \"é\") Fiber.abort(\"CP437 history cells\")\n"
	    "Screen.modalRun(Fn.new {\n"
	    "  var x = CTerm.originX\n  var y = CTerm.originY\n"
	    "  var live = Screen.readRect(x, y, x + Scrollback.width - 1, y + CTerm.height - 1)\n"
	    "  var viewport = Surface.new(Scrollback.width, CTerm.height)\n"
	    "  ScrollbackView.blankSurroundings_()\n"
	    "  ScrollbackView.paint_(viewport, live, Scrollback.height, 0, x, y)\n"
	    "  var shown = Screen.readRect(x, y + 1, x + 8, y + 1)\n"
	    "  if (shown.cellAt(0, 0).ch != \"é\" || shown.cellAt(2, 0).ch != \"h\") "
	    "Fiber.abort(\"CP437 scrollback paint\")\n"
	    "})\n") == 0);
	struct st_wren_host *menu_host = host;
	struct st_console_cell *saved_history = NULL;
	struct st_console_cell *codepage_history = NULL;
	uint32_t saved_columns;
	size_t saved_rows, codepage_rows;
	enum st_screen_mode saved_mode;
	bool saved_status;
	CHECK(st_session_history_copy(menu_host->config.session_history,
	    &saved_history, &saved_columns, &saved_rows, &saved_mode, &saved_status) == 0);
	CHECK(st_cterm_copy_history(cterm, &codepage_history, &width, &codepage_rows) == 0);
	CHECK(st_session_history_publish(menu_host->config.session_history,
	    codepage_history, width, codepage_rows, ST_SCREEN_MODE_80X25, false) == 0);
	CHECK(wrenInterpret(menu_host->vm, "offline_scrollback_codepage",
	    "import \"syncterm_menu\" for Menu\n"
	    "import \"menu_scrollback\" for OfflineScrollbackCanvas\n"
	    "import \"ui_widget\" for Rect\n"
	    "var source = Menu.offlineScrollback\n"
	    "var viewer = OfflineScrollbackCanvas.new(null, source, null, false, null)\n"
	    "viewer.bounds = Rect.new(1, 1, source.width, source.height - 2)\n"
	    "if (viewer.draw().cellAt(0, 1).ch != \"é\") Fiber.abort(\"CP437 offline newest\")\n"
	    "viewer.move_(-source.height)\n"
	    "if (viewer.draw().cellAt(0, 1).ch != \"é\") Fiber.abort(\"CP437 offline oldest\")\n")
	    == WREN_RESULT_SUCCESS);
	CHECK(st_session_history_publish(menu_host->config.session_history,
	    saved_history, saved_columns, saved_rows, saved_mode, saved_status) == 0);
	free(codepage_history);
	free(saved_history);
	st_wren_connected_destroy(connected);
	st_cterm_destroy(&cterm);
	st_font_catalog_destroy(fonts);
	CHECK(fs_remove_file(font_path) == 0);
	st_profile_dispose(&session_profile);
	return 0;
}

static int
exercise_connected_sftp(st_wren_host_t *host, st_console_t *console,
	struct st_profile *profile)
{
	static const unsigned char remote[] =
	    "short reads must fill every reserved interval without holes";
	struct fake_sftp fake = {
		.remote = remote,
		.remote_size = sizeof(remote) - 1,
		.modified = INT64_C(1700000123),
	};
	const struct st_sftp_provider provider = {
		.context = &fake,
		.available = fake_sftp_available,
		.public_directory = fake_sftp_public_directory,
		.submit = fake_sftp_submit,
		.poll = fake_sftp_poll,
	};
	struct st_wren_connected_config config = {
		.host = host,
		.console = console,
		.profile = profile,
		.sftp = &provider,
	};
	st_wren_connected_t *connected = NULL;
	char path[sizeof(ST_WREN_TEST_DIRECTORY) + 32];
	char queue_path[sizeof(ST_WREN_TEST_DIRECTORY) + 96];
	struct timespec delay = {.tv_nsec = 300000000L};
	FILE *stream;
	unsigned char local[sizeof(remote)];

	free(profile->download_directory);
	profile->download_directory = strdup(ST_WREN_TEST_DIRECTORY);
	CHECK(profile->download_directory != NULL);
	(void)snprintf(queue_path, sizeof(queue_path),
	    "%s/cache/[u18-8ed3f6ad685b959ead7022518e1af76cd816f8e8ec7ccdda1ed4018e8f2223f8]/sftp_queue.won",
	    ST_WREN_TEST_DIRECTORY);
	(void)fs_remove_file(queue_path);
	CHECK(st_wren_connected_create(&connected, &config) == 0);
	CHECK(exercise_filesystem_path_handles(connected) == 0);
	CHECK(st_wren_connected_interpret(connected, "sftp_batch",
	    "import \"syncterm\" for SFTP\n"
	    "class Batch {\n"
	    "  static count { __count }\n"
	    "  static count=(n) { __count = n }\n"
	    "}\n"
	    "Batch.count = 0\n"
	    "for (i in 0...3) SFTP.stat(Fiber.new {|r| Batch.count = Batch.count + 1 }, \"/remote.bin\")\n") == 0);
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "sftp_batch_result",
	    "import \"sftp_batch\" for Batch\n"
	    "if (Batch.count != 3) Fiber.abort(\"SFTP replies were not drained as a batch\")\n") == 0);
	CHECK(st_wren_connected_interpret(connected, "connected_hyperlinks",
	    "import \"syncterm\" for Hyperlinks\n"
	    "if (!Hyperlinks.containsKey(1) || Hyperlinks[1] != "
	    "\"https://history.invalid/\") Fiber.abort(\"hyperlinks\")\n")
	    == 0);
	CHECK(st_wren_connected_interpret(connected, "connected_fs_won_test",
	    "import \"syncterm\" for Cache, File, FileError, WON, WONError\n"
	    "var decoded = WON.deserialize(\"{\\\"value\\\":[1,true,null]}\")\n"
	    "if (decoded is WONError || decoded[\"value\"][0] != 1) Fiber.abort(\"WON decode\")\n"
	    "if (!(WON.deserialize(\"{\") is WONError)) Fiber.abort(\"WON error\")\n"
	    "var replaced = Cache.replace(\"fs-test.won\", \"atomic\")\n"
	    "if (replaced is FileError || replaced != true) Fiber.abort(\"replace\")\n"
	    "var f = Cache.list[\"fs-test.won\"]\n"
	    "if (!(f is File) || (f.open() is FileError) || f.read() != \"atomic\" || (f.close() is FileError)) Fiber.abort(\"file round trip\")\n"
	    "if (!Cache.delete(\"fs-test.won\")) Fiber.abort(\"delete\")\n") == 0);
	CHECK(st_wren_connected_interpret(connected, "sftp_short_read_test",
	    "import \"sftp_queue\" for SftpQueue\n"
	    "if (SftpQueue.enqueue(SftpQueue.DOWNLOAD, \"short.bin\", "
	    "\"/remote.bin\", 58) == null) Fiber.abort(\"enqueue\")\n") == 0);
	(void)st_thread_sleep(&delay, NULL);
	for (size_t i = 0; i < 64; ++i)
		CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "sftp_short_read_result",
	    "import \"sftp_queue\" for SftpQueue\n"
	    "var jobs = SftpQueue.snapshot\n"
	    "if (jobs.count != 1 || jobs[0].status != SftpQueue.DONE || "
	    "jobs[0].done != jobs[0].total) Fiber.abort(\"download result\")\n")
	    == 0);
	(void)snprintf(path, sizeof(path), "%s/short.bin",
	    ST_WREN_TEST_DIRECTORY);
	CHECK(fs_open(&stream, path, "rb") == 0);
	CHECK(fread(local, 1, sizeof(remote) - 1, stream) == sizeof(remote) - 1);
	CHECK(fclose(stream) == 0);
	CHECK(memcmp(local, remote, sizeof(remote) - 1) == 0);
	fake.close_error = true;
	CHECK(st_wren_connected_interpret(connected, "sftp_close_error_test",
	    "import \"sftp_queue\" for SftpQueue\n"
	    "SftpQueue.enqueue(SftpQueue.DOWNLOAD, \"close.bin\", "
	    "\"/close-error.bin\", 58)\n") == 0);
	(void)st_thread_sleep(&delay, NULL);
	for (size_t i = 0; i < 64; ++i)
		CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "sftp_close_error_result",
	    "import \"sftp_queue\" for SftpQueue\n"
	    "var failed = SftpQueue.snapshot.where {|j| "
	    "j.remote == \"/close-error.bin\" }.toList\n"
	    "if (failed.count != 1 || failed[0].status != SftpQueue.FAILED) "
	    "Fiber.abort(\"close failure reported DONE\")\n") == 0);
	(void)snprintf(path, sizeof(path), "%s/close.bin",
	    ST_WREN_TEST_DIRECTORY);
	CHECK(fs_get_info(&(struct fs_info){0}, path) == 0);
	CHECK(fs_remove_file(path) == 0);
	fake.close_error = false;
	fake.transient_read = true;
	fake.transient_sent = false;
	fake.transient_offset = 21;
	CHECK(st_wren_connected_interpret(connected, "sftp_suspend_test",
	    "import \"sftp_queue\" for SftpQueue\n"
	    "SftpQueue.enqueue(SftpQueue.DOWNLOAD, \"resume.bin\", "
	    "\"/resume.bin\", 58)\n") == 0);
	(void)st_thread_sleep(&delay, NULL);
	for (size_t i = 0; i < 64; ++i)
		CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "sftp_suspend_result",
	    "import \"sftp_queue\" for SftpQueue\n"
	    "var jobs = SftpQueue.snapshot.where {|j| "
	    "j.remote == \"/resume.bin\" }.toList\n"
	    "if (!SftpQueue.suspended || jobs.count != 1 || "
	    "jobs[0].status != SftpQueue.ACTIVE) Fiber.abort(\"not suspended\")\n")
	    == 0);
	st_wren_connected_destroy(connected);
	connected = NULL;
	fake.transient_read = false;
	CHECK(st_wren_connected_create(&connected, &config) == 0);
	(void)st_thread_sleep(&delay, NULL);
	for (size_t i = 0; i < 64; ++i)
		CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "sftp_resume_result",
	    "import \"sftp_queue\" for SftpQueue\n"
	    "var jobs = SftpQueue.snapshot\n"
	    "if (jobs.count != 1 || jobs[0].status != SftpQueue.DONE || "
	    "jobs[0].done != jobs[0].total) Fiber.abort(\"resume result\")\n")
	    == 0);
	(void)snprintf(path, sizeof(path), "%s/resume.bin",
	    ST_WREN_TEST_DIRECTORY);
	CHECK(fs_open(&stream, path, "rb") == 0);
	CHECK(fread(local, 1, sizeof(remote) - 1, stream) == sizeof(remote) - 1);
	CHECK(fclose(stream) == 0);
	CHECK(memcmp(local, remote, sizeof(remote) - 1) == 0);
	CHECK(fs_remove_file(path) == 0);
	st_wren_connected_destroy(connected);
	connected = NULL;
	(void)snprintf(path, sizeof(path), "%s/short.bin",
	    ST_WREN_TEST_DIRECTORY);
	CHECK(fs_remove_file(path) == 0);
	CHECK(write_file(queue_path, "{ malformed queue") == 0);
	CHECK(st_wren_connected_create(&connected, &config) == 0);
	CHECK(st_wren_connected_interpret(connected, "invalid_queue_result",
	    "import \"sftp_queue\" for SftpQueue\n"
	    "if (!SftpQueue.fatal || SftpQueue.enqueue(SftpQueue.DOWNLOAD, "
	    "\"x\", \"/remote.bin\", 1) != null) Fiber.abort(\"invalid queue\")\n")
	    == 0);
	st_wren_connected_destroy(connected);
	CHECK(file_equals(queue_path, "{ malformed queue"));
	CHECK(fs_remove_file(queue_path) == 0);
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
		if (cells[i].glyph != (unsigned char)text[i])
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
	CHECK(before.glyph == after.glyph
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
	    "if (Menu.entries.count != 2 || Menu.entries[0].name != \"Zulu\" || Menu.entries[0].searchKey != \"zulu\") Fiber.abort(\"entries/sort\")\n"
	    "if (Menu.entries[0].id == Menu.entries[1].id) Fiber.abort(\"duplicate IDs\")\n"
	    "var q = Menu.quickConnect(\"ssh://guest:secret@[::1]:2222\")\n"
	    "if (q == null || q.connType != 5 || q.addr != \"::1\" || q.port != 2222 || q.user != \"guest\" || q.password != \"secret\") Fiber.abort(\"quick\")\n"
	    "if (Menu.fileLocations[\"configuration\"].count == 0) Fiber.abort(\"paths\")\n"
	    "if (Menu.paletteDefaults(1)[1].count != 16) Fiber.abort(\"palette\")\n"
	    "var scalers = Menu.scalingModes\n"
	    "if (scalers.count != 3 || scalers[0] != \"Blocky\" || scalers[1] != \"Pointy\" || scalers[2] != \"External\") Fiber.abort(\"scaling catalog\")\n"
	    "if (Menu.fontsCatalog.count == 0) Fiber.abort(\"fonts\")\n"
	    "if (Menu.wrenScriptsMaxLength < 1 || Menu.maxPathLength < 1) Fiber.abort(\"limits\")\n"
	    "if (Menu.addWebList(\"Test\", \"invalid\", 0) == null || Menu.webListsDirty || Menu.webLists.count != 0) Fiber.abort(\"web add failure\")\n"
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
	    "b.encoding = 255\n"
	    "if (b.encoding != 255) Fiber.abort(\"bbs encoding\")\n"
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
	    "b.wrenScripts = [\"ui_list\", \"menu_bbs_editor\", \"ui_list\"]\n"
	    "b.sortOrder = 4\n"
	    "b.lfExpand = false\n"
	    "if (!b.dirty || b.connTypeName.count == 0 || b.palette.count != 2 || b.wrenScripts.count != 2) Fiber.abort(\"bbs\")\n"
	    "if (Menu.encryptionAvailable) {\n"
	    "  if (!Menu.setEncryption(6, 128, \"secret\") || Menu.encryptionAlgorithm != 6 || Menu.encryptionKeySize != 128) Fiber.abort(\"encrypt\")\n"
	    "  if (!Menu.setEncryption(7, 256, null) || Menu.encryptionAlgorithm != 7) Fiber.abort(\"reencrypt\")\n"
	    "  if (!Menu.setEncryption(0, 0, null) || Menu.encryptionAlgorithm != 0) Fiber.abort(\"decrypt\")\n"
	    "}\n";
	static const char numeric_source[] =
	    "import \"syncterm_menu\" for Menu\n"
	    "var s = Menu.settings\n"
	    "s.audioModes = 4294967295\n"
	    "s.modemRate = 4294967295\n"
	    "s.scrollbackLines = 2147483647\n"
	    "s.frameColor = 16\n"
	    "if (s.audioModes != 4294967295 || s.modemRate != 4294967295 || s.scrollbackLines != 2147483647 || s.frameColor != 16) Fiber.abort(\"settings numeric maxima\")\n"
	    "var bad = Fiber.new { s.audioModes = 4294967296 }\n"
	    "bad.try()\n"
	    "if (bad.error == null || s.audioModes != 4294967295) Fiber.abort(\"settings overflow\")\n"
	    "bad = Fiber.new { s.scrollbackLines = 0 }\n"
	    "bad.try()\n"
	    "if (bad.error == null || s.scrollbackLines != 2147483647) Fiber.abort(\"settings minimum\")\n"
	    "bad = Fiber.new { s.modemRate = 0.5 }\n"
	    "bad.try()\n"
	    "if (bad.error == null || s.modemRate != 4294967295) Fiber.abort(\"settings fractional\")\n"
	    "s.audioModes = 0\n"
	    "s.modemRate = 0\n"
	    "s.scrollbackLines = 1\n"
	    "if (s.audioModes != 0 || s.modemRate != 0 || s.scrollbackLines != 1) Fiber.abort(\"settings numeric minima\")\n"
	    "var b = Menu.entries[0]\n"
	    "var saved = [b.port, b.bpsRate, b.sortOrder]\n"
	    "b.port = 65535\n"
	    "b.bpsRate = 4294967295\n"
	    "b.sortOrder = -2147483648\n"
	    "if (b.port != 65535 || b.bpsRate != 4294967295 || b.sortOrder != -2147483648) Fiber.abort(\"bbs numeric bounds\")\n"
	    "bad = Fiber.new { b.port = 65536 }\n"
	    "bad.try()\n"
	    "if (bad.error == null || b.port != 65535) Fiber.abort(\"bbs port overflow\")\n"
	    "bad = Fiber.new { b.bpsRate = 4294967296 }\n"
	    "bad.try()\n"
	    "if (bad.error == null || b.bpsRate != 4294967295) Fiber.abort(\"bbs rate overflow\")\n"
	    "bad = Fiber.new { b.sortOrder = -2147483649 }\n"
	    "bad.try()\n"
	    "if (bad.error == null || b.sortOrder != -2147483648) Fiber.abort(\"bbs sort underflow\")\n"
	    "b.port = 0\n"
	    "b.bpsRate = 0\n"
	    "b.sortOrder = 2147483647\n"
	    "if (b.port != 0 || b.bpsRate != 0 || b.sortOrder != 2147483647) Fiber.abort(\"bbs numeric opposite bounds\")\n"
	    "b.port = saved[0]\n"
	    "b.bpsRate = saved[1]\n"
	    "b.sortOrder = saved[2]\n";
	static const char encoding_source[] =
	    "import \"syncterm\" for ConnType\n"
	    "if (ConnType.mqtt != 13) Fiber.abort(\"MQTT connection type\")\n"
	    "import \"syncterm_menu\" for Menu\n"
	    "import \"menu_bbs_editor\" for BbsEditor\n"
	    "import \"syncterm\" for Key, KeyEvent\n"
	    "import \"ui_style\" for Theme\n"
	    "class FontPickerApp {\n"
	    "  construct new(cancel) {\n"
	    "    _cancel = cancel\n"
	    "    _stack = []\n"
	    "  }\n"
	    "  modalStack { _stack }\n"
	    "  effectiveTheme { Theme.default }\n"
	    "  markDirty() {}\n"
	    "  labels { _labels }\n"
	    "  initial { _initial }\n"
	    "  popModal() {\n"
	    "    var pane = _stack.removeAt(-1)\n"
	    "    pane.parent = null\n"
	    "    return pane\n"
	    "  }\n"
	    "  modal(pane) {\n"
	    "    pane.parent = this\n"
	    "    _stack.add(pane)\n"
	    "    var list = pane.children[0]\n"
	    "    _labels = list.items.toList\n"
	    "    _initial = list.selected\n"
	    "    pane.handle(KeyEvent.new(_cancel ? Key.home : Key.end))\n"
	    "    pane.handle(KeyEvent.new(_cancel ? Key.escape : Key.enter))\n"
	    "    return pane\n"
	    "  }\n"
	    "}\n"
	    "if (Menu.encodings.count < 20 || Menu.fontsForEncoding(255).count != Menu.fontsCatalog.count) Fiber.abort(\"encodings\")\n"
	    "if (Menu.fontsForEncoding(12).count < 2 || Menu.fontEncoding(\"Codepage 850 Multilingual Latin I\") != 12) Fiber.abort(\"font encoding\")\n"
	    "var linked = {\"screenMode\": 1, \"encoding\": 0, \"font\": Menu.fontsCatalog[0][1], \"rip\": 0, \"noStatus\": false}\n"
	    "BbsEditor.changeEncoding_(linked, 24)\n"
	    "if (linked[\"screenMode\"] != 17 || linked[\"font\"] != Menu.fontsCatalog[32][1]) Fiber.abort(\"PETSCII upper\")\n"
	    "BbsEditor.changeFont_(linked, Menu.fontsCatalog[35][1])\n"
	    "if (linked[\"screenMode\"] != 18 || linked[\"encoding\"] != 25) Fiber.abort(\"PETSCII lower\")\n"
	    "BbsEditor.changeEncoding_(linked, 255)\n"
	    "if (linked[\"screenMode\"] != 1 || linked[\"font\"] != Menu.fontsCatalog[0][1]) Fiber.abort(\"UTF-8 switch\")\n"
	    "BbsEditor.changeFont_(linked, Menu.fontsCatalog[44][1])\n"
	    "if (linked[\"screenMode\"] != 1 || linked[\"encoding\"] != 255) Fiber.abort(\"UTF-8 font\")\n"
	    "BbsEditor.changeRip_(linked, 1)\n"
	    "if (linked[\"screenMode\"] != 5 || linked[\"encoding\"] != 0 || linked[\"font\"] != Menu.fontsCatalog[45][1]) Fiber.abort(\"RIP v1\")\n"
	    "BbsEditor.changeScreen_(linked, 25)\n"
	    "if (linked[\"rip\"] != 0 || linked[\"encoding\"] != 26 || linked[\"font\"] != Menu.fontsCatalog[43][1]) Fiber.abort(\"Prestel\")\n"
	    "for (defaults in [false, true]) {\n"
	    "  var bbs = defaults ? Menu.defaults : Menu.entries[0]\n"
	    "  for (encoding in [0, 12, 255]) {\n"
	    "    var draft = BbsEditor.draft_(bbs)\n"
	    "    draft[\"screenMode\"] = 1\n"
	    "    draft[\"rip\"] = 0\n"
	    "    draft[\"encoding\"] = encoding\n"
	    "    var choices = Menu.fontsForEncoding(encoding)\n"
	    "    draft[\"font\"] = choices[0][1]\n"
	    "    for (cancel in [false, true]) {\n"
	    "      var app = FontPickerApp.new(cancel)\n"
	    "      var rows = BbsEditor.rows_(app, bbs, draft, defaults)\n"
	    "      var fontRows = rows.where {|row| row[0].startsWith(\"Font \") }.toList\n"
	    "      if (fontRows.count != 1) Fiber.abort(\"missing Font row\")\n"
	    "      fontRows[0][1].call()\n"
	    "      if (app.labels.count != choices.count) Fiber.abort(\"font choice count\")\n"
	    "      for (i in 0...choices.count) {\n"
	    "        if (app.labels[i] != choices[i][1]) Fiber.abort(\"font choice label\")\n"
	    "      }\n"
	    "      var initial = cancel ? choices.count - 1 : 0\n"
	    "      if (app.initial != initial) Fiber.abort(\"current font selection\")\n"
	    "      if (draft[\"font\"] != choices[-1][1] || draft[\"encoding\"] != encoding) Fiber.abort(\"font picker result\")\n"
	    "      if (app.modalStack.count != 0) Fiber.abort(\"font picker dismissal\")\n"
	    "    }\n"
	    "  }\n"
	    "}\n";

	if (wrenInterpret(host->vm, "menu_contract", source)
	    != WREN_RESULT_SUCCESS) {
		fprintf(stderr, "menu contract failed: %s\n", host->last_error);
		return -1;
	}
	if (wrenInterpret(host->vm, "menu_numeric_contract", numeric_source)
	    != WREN_RESULT_SUCCESS) {
		fprintf(stderr, "menu numeric contract failed: %s\n",
		    host->last_error);
		return -1;
	}
	if (wrenInterpret(host->vm, "menu_encoding_contract", encoding_source)
	    != WREN_RESULT_SUCCESS) {
		fprintf(stderr, "menu encoding contract failed: %s\n",
		    host->last_error);
		return -1;
	}
	CHECK(host->bbs_model.count != 0);
	size_t key_size;
	CHECK(st_wren_bbs_model_search_key(&host->bbs_model,
	    host->bbs_model.entries[0], &key_size) != NULL);
	if (st_bbs_list_generation(host->bbs_list)
	    != host->bbs_model.source_generation)
		CHECK(host->bbs_model.entries[0]->name_key != NULL);
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
	    "Fiber.abort(\"deleted draft remained usable\")\n"
	    "var accent = Menu.create(\"Cafe\\u0301\")\n"
	    "if (accent == null || accent.name != \"Caf\\u00e9\" || "
	    "!accent.delete()) Fiber.abort(\"normalized Wren name\")\n";
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
	    "|| same.name != \"Aardvark\" || same.searchKey != \"aardvark\" || !Menu.sort() || "
	    "same.name != \"Aardvark\" || !copy.save() || copy.dirty || "
	    "copy.searchKey != \"aardvark\") "
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
	CHECK(host->bbs_model.count != 0);
	struct st_wren_bbs_record *loaded = host->bbs_model.entries[0];
	size_t loaded_key_size;
	CHECK(loaded->source_entry != NULL && loaded->name_key == NULL);
	CHECK(st_wren_bbs_model_search_key(&host->bbs_model, loaded,
	    &loaded_key_size) == loaded->source_entry->name_key);
	CHECK(loaded_key_size == loaded->source_entry->name_key_size);
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
exercise_ssh_key_persistence(st_wren_host_t *public_host)
{
	struct st_wren_host *host = public_host;
	struct st_profile profile;
	const struct st_bbs_entry *entry;
	uint8_t fingerprint[32];
	st_profile_init(&profile);
	strcpy(profile.name, "SSH key fixture");
	strcpy(profile.address, "ssh.example");
	memset(fingerprint, 0xa5, sizeof(fingerprint));
	/* First contact must not silently create a personal directory entry. */
	CHECK(st_wren_host_remember_ssh_key(host, &profile, fingerprint, false) == 0);
	CHECK(st_bbs_list_find(host->bbs_list, profile.name) == NULL);
	CHECK(st_wren_host_remember_ssh_key(host, &profile, fingerprint, true) == 0);
	CHECK(st_bbs_list_reload(host->bbs_list) == 0);
	entry = st_bbs_list_find(host->bbs_list, profile.name);
	CHECK(entry != NULL && entry->source == ST_BBS_SOURCE_PERSONAL);
	CHECK(entry->profile->ssh_fingerprint_size == 32
	    && memcmp(entry->profile->ssh_fingerprint, fingerprint, 32) == 0);
	/* Recording a key must preserve edits made since the session started. */
	strcpy(profile.address, "stale.example");
	memset(fingerprint, 0x5a, sizeof(fingerprint));
	CHECK(st_wren_host_remember_ssh_key(host, &profile, fingerprint, false) == 0);
	entry = st_bbs_list_find(host->bbs_list, profile.name);
	CHECK(entry != NULL && strcmp(entry->profile->address, "ssh.example") == 0
	    && memcmp(entry->profile->ssh_fingerprint, fingerprint, 32) == 0);
	memset(fingerprint, 0x3c, sizeof(fingerprint));
	CHECK(st_wren_host_remember_ssh_key(host, &profile, fingerprint, true) == 0);
	CHECK(st_bbs_list_reload(host->bbs_list) == 0);
	entry = st_bbs_list_find(host->bbs_list, profile.name);
	CHECK(entry != NULL && memcmp(entry->profile->ssh_fingerprint, fingerprint, 32) == 0);
	CHECK(!st_wren_host_can_trust_tls(host, &profile));
	CHECK(st_profile_copy(&profile, entry->profile));
	CHECK(st_wren_host_can_trust_tls(host, &profile));
	CHECK(st_wren_host_remember_tls_version(host, &profile, ST_TLS_VERSION_1_3) == 0);
	CHECK(st_bbs_list_reload(host->bbs_list) == 0);
	entry = st_bbs_list_find(host->bbs_list, profile.name);
	CHECK(entry != NULL && entry->profile->tls_version_floor == ST_TLS_VERSION_1_3);
	{
		const char pem[] = "-----BEGIN CERTIFICATE-----\nfixture\n-----END CERTIFICATE-----\n";
		char *first = NULL, *second = NULL;
		CHECK(st_wren_host_trust_tls(host, &profile, pem, &first) == 0 && first != NULL);
		CHECK(file_equals(first, pem));
		CHECK(st_bbs_list_reload(host->bbs_list) == 0);
		entry = st_bbs_list_find(host->bbs_list, profile.name);
		CHECK(entry != NULL && !entry->profile->tls_trust_web_pki
		    && entry->profile->tls_version_floor == ST_TLS_VERSION_UNKNOWN
		    && strcmp(entry->profile->tls_trusted_certificate, first) == 0);
		CHECK(st_profile_copy(&profile, entry->profile));
		CHECK(st_wren_host_trust_tls(host, &profile, pem, &second) == 0 && second != NULL);
		CHECK(strcmp(first, second) != 0 && file_equals(first, pem) && file_equals(second, pem));
		entry = st_bbs_list_find(host->bbs_list, profile.name);
		CHECK(entry != NULL && st_profile_copy(&profile, entry->profile));
		uint64_t generation = st_bbs_list_generation(host->bbs_list);
		host->config.safe_mode = true;
		CHECK(!st_wren_host_can_trust_tls(host, &profile));
		char *unused = NULL;
		CHECK(st_wren_host_trust_tls(host, &profile, pem, &unused) == IO_ERROR_PERMISSION_DENIED);
		CHECK(st_wren_host_remember_tls_version(host, &profile, ST_TLS_VERSION_1_2) == 0);
		CHECK(st_bbs_list_generation(host->bbs_list) == generation && unused == NULL);
		profile.tls_version_floor = ST_TLS_VERSION_UNKNOWN;
		st_wren_host_apply_tls_version(host, &profile);
		CHECK(profile.tls_version_floor == ST_TLS_VERSION_1_2);
		CHECK(st_bbs_list_reload(host->bbs_list) == 0);
		profile.tls_version_floor = ST_TLS_VERSION_UNKNOWN;
		st_wren_host_apply_tls_version(host, &profile);
		CHECK(profile.tls_version_floor == ST_TLS_VERSION_1_2);
		/* A deliberate edit to trust/version policy invalidates the observation. */
		profile.tls_version_floor = ST_TLS_VERSION_1_3;
		st_wren_host_apply_tls_version(host, &profile);
		CHECK(profile.tls_version_floor == ST_TLS_VERSION_1_3);
		profile.tls_version_floor = ST_TLS_VERSION_UNKNOWN;
		profile.tls_trust_web_pki = !profile.tls_trust_web_pki;
		st_wren_host_apply_tls_version(host, &profile);
		CHECK(profile.tls_version_floor == ST_TLS_VERSION_UNKNOWN);
		profile.tls_trust_web_pki = !profile.tls_trust_web_pki;
		generation = st_bbs_list_generation(host->bbs_list);
		CHECK(st_bbs_list_generation(host->bbs_list) == generation && unused == NULL);
		host->config.safe_mode = false;
		struct st_app_paths failed_paths = *host->config.paths;
		const struct st_app_paths *saved_paths = host->config.paths;
		char blocked[1024];
		CHECK(snprintf(blocked, sizeof(blocked), "%s/settings.ini", first) < (int)sizeof(blocked));
		failed_paths.settings_file = blocked;
		host->config.paths = &failed_paths;
		CHECK(st_wren_host_trust_tls(host, &profile, pem, &unused) < 0 && unused == NULL);
		CHECK(st_bbs_list_generation(host->bbs_list) == generation && file_equals(first, pem));
		host->config.paths = saved_paths;
		CHECK(fs_remove_file(first) == 0 && fs_remove_file(second) == 0);
		free(first); free(second);
	}
	CHECK(st_bbs_list_remove(host->bbs_list, profile.name, NULL) == 0);
	st_profile_dispose(&profile);
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

struct catalog_row {
	uint32_t value;
	const char *name;
};

static bool
catalog_matches(WrenVM *vm, const char *module, const char *variable,
	const struct catalog_row *expected, size_t count)
{
	wrenEnsureSlots(vm, 4);
	wrenGetVariable(vm, module, variable, 0);
	if (wrenGetSlotType(vm, 0) != WREN_TYPE_LIST
	    || wrenGetListCount(vm, 0) != (int)count)
		return false;
	for (size_t i = 0; i < count; ++i) {
		wrenGetListElement(vm, 0, (int)i, 1);
		if (wrenGetSlotType(vm, 1) != WREN_TYPE_LIST
		    || wrenGetListCount(vm, 1) != 2)
			return false;
		wrenGetListElement(vm, 1, 0, 2);
		wrenGetListElement(vm, 1, 1, 3);
		if (wrenGetSlotType(vm, 2) != WREN_TYPE_NUM
		    || wrenGetSlotDouble(vm, 2) != expected[i].value
		    || wrenGetSlotType(vm, 3) != WREN_TYPE_STRING
		    || strcmp(wrenGetSlotString(vm, 3), expected[i].name) != 0)
			return false;
	}
	return true;
}

#if ST_WREN_TEST_HAVE_PTY
static bool
serial_catalog_valid(WrenVM *vm, const char *module, const char *variable,
	bool require_native_rates)
{
	uint32_t previous = 0;
	int count;

	wrenEnsureSlots(vm, 4);
	wrenGetVariable(vm, module, variable, 0);
	if (wrenGetSlotType(vm, 0) != WREN_TYPE_LIST)
		return false;
	count = wrenGetListCount(vm, 0);
	if (count < 2)
		return false;
	for (int i = 0; i < count; ++i) {
		double number;
		char expected[16];

		wrenGetListElement(vm, 0, i, 1);
		if (wrenGetSlotType(vm, 1) != WREN_TYPE_LIST
		    || wrenGetListCount(vm, 1) != 2)
			return false;
		wrenGetListElement(vm, 1, 0, 2);
		wrenGetListElement(vm, 1, 1, 3);
		if (wrenGetSlotType(vm, 2) != WREN_TYPE_NUM
		    || wrenGetSlotType(vm, 3) != WREN_TYPE_STRING)
			return false;
		number = wrenGetSlotDouble(vm, 2);
		if (i == count - 1)
			return number == 0
			    && strcmp(wrenGetSlotString(vm, 3), "Current") == 0;
		if (number <= previous || number > UINT32_MAX)
			return false;
		if (i == 0 && require_native_rates && number >= 300)
			return false;
		previous = (uint32_t)number;
		(void)snprintf(expected, sizeof(expected), "%" PRIu32, previous);
		if (strcmp(wrenGetSlotString(vm, 3), expected) != 0)
			return false;
	}
	return false;
}
#endif

static int
exercise_menu_catalog_contract(st_wren_host_t *public_host)
{
	struct st_wren_host *host = public_host;
	static const struct catalog_row rates[] = {
		{300, "300"}, {600, "600"}, {1200, "1200"},
		{2400, "2400"}, {4800, "4800"}, {9600, "9600"},
		{19200, "19200"}, {38400, "38400"}, {57600, "57600"},
		{76800, "76800"}, {115200, "115200"}, {0, "Current"},
	};
	static const struct catalog_row no_rts[] = {
		{ST_PROFILE_FLOW_XON_XOFF, "XON/XOFF"},
		{ST_PROFILE_FLOW_NONE, "None"},
	};
	static const struct catalog_row output_modes[] = {
		{0, "Automatic"},
		{1, "Curses"}, {2, "Curses on cp437 Device"},
		{3, "Curses using US-ASCII"}, {4, "ANSI"},
		{5, "X11"}, {6, "X11 Fullscreen"},
		{7, "Win32 Console"}, {8, "Win32 Console Fullscreen"},
		{9, "SDL"}, {10, "SDL Fullscreen"},
		{11, "GDI"}, {12, "GDI Fullscreen"},
		{14, "Wayland"}, {15, "Wayland Fullscreen"},
		{16, "Quartz"}, {17, "Quartz Fullscreen"},
	};
	static const uint32_t output_features[] = {
		0,
		ST_CONSOLE_BUILD_NCURSES, ST_CONSOLE_BUILD_NCURSES,
		ST_CONSOLE_BUILD_NCURSES, ST_CONSOLE_BUILD_ANSI,
		ST_CONSOLE_BUILD_X11, ST_CONSOLE_BUILD_X11,
		ST_CONSOLE_BUILD_WIN32, ST_CONSOLE_BUILD_WIN32,
		ST_CONSOLE_BUILD_SDL, ST_CONSOLE_BUILD_SDL,
		ST_CONSOLE_BUILD_GDI, ST_CONSOLE_BUILD_GDI,
		ST_CONSOLE_BUILD_WAYLAND, ST_CONSOLE_BUILD_WAYLAND,
		ST_CONSOLE_BUILD_QUARTZ, ST_CONSOLE_BUILD_QUARTZ,
	};
	struct catalog_row expected_output[
	    sizeof(output_modes) / sizeof(output_modes[0])];
	size_t output_count = 0;
	uint32_t features = st_console_build_features();
	const char *expected_platform = "Unknown";
	static const char source[] =
	    "import \"syncterm\" for Platform\n"
	    "import \"syncterm_menu\" for Menu\n"
	    "var platformResult = Platform.name\n"
	    "var ratesResult = Menu.rates\n"
	    "var missingRatesResult = Menu.serialRates(\"/no/such/syncterm-device\")\n"
	    "var noRtsResult = Menu.flowControlsNoRts\n"
	    "var outputModesResult = Menu.outputModes\n"
	    "if (!(Fiber.new { Menu.serialRates(123) }.try() is String)) Fiber.abort(\"serial type validation\")\n"
	    "if (!(Fiber.new { Menu.serialRates(\"bad\\u0000device\") }.try() is String)) Fiber.abort(\"serial NUL validation\")\n";
	static const char platform_source[] =
	    "import \"syncterm\" for Platform\n"
	    "var platformResult = Platform.name\n";

	if (wrenInterpret(host->vm, "menu_catalog_contract", source)
	    != WREN_RESULT_SUCCESS) {
		fprintf(stderr, "menu catalog contract failed: %s\n",
		    host->last_error);
		return -1;
	}
#if SYNCTERM_PLATFORM_WINDOWS
	expected_platform = "Windows";
#elif SYNCTERM_PLATFORM_POSIX
	struct utsname information;

	if (uname(&information) == 0)
		expected_platform = information.sysname;
#endif
	wrenEnsureSlots(host->vm, 1);
	wrenGetVariable(host->vm, "menu_catalog_contract", "platformResult", 0);
	CHECK(wrenGetSlotType(host->vm, 0) == WREN_TYPE_STRING);
	CHECK(strcmp(wrenGetSlotString(host->vm, 0), expected_platform) == 0);
	CHECK(wrenInterpret(host->picker_vm, "picker_platform_contract",
	    platform_source) == WREN_RESULT_SUCCESS);
	wrenEnsureSlots(host->picker_vm, 1);
	wrenGetVariable(host->picker_vm, "picker_platform_contract",
	    "platformResult", 0);
	CHECK(wrenGetSlotType(host->picker_vm, 0) == WREN_TYPE_STRING);
	CHECK(strcmp(wrenGetSlotString(host->picker_vm, 0), expected_platform)
	    == 0);
	CHECK(catalog_matches(host->vm, "menu_catalog_contract", "ratesResult",
	    rates, sizeof(rates) / sizeof(rates[0])));
	CHECK(catalog_matches(host->vm, "menu_catalog_contract",
	    "missingRatesResult", rates, sizeof(rates) / sizeof(rates[0])));
	CHECK(catalog_matches(host->vm, "menu_catalog_contract", "noRtsResult",
	    no_rts, sizeof(no_rts) / sizeof(no_rts[0])));
	for (size_t i = 0; i < sizeof(output_modes) / sizeof(output_modes[0]);
	    ++i) {
		if (output_features[i] != 0
		    && (features & output_features[i]) == 0)
			continue;
		expected_output[output_count++] = output_modes[i];
	}
	CHECK(catalog_matches(host->vm, "menu_catalog_contract",
	    "outputModesResult", expected_output, output_count));
	return 0;
}

#if ST_WREN_TEST_HAVE_PTY
static int
exercise_serial_catalog_contract(st_wren_host_t *public_host)
{
	struct st_wren_host *host = public_host;
	char source[512];
	int master = posix_openpt(O_RDWR | O_NOCTTY);
	char *name;

	CHECK(master >= 0);
	CHECK(grantpt(master) == 0);
	CHECK(unlockpt(master) == 0);
	name = ptsname(master);
	CHECK(name != NULL);
	CHECK(snprintf(source, sizeof(source),
	    "import \"syncterm_menu\" for Menu\n"
	    "var serialResult = Menu.serialRates(\"%s\")\n", name)
	    < (int)sizeof(source));
	CHECK(wrenInterpret(host->vm, "serial_catalog_contract", source)
	    == WREN_RESULT_SUCCESS);
	CHECK(serial_catalog_valid(host->vm, "serial_catalog_contract",
	    "serialResult", true));
	CHECK(close(master) == 0);
	return 0;
}
#endif

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
	    "if (FillContract.row(fill, 0) != \"    \" || FillContract.row(fill, 2) != \"    \") Fiber.abort(\"initial spaces\")\n"
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
	    "var wideCell = Cell.new()\n"
	    "wideCell.ch = \"\\u4e00\"\n"
	    "var wideFill = Fiber.new { fill.fill(Rect.new(0, 0, 1, 1), wideCell) }\n"
	    "wideFill.try()\n"
	    "if (wideFill.error == null || fill.cellAt(0, 0).ch != \"X\") Fiber.abort(\"wide fill\")\n"
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
exercise_zero_width_contract(st_wren_host_t *public_host)
{
	struct st_wren_host *host = public_host;
	static const char source[] =
	    "import \"syncterm\" for Cell, Screen, Surface\n"
	    "import \"ui_draw\" for Painter\n"
	    "import \"ui_style\" for Style\n"
	    "var z = \"\\u2060\\ufeff\"\n"
	    "if (Screen.textWidth(z, 0) != 0 || Screen.textWidth(\"A\" + z + \"B\", 0) != 2) Fiber.abort(\"zero width measurement\")\n"
	    "var s = Surface.new(4, 1)\n"
	    "for (x in 0...4) s.cellAt(x, 0).ch = \"X\"\n"
	    "var style = Style.new(0, 0x1f, 0xffffff, 0x0000a8)\n"
	    "Painter.text(s, 0, 0, z, style)\n"
	    "Painter.text(s, 0, 0, z, style, 3)\n"
	    "Painter.hline(s, 0, 0, 4, \"\\u2060\", style)\n"
	    "for (x in 0...4) {\n"
	    "  if (s.cellAt(x, 0).ch != \"X\" || s.cellAt(x, 0).legacyAttr == 0x1f) Fiber.abort(\"zero text mutated cells\")\n"
	    "}\n"
	    "Painter.text(s, 0, 0, \"A\" + z + \"B\", style)\n"
	    "if (s.cellAt(0, 0).ch != \"A\" || s.cellAt(1, 0).ch != \"B\" || s.cellAt(2, 0).ch != \"X\") Fiber.abort(\"zero text layout\")\n"
	    "var single = Cell.new()\n"
	    "single.ch = \"\\u2060\"\n"
	    "if (single.columns != 1 || single.ch != \"?\") Fiber.abort(\"standalone diagnostic tofu\")\n"
	    "single.ch = \"\\ufeff\"\n"
	    "if (single.columns != 1 || single.ch != \"\\ufeff\") Fiber.abort(\"narrow diagnostic mapping\")\n"
	    "single.ch = \"\\u2061\"\n"
	    "if (single.columns != 1 || single.ch != \"?\") Fiber.abort(\"missing diagnostic tofu\")\n"
	    "single.ch = \"\\u0000\"\n"
	    "if (single.chByte != 0 || single.columns != 1) Fiber.abort(\"explicit NUL mapping\")\n"
	    "single.chByte = 0\n"
	    "if (single.chByte != 0 || single.columns != 1) Fiber.abort(\"raw glyph zero\")\n"
	    "var cell = s.cellAt(1, 0)\n"
	    "cell.font = 0\n"
	    "cell.legacyAttr = 0x1f\n"
	    "cell.fgRgb = 0x123456\n"
	    "cell.bgRgb = 0x654321\n"
	    "cell.blink = true\n"
	    "cell.hyperlinkId = 1\n"
	    "cell.ch = \"\\u2060\"\n"
	    "if (cell.columns != 2 || cell.ch != \"\\u2060\" || s.cellAt(2, 0).columns != 0) Fiber.abort(\"wide diagnostic pair\")\n"
	    "if (cell.font != 0 || cell.fgRgb != 0x123456 || cell.bgRgb != 0x654321 || !cell.blink || cell.hyperlinkId != 1) Fiber.abort(\"diagnostic attributes\")\n"
	    "cell.ch = \"\\ufeff\"\n"
	    "if (cell.columns != 1 || s.cellAt(2, 0).columns != 1 || s.cellAt(2, 0).ch != \" \") Fiber.abort(\"old diagnostic pair repair\")\n"
	    "s.cellAt(2, 0).ch = \"\\u2060\"\n"
	    "cell.ch = \"\\u2060\"\n"
	    "if (cell.columns != 2 || s.cellAt(2, 0).columns != 0 || s.cellAt(3, 0).columns != 1) Fiber.abort(\"overwritten pair repair\")\n"
	    "s.cellAt(3, 0).ch = \"\\u2060\"\n"
	    "if (s.cellAt(3, 0).columns != 1 || s.cellAt(3, 0).ch != \"?\") Fiber.abort(\"edge diagnostic tofu\")\n"
	    "var rows = Surface.new(2, 2)\n"
	    "rows.cellAt(1, 0).ch = \"\\u2060\"\n"
	    "if (rows.cellAt(1, 0).columns != 1 || rows.cellAt(1, 0).ch != \"?\" || rows.cellAt(0, 1).ch != \" \") Fiber.abort(\"diagnostic crossed row boundary\")\n";

	if (wrenInterpret(host->vm, "zero_width_contract", source) != WREN_RESULT_SUCCESS) {
		fprintf(stderr, "zero width contract failed: %s\n", host->last_error);
		return -1;
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
	    "right.ch = \"e\\u0301\"\n"
	    "if (right.ch != \"\\u00e9\") Fiber.abort(\"Cell.ch NFC\")\n"
	    "right.ch = \"A\"\n"
	    "var trailing = Fiber.new { right.ch = \"AB\" }\n"
	    "trailing.try()\n"
	    "if (trailing.error == null || right.ch != \"A\") Fiber.abort(\"one-codepoint character\")\n"
	    "right.font = 0\n"
	    "right.chByte = 192\n"
	    "if (right.chByte != 192 || right.ch != \"\\u2514\") Fiber.abort(\"CP437 byte\")\n"
	    "right.font = 1\n"
	    "if (right.chByte != 192 || right.ch != \"\\u0410\") Fiber.abort(\"font byte reinterpretation\")\n"
	    "right.font = 0\n"
	    "if (right.chByte != 192 || right.ch != \"\\u2514\") Fiber.abort(\"font byte round trip\")\n"
	    "right.chByte = 1\n"
	    "if (right.chByte != 1 || right.ch != \"\\u263a\") Fiber.abort(\"graphic control byte\")\n"
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
	    "if (durationType.error == null) Fiber.abort(\"duration type\")\n"
	    "var saved = Screen.save()\n"
	    "Screen.window.bounds = [3, 4, 6, 7]\n"
	    "Screen.window.position = [2, 3]\n"
	    "if (Screen.window.position[0] != 2 || Screen.window.position[1] != 3) Fiber.abort(\"relative window position\")\n"
	    "Screen.window.putChar(88)\n"
	    "if (Screen.readRect(4, 6, 4, 6).cellAt(0, 0).ch != \"X\") Fiber.abort(\"relative window output\")\n"
	    "var badPosition = Fiber.new { Screen.window.position = [5, 1] }\n"
	    "badPosition.try()\n"
	    "if (badPosition.error == null || Screen.window.position[0] != 3 || Screen.window.position[1] != 3) Fiber.abort(\"invalid window position\")\n"
	    "var badBounds = Fiber.new { Screen.window.bounds = [3, 4, 2, 7] }\n"
	    "badBounds.try()\n"
	    "if (badBounds.error == null || Screen.window.bounds[0] != 3 || Screen.window.bounds[2] != 6) Fiber.abort(\"invalid window bounds\")\n"
	    "var badCharacter = Fiber.new { Screen.window.putChar(1114112) }\n"
	    "badCharacter.try()\n"
	    "if (badCharacter.error == null || Screen.window.position[0] != 3 || Screen.window.position[1] != 3) Fiber.abort(\"invalid window character\")\n"
	    "Screen.restore(saved)\n";

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
	    "Screen.window.position = [2, 2]\n"
	    "Screen.window.clearToLineEnd()\n"
	    "if (Screen.readRect(2, 3, 2, 3).cellAt(0, 0).font != 5) Fiber.abort(\"clear-line font selection\")\n"
	    "VideoFlags.blinkAltChars = true\n"
	    "Screen.font[3] = 0\n"
	    "Screen.attr = 0x80\n"
	    "Screen.font[3] = 6\n"
	    "Screen.window.position = [1, 3]\n"
	    "Screen.window.insertLine()\n"
	    "if (Screen.readRect(1, 4, 1, 4).cellAt(0, 0).font != 6) Fiber.abort(\"insert-line font selection\")\n"
	    "Screen.font[4] = 0\n"
	    "Screen.attr = 0x88\n"
	    "Screen.font[4] = 7\n"
	    "Screen.window.position = [1, 3]\n"
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
	    "if (!Codepage.encodes_(\"A\") || Codepage.encodes_(\"🙂\") || Codepage.encodes_(\"AB\")) Fiber.abort(\"codepage encoding\")\n"
	    "if (Font.name(0) != \"Codepage 437 English\" || Font.name(43) != \"Prestel\" || Font.name(200) != \"\") Fiber.abort(\"font names\")\n"
	    "if (Font.count != 47 || !Font.available(0) || Font.available(45)) Fiber.abort(\"font catalog\")\n"
	    "Screen.font[0] = 1\n"
	    "if (!Codepage.encodes_(\"\\u0410\") || Codepage.encodes_(\"\\u2514\")) Fiber.abort(\"selected codepage encoding\")\n"
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
exercise_default_port_contract(st_wren_host_t *public_host)
{
	struct st_wren_host *host = public_host;
	static const char source[] =
	    "import \"syncterm_menu\" for Menu\n"
	    "var expected = [0, 513, 513, 23, 0, 22, 22, 0, 0, 0, 0, 65535, 992, 8883]\n"
	    "for (i in 0...expected.count) {\n"
	    "  if (Menu.defaultPort(i) != expected[i]) Fiber.abort(\"default port\")\n"
	    "}\n";

	if (wrenInterpret(host->vm, "default_port_contract", source)
	    != WREN_RESULT_SUCCESS) {
		fprintf(stderr, "default-port contract failed: %s\n",
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
	CHECK(st_wren_host_select_url(host, "sshna://example.com",
	    ST_CONNECTION_RLOGIN, &profile) == 0);
	CHECK(profile.connection_type == ST_CONNECTION_SSH_NO_AUTH && profile.port == 22);
	CHECK(st_wren_host_select_url(host, "telnets://example.com",
	    ST_CONNECTION_RLOGIN, &profile) == 0);
	CHECK(profile.connection_type == ST_CONNECTION_TELNET_TLS && profile.port == 992);
	CHECK(st_wren_host_select_url(host, "raw://example.com",
	    ST_CONNECTION_RLOGIN, &profile) == 0);
	CHECK(profile.connection_type == ST_CONNECTION_RAW);
	CHECK(profile.port == 0);
	CHECK(st_wren_host_select_url(host, "ghost://example.com/program",
	    ST_CONNECTION_RLOGIN, &profile) == 0);
	CHECK(profile.connection_type == ST_CONNECTION_MBBS_GHOST);
	CHECK(profile.port == UINT16_MAX);
	CHECK(strcmp(profile.ghost_program, "program") == 0);
	CHECK(st_wren_host_select_url(host, "mqtts://example.com",
	    ST_CONNECTION_RLOGIN, &profile) == 0);
	CHECK(profile.connection_type == ST_CONNECTION_MQTT);
	CHECK(profile.port == 8883);
	CHECK(st_wren_host_select_url(host, "shell:echo hello",
	    ST_CONNECTION_RLOGIN, &profile) == 0);
	CHECK(profile.connection_type == ST_CONNECTION_SHELL);
	CHECK(profile.port == 0);
	CHECK(strcmp(profile.address, "echo hello") == 0);
	CHECK(st_wren_host_select_url(host, "raw://example.com:4242",
	    ST_CONNECTION_RLOGIN, &profile) == 0);
	CHECK(profile.connection_type == ST_CONNECTION_RAW);
	CHECK(profile.port == 4242);
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

struct prompt_poster {
	st_console_t *console;
	const char *text;
	uint32_t codepoint;
	uint32_t finish;
	bool close;
};

static int
post_prompt_input(void *argument)
{
	struct prompt_poster *poster = argument;
	const struct timespec delay = {0, 100000000};
	const struct st_console_event enter = {
		.type = ST_CONSOLE_EVENT_TEXT,
		.data.text.codepoint = poster->finish,
	};
	const struct st_console_event unicode = {
		.type = ST_CONSOLE_EVENT_TEXT,
		.data.text.codepoint = poster->codepoint,
	};
	const struct st_console_event close = {
		.type = ST_CONSOLE_EVENT_CLOSE,
	};

	if (st_thread_sleep(&delay, NULL) != ST_THREAD_SUCCESS)
		return 1;
	if (poster->close)
		return st_console_post_event(poster->console, &close) == 0
		    ? 0 : 1;
	if (queue_text(poster->console, poster->text) != 0
	    || (poster->codepoint != 0
	    && st_console_post_event(poster->console, &unicode) != 0)
	    || st_console_post_event(poster->console, &enter) != 0)
		return 1;
	return 0;
}

static int
wait_for_screen_text(st_console_t *console, const char *needle)
{
	const struct timespec pause = {0, 20000000};
	for (unsigned attempt = 0; attempt < 500; ++attempt) {
		uint32_t width, height;
		CHECK(st_console_size(console, &width, &height) == 0);
		struct st_console_cell *cells = calloc(width, sizeof(*cells));
		char *line = malloc((size_t)width + 1);
		CHECK(cells != NULL && line != NULL);
		bool found = false;
		for (uint32_t y = 0; y < height && !found; ++y) {
			const struct st_console_rect row = {0, y, width, 1};
			CHECK(st_console_read_cells(console, &row, cells, width) == 0);
			for (uint32_t x = 0; x < width; ++x) {
				char text[8];
				size_t required;
				line[x] = st_console_font_glyph_text(console, cells[x].glyph_font,
				    cells[x].glyph, text, sizeof(text), &required) == 0 && required == 1
				    ? text[0] : '?';
			}
			line[width] = '\0';
			found = strstr(line, needle) != NULL;
		}
		free(cells); free(line);
		if (found) return 0;
		(void)st_thread_sleep(&pause, NULL);
	}
	/* Never leave the owner blocked when a rendering assertion fails. */
	const struct st_console_event close = {.type = ST_CONSOLE_EVENT_CLOSE};
	(void)st_console_post_event(console, &close);
	return 1;
}

static int
post_disconnect_input(void *argument)
{
	st_console_t *console = argument;
	const struct st_console_event f1 = {
		.type = ST_CONSOLE_EVENT_KEY_CODE, .data.key.code = 0x3b00,
	};
	CHECK(wait_for_screen_text(console, "Disconnected: Enter exits, F1 details") == 0);
	CHECK(st_console_post_event(console, &f1) == 0);
	CHECK(wait_for_screen_text(console, "fixture teardown diagnostics") == 0);
	CHECK(wait_for_screen_text(console, "literal `key` **value** \\path") == 0);
	CHECK(queue_text(console, "\x1b") == 0);
	CHECK(wait_for_screen_text(console, "Disconnected: Enter exits, F1 details") == 0);
	CHECK(queue_text(console, "\r") == 0);
	return 0;
}

static int
exercise_prompt_close_during(struct st_wren_host *host,
	st_console_t *console)
{
	struct prompt_poster poster = {.console = console, .close = true};
	struct st_console_event forwarded;
	st_thread_t thread;
	char *answer = NULL;
	int thread_result;

	CHECK(st_thread_create(&thread, post_prompt_input, &poster)
	    == ST_THREAD_SUCCESS);
	CHECK(st_wren_host_prompt(host, "RIP Input", "Close?", "",
	    20, &answer) == 0);
	CHECK(st_thread_join(thread, &thread_result) == ST_THREAD_SUCCESS);
	CHECK(thread_result == 0 && answer == NULL);
	CHECK(st_wren_host_exit_requested(host));
	CHECK(st_console_event_try(console, &forwarded) == 0);
	CHECK(forwarded.type == ST_CONSOLE_EVENT_CLOSE);
	host->exit_requested = false;
	CHECK(st_thread_create(&thread, post_prompt_input, &poster) == ST_THREAD_SUCCESS);
	CHECK(st_wren_host_disconnected(host, "Connection closed") == 0);
	CHECK(st_thread_join(thread, &thread_result) == ST_THREAD_SUCCESS && thread_result == 0);
	CHECK(st_wren_host_exit_requested(host));
	CHECK(st_console_event_try(console, &forwarded) == 0
	    && forwarded.type == ST_CONSOLE_EVENT_CLOSE);
	return 0;
}

static int
exercise_menu_prompt(struct st_wren_host *host, st_console_t *console)
{
	struct prompt_poster poster = {
		.console = console, .text = "fresh", .finish = '\r'
	};
	const struct st_console_style style = {
		.foreground = ST_CONSOLE_COLOR_RGB_VALUE(0x12, 0x34, 0x56),
		.background = ST_CONSOLE_COLOR_RGB_VALUE(0x65, 0x43, 0x21),
		.font = 0,
	};
	struct st_console_style restored;
	st_thread_t thread;
	char *answer = NULL;
	uint32_t x, y;
	bool cursor_visible;
	enum st_console_cursor_shape cursor_shape;
	int thread_result;

	CHECK(st_console_set_style(console, &style) == 0);
	CHECK(st_console_set_position(console, 7, 8) == 0);
	CHECK(st_console_set_cursor(console, false,
	    ST_CONSOLE_CURSOR_BLOCK) == 0);
	CHECK(queue_text(console, "stale\r") == 0);
	CHECK(st_thread_create(&thread, post_prompt_input, &poster)
	    == ST_THREAD_SUCCESS);
	CHECK(st_wren_host_prompt(host, "RIP Input", "Your name?", "seed",
	    20, &answer) == 0);
	CHECK(st_thread_join(thread, &thread_result) == ST_THREAD_SUCCESS);
	CHECK(thread_result == 0);
	CHECK(answer != NULL && strcmp(answer, "fresh") == 0);
	CHECK(st_console_get_style(console, &restored) == 0);
	CHECK(restored.foreground == style.foreground
	    && restored.background == style.background
	    && restored.font == style.font);
	CHECK(st_console_get_position(console, &x, &y) == 0
	    && x == 7 && y == 8);
	CHECK(st_console_get_cursor(console, &cursor_visible,
	    &cursor_shape) == 0);
	CHECK(!cursor_visible && cursor_shape == ST_CONSOLE_CURSOR_BLOCK);
	free(answer);
	answer = NULL;
	poster.text = "";
	poster.finish = 27;
	CHECK(st_thread_create(&thread, post_prompt_input, &poster)
	    == ST_THREAD_SUCCESS);
	CHECK(st_wren_host_prompt(host, "RIP Input", "Cancel?", "seed",
	    20, &answer) == 0);
	CHECK(st_thread_join(thread, &thread_result) == ST_THREAD_SUCCESS);
	CHECK(thread_result == 0 && answer == NULL);
	poster.codepoint = 0xe9;
	poster.finish = '\r';
	CHECK(st_thread_create(&thread, post_prompt_input, &poster)
	    == ST_THREAD_SUCCESS);
	CHECK(st_wren_host_prompt(host, "RIP Input", "Unicode?", "A",
	    1, &answer) == 0);
	CHECK(st_thread_join(thread, &thread_result) == ST_THREAD_SUCCESS);
	CHECK(thread_result == 0);
	CHECK(answer != NULL && strcmp(answer, "\xc3\xa9") == 0);
	free(answer);
	answer = NULL;
	poster.text = "secret";
	poster.codepoint = 0;
	CHECK(st_thread_create(&thread, post_prompt_input, &poster) == ST_THREAD_SUCCESS);
	CHECK(st_wren_host_prompt_masked(host, "SSH authentication", "Password",
	    "", 128, true, &answer) == 0);
	CHECK(st_thread_join(thread, &thread_result) == ST_THREAD_SUCCESS && thread_result == 0);
	CHECK(answer != NULL && strcmp(answer, "secret") == 0);
	free(answer);
	answer = NULL;
	{
		const char *choices[] = {"Disconnect", "Update", "Ignore"};
		poster.text = "";
		CHECK(st_thread_create(&thread, post_prompt_input, &poster) == ST_THREAD_SUCCESS);
		CHECK(st_wren_host_choice(host, "Fingerprint Changed", "Select", choices, 3) == 0);
		CHECK(st_thread_join(thread, &thread_result) == ST_THREAD_SUCCESS && thread_result == 0);
		poster.finish = 27;
		CHECK(st_thread_create(&thread, post_prompt_input, &poster) == ST_THREAD_SUCCESS);
		CHECK(st_wren_host_choice(host, "Fingerprint Changed", "Select", choices, 3)
		    == IO_ERROR_CANCELLED);
		CHECK(st_thread_join(thread, &thread_result) == ST_THREAD_SUCCESS && thread_result == 0);
	}
	poster.finish = 13;
	CHECK(st_thread_create(&thread, post_prompt_input, &poster) == ST_THREAD_SUCCESS);
	CHECK(st_wren_host_alert(host, "SSH Banner", "Fixture banner") == 0);
	CHECK(st_thread_join(thread, &thread_result) == ST_THREAD_SUCCESS && thread_result == 0);
	poster.finish = 27;
	CHECK(st_thread_create(&thread, post_prompt_input, &poster) == ST_THREAD_SUCCESS);
	CHECK(st_wren_host_help(host, "TLS Certificate Error", "# Protocol Log\n\nFixture TLS error") == 0);
	CHECK(st_thread_join(thread, &thread_result) == ST_THREAD_SUCCESS && thread_result == 0);
	CHECK(st_wren_host_status(host, "Connecting", "Negotiating TLS") == 0);
	CHECK(st_wren_host_status(host, NULL, NULL) == 0);
	CHECK(queue_text(console, "stale\r") == 0);
	CHECK(st_thread_create(&thread, post_disconnect_input, console) == ST_THREAD_SUCCESS);
	CHECK(st_wren_host_disconnected(host,
	    "# Connection Details\n\nRemote host dropped connection  \n"
	    "Connected: 00:00:12\n\n# Protocol Session Log\n\n"
	    "- fixture teardown diagnostics\n"
	    "- literal \\`key\\` \\*\\*value\\*\\* \\\\path\n") == 0);
	CHECK(st_thread_join(thread, &thread_result) == ST_THREAD_SUCCESS && thread_result == 0);
	CHECK(st_console_get_position(console, &x, &y) == 0 && x == 7 && y == 8);
	CHECK(st_console_get_cursor(console, &cursor_visible, &cursor_shape) == 0);
	CHECK(!cursor_visible && cursor_shape == ST_CONSOLE_CURSOR_BLOCK);
	{
		const struct st_console_event close = {
			.type = ST_CONSOLE_EVENT_CLOSE,
		};
		struct st_console_event forwarded;

		CHECK(st_console_post_event(console, &close) == 0);
		CHECK(st_wren_host_prompt(host, "RIP Input", "Closed?", "",
		    20, &answer) == 0);
		CHECK(answer == NULL);
		CHECK(st_console_event_try(console, &forwarded) == 0);
		CHECK(forwarded.type == ST_CONSOLE_EVENT_CLOSE);
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
	struct st_console_rect probe_area;
	struct st_console_event release;
	struct st_console_cell highlighted[6];
	bool probe_selected;
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
		struct st_console_cell probe;

		if (st_console_read_cells(observer->console, &observer->area,
		    observer->highlighted, observer->area.width) != 0)
			break;
		if (st_console_read_cells(observer->console,
		    &observer->probe_area, &probe, 1) != 0)
			break;
		for (size_t index = 0; index < 6; ++index) {
			if (observer->highlighted[index].background
			    != expected_backgrounds[index]) {
				highlighted = false;
				break;
			}
		}
		if (highlighted && (probe.background == 4)
		    == observer->probe_selected) {
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
	static const struct {
		bool rectangular;
		uint32_t press_modifiers;
		uint32_t move_modifiers;
		bool probe_selected;
	} cases[] = {
		{true, 0, 0, false},
		{true, ST_CONSOLE_MODIFIER_ALT, 0, true},
		{false, 0, 0, true},
		{false, ST_CONSOLE_MODIFIER_ALT, 0, false},
		{true, 0, ST_CONSOLE_MODIFIER_ALT, false},
	};
	const uint32_t selection_blue = UINT32_C(0x0000a8);
	const struct st_console_rect area = {1, 1, 3, 2};
	const struct st_console_rect probe_area = {4, 1, 1, 1};
	const struct st_console_cell probe_cell = {
		.glyph = 'G', .foreground = 7, .background = 1,
	};
	struct st_console_cell cells[6] = {
		{.glyph = 'A',
		    .foreground = ST_CONSOLE_COLOR_RGB_VALUE(255, 255, 255),
		    .background = 500},
		{.glyph = 127,
		    .foreground = ST_CONSOLE_COLOR_RGB_VALUE(0, 0, 168)},
		{.glyph = ' ', .foreground = 7, .background = 1},
		{.glyph = 'B', .foreground = 7},
		{.glyph = 'C', .foreground = 0, .background = 7,
		    .flags = ST_CONSOLE_CELL_REVERSE},
		{.glyph = 'D', .foreground = 1},
	};
	struct st_console_cell before[6];
	struct st_console_cell after[6];
	struct selection_observer observer;
	st_thread_t observer_thread;
	int observer_result;
	WrenInterpretResult interpret_result;
	struct st_console_event press = {
		.type = ST_CONSOLE_EVENT_MOUSE,
		.data.mouse = {
			.x = 1, .y = 1,
			.pixel_x = 8, .pixel_y = 16,
			.buttons = ST_CONSOLE_MOUSE_BUTTON_LEFT,
			.action = ST_CONSOLE_MOUSE_BUTTON,
		},
	};
	struct st_console_event move = {
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
	static const char source_prefix[] =
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
	    "Input.mousedrag(";
	char source[sizeof(source_prefix) + sizeof("true)\n")];
	char module[40];

	CHECK(st_console_set_palette(console, 500, &selection_blue, 1) == 0);
	CHECK(st_console_write_cells(console, &area, cells, 3) == 0);
	CHECK(st_console_write_cells(console, &probe_area, &probe_cell, 1) == 0);
	CHECK(st_console_present(console) == 0);
	CHECK(st_console_read_cells(console, &area, before, 3) == 0);
	for (size_t case_index = 0; case_index < sizeof(cases)
	    / sizeof(cases[0]); ++case_index) {
		press.data.mouse.modifiers = cases[case_index].press_modifiers;
		move.data.mouse.modifiers = cases[case_index].move_modifiers;
		CHECK(snprintf(source, sizeof(source), "%s%s)\n", source_prefix,
		    cases[case_index].rectangular ? "true" : "") > 0);
		CHECK(snprintf(module, sizeof(module), "mouse_drag_selection_%zu",
		    case_index) > 0);
		CHECK(st_console_post_event(console, &press) == 0);
		CHECK(st_console_post_event(console, &move) == 0);
		observer = (struct selection_observer){
			.console = console,
			.area = area,
			.probe_area = probe_area,
			.release = release,
			.probe_selected = cases[case_index].probe_selected,
		};
		CHECK(st_thread_create(&observer_thread, observe_selection,
		    &observer) == ST_THREAD_SUCCESS);
		interpret_result = wrenInterpret(host->vm, module, source);
		CHECK(st_thread_join(observer_thread, &observer_result)
		    == ST_THREAD_SUCCESS);
		CHECK(interpret_result == WREN_RESULT_SUCCESS);
		CHECK(observer_result == 0);
		CHECK(observer.highlighted[0].foreground
		    == ST_CONSOLE_COLOR_RGB_VALUE(255, 255, 255));
		CHECK(observer.highlighted[1].foreground == 12);
		CHECK(observer.highlighted[4].foreground == 7);
		CHECK((observer.highlighted[4].flags
		    & ST_CONSOLE_CELL_REVERSE) == 0);
		CHECK(observer.highlighted[5].foreground == 1);
		CHECK(st_console_read_cells(console, &area, after, 3) == 0);
		for (size_t index = 0; index < 6; ++index) {
			CHECK(before[index].glyph == after[index].glyph);
			CHECK(before[index].glyph_font == after[index].glyph_font);
			CHECK(before[index].foreground == after[index].foreground);
			CHECK(before[index].background == after[index].background);
			CHECK((before[index].flags & ST_CONSOLE_CELL_PUBLIC_FLAGS)
			    == (after[index].flags & ST_CONSOLE_CELL_PUBLIC_FLAGS));
			CHECK(before[index].font == after[index].font);
			CHECK(before[index].hyperlink == after[index].hyperlink);
		}
		CHECK(!host->menu.mouse_gestures[0].pressed);
		CHECK(!host->menu.mouse_gestures[0].dragging);
	}
	return 0;
}

static int
exercise_input_value_contract(struct st_wren_host *host,
	st_console_t *console)
{
	static const char values[] =
	    "import \"syncterm\" for Input, KeyEvent, Mouse, MouseEvent, PhysicalKeyEvent\n"
	    "var key = KeyEvent.new(4660)\n"
	    "if (key.codepoint != null || key.text != \"\") Fiber.abort(\"extended key text\")\n"
	    "Input.unget(key)\n"
	    "var gotKey = Input.poll()\n"
	    "if (!(gotKey is KeyEvent) || gotKey.code != 4660 || gotKey.codepoint != null || gotKey.text != \"\") Fiber.abort(\"key unget\")\n"
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
	static const char codepage_source[] =
	    "import \"syncterm\" for Codepage, Font, Input, Key, KeyEvent, Screen\n"
	    "var original = Screen.font[0]\n"
	    "Screen.font[0] = 20\n"
	    "if (Font.codepage != Codepage.cp1251) Fiber.abort(\"selected codepage\")\n"
	    "var byte = KeyEvent.new(0xc0)\n"
	    "if (byte.code != 0xc0 || byte.codepoint != 0x0410 || byte.text != String.fromCodePoint(0x0410)) Fiber.abort(\"constructor codepage\")\n"
	    "var escape = KeyEvent.new(0x01e0)\n"
	    "if (escape.code != Key.escape || escape.codepoint != Key.escape) Fiber.abort(\"escape normalization\")\n"
	    "var encodable = Input.poll()\n"
	    "if (!(encodable is KeyEvent) || encodable.code != 0xc0 || encodable.codepoint != 0x0410 || encodable.text != String.fromCodePoint(0x0410)) Fiber.abort(\"encodable text\")\n"
	    "Input.unget(encodable)\n"
	    "var returned = Input.poll()\n"
	    "if (returned.code != 0xc0 || returned.codepoint != 0x0410 || returned.text != String.fromCodePoint(0x0410)) Fiber.abort(\"unicode unget\")\n"
	    "var unencodable = Input.poll()\n"
	    "if (unencodable.code != 0x3f || unencodable.codepoint != 0x4e00 || unencodable.text != String.fromCodePoint(0x4e00)) Fiber.abort(\"unencodable text\")\n"
	    "var compatibility = Input.poll()\n"
	    "if (compatibility.code != 0xc0 || compatibility.codepoint != 0x0410 || compatibility.text != String.fromCodePoint(0x0410)) Fiber.abort(\"compatibility codepage\")\n"
	    "Screen.font[0] = original\n";
	const struct st_console_event encodable = {
		.type = ST_CONSOLE_EVENT_TEXT,
		.data.text.codepoint = UINT32_C(0x0410),
	};
	const struct st_console_event unencodable = {
		.type = ST_CONSOLE_EVENT_TEXT,
		.data.text.codepoint = UINT32_C(0x4e00),
	};
	const struct st_console_event compatibility = {
		.type = ST_CONSOLE_EVENT_KEY_CODE,
		.data.key.code = UINT16_C(0x00c0),
	};
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
	CHECK(st_console_post_event(console, &encodable) == 0);
	CHECK(st_console_post_event(console, &unencodable) == 0);
	CHECK(st_console_post_event(console, &compatibility) == 0);
	CHECK(wrenInterpret(host->vm, "input_codepage_contract", codepage_source)
	    == WREN_RESULT_SUCCESS);
	CHECK(st_console_post_event(console, &press) == 0);
	CHECK(st_console_post_event(console, &move) == 0);
	CHECK(st_console_post_event(console, &release) == 0);
	CHECK(st_console_post_event(console, &wheel) == 0);
	CHECK(wrenInterpret(host->vm, "mouse_input_contract", mouse_source)
	    == WREN_RESULT_SUCCESS);
	const struct {
		uint16_t key;
		uint32_t modifiers;
		uint16_t expected;
	} modified[] = {
		{13, ST_CONSOLE_MODIFIER_CONTROL, 10},
		{8, ST_CONSOLE_MODIFIER_CONTROL, 127},
		{ST_CONSOLE_KEY_HOME, ST_CONSOLE_MODIFIER_SHIFT, 0x3700},
		{ST_CONSOLE_KEY_HOME, ST_CONSOLE_MODIFIER_CONTROL, 0x7700},
		{ST_CONSOLE_KEY_PAGE_UP, ST_CONSOLE_MODIFIER_SHIFT, 0x3900},
		{ST_CONSOLE_KEY_PAGE_UP, ST_CONSOLE_MODIFIER_CONTROL, 0x8400},
		{ST_CONSOLE_KEY_PAGE_DOWN, ST_CONSOLE_MODIFIER_SHIFT, 0x3300},
		{ST_CONSOLE_KEY_PAGE_DOWN, ST_CONSOLE_MODIFIER_CONTROL, 0x7600},
	};
	for (size_t i = 0; i < sizeof(modified) / sizeof(modified[0]); ++i) {
		const struct st_console_event event = {
			.type = ST_CONSOLE_EVENT_KEY_CODE,
			.data.key = {.code = modified[i].key, .modifiers = modified[i].modifiers},
		};
		CHECK(st_wren_input_key_code(&event) == modified[i].expected);
		CHECK(st_console_post_event(console, &event) == 0);
	}
	CHECK(wrenInterpret(host->vm, "modified_input_contract",
	    "import \"syncterm\" for Input\n"
	    "for (expected in [10, 127, 14080, 30464, 14592, 33792, 13056, 30208]) {\n"
	    "  if (Input.poll().code != expected) Fiber.abort(\"modified key mapping\")\n"
	    "}\n") == WREN_RESULT_SUCCESS);
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
	    "if (Input.next(100) != null) Fiber.abort(\"unexpected input\")\n"
	    "if (Input.nextForWidget_(false, 0) != null) Fiber.abort(\"widget timeout\")\n")
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
	    "var first = Input.nextForWidget_(false, 100)\n"
	    "if (first == null || first.code != Key.quit) Fiber.abort(\"initial quit\")\n"
	    "if (Input.nextForWidget_(true, 0) != null) Fiber.abort(\"at-exit timeout\")\n"
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
	{
		struct st_profile profile;
		uint8_t fingerprint[32] = {1};
		uint64_t generation = st_bbs_list_generation(host->bbs_list);
		st_profile_init(&profile);
		strcpy(profile.name, "Alpha");
		CHECK(st_wren_host_remember_ssh_key(host, &profile, fingerprint, true) == 0);
		CHECK(st_bbs_list_generation(host->bbs_list) == generation);
		st_profile_dispose(&profile);
	}
	CHECK(file_equals(personal, personal_contents));
	CHECK(file_equals(settings, settings_contents));
	CHECK(fs_get_info(&info, cache_sentinel) == 0
	    && info.kind == FS_ENTRY_FILE);
	return 0;
}

#ifdef ST_WREN_HAVE_LIVE_SFTP
int st_wren_test_live_sftp(st_wren_host_t *host, st_console_t *console,
	const char *directory, const char *fixture);
#endif
int
main(int argc, char **argv)
{
	bool menu_only = argc == 2 && strcmp(argv[1], "--menu-only") == 0;
	static const uint8_t default_bitmap[128 * 16] = {0};
	static const uint8_t default_wide_bitmap[96] = {0};
	static const struct st_console_font_mapping default_mapping[] = {
		{0, 0},
		{0x20, 0x20}, {0x21, 0x21}, {0x22, 0x22}, {0x23, 0x23},
		{0x24, 0x24}, {0x25, 0x25}, {0x26, 0x26}, {0x27, 0x27},
		{0x28, 0x28}, {0x29, 0x29}, {0x2a, 0x2a}, {0x2b, 0x2b},
		{0x2c, 0x2c}, {0x2d, 0x2d}, {0x2e, 0x2e}, {0x2f, 0x2f},
		{0x30, 0x30}, {0x31, 0x31}, {0x32, 0x32}, {0x33, 0x33},
		{0x34, 0x34}, {0x35, 0x35}, {0x36, 0x36}, {0x37, 0x37},
		{0x38, 0x38}, {0x39, 0x39}, {0x3a, 0x3a}, {0x3b, 0x3b},
		{0x3c, 0x3c}, {0x3d, 0x3d}, {0x3e, 0x3e}, {0x3f, 0x3f},
		{0x40, 0x40}, {0x41, 0x41}, {0x42, 0x42}, {0x43, 0x43},
		{0x44, 0x44}, {0x45, 0x45}, {0x46, 0x46}, {0x47, 0x47},
		{0x48, 0x48}, {0x49, 0x49}, {0x4a, 0x4a}, {0x4b, 0x4b},
		{0x4c, 0x4c}, {0x4d, 0x4d}, {0x4e, 0x4e}, {0x4f, 0x4f},
		{0x50, 0x50}, {0x51, 0x51}, {0x52, 0x52}, {0x53, 0x53},
		{0x54, 0x54}, {0x55, 0x55}, {0x56, 0x56}, {0x57, 0x57},
		{0x58, 0x58}, {0x59, 0x59}, {0x5a, 0x5a}, {0x5b, 0x5b},
		{0x5c, 0x5c}, {0x5d, 0x5d}, {0x5e, 0x5e}, {0x5f, 0x5f},
		{0x60, 0x60}, {0x61, 0x61}, {0x62, 0x62}, {0x63, 0x63},
		{0x64, 0x64}, {0x65, 0x65}, {0x66, 0x66}, {0x67, 0x67},
		{0x68, 0x68}, {0x69, 0x69}, {0x6a, 0x6a}, {0x6b, 0x6b},
		{0x6c, 0x6c}, {0x6d, 0x6d}, {0x6e, 0x6e}, {0x6f, 0x6f},
		{0x70, 0x70}, {0x71, 0x71}, {0x72, 0x72}, {0x73, 0x73},
		{0x74, 0x74}, {0x75, 0x75}, {0x76, 0x76}, {0x77, 0x77},
		{0x78, 0x78}, {0x79, 0x79}, {0x7a, 0x7a}, {0x7b, 0x7b},
		{0x7c, 0x7c}, {0x7d, 0x7d}, {0x7e, 0x7e},
		{UINT32_C(0x00e9), 31},
		{UINT32_C(0x03a9), 127},
		{UINT32_C(0x2060), 129},
		{UINT32_C(0x4e00), 128},
		{UINT32_C(0x4e16), 130},
		{UINT32_C(0xfeff), 30},
	};
	static const struct st_console_font default_font = {
		.bitmap = default_bitmap,
		.bitmap_size = sizeof(default_bitmap),
		.width = 8,
		.height = 16,
		.glyph_count = 128,
		.row_stride = 1,
		.glyph_stride = 16,
		.wide_bitmap = default_wide_bitmap,
		.wide_bitmap_size = sizeof(default_wide_bitmap),
		.wide_glyph_count = 3,
		.wide_row_stride = 2,
		.wide_glyph_stride = 32,
		.mapping = default_mapping,
		.mapping_count = sizeof(default_mapping) / sizeof(default_mapping[0]),
		.fallback_glyph = 63,
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
	st_hyperlink_registry_t *hyperlinks = NULL;
	st_wren_host_t *host = NULL;
	bool has_selection = false;
	bool font_registered = false;
	bool hyperlink_present = false;
	uint16_t default_identifier;
	uint16_t configured_identifier;
	uint16_t history_hyperlink;

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
		{.glyph = 'A'}, {.glyph = 'B'},
		{.glyph = 'C'}, {.glyph = 'D'},
	};
	CHECK(st_session_history_create(&session_history, 4) == 0);
	CHECK(st_hyperlink_registry_create(&hyperlinks) == 0);
	CHECK(st_hyperlink_registry_add(hyperlinks, "https://history.invalid/", 24,
	    "history", 7, &history_hyperlink) == 0);
	CHECK(history_hyperlink == 1);
	history_cells[0].hyperlink = history_hyperlink;
	CHECK(st_audio_runtime_create(&audio_runtime, UINT32_MAX) == 0);
	CHECK(st_session_history_publish(session_history, history_cells, 2, 2,
	    ST_SCREEN_MODE_CURRENT, true) == 0);
	host_config = (struct st_wren_host_config){
		.console = console,
		.paths = &paths,
		.application_title = "SyncTERM test",
		.session_history = session_history,
		.audio_runtime = audio_runtime,
		.hyperlinks = hyperlinks,
	};
	CHECK(st_wren_host_create(&host, &host_config) == 0);
	CHECK(exercise_ssh_key_persistence(host) == 0);
	CHECK(exercise_menu_prompt(host, console) == 0);
	CHECK(wrenInterpret(((struct st_wren_host *)host)->vm,
	    "hyperlink_contract",
	    "import \"syncterm\" for Cell, Hyperlinks\n"
	    "import \"syncterm_menu\" for Menu\n"
	    "if (!Hyperlinks.containsKey(1) || "
	    "Hyperlinks[1] != \"https://history.invalid/\" || "
	    "Hyperlinks.params(1) != \"history\") "
	    "Fiber.abort(\"registry lookup\")\n"
	    "var tagged = Hyperlinks.add(\"https://tagged.invalid/\", \"tag\")\n"
	    "if (tagged != Hyperlinks.add(\"https://tagged.invalid/\", \"tag\")) "
	    "Fiber.abort(\"explicit deduplication\")\n"
	    "if (Hyperlinks.add(\"https://fresh.invalid/\", null) == "
	    "Hyperlinks.add(\"https://fresh.invalid/\", null)) "
	    "Fiber.abort(\"implicit identity reused\")\n"
	    "var cell = Cell.new()\n"
	    "var scrollback = Menu.offlineScrollback\n"
	    "if (!scrollback.cellAt(0, 0, cell) || cell.hyperlinkId != 1) "
	    "Fiber.abort(\"scrollback hyperlink\")\n"
	    "cell.hyperlinkId = tagged\n"
	    "if (Menu.openHyperlink(Hyperlinks.add(\"mailto:x@y\", null))) "
	    "Fiber.abort(\"disallowed scheme opened\")\n")
	    == WREN_RESULT_SUCCESS);
	CHECK(st_hyperlink_registry_collect(hyperlinks) == 0);
	CHECK(st_hyperlink_registry_contains(hyperlinks, 2, &hyperlink_present) == 0
	    && hyperlink_present);
	CHECK(st_hyperlink_registry_contains(hyperlinks, 3, &hyperlink_present) == 0
	    && !hyperlink_present);
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
	CHECK(exercise_zero_width_contract(host) == 0);
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
	CHECK(exercise_menu_catalog_contract(host) == 0);
#if ST_WREN_TEST_HAVE_PTY
	CHECK(exercise_serial_catalog_contract(host) == 0);
#endif
	CHECK(exercise_configuration_collections(host) == 0);
	CHECK(exercise_default_port_contract(host) == 0);
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
	    "  observed = text.contains(\"pwd\")\n"
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
	if (!menu_only) {
		CHECK(exercise_connected_profile_scripts(host, console, &selected,
		    scripts) == 0);
		CHECK(exercise_connected_without_sftp(host, console, &selected) == 0);
		CHECK(exercise_connected_session_log(host, console, &selected) == 0);
		CHECK(exercise_connected_link_hover(host, console, &selected) == 0);
#if !SYNCTERM_PLATFORM_WINDOWS
		CHECK(exercise_connected_throttle(host, console) == 0);
#endif
		CHECK(exercise_connected_sftp(host, console, &selected) == 0);
#ifdef ST_WREN_HAVE_LIVE_SFTP
		if (argc == 2) CHECK(st_wren_test_live_sftp(host, console,
		    ST_WREN_TEST_DIRECTORY, argv[1]) == 0);
#endif
		CHECK(exercise_connected_scrollback(host, console, &selected) == 0);
	}
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
	CHECK(wrenInterpret(((struct st_wren_host *)host)->vm,
	    "safe_hyperlink_contract",
	    "import \"syncterm_menu\" for Menu\n"
	    "if (Menu.openUrl(\"https://example.invalid/\") || "
	    "Menu.openHyperlink(1)) Fiber.abort(\"safe URL launch\")\n")
	    == WREN_RESULT_SUCCESS);
	CHECK(exercise_safe_mode_persistence_contract(host, personal, settings,
	    cache) == 0);
	CHECK(exercise_prompt_close_during(host, console) == 0);
	st_wren_host_destroy(host);
	st_console_destroy(console);
	st_session_history_destroy(session_history);
	st_audio_runtime_destroy(audio_runtime);
	st_hyperlink_registry_destroy(hyperlinks);
	st_settings_shutdown();
	return 0;
}
