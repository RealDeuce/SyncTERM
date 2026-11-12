/* Integration test for the embedded Wren menu graph. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_wren_host.h"
#include "wren_host_internal.h"
#include "st_wren_screen_test_control.h"
#include "wren_vm.h"
#include "wren_value.h"

#include "filesystem.h"
#include "io_error.h"
#include "st_app_paths.h"
#include "st_bbs_list.h"
#include "st_consent_token.h"
#include "path.h"
#include "st_audio_runtime.h"
#include "st_music.h"
#include "st_audio_file.h"
#include "st_pixel_image.h"
#include "st_list_crypto.h"
#include "st_language.h"
#include "st_crypto.h"
#include "st_console.h"
#include "console_internal.h"
#include "st_cterm.h"
#include "cterm.h"
#include "cterm_skypix.h"
#include "cterm_rip_engine.h"
#include "st_pixels.h"
#include "st_font_catalog.h"
#include "st_profile.h"
#include "st_settings.h"
#include "st_session_history.h"
#include "st_theme.h"
#include "st_term.h"
#include "st_transfer.h"
#include "connection_internal.h"
#include "st_threads.h"
#include "st_time.h"
#include "string_compat.h"
#include "syncterm_config.h"

#include <inttypes.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int queue_text(st_console_t *console, const char *text);
static bool console_has_text(st_console_t *console, uint32_t x, uint32_t y,
	const char *text);

extern int st_wren_test_transfer_create_status;
extern int st_wren_test_transfer_claim_status;
extern int st_wren_test_transfer_launch_status;
extern unsigned st_wren_test_transfer_create_calls;
extern bool st_wren_test_cterm_write_override;
extern int st_wren_test_cterm_write_status;
extern size_t st_wren_test_cterm_write_consumed;
extern size_t st_wren_test_cterm_write_size;
extern unsigned st_wren_test_cterm_write_calls;
extern bool st_wren_test_cterm_write_prefix;
extern int st_wren_test_cterm_write_last_status;
extern size_t st_wren_test_cterm_write_last_consumed;
extern bool st_wren_test_cterm_reset_override;
extern int st_wren_test_cterm_reset_status;
extern unsigned st_wren_test_cterm_reset_calls;
extern bool st_wren_test_cterm_key_override;
extern int st_wren_test_cterm_key_status;
extern bool st_wren_test_cterm_key_handled;
extern unsigned st_wren_test_cterm_key_calls;

#if ST_WREN_TEST_HAVE_PTY
#include <fcntl.h>
#endif

#if SYNCTERM_PLATFORM_POSIX
#include <unistd.h>
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

static uint32_t
command_modifiers(struct st_wren_host *host)
{
	WrenVM *vm = host->vm;
	wrenEnsureSlots(vm, 1);
	wrenGetVariable(vm, "syncterm", "Mod", 0);
	WrenHandle *getter = wrenMakeCallHandle(vm, "command");
	CHECK(wrenCall(vm, getter) == WREN_RESULT_SUCCESS);
	uint32_t modifiers = (uint32_t)wrenGetSlotDouble(vm, 0);
	wrenReleaseHandle(vm, getter);
	return modifiers;
}

static const char foreign_type_contract[] =
	"import \"syncterm\" for Screen, Surface, Cell, WON, Input, PixelBuffer, PixelMask, PixelBlit\n"
	"import \"ui_widget\" for Rect\n"
	"class ForeignTypeProbe {\n"
	"  static rejects(fn, expected) {\n"
	"    var attempt = Fiber.new { fn.call() }\n"
	"    attempt.try()\n"
	"    if (!(attempt.error is String) || !attempt.error.contains(expected)) Fiber.abort(\"foreign type was not rejected as \" + expected)\n"
	"  }\n"
	"  static run(bad) {\n"
	"    rejects(Fn.new { Screen.putRect(bad, 1, 1) }, \"Surface\")\n"
	"    rejects(Fn.new { Screen.restore(bad) }, \"ScreenSnapshot\")\n"
	"    var surface = Surface.new(1, 1)\n"
	"    rejects(Fn.new { surface.putRect(bad, 0, 0) }, \"Surface\")\n"
	"    rejects(Fn.new { surface.fill(Rect.new(0, 0, 1, 1), bad) }, \"Cell\")\n"
	"    var pixels = PixelBuffer.new(1, 1)\n"
	"    var blit = PixelBlit.new()\n"
	"    rejects(Fn.new { Screen.blitPixels(bad, blit) }, \"PixelBuffer\")\n"
	"    rejects(Fn.new { Screen.blitPixels(pixels, bad, blit) }, \"PixelMask\")\n"
	"    rejects(Fn.new { Screen.blitPixels(pixels, bad) }, \"PixelBlit\")\n"
	"    rejects(Fn.new { pixels.blit(bad, blit) }, \"PixelBuffer\")\n"
	"    rejects(Fn.new { pixels.blit(pixels, bad, blit) }, \"PixelMask\")\n"
	"    rejects(Fn.new { pixels.blit(pixels, bad) }, \"PixelBlit\")\n"
	"    rejects(Fn.new { Input.ungetKey_(bad) }, \"KeyEvent\")\n"
	"    rejects(Fn.new { Input.ungetPhysicalKey_(bad) }, \"PhysicalKeyEvent\")\n"
	"    rejects(Fn.new { Input.ungetMouse_(bad) }, \"MouseEvent\")\n"
	"    if (Cell.new().eqContent(bad)) Fiber.abort(\"non-Cell content equality\")\n"
	"  }\n"
	"}\n"
	"ForeignTypeProbe.run(WON.deserialize(\"]\"))\n"
	"ForeignTypeProbe.run(WON.deserialize(\"[\"))\n";

static int
write_file(const char *path, const char *contents)
{
	FILE *stream = NULL;
	size_t length = strlen(contents);

	if (fs_open(&stream, path, "wb") != 0)
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
	struct fake_sftp_response responses[512];
	size_t head;
	size_t count;
	uint64_t next_id;
	uint64_t submitted_offset;
	uint64_t submitted_modified;
	size_t submitted_size;
	char submitted_path[128];
	const unsigned char *remote;
	size_t remote_size;
	int64_t modified;
	bool submit_error;
	size_t close_count;
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

	if (fake->submit_error)
		return IO_ERROR_IO;
	if (fake->count == sizeof(fake->responses) / sizeof(fake->responses[0]))
		return IO_ERROR_OUT_OF_RANGE;
	index = (fake->head + fake->count) %
	    (sizeof(fake->responses) / sizeof(fake->responses[0]));
	stored = &fake->responses[index];
	memset(stored, 0, sizeof(*stored));
	stored->response.request_id = ++fake->next_id;
	*request_id = stored->response.request_id;
	fake->submitted_offset = request->offset;
	fake->submitted_modified = request->modified_time;
	fake->submitted_size = request->size;
	(void)snprintf(fake->submitted_path, sizeof(fake->submitted_path), "%s",
	    request->path == NULL ? "" : request->path);
	switch (request->operation) {
	case ST_SFTP_START:
		stored->response.kind = ST_SFTP_RESULT_STARTED;
		break;
	case ST_SFTP_STAT:
		stored->response.kind = ST_SFTP_RESULT_STAT;
		stored->response.stat.size = fake->remote_size;
		stored->response.stat.modified_time = strcmp(request->path, "/first") == 0 ? 111
		    : strcmp(request->path, "/second") == 0 ? 222 : fake->modified;
		break;
	case ST_SFTP_OPEN:
	case ST_SFTP_OPENDIR:
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
	case ST_SFTP_READDIR:
		stored->response.kind = ST_SFTP_RESULT_EOF;
		break;
	case ST_SFTP_CLOSE:
		++fake->close_count;
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
exercise_remote_resource_contract(st_wren_connected_t *connected,
	struct fake_sftp *fake)
{
	CHECK(st_wren_connected_interpret(connected, "remote_resources",
	    "import \"syncterm\" for SFTP, SFTPError, RemoteFile, RemoteDirectory, FileFlag, OperationError, ScriptError, Cleanup_\n"
	    "class RemoteChecks {\n"
	    "  static rejects(fn) {\n"
	    "    var failed = Fiber.new { fn.call() }\n"
	    "    failed.try()\n"
	    "    if (failed.error == null) Fiber.abort(\"invalid remote operation accepted\")\n"
	    "  }\n"
	    "}\n"
	    "var rwOpen = SFTP.open(\"/rw\", FileFlag.read | FileFlag.write)\n"
	    "var readOpen = SFTP.open(\"/read\", FileFlag.read)\n"
	    "var writeOpen = SFTP.open(\"/write\", FileFlag.write)\n"
	    "var appendOpen = SFTP.open(\"/append\", FileFlag.read | FileFlag.write | FileFlag.append)\n"
	    "var dirOpen = SFTP.opendir(\"/directory\")\n"
	    ) == 0);
	for (unsigned tick = 0; tick < 4; ++tick)
		CHECK(st_wren_connected_pump(connected, 0) == 0);
	uint64_t before = fake->next_id;
	size_t closes = fake->close_count;
	CHECK(st_wren_connected_interpret(connected, "remote_resources",
	    "var rw = rwOpen.result\n"
	    "var reader = readOpen.result\n"
	    "var writer = writeOpen.result\n"
	    "var appender = appendOpen.result\n"
	    "var directory = dirOpen.result\n"
	    "if (!(rw is RemoteFile) || !(directory is RemoteDirectory) || !rw.readable || !rw.writable || rw.append || reader.writable || writer.readable || !appender.append || rw.isClosed || directory.isClosing) Fiber.abort(\"remote type/rights metadata\")\n"
	    "for (flags in [0, 64, FileFlag.creat, FileFlag.read | FileFlag.trunc, FileFlag.write | FileFlag.excl]) RemoteChecks.rejects(Fn.new { SFTP.open(\"/invalid\", flags) })\n"
	    "for (action in [Fn.new { directory.readBytes(1, 0) }, Fn.new { rw.readEntries() }, Fn.new { reader.writeBytes(\"x\", 0) }, Fn.new { writer.readBytes(1, 0) }, Fn.new { appender.writeBytes(\"x\", 0) }, Fn.new { rw.appendBytes(\"x\") }]) RemoteChecks.rejects(action)\n"
	    ) == 0);
	CHECK(fake->next_id == before);
	CHECK(st_wren_connected_interpret(connected, "remote_resources",
	    "var reading = rw.readBytes(2, 0)\n"
	    "var otherRead = rw.readBytes(2, 2)\n"
	    "var busyClose = rw.close()\n"
	    "if (!(busyClose.result is SFTPError) || rw.isClosing || rw.isClosed) Fiber.abort(\"close raced pending reads\")\n"
	    "var listing = directory.readEntries()\n"
	    "if (!(directory.readEntries().result is SFTPError)) Fiber.abort(\"concurrent directory cursor read\")\n"
	    "System.gc()\n"
	    ) == 0);
	CHECK(fake->next_id == before + 3 && fake->close_count == closes);
	for (unsigned tick = 0; tick < 4; ++tick)
		CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "remote_resources",
	    "if (!(reading.result is String) || !(otherRead.result is String) || listing.result != null) Fiber.abort(\"pending resource completion\")\n"
	    "var closeOne = rw.close()\n"
	    "var closeTwo = rw.close()\n"
	    "if (closeOne != closeTwo || !rw.isClosing || rw.isClosed) Fiber.abort(\"concurrent close identity\")\n"
	    "RemoteChecks.rejects(Fn.new { rw.readBytes(1, 0) })\n"
	    ) == 0);
	CHECK(fake->close_count == closes + 1);
	for (unsigned tick = 0; tick < 4; ++tick)
		CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "remote_resources",
	    "if (!rw.isClosed || rw.isClosing || closeOne.result != null || rw.close().result != null || reader.isClosed) Fiber.abort(\"close ownership/idempotence\")\n"
	    "RemoteChecks.rejects(Fn.new { rw.readBytes(1, 0) })\n"
	    ) == 0);
	CHECK(fake->close_count == closes + 1);
	fake->close_error = true;
	CHECK(st_wren_connected_interpret(connected, "remote_resources",
	    "var failedClose = writer.close()\n"
	    ) == 0);
	for (unsigned tick = 0; tick < 4; ++tick)
		CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "remote_resources",
	    "if (!(failedClose.result is SFTPError) || writer.isClosed || writer.isClosing) Fiber.abort(\"failed close lost ownership\")\n"
	    ) == 0);
	fake->close_error = false;
	CHECK(st_wren_connected_interpret(connected, "remote_resources",
	    "var retryClose = writer.close()\n"
	    ) == 0);
	for (unsigned tick = 0; tick < 4; ++tick)
		CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "remote_resources",
	    "if (retryClose.result != null || !writer.isClosed) Fiber.abort(\"close retry\")\n"
	    "var cancelledClose = appender.close()\n"
	    "if (!cancelledClose.cancel() || !(cancelledClose.result is OperationError) || appender.close() != cancelledClose || !appender.isClosing) Fiber.abort(\"cancelled native close state\")\n"
	    ) == 0);
	for (unsigned tick = 0; tick < 4; ++tick)
		CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "remote_resources",
	    "if (!appender.isClosed || appender.isClosing || appender.close().result != null) Fiber.abort(\"cancelled close not drained\")\n"
	    "var cancelledRead = reader.readBytes(2, 0)\n"
	    "if (!cancelledRead.cancel() || !(reader.close().result is SFTPError)) Fiber.abort(\"cancelled read released native ownership early\")\n"
	    ) == 0);
	for (unsigned tick = 0; tick < 4; ++tick)
		CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "remote_resources",
	    "reader.close()\n"
	    "directory.close()\n"
	    "var orphanOpen = SFTP.open(\"/orphan\", FileFlag.read)\n"
	    ) == 0);
	for (unsigned tick = 0; tick < 4; ++tick)
		CHECK(st_wren_connected_pump(connected, 0) == 0);
	closes = fake->close_count;
	CHECK(st_wren_connected_interpret(connected, "remote_resources",
	    "var orphan = orphanOpen.result\n"
	    "var orphanRead = orphan.readBytes(2, 0)\n"
	    "orphanOpen = null\n"
	    "orphan = null\n"
	    "System.gc()\n"
	    ) == 0);
	CHECK(fake->close_count == closes);
	for (unsigned tick = 0; tick < 4; ++tick)
		CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "remote_resources",
	    "System.gc()\n"
	    "System.gc()\n"
	    ) == 0);
	CHECK(fake->close_count == closes + 1);
	for (unsigned tick = 0; tick < 4; ++tick)
		CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "remote_resources",
	    "var normalOpen = SFTP.open(\"/normal-scope\", FileFlag.read)\n"
	    "var errorOpen = SFTP.open(\"/error-scope\", FileFlag.read)\n"
	    ) == 0);
	for (unsigned tick = 0; tick < 4; ++tick)
		CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "remote_resources",
	    "var normal = normalOpen.result\n"
	    "var failed = errorOpen.result\n"
	    "var normalResult = null\n"
	    "var scopedFailure = ScriptError.new(63, \"resource scope failure\")\n"
	    "var caught = null\n"
	    "Fiber.new {\n"
	    "  normalResult = normal.use(Fn.new {|file|\n"
	    "    var bytes = file.readBytes(2, 0).await()\n"
	    "    if (!(bytes is String)) Fiber.abort(\"scoped read\")\n"
	    "    return 42\n"
	    "  })\n"
	    "}.call()\n"
	    ) == 0);
	for (unsigned tick = 0; tick < 4; ++tick)
		CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "remote_resources",
	    "if (normalResult != 42 || !normal.isClosed) Fiber.abort(\"normal resource scope cleanup\")\n"
	    "Fiber.new {\n"
	    "  var task = Fiber.new {\n"
	    "    failed.use(Fn.new {|file|\n"
	    "      file.readBytes(2, 0).await()\n"
	    "      Fiber.abort(scopedFailure)\n"
	    "    })\n"
	    "  }\n"
	    "  Cleanup_.try_(task)\n"
	    "  caught = task.error\n"
	    "}.call()\n"
	    ) == 0);
	for (unsigned tick = 0; tick < 4; ++tick)
		CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "remote_resources",
	    "if (caught != scopedFailure || !failed.isClosed) Fiber.abort(\"async failed resource scope cleanup\")\n"
	    "var cancelledOpen = SFTP.open(\"/cancelled-scope\", FileFlag.read)\n"
	    ) == 0);
	for (unsigned tick = 0; tick < 4; ++tick)
		CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "remote_resources",
	    "var cancelledFile = cancelledOpen.result\n"
	    "var cancelledScope = null\n"
	    "Fiber.new {\n"
	    "  var task = Fiber.new {\n"
	    "    cancelledFile.use(Fn.new {|file|\n"
	    "      var operation = file.readBytes(2, 0)\n"
	    "      operation.cancel()\n"
	    "      var result = operation.await()\n"
	    "      if (!(result is OperationError)) Fiber.abort(\"cancelled scope read\")\n"
	    "      Fiber.abort(scopedFailure)\n"
	    "    })\n"
	    "  }\n"
	    "  Cleanup_.try_(task)\n"
	    "  cancelledScope = task.error\n"
	    "}.call()\n"
	    ) == 0);
	for (unsigned tick = 0; tick < 4; ++tick)
		CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "remote_resources",
	    "if (cancelledScope != scopedFailure || cancelledFile.isClosed || cancelledFile.isClosing) Fiber.abort(\"cancelled scope ownership\")\n"
	    "cancelledFile.close()\n"
	    ) == 0);
	for (unsigned tick = 0; tick < 4; ++tick)
		CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "remote_resources",
	    "var failureOpen = SFTP.open(\"/failed-scope-close\", FileFlag.read)\n"
	    ) == 0);
	for (unsigned tick = 0; tick < 4; ++tick)
		CHECK(st_wren_connected_pump(connected, 0) == 0);
	fake->close_error = true;
	CHECK(st_wren_connected_interpret(connected, "remote_resources",
	    "var closeFailedFile = failureOpen.result\n"
	    "var closeFailure = null\n"
	    "Fiber.new {\n"
	    "  var task = Fiber.new { closeFailedFile.use(Fn.new {|file| 7 }) }\n"
	    "  Cleanup_.try_(task)\n"
	    "  closeFailure = task.error\n"
	    "}.call()\n"
	    ) == 0);
	for (unsigned tick = 0; tick < 4; ++tick)
		CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "remote_resources",
	    "if (!(closeFailure is SFTPError) || closeFailedFile.isClosed) Fiber.abort(\"scoped close failure hidden\")\n"
	    "var originalFailure = null\n"
	    "Fiber.new {\n"
	    "  var task = Fiber.new { closeFailedFile.use(Fn.new {|file| Fiber.abort(scopedFailure) }) }\n"
	    "  Cleanup_.try_(task)\n"
	    "  originalFailure = task.error\n"
	    "}.call()\n"
	    ) == 0);
	for (unsigned tick = 0; tick < 4; ++tick)
		CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "remote_resources",
	    "if (originalFailure != scopedFailure || closeFailedFile.isClosed) Fiber.abort(\"close failure replaced callback failure\")\n"
	    ) == 0);
	fake->close_error = false;
	CHECK(st_wren_connected_interpret(connected, "remote_resources",
	    "closeFailedFile.close()\n"
	    ) == 0);
	for (unsigned tick = 0; tick < 4; ++tick)
		CHECK(st_wren_connected_pump(connected, 0) == 0);
	return 0;
}


static int
exercise_remote_outcomes(st_wren_connected_t *connected, struct fake_sftp *fake)
{
	CHECK(st_wren_connected_interpret(connected, "remote_outcomes",
	    "import \"syncterm\" for SFTP, FileFlag, SFTPError\n"
	    "var opened = SFTP.open(\"/outcomes\", FileFlag.read | FileFlag.write)\n"
	    "var openedAppend = SFTP.open(\"/outcomes-append\", FileFlag.write | FileFlag.append)\n"
	    "var openedDirectory = SFTP.opendir(\"/outcomes-dir\")\n"
	    ) == 0);
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	uint64_t before = fake->next_id;
	CHECK(st_wren_connected_interpret(connected, "remote_outcomes",
	    "var file = opened.result\n"
	    "var appendFile = openedAppend.result\n"
	    "var directory = openedDirectory.result\n"
	    "var emptyRead = file.readBytes(0, 9007199254740991)\n"
	    "var emptyWrite = file.writeBytes(\"\", 9007199254740991)\n"
	    "var emptyAppend = appendFile.appendBytes(\"\")\n"
	    "if (!emptyRead.isDone || emptyRead.result != \"\" || emptyRead.cancel() || emptyWrite.result != null || emptyAppend.result != null) Fiber.abort(\"zero remote request semantics\")\n"
	    "var large = \"x\"\n"
	    "for (i in 0...18) large = large + large\n"
	    "var oversized = Fiber.new { file.writeBytes(large[0...262124], 0) }\n"
	    "oversized.try()\n"
	    "if (oversized.error == null) Fiber.abort(\"oversized native write accepted\")\n"
	    ) == 0);
	CHECK(fake->next_id == before && fake->count == 0);
	CHECK(st_wren_connected_interpret(connected, "remote_outcomes",
	    "var remotePath = SFTP.stat(\"/cafe\\u0301\")\n"
	    ) == 0);
	CHECK(strcmp(fake->submitted_path, "/caf\xc3\xa9") == 0);
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "remote_outcomes",
	    "var bytes = file.readBytes(1, 0)\n"
	    ) == 0);
	struct st_sftp_response *response = &fake->responses[fake->head].response;
	response->kind = ST_SFTP_RESULT_BYTES;
	response->size = 0;
	response->data = NULL;
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "remote_outcomes",
	    "if (bytes.result != null) Fiber.abort(\"empty remote byte reply is EOF\")\n"
	    "bytes = file.readBytes(1, 0)\n"
	    ) == 0);
	response = &fake->responses[fake->head].response;
	response->kind = ST_SFTP_RESULT_BYTES;
	response->size = 2;
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "remote_outcomes",
	    "if (!(bytes.result is SFTPError) || bytes.result.isTransient) Fiber.abort(\"oversized provider read result\")\n"
	    "bytes = file.readBytes(1, 0)\n"
	    ) == 0);
	response = &fake->responses[fake->head].response;
	response->kind = ST_SFTP_RESULT_BYTES;
	response->size = 1;
	response->data = NULL;
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "remote_outcomes",
	    "if (!(bytes.result is SFTPError)) Fiber.abort(\"missing provider byte payload\")\n"
	    "var listing = directory.readEntries()\n"
	    ) == 0);
	response = &fake->responses[fake->head].response;
	response->kind = ST_SFTP_RESULT_ENTRIES;
	response->entry_count = 0;
	response->entries = NULL;
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "remote_outcomes",
	    "if (listing.result != null) Fiber.abort(\"empty directory reply is EOF\")\n"
	    "listing = directory.readEntries()\n"
	    ) == 0);
	response = &fake->responses[fake->head].response;
	response->kind = ST_SFTP_RESULT_ENTRIES;
	response->entry_count = 1;
	response->entries = NULL;
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "remote_outcomes",
	    "if (!(listing.result is SFTPError)) Fiber.abort(\"missing provider directory payload\")\n"
	    "bytes = file.readBytes(1, 0)\n"
	    ) == 0);
	response = &fake->responses[fake->head].response;
	response->kind = ST_SFTP_RESULT_ERROR;
	response->error_code = 0;
	response->server_status = 3;
	response->message = "permission denied";
	response->transient = false;
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "remote_outcomes",
	    "if (!(bytes.result is SFTPError) || bytes.result.code != 0 || bytes.result.serverStatus != 3 || bytes.result.message != \"permission denied\" || bytes.result.isTransient) Fiber.abort(\"protocol failure details lost\")\n"
	    "file.close()\n"
	    "appendFile.close()\n"
	    "directory.close()\n"
	    ) == 0);
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	return 0;
}


static int
exercise_sftp_integer_contract(st_wren_connected_t *connected,
	struct fake_sftp *fake)
{
	CHECK(st_wren_connected_interpret(connected, "sftp_integer_contract",
	    "import \"syncterm\" for SFTP, SFTPError, SFTPStat, FileFlag\n"
	    "var opening = SFTP.open(\"/integers\", FileFlag.read | FileFlag.write)\n"
	    "var openingDirectory = SFTP.opendir(\"/integers-dir\")\n") == 0);
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	uint64_t before = fake->next_id;
	CHECK(st_wren_connected_interpret(connected, "sftp_integer_contract",
	    "var handle = opening.result\n"
	    "var directory = openingDirectory.result\n"
	    "for (n in [-1, 0.5, Num.nan, Num.infinity, -Num.infinity, 9007199254740992, 9223372036854775808, 18446744073709551616]) {\n"
	    "  var actions = [Fn.new { handle.readBytes(n, 0) }, Fn.new { handle.readBytes(0, n) }, Fn.new { handle.writeBytes(\"\", n) }, Fn.new { SFTP.setMtime(\"/integers\", n) }, Fn.new { SFTP.open(\"/integers\", n) }]\n"
	    "  for (action in actions) {\n"
	    "    var failed = Fiber.new { action.call() }\n"
	    "    failed.try()\n"
	    "    if (failed.error == null) Fiber.abort(\"SFTP accepted inexact integer\")\n"
	    "  }\n"
	    "}\n") == 0);
	CHECK(fake->next_id == before && fake->count == 0);
	CHECK(st_wren_connected_interpret(connected, "sftp_integer_contract",
	    "var farRead = handle.readBytes(1, 9007199254740991)\n") == 0);
	CHECK(fake->submitted_size == 1 && fake->submitted_offset == ST_WREN_MAX_INTEGER);
	CHECK(st_wren_connected_interpret(connected, "sftp_integer_contract",
	    "var zeroTime = SFTP.setMtime(\"/integers\", 0)\n"
	    "var farTime = SFTP.setMtime(\"/integers\", 4294967295)\n") == 0);
	CHECK(fake->count == 3 && fake->submitted_modified == UINT32_MAX);
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "sftp_integer_contract",
	    "if (farRead.result != null || zeroTime.result != null || farTime.result != null) Fiber.abort(\"exact boundary submission\")\n"
	    "var metadata = SFTP.stat(\"/integers\")\n") == 0);
	struct st_sftp_response *response = &fake->responses[fake->head].response;
	response->stat = (struct st_sftp_stat){
		.size = ST_WREN_MAX_INTEGER,
		.modified_time = -(int64_t)ST_WREN_MAX_INTEGER,
		.accessed_time = (int64_t)ST_WREN_MAX_INTEGER,
	};
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "sftp_integer_contract",
	    "if (!(metadata.result is SFTPStat) || metadata.result.size != 9007199254740991 || metadata.result.mtime != -9007199254740991 || metadata.result.atime != 9007199254740991) Fiber.abort(\"exact metadata boundaries\")\n") == 0);
	CHECK(st_wren_connected_interpret(connected, "sftp_integer_contract",
	    "var listing = directory.readEntries()\n") == 0);
	struct st_sftp_entry listing_entries[] = {
		{.name = "boundary", .stat = {
			.size = ST_WREN_MAX_INTEGER,
			.modified_time = -(int64_t)ST_WREN_MAX_INTEGER,
			.accessed_time = (int64_t)ST_WREN_MAX_INTEGER,
			.mode = UINT32_MAX, .uid = UINT32_MAX, .gid = UINT32_MAX}},
		{.name = "distinct", .stat = {.size = 123, .modified_time = -456,
			.accessed_time = 789, .mode = 33188, .uid = 42, .gid = 84}},
		{.name = "omitted"},
	};
	response = &fake->responses[fake->head].response;
	response->kind = ST_SFTP_RESULT_ENTRIES;
	response->entries = listing_entries;
	response->entry_count = sizeof(listing_entries) / sizeof(listing_entries[0]);
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	memset(listing_entries, 0, sizeof(listing_entries));
	before = fake->next_id;
	CHECK(st_wren_connected_interpret(connected, "sftp_integer_contract",
	    "var entry = listing.result[0]\n"
	    "var snapshot = entry.stat\n"
	    "var again = entry.stat\n"
	    "var distinct = listing.result[1].stat\n"
	    "var omitted = listing.result[2].stat\n"
	    "if (!(snapshot is SFTPStat) || snapshot == again || snapshot.size != 9007199254740991 || snapshot.mtime != -9007199254740991 || snapshot.atime != 9007199254740991 || snapshot.mode != 4294967295 || snapshot.uid != 4294967295 || snapshot.gid != 4294967295) Fiber.abort(\"listing boundaries or fresh snapshot\")\n"
	    "if (distinct.size != 123 || distinct.mtime != -456 || distinct.atime != 789 || distinct.mode != 33188 || distinct.uid != 42 || distinct.gid != 84) Fiber.abort(\"listing fields mixed\")\n"
	    "if (omitted.size != 0 || omitted.mtime != 0 || omitted.atime != 0 || omitted.mode != 0 || omitted.uid != 0 || omitted.gid != 0) Fiber.abort(\"omitted attribute defaults\")\n"
	    "for (action in [Fn.new { entry.size }, Fn.new { entry.mtime }, Fn.new { entry.stat = null }, Fn.new { snapshot.size = 1 }, Fn.new { snapshot.mtime = 1 }, Fn.new { snapshot.atime = 1 }, Fn.new { snapshot.mode = 1 }, Fn.new { snapshot.uid = 1 }, Fn.new { snapshot.gid = 1 }]) {\n"
	    "  var failed = Fiber.new { action.call() }\n"
	    "  failed.try()\n"
	    "  if (failed.error == null) Fiber.abort(\"obsolete getter or mutable snapshot\")\n"
	    "}\n"
	    "entry = null\nlisting = null\nSystem.gc()\nSystem.gc()\n") == 0);
	CHECK(fake->next_id == before && fake->count == 0);
	/* SFTP v3 counts/times are 32-bit; file offsets retain the exact 53-bit range. */
	before = fake->next_id;
	CHECK(st_wren_connected_interpret(connected, "sftp_integer_contract",
	    "for (action in [Fn.new { handle.readBytes(4294967296, 0) }, Fn.new { SFTP.setMtime(\"/integers\", 4294967296) }]) {\n"
	    "  var failed = Fiber.new { action.call() }\n"
	    "  failed.try()\n"
	    "  if (failed.error == null) Fiber.abort(\"SFTP v3 integer overflow\")\n"
	    "}\n"
	    "var largestRead = handle.readBytes(4294967295, 0)\n") == 0);
	CHECK(fake->next_id == before + 1 && fake->submitted_size == UINT32_MAX);
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	const struct st_sftp_stat invalid[] = {
		{.size = ST_WREN_MAX_INTEGER + 1},
		{.size = UINT64_MAX},
		{.modified_time = (int64_t)ST_WREN_MAX_INTEGER + 1},
		{.modified_time = INT64_MAX},
		{.accessed_time = -(int64_t)ST_WREN_MAX_INTEGER - 1},
		{.accessed_time = INT64_MIN},
	};
	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
		for (unsigned kind = 0; kind < 2; ++kind) {
			CHECK(st_wren_connected_interpret(connected, "sftp_integer_contract",
			    kind == 0 ? "metadata = SFTP.stat(\"/integers\")\n"
			    : "metadata = directory.readEntries()\n") == 0);
			response = &fake->responses[fake->head].response;
			struct st_sftp_entry entries[2] = {{.name = "small"},
			    {.name = "unrepresentable", .stat = invalid[i]}};
			if (kind == 0)
				response->stat = invalid[i];
			else {
				response->kind = ST_SFTP_RESULT_ENTRIES;
				response->entries = entries;
				response->entry_count = 2;
			}
			CHECK(st_wren_connected_pump(connected, 0) == 0);
			CHECK(st_wren_connected_interpret(connected, "sftp_integer_contract",
			    "if (!(metadata.result is SFTPError) || metadata.result.isTransient || metadata.result.serverStatus != 0 || metadata.result.code >= 0) Fiber.abort(\"inexact native metadata escaped\")\n") == 0);
		}
	}
	CHECK(st_wren_connected_interpret(connected, "sftp_integer_contract",
	    "var closing = handle.close()\n"
	    "directory.close()\n") == 0);
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "sftp_integer_contract",
	    "System.gc()\n"
	    "if (snapshot.size != 9007199254740991 || snapshot.mtime != -9007199254740991 || snapshot.atime != 9007199254740991 || snapshot.mode != 4294967295 || snapshot.uid != 4294967295 || snapshot.gid != 4294967295 || again.size != snapshot.size || distinct.size != 123 || distinct.mtime != -456 || distinct.atime != 789 || distinct.mode != 33188 || distinct.uid != 42 || distinct.gid != 84) Fiber.abort(\"snapshot changed after collection, responses or close\")\n") == 0);
	return 0;
}

static int
exercise_filesystem_outcomes(st_wren_connected_t *connected)
{
	(void)fs_remove_file(ST_WREN_TEST_DIRECTORY "/outcomes.tmp");
	(void)fs_remove_file(ST_WREN_TEST_DIRECTORY "/outcomes-dir/child.tmp");
	(void)fs_remove_directory(ST_WREN_TEST_DIRECTORY "/outcomes-dir");
	CHECK(st_wren_connected_interpret(connected, "filesystem_outcomes",
	    "import \"syncterm\" for Download, Cache, File, Directory, FileError, IOErr\n"
	    "var file = Download.create(\"outcomes.tmp\")\n"
	    "if (!(file is File)) Fiber.abort(\"filesystem outcome setup\")\n"
	    "var duplicate = Download.create(\"outcomes.tmp\")\n"
	    "if (!(duplicate is FileError) || duplicate.code != IOErr.alreadyExists) Fiber.abort(\"create discarded the native reason\")\n"
	    "if (file.open() != null || file.write(\"abc\") != null) Fiber.abort(\"file mutation success\")\n"
	    "file.offset = 0\n"
	    "if (file.readBytes(2) != \"ab\" || file.offset != 2 || file.readBytes(2) != \"c\" || file.readBytes(1) != null) Fiber.abort(\"byte read/short read/EOF\")\n"
	    "if (file.readBytes(0) != \"\" || file.readBytes(0, 9007199254740991) != \"\" || file.offset != 3) Fiber.abort(\"zero read semantics\")\n"
	    "if (file.writeBytes(\"\", 9007199254740991) != null || file.offset != 3 || file.size != 3) Fiber.abort(\"zero write semantics\")\n"
	    "file.offset = 0\n"
	    "if (file.readBytes(1) != \"a\" || file.writeBytes(\"Z\") != null || file.offset != 2 || file.size != 3 || file.read() != \"aZc\") Fiber.abort(\"C update-stream direction leaked\")\n"
	    "var bytes = String.fromByte(0) + String.fromByte(255) + \"é\"\n"
	    "if (file.write(bytes) != null || file.read() != bytes) Fiber.abort(\"byte-preserving filesystem data\")\n"
	    "if (file.write(\"\") != null || file.read() != \"\" || file.readBytes(1) != null || file.readLine() != null) Fiber.abort(\"empty file semantics\")\n"
	    "if (file.writeLine(\"\") != null || file.writeBytes(\"last\\r\") != null) Fiber.abort(\"line write success\")\n"
	    "file.offset = 0\n"
	    "if (file.readLine() != \"\" || file.readLine() != \"last\\r\" || file.readLine() != null) Fiber.abort(\"blank line/unterminated CR/EOF\")\n"
	    "if (file.close() != null || file.close() != null) Fiber.abort(\"idempotent local close\")\n"
	    ) == 0);
	CHECK(st_wren_connected_interpret(connected, "filesystem_outcomes",
	    "var decomposed = \"cafe\\u0301.tmp\"\n"
	    "var exact = Download.create(decomposed)\n"
	    "if (!(exact is File) || !exact.path.endsWith(\"café.tmp\") || !Download.contains(\"café.tmp\") || !Download.contains(decomposed) || Download.delete(decomposed) != null) Fiber.abort(\"filename NFC boundary\")\n"
	    "var directory = Download.createDir(\"outcomes-dir\")\n"
	    "if (!(directory is Directory)) Fiber.abort(\"directory creation result\")\n"
	    "var existingDirectory = Download.createDir(\"outcomes-dir\")\n"
	    "if (!(existingDirectory is FileError) || existingDirectory.code != IOErr.alreadyExists) Fiber.abort(\"mkdir error reason\")\n"
	    "var child = directory.create(\"child.tmp\")\n"
	    "if (!(child is File) || directory.contains(\"child.tmp\") != true || directory.contains(\"missing.tmp\") != false) Fiber.abort(\"directory query results\")\n"
	    "var notEmpty = Download.delete(\"outcomes-dir\")\n"
	    "if (!(notEmpty is FileError) || notEmpty.code != IOErr.directoryNotEmpty) Fiber.abort(\"delete nonempty reason\")\n"
	    "if (directory.delete(\"child.tmp\") != null || Download.delete(\"outcomes-dir\") != null) Fiber.abort(\"delete success result\")\n"
	    "var missingDirectory = directory.list\n"
	    "if (!(missingDirectory is FileError) || missingDirectory.code != IOErr.notFound) Fiber.abort(\"missing directory result\")\n"
	    "var missingDelete = Download.delete(\"outcomes-missing.tmp\")\n"
	    "if (!(missingDelete is FileError) || missingDelete.code != IOErr.notFound) Fiber.abort(\"delete missing reason\")\n"
	    "if (Download.replace(\"outcomes.tmp\", \"replacement\") != null) Fiber.abort(\"replace success result\")\n"
	    "if (file.open() != null || file.read() != \"replacement\" || file.close() != null) Fiber.abort(\"replacement grant reuse\")\n"
	    "for (name in [\"../escape\", \"nul\\u0000suffix\", String.fromByte(255)]) {\n"
	    "  for (action in [Fn.new { Download.contains(name) }, Fn.new { Download.create(name) }, Fn.new { Download.createDir(name) }, Fn.new { Download.delete(name) }, Fn.new { Download.replace(name, \"x\") }]) {\n"
	    "    var failed = Fiber.new { action.call() }\n"
	    "    failed.try()\n"
	    "    if (failed.error == null) Fiber.abort(\"invalid filesystem name accepted\")\n"
	    "  }\n"
	    "}\n"
	    "if (Download.delete(\"outcomes.tmp\") != null || file.close() != null) Fiber.abort(\"closed missing grant close\")\n"
	    "var missingFile = file.open()\n"
	    "if (!(missingFile is FileError) || missingFile.code != IOErr.notFound) Fiber.abort(\"missing file reason\")\n"
	    ) == 0);

#if SYNCTERM_PLATFORM_POSIX
	const char *target = ST_WREN_TEST_DIRECTORY "/outcomes-ungranted.tmp";
	const char *root = ST_WREN_TEST_DIRECTORY "/outcomes-links";
	const char *link = ST_WREN_TEST_DIRECTORY "/outcomes-links/alias.tmp";
	(void)unlink(link);
	(void)fs_remove_directory(root);
	CHECK(write_file(target, "outside the directory grant") == 0);
	CHECK(fs_create_directory(root) == 0);
	CHECK(symlink("../outcomes-ungranted.tmp", link) == 0);
	CHECK(st_wren_connected_interpret(connected, "filesystem_outcomes",
	    "var sandbox = Download.list[\"outcomes-links\"]\n"
	    "if (sandbox.contains(\"alias.tmp\") || sandbox.list.containsKey(\"alias.tmp\")) Fiber.abort(\"directory followed an ungranted link\")\n"
	    "var rejectedLink = sandbox.delete(\"alias.tmp\")\n"
	    "if (!(rejectedLink is FileError) || rejectedLink.code != IOErr.notSupported) Fiber.abort(\"link deletion policy\")\n") == 0);
	CHECK(file_equals(target, "outside the directory grant"));
	CHECK(unlink(link) == 0);
	CHECK(fs_remove_directory(root) == 0);
	CHECK(fs_remove_file(target) == 0);
#endif
	return 0;
}

static int
exercise_filesystem_failures(st_wren_host_t *host)
{
	struct fs_info info;
	WrenVM *vm = host->vm;

	/* A host-issued grant to the full device injects real stdio failures
	 * without adding arbitrary-path factories to the script API. */
	CHECK(wrenInterpret(vm, "text_failure_contract",
	    "import \"syncterm\" for Text\n"
	    "var invalid = Fiber.new { Text.graphemes(String.fromByte(255)) }\n"
	    "invalid.try()\n"
	    "if (invalid.error == null) Fiber.abort(\"invalid Unicode accepted\")\n") == WREN_RESULT_SUCCESS);
	if (fs_get_info(&info, "/dev/full") != 0 || info.kind != FS_ENTRY_OTHER)
		return 0;
	CHECK(wrenInterpret(vm, "filesystem_failures",
	    "import \"syncterm\" for FileError\nvar files = []\n") == WREN_RESULT_SUCCESS);
	wrenEnsureSlots(vm, 3);
	wrenGetVariable(vm, "filesystem_failures", "files", 0);
	CHECK(st_wren_file_push(vm, 1, "/dev/full", ST_WREN_PICKER_DISPOSITION_OVERWRITE, false));
	wrenInsertInList(vm, 0, -1, 1);
	CHECK(st_wren_file_push(vm, 1, "/dev/full", ST_WREN_PICKER_DISPOSITION_OVERWRITE, false));
	wrenInsertInList(vm, 0, -1, 1);
	CHECK(wrenInterpret(vm, "filesystem_failures",
	    "var buffered = files[0]\n"
	    "buffered.open()\n"
	    "var written = buffered.writeBytes(\"x\")\n"
	    "var closed = buffered.close()\n"
	    "if (!(closed is FileError) || closed.code != -10 || closed.errno <= 0 || buffered.isOpen) Fiber.abort(\"close lost the disk-full reason\")\n"
	    "if (written != null || buffered.close() != null) Fiber.abort(\"buffered write/close success\")\n"
	    "var direct = files[1]\n"
	    "direct.open()\n"
	    "var large = \"x\"\n"
	    "for (i in 0...16) large = large + large\n"
	    "var failure = direct.writeBytes(large)\n"
	    "if (!(failure is FileError) || failure.code != -10 || failure.errno <= 0) Fiber.abort(\"write lost the disk-full reason\")\n"
	    "direct.close()\n"
	    "if (direct.isOpen || direct.close() != null) Fiber.abort(\"failed file close ownership\")\n") == WREN_RESULT_SUCCESS);
	return 0;
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
	(void)fs_remove_file(ST_WREN_TEST_DIRECTORY "/exact-integers.tmp");
	(void)fs_remove_file(external_file);
	(void)fs_remove_file(external_child);
	(void)fs_remove_directory(external_directory);

	CHECK(st_wren_connected_interpret(connected, "filesystem_path_handles",
	    "import \"syncterm\" for Cache, Download, File, FileError, IOErr\n"
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
	    "    if (!(missing is FileError) || missing.code != IOErr.notFound) Fiber.abort(\"missing file result\")\n"
	    "    var unavailable = Fiber.new { __directory.contains(\"child.tmp\") }.try()\n"
	    "    if (!(unavailable is FileError) || unavailable.code != IOErr.notFound) Fiber.abort(\"missing directory result\")\n"
	    "    if (!(__directory.toString is String)) Fiber.abort(\"missing directory metadata\")\n"
	    "  }\n"
	    "  static checkRestored() {\n"
	    "    var opened = __file.open()\n"
	    "    if (opened is FileError || __file.read() != \"external-restored\") Fiber.abort(\"restored file\")\n"
	    "    if (__file.close() is FileError) Fiber.abort(\"restored close\")\n"
	    "    var child = __directory.create(\"child.tmp\")\n"
	    "    if (!(child is File) || __directory.delete(\"child.tmp\") != null) Fiber.abort(\"restored directory\")\n"
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
	    "import \"syncterm\" for Cache, Download, File, FileError, IOErr\n"
	    "var oldFile = Download.create(\"wren-delete-file.tmp\")\n"
	    "if (!(oldFile is File) || Download.delete(\"wren-delete-file.tmp\") != null) Fiber.abort(\"file delete\")\n"
	    "var missing = Fiber.new { oldFile.open() }.try()\n"
	    "if (!(missing is FileError) || missing.code != IOErr.notFound) Fiber.abort(\"deleted file result\")\n"
	    "var newFile = Download.create(\"wren-delete-file.tmp\")\n"
	    "if (!(newFile is File) || (newFile.open() is FileError) || (newFile.write(\"recreated\") is FileError) || (newFile.close() is FileError)) Fiber.abort(\"file recreate\")\n"
	    "if ((oldFile.open() is FileError) || oldFile.read() != \"recreated\" || (oldFile.close() is FileError)) Fiber.abort(\"file handle reuse\")\n"
	    "if (Download.delete(\"wren-delete-file.tmp\") != null) Fiber.abort(\"file cleanup\")\n"
	    "var oldDir = Download.createDir(\"wren-delete-dir\")\n"
	    "if (oldDir == null || Download.delete(\"wren-delete-dir\") != null) Fiber.abort(\"directory delete\")\n"
	    "var unavailable = Fiber.new { oldDir.contains(\"child.tmp\") }.try()\n"
	    "if (!(unavailable is FileError) || unavailable.code != IOErr.notFound) Fiber.abort(\"deleted directory result\")\n"
	    "var newDir = Download.createDir(\"wren-delete-dir\")\n"
	    "if (newDir == null || oldDir.contains(\"child.tmp\")) Fiber.abort(\"directory handle reuse\")\n"
	    "if (Download.delete(\"wren-delete-dir\") != null) Fiber.abort(\"directory cleanup\")\n"
	    "var replacement = Download.create(\"wren-replace.tmp\")\n"
	    "if (!(replacement is File) || (Download.replace(\"wren-replace.tmp\", \"replacement\") is FileError)) Fiber.abort(\"replace setup\")\n"
	    "if ((replacement.open() is FileError) || replacement.read() != \"replacement\" || (replacement.close() is FileError)) Fiber.abort(\"replace handle\")\n"
	    "if (Download.delete(\"wren-replace.tmp\") != null) Fiber.abort(\"replace cleanup\")\n"
	    "var relaxed = Download.create(\"file with spaces.tmp\")\n"
	    "if (!(relaxed is File) || Download.delete(\"file with spaces.tmp\") != null) Fiber.abort(\"download names\")\n"
	    "for (action in [Fn.new { Download.create(\"../escape\") }, Fn.new { Download.create(\"nul\\u0000suffix\") }, Fn.new { Cache.create(\".hidden\") }]) {\n"
	    "  var denied = Fiber.new { action.call() }\n"
	    "  denied.try()\n"
	    "  if (denied.error == null) Fiber.abort(\"name policies\")\n"
	    "}\n") == 0);

	CHECK(st_wren_connected_interpret(connected, "file_integer_contract",
	    "import \"syncterm\" for Download, FileError, IOErr\n"
	    "var file = Download.create(\"exact-integers.tmp\")\n"
	    "if (file.open() is FileError || file.write(\"unchanged\") is FileError) Fiber.abort(\"integer fixture\")\n"
	    "file.offset = 3\n"
	    "var stamp = file.mtime\n"
	    "for (n in [-1, 0.5, Num.nan, Num.infinity, -Num.infinity, 9007199254740992, 9223372036854775808]) {\n"
	    "  var actions = [Fn.new { file.offset = n }, Fn.new { file.readBytes(n) }, Fn.new { file.readBytes(0, n) }, Fn.new { file.writeBytes(\"X\", n) }]\n"
	    "  if (n != -1) actions.add(Fn.new { file.mtime = n })\n"
	    "  for (action in actions) {\n"
	    "    var failed = Fiber.new { action.call() }\n"
	    "    failed.try()\n"
	    "    if (failed.error == null || file.offset != 3 || file.size != 9 || file.mtime != stamp) Fiber.abort(\"integer validation side effect\")\n"
	    "  }\n"
	    "}\n"
	    "var signedTime = (file.mtime = -2)\n"
	    "if (!(signedTime is FileError) && (signedTime != -2 || file.mtime != -2)) Fiber.abort(\"signed timestamp round trip\")\n"
	    "var changed = (file.mtime = -1)\n"
	    "if (changed is FileError && changed.code != IOErr.outOfRange) Fiber.abort(\"native timestamp range reason\")\n"
	    "if (!(changed is FileError) && (changed != -1 || file.mtime != -1)) Fiber.abort(\"signed native file timestamp: %(changed), %(file.mtime)\")\n"
	    "if ((file.mtime = stamp) is FileError) Fiber.abort(\"restore timestamp\")\n"
	    "if (file.readBytes(0) != \"\" || file.offset != 3) Fiber.abort(\"zero read offset\")\n"
	    "file.close()\n"
	    "Download.delete(\"exact-integers.tmp\")\n") == 0);
	CHECK(fs_remove_file(external_file) == 0);
	CHECK(fs_remove_directory(external_directory) == 0);
	return 0;
}

static bool
emoji_result_interpret(WrenVM *vm, st_wren_connected_t *connected,
	const char *source)
{
	return connected != NULL
	    ? st_wren_connected_interpret(connected, "emoji_result_contract", source) == 0
	    : wrenInterpret(vm, "emoji_result_contract", source) == WREN_RESULT_SUCCESS;
}

static int
exercise_emoji_result_contract(struct st_wren_host *host, WrenVM *vm,
	st_wren_connected_t *connected)
{
	const char *settings = host->config.paths->settings_file;
	char backup[sizeof(ST_WREN_TEST_DIRECTORY) + 128];
	char source[1024];
	bool passed;

	CHECK(snprintf(backup, sizeof(backup), "%s.emoji-backup", settings)
	    < (int)sizeof(backup));
	CHECK(emoji_result_interpret(vm, connected,
	    "import \"syncterm\" for Emoji, EmojiEntry, FileError, Error\n"
	    "var grin = Emoji.entries.where {|e| e.text == \"\xf0\x9f\x98\x80\" }.toList[0]\n"));
	CHECK(rename(settings, backup) == 0);
	CHECK(fs_create_directory(settings) == 0);
	int status = st_settings_remember_emoji("\xf0\x9f\x98\x83", true);
	CHECK(status < 0);
	CHECK(snprintf(source, sizeof(source),
	    "var failure = Emoji.recordUse(grin)\n"
	    "if (!(failure is FileError) || !(failure is Error) || failure.code != %d || failure.errno != 0) Fiber.abort(\"emoji persistence must preserve native FileError\")\n"
	    "if (!failure.message.contains(\"session\") || !failure.message.contains(\"saved\") || Emoji.recent[0].text != grin.text) Fiber.abort(\"emoji failure lost session effect\")\n",
	    status) < (int)sizeof(source));
	errno = EDOM;
	passed = emoji_result_interpret(vm, connected, source);
	CHECK(fs_remove_directory(settings) == 0);
	CHECK(rename(backup, settings) == 0);
	CHECK(passed);
	CHECK(emoji_result_interpret(vm, connected,
	    "if (Emoji.recordUse(grin) != null) Fiber.abort(\"emoji persistence success\")\n"
	    "for (id in [-1, 0.5, 9007199254740992, \"bad\"]) {\n"
	    "  var bad = Fiber.new { Emoji.recordUse(EmojiEntry.from_(id)) }\n"
	    "  bad.try()\n"
	    "  if (bad.error == null) Fiber.abort(\"invalid emoji entry accepted\")\n"
	    "}\n"
	    "System.gc()\nSystem.gc()\n"
	    "if (!(failure is FileError) || failure.errno != 0 || !(failure.message is String) || !failure.toString.startsWith(\"FileError\")) Fiber.abort(\"emoji error lifetime\")\n"));
	CHECK(file_contains(settings, "RecentEmoji"));
	CHECK(file_contains(settings, "\xf0\x9f\x98\x80"));
	CHECK(rename(settings, backup) == 0);
	CHECK(fs_create_directory(settings) == 0);
	bool old_safe_mode = host->config.safe_mode;
	host->config.safe_mode = true;
	passed = emoji_result_interpret(vm, connected,
	    "var smile = Emoji.entries.where {|e| e.text == \"\xf0\x9f\x98\x83\" }.toList[0]\n"
	    "if (Emoji.recordUse(smile) != null || Emoji.recent[0].text != smile.text) Fiber.abort(\"session-only emoji persistence\")\n");
	host->config.safe_mode = old_safe_mode;
	CHECK(fs_remove_directory(settings) == 0);
	CHECK(rename(backup, settings) == 0);
	CHECK(passed);
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
	CHECK(exercise_emoji_result_contract(host, NULL, connected) == 0);
	CHECK(st_wren_connected_interpret(connected, "profile_script_assert",
	    "import \"syncterm\" for Status, Session\n"
	    "if (Status.details.call() != \"second\") Fiber.abort(\"not loaded\")\n"
	    "if (Session.current.profile.wrenScripts.count != 2 || Session.current.profile.wrenScripts[0] != \"profile_first\" || Session.current.profile.wrenScripts[1] != \"profile_second\") Fiber.abort(\"script order view\")\n") == 0);
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

static int
transfer_interpret(WrenVM *vm, st_wren_connected_t *connected, const char *source)
{
	return connected != NULL ? st_wren_connected_interpret(connected,
	    "transfer_result_contract", source) : wrenInterpret(vm,
	    "transfer_result_contract", source) != WREN_RESULT_SUCCESS;
}

/* The same API is present in every VM; real I/O still requires a session. */
static int
exercise_transfer_results(WrenVM *vm, st_wren_connected_t *connected)
{
	CHECK(transfer_interpret(vm, connected,
	    "import \"syncterm\" for Transfer, TransferError, Error, IOErr\n"
	    "var invalid = Fiber.new { Transfer.result }\n"
	    "invalid.try()\n"
	    "if (invalid.error == null) Fiber.abort(\"inactive result accepted\")\n"
	    "for (kind in [null, 3, \"unknown\", \"fake\\u0000suffix\", String.fromByte(255)]) {\n"
	    "  invalid = Fiber.new { Transfer.beginSession(kind) }\n"
	    "  invalid.try()\n"
	    "  if (invalid.error == null || Transfer.active) Fiber.abort(\"invalid synthetic kind accepted\")\n"
	    "}\n"
	    "for (kind in [\"raw\", \"xmodem-1k\", \"xmodem-g\\u0000suffix\", String.fromByte(255)]) {\n"
	    "  invalid = Fiber.new { Transfer.download(kind, \"test.bin\") }\n"
	    "  invalid.try()\n"
	    "  if (invalid.error == null || Transfer.active) Fiber.abort(\"invalid download kind accepted\")\n"
	    "}\n"
	    "for (name in [null, 3, \"x\\u0000y\", String.fromByte(255)]) {\n"
	    "  invalid = Fiber.new { Transfer.download(\"xmodem-g\", name) }\n"
	    "  invalid.try()\n"
	    "  if (invalid.error == null || Transfer.active) Fiber.abort(\"invalid download filename accepted\")\n"
	    "}\n"
	    "var denied = Transfer.download(\"xmodem-g\", \"test.bin\")\n"
	    "if (!(denied is TransferError) || !(denied is Error) || denied.code != IOErr.permissionDenied || Transfer.active) Fiber.abort(\"missing session error\")\n"
	    "if (Transfer.beginSession(\"fake\") != null || !Transfer.active) Fiber.abort(\"synthetic startup result\")\n"
	    "var held = Transfer.beginSession(\"fake\")\n"
	    "if (!(held is TransferError) || held.code != IOErr.busy || !held.toString.startsWith(\"TransferError\") || !held.message.contains(\"startup\")) Fiber.abort(\"busy result lost\")\n"
	    "invalid = Fiber.new { Transfer.result }\n"
	    "invalid.try()\n"
	    "if (invalid.error == null) Fiber.abort(\"pending result accepted\")\n"
	    "invalid = Fiber.new { held.code = 0 }\n"
	    "invalid.try()\n"
	    "if (invalid.error == null || held.code != IOErr.busy) Fiber.abort(\"mutable error\")\n"
	    "invalid = Fiber.new { Transfer.success }\n"
	    "invalid.try()\n"
	    "if (invalid.error == null) Fiber.abort(\"obsolete success getter retained\")\n"
	    "if (!Transfer.requestAbort() || Transfer.requestAbort() || !Transfer.aborted) Fiber.abort(\"abort latch changed\")\n"
	    "Transfer.endSession()\n"
	    "Transfer.endSession()\n"
	    "System.gc()\n"
	    "if (held.code != IOErr.busy || held.message.count == 0 || Transfer.active) Fiber.abort(\"startup error lifetime\")\n") == 0);
	return 0;
}

/* Exercise upload authority at the actual foreign-call boundary. */
static int
exercise_upload_consent(st_wren_host_t *host, st_console_t *console)
{
	static const struct st_connection_transport_ops operations = {0};
	struct st_connection connection = {.operations = &operations};
	struct st_profile profile;
	st_font_catalog_t *fonts = NULL;
	st_term_session_t *session = NULL;
	st_wren_connected_t *connected = NULL;
	st_console_snapshot_t *snapshot = NULL;
	char root[1024], nested[1024], local[1024], child[1024], unicode[1024];
	char outside[1024], sibling[1024], traversal[1024];
	char *cache = NULL, *cached = NULL;
	char grant_list_path[1024];
	st_bbs_list_t *saved_list = host->bbs_list, *grant_list = NULL;
	char *token = NULL;
	if (!st_list_crypto_encryption_available()) return 0;
	const char payload[] = "upload-grant";

	CHECK(snprintf(root, sizeof(root), "%s/upload-consent", ST_WREN_TEST_DIRECTORY) < (int)sizeof(root));
	CHECK(snprintf(nested, sizeof(nested), "%s/nested", root) < (int)sizeof(nested));
	CHECK(snprintf(local, sizeof(local), "%s/source.txt", root) < (int)sizeof(local));
	CHECK(snprintf(child, sizeof(child), "%s/source.txt", nested) < (int)sizeof(child));
	CHECK(snprintf(unicode, sizeof(unicode), "%s/caf\xc3\xa9.txt", root) < (int)sizeof(unicode));
	CHECK(snprintf(outside, sizeof(outside), "%s/upload-outside.txt", ST_WREN_TEST_DIRECTORY) < (int)sizeof(outside));
	CHECK(snprintf(sibling, sizeof(sibling), "%s-sibling.txt", root) < (int)sizeof(sibling));
	CHECK(snprintf(traversal, sizeof(traversal), "%s/../upload-outside.txt", root) < (int)sizeof(traversal));
	CHECK(fs_create_directories(nested) == 0);
	CHECK(write_file(local, payload) == 0 && write_file(child, payload) == 0 && write_file(unicode, payload) == 0);
	/* Verify the UTF-8 fixture pathname before testing upload authority. */
	struct fs_info unicode_info;
	CHECK(fs_get_info(&unicode_info, unicode) == 0 && unicode_info.kind == FS_ENTRY_FILE);
	CHECK(write_file(outside, payload) == 0 && write_file(sibling, payload) == 0);
	CHECK(st_bbs_list_cache_path(&cache, host->bbs_list, "Alpha") == 0);
	CHECK(fs_create_directories(cache) == 0);
	CHECK(path_join_alloc(&cached, cache, "upload-consent.txt") == 0);
	CHECK(write_file(cached, payload) == 0);
	st_profile_init(&profile);
	strcpy(profile.name, "Alpha");
	profile.connection_type = ST_CONNECTION_RAW;
	profile.screen_mode = ST_SCREEN_MODE_80X25;
	profile.upload_directory = strdup(root);
	CHECK(profile.upload_directory != NULL);
	atomic_init(&connection.connected, true);
	atomic_init(&connection.binary_mode, false);
	atomic_init(&connection.stopping, false);
	CHECK(st_mutex_init(&connection.input.mutex, ST_MUTEX_PLAIN) == ST_THREAD_SUCCESS);
	CHECK(st_condition_init(&connection.input.changed) == ST_THREAD_SUCCESS);
	CHECK(st_mutex_init(&connection.output.mutex, ST_MUTEX_PLAIN) == ST_THREAD_SUCCESS);
	CHECK(st_condition_init(&connection.output.changed) == ST_THREAD_SUCCESS);
	CHECK(st_console_snapshot_create(&snapshot, console) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	const struct st_term_config term_config = {.console = console,
	    .connection = &connection, .fonts = fonts, .hide_status = true,
	    .screen_mode = ST_SCREEN_MODE_80X25, .encoding = ST_CTERM_ENCODING_UTF8};
	CHECK(st_term_session_create(&session, &term_config) == 0);
	const struct st_wren_connected_config config = {.host = host,
	    .console = console, .profile = &profile, .session = session,
	    .connection = &connection, .cterm = st_term_session_cterm(session)};
	CHECK(st_wren_connected_create(&connected, &config) == 0);
	CHECK(snprintf(grant_list_path, sizeof(grant_list_path), "%s/upload-consent.lst", ST_WREN_TEST_DIRECTORY) < (int)sizeof(grant_list_path));
	CHECK(write_file(grant_list_path, "") == 0);
	struct st_app_paths grant_paths = *host->config.paths;
	grant_paths.personal_list = grant_list_path;
	struct st_settings grant_settings = st_settings;
	grant_settings.list_path = grant_list_path;
	grant_settings.download_system_list = false;
	CHECK(st_bbs_list_open_replacement(&grant_list, NULL, &grant_paths, &grant_settings) == 0);
	struct st_list_crypto_options crypto = ST_LIST_CRYPTO_OPTIONS_AES256;
	crypto.cost_log2 = 8;
	CHECK(st_bbs_list_rewrite_encryption(grant_list, &crypto, "test", 4) == 0);
	uint8_t key[ST_CONSENT_TOKEN_KEY_SIZE];
	bool available = false;
	CHECK(st_bbs_list_picker_key(grant_list, key, &available) == 0 && available);
	CHECK(st_consent_token_sign(&token, key, outside) == 0 && token != NULL);
	st_crypto_clear(key, sizeof(key));
	host->bbs_list = grant_list;
	CHECK(wrenInterpret(host->vm, "upload_write_consent",
	    "import \"syncterm\" for Transfer\nvar files = []\n") == WREN_RESULT_SUCCESS);
	wrenEnsureSlots(host->vm, 3);
	wrenGetVariable(host->vm, "upload_write_consent", "files", 0);
	CHECK(st_wren_file_push(host->vm, 1, outside, ST_WREN_PICKER_DISPOSITION_OVERWRITE, false));
	wrenInsertInList(host->vm, 0, -1, 1);
	CHECK(wrenInterpret(host->vm, "upload_write_consent",
	    "var call = Fiber.new { Transfer.upload(\"raw\", files[0], 0) }\n"
	    "call.try()\n"
	    "if (call.error != \"Upload source requires a readable File\") Fiber.abort(\"write-only grant accepted for upload\")\n") == WREN_RESULT_SUCCESS);
	char setup[8192];
	CHECK(snprintf(setup, sizeof(setup),
	    "import \"syncterm\" for Transfer, Cache, Host, File, PixelBuffer, Session\n"
	    "var granted = Host.openLocalFile(\"%s\")\n"
	    "if (!(granted is File)) Fiber.abort(\"grant reconstruction\")\n"
	    "var approved = [granted, Cache.list[\"upload-consent.txt\"].path, Host.uploadPath + \"/source.txt\", Host.uploadPath + \"/nested/source.txt\", Host.uploadPath + \"/cafe\\u0301.txt\", Host.uploadPath + \"/nested/../source.txt\"]\n"
	    "var denied = [granted.path, Host.uploadPath + \"-sibling.txt\", Host.uploadPath + \"/../upload-outside.txt\", Host.uploadPath]\n",
	    token) < (int)sizeof(setup));
	CHECK(st_wren_connected_interpret(connected, "upload_consent", setup) == 0);
#if SYNCTERM_PLATFORM_POSIX
	char link[1024], directory_link[1024];
	CHECK(snprintf(link, sizeof(link), "%s/alias.txt", root) < (int)sizeof(link));
	CHECK(snprintf(directory_link, sizeof(directory_link), "%s/alias-dir", root) < (int)sizeof(directory_link));
	(void)unlink(link);
	(void)unlink(directory_link);
	CHECK(symlink(outside, link) == 0);
	CHECK(symlink(ST_WREN_TEST_DIRECTORY, directory_link) == 0);
	CHECK(st_wren_connected_interpret(connected, "upload_consent",
	    "denied.add(Host.uploadPath + \"/alias.txt\")\n"
	    "denied.add(Host.uploadPath + \"/alias-dir/upload-outside.txt\")\n") == 0);
#endif
	unsigned before_validation = st_wren_test_transfer_create_calls;
	CHECK(st_wren_connected_interpret(connected, "upload_consent",
	    "for (item in [null, 3, PixelBuffer.new(1, 1), \"\", \"x\\u0000y\", String.fromByte(255)]) denied.add(item)\n"
	    "for (item in denied) {\n"
	    "  var call = Fiber.new { Transfer.upload(\"raw\", item, 0) }\n"
	    "  call.try()\n"
	    "  if (call.error == null || Transfer.active) Fiber.abort(\"ungranted upload started\")\n"
	    "}\n"
	    "var batch = Fiber.new { Transfer.uploadBatch(\"ymodem\", [approved[0], denied[0]], 0) }\n"
	    "batch.try()\n"
	    "if (batch.error == null || Transfer.active) Fiber.abort(\"partially validated batch started\")\n"
	    "var copy = Session.current.profile.copy()\n"
	    "copy.ulDir = \"/\"\n") == 0);
	CHECK(st_wren_test_transfer_create_calls == before_validation);
	CHECK(connection.output.count == 0);
	for (unsigned i = 0; i < 6; ++i) {
		char source[160];
		CHECK(snprintf(source, sizeof(source), "Transfer.upload(\"raw\", approved[%u], 0)\n", i) < (int)sizeof(source));
		CHECK(st_wren_connected_interpret(connected, "upload_consent", source) == 0);
		bool done = false;
		for (unsigned tick = 0; tick < 2000 && !done; ++tick) {
			CHECK(st_wren_connected_pump(connected, 0) == 0);
			struct st_transfer_inspection progress;
			bool reserved;
			CHECK(st_term_session_inspect_transfer(session, &progress, &reserved));
			done = progress.done;
			if (!done) (void)st_thread_sleep(&(struct timespec){.tv_nsec = 1000000L}, NULL);
		}
		CHECK(done);
		CHECK(st_wren_connected_interpret(connected, "upload_consent",
		    "if (Transfer.result != null) Fiber.abort(\"authorized upload failed\")\nTransfer.endSession()\n") == 0);
		CHECK(connection.output.count == sizeof(payload) - 1);
		CHECK(memcmp(connection.output.data, payload, sizeof(payload) - 1) == 0);
		connection.output.count = 0;
		connection.output.head = 0;
	}
	CHECK(st_wren_connected_interpret(connected, "upload_consent",
	    "if (Transfer.uploadBatch(\"ymodem\", [approved[0], approved[1]], 0) != null || !Transfer.active) Fiber.abort(\"authorized batch rejected\")\n"
	    "Transfer.endSession()\n") == 0);
	CHECK(st_wren_connected_interpret(connected, "upload_consent",
	    "import \"sftp_queue\" for SftpQueue\n"
	    "import \"syncterm\" for WON\n"
	    "SftpQueue.stop()\n"
	    "Cache.replace(SftpQueue.FILENAME, WON.serialize({\"version\": 2, \"nextId\": 1, \"jobs\": []}))\n"
	    "SftpQueue.start()\n"
	    "if (SftpQueue.enqueue(SftpQueue.UPLOAD, granted.path, \"/consent-check\", granted.size, granted.token) == null) Fiber.abort(\"grant queueing failed\")\n"
	    "SftpQueue.stop()\n") == 0);
	st_wren_connected_destroy(connected);
	connected = NULL;
	CHECK(st_wren_connected_create(&connected, &config) == 0);
	CHECK(snprintf(setup, sizeof(setup),
	    "import \"syncterm\" for Host, Cache\n"
	    "import \"sftp_queue\" for SftpQueue, Job\n"
	    "SftpQueue.start()\n"
	    "var restored = SftpQueue.claimNext_(SftpQueue.UPLOAD)\n"
	    "if (restored == null || restored.localToken != \"%s\" || Host.openLocalFile(restored.localToken) == null) Fiber.abort(\"queue lost upload grant across VMs\")\n"
	    "var tampered = Job.new(2, SftpQueue.UPLOAD, restored.local, \"/tampered\", restored.total, \"tampered-token\")\n"
	    "SftpQueue.runUpload_(tampered)\n"
	    "if (tampered.status != SftpQueue.FAILED || !tampered.errMsg.contains(\"consent token rejected\")) Fiber.abort(\"queue accepted forged grant\")\n",
	    token) < (int)sizeof(setup));
	CHECK(st_wren_connected_interpret(connected, "upload_queue_consent", setup) == 0);
	CHECK(write_file(outside, "changed after consent") == 0);
	CHECK(st_wren_connected_interpret(connected, "upload_queue_consent",
	    "SftpQueue.runUpload_(restored)\n"
	    "if (restored.status != SftpQueue.FAILED || !restored.errMsg.contains(\"consent token rejected\")) Fiber.abort(\"queue accepted changed source\")\n"
	    "SftpQueue.stop()\n"
	    "if (Cache.delete(SftpQueue.FILENAME) != null) Fiber.abort(\"queue cleanup failed\")\n") == 0);
	st_wren_connected_destroy(connected);
	host->bbs_list = saved_list;
	st_bbs_list_close(grant_list);
	CHECK(fs_remove_file(grant_list_path) == 0);
	free(token);
	st_term_session_destroy(&session);
	st_font_catalog_destroy(fonts);
	CHECK(st_console_snapshot_restore(console, snapshot, NULL) == 0);
	st_console_snapshot_destroy(snapshot);
	st_condition_destroy(&connection.output.changed); st_mutex_destroy(&connection.output.mutex);
	st_condition_destroy(&connection.input.changed); st_mutex_destroy(&connection.input.mutex);
	st_profile_dispose(&profile);
#if SYNCTERM_PLATFORM_POSIX
	CHECK(unlink(link) == 0 && unlink(directory_link) == 0);
#endif
	CHECK(fs_remove_file(local) == 0 && fs_remove_file(child) == 0 && fs_remove_file(unicode) == 0);
	CHECK(fs_remove_file(outside) == 0 && fs_remove_file(sibling) == 0);
	CHECK(fs_remove_file(cached) == 0);
	CHECK(fs_remove_directory(nested) == 0 && fs_remove_directory(root) == 0);
	free(cache); free(cached);
	return 0;
}

static int
wait_transfer(st_term_session_t *session, bool done, int64_t bytes,
	struct st_transfer_inspection *state)
{
	for (unsigned i = 0; i < 500; ++i) {
		bool reserved, finished;
		enum st_term_disposition disposition;
		CHECK(st_term_session_inspect_transfer(session, state, &reserved));
		if (state->done == done && state->progress.bytes_current >= bytes) return 0;
		CHECK(st_term_session_pump(session, &disposition, &finished) == 0);
		(void)st_thread_sleep(&(struct timespec){.tv_nsec = 1000000L}, NULL);
	}
	CHECK(false);
	return 1;
}

static int
feed_transfer(struct st_connection *connection, st_term_session_t *session,
	const uint8_t *bytes, size_t size)
{
	bool finished;
	enum st_term_disposition disposition;
	(void)st_mutex_lock(&connection->input.mutex);
	CHECK(connection->input.count == 0 && size <= sizeof(connection->input.data));
	memcpy(connection->input.data, bytes, size);
	connection->input.head = 0;
	connection->input.count = size;
	(void)st_mutex_unlock(&connection->input.mutex);
	return st_term_session_pump(session, &disposition, &finished);
}

static int
transfer_key(st_wren_connected_t *connected, unsigned key)
{
	bool consumed;
	const struct st_console_event event = {.type = ST_CONSOLE_EVENT_KEY_CODE,
	    .data.key.code = key};
	CHECK(st_wren_connected_handle_event(connected, &event, &consumed) == 0 && consumed);
	for (unsigned i = 0; i < 60; ++i) {
		CHECK(st_wren_connected_pump(connected, 0) == 0);
		(void)st_thread_sleep(&(struct timespec){.tv_nsec = 1000000L}, NULL);
	}
	return 0;
}

static int
wait_transfer_notice(st_wren_connected_t *connected, st_console_t *console,
	const char *notice)
{
	uint32_t width, height;
	CHECK(st_console_size(console, &width, &height) == 0);
	size_t count = (size_t)width * height;
	struct st_console_cell *cells = calloc(count, sizeof(*cells));
	CHECK(cells != NULL);
	const struct st_console_rect area = {0, 0, width, height};
	size_t length = strlen(notice);
	bool found = false;
	/* Worker completion and the flow's 50 ms timer are asynchronous. Wait
	 * for the observable notice instead of assuming a 100 ms pump budget. */
	for (unsigned attempt = 0; attempt < 5000 && !found; ++attempt) {
		CHECK(st_wren_connected_pump(connected, 0) == 0);
		CHECK(st_console_read_cells(console, &area, cells, width) == 0);
		for (uint32_t y = 0; y < height && !found; ++y)
			for (size_t x = 0; x + length <= width && !found; ++x) {
				size_t i = 0;
				while (i < length && cells[(size_t)y * width + x + i].glyph == (unsigned char)notice[i])
					++i;
				found = i == length;
			}
		if (!found)
			(void)st_thread_sleep(&(struct timespec){.tv_nsec = 1000000L}, NULL);
	}
	free(cells);
	CHECK(found);
	return 0;
}

/* Real protocol input and startup failures use the same session/worker owner. */
static int
exercise_transfer_wire(st_wren_host_t *host, st_console_t *console)
{
	static const struct st_connection_transport_ops operations = {0};
	struct st_connection connection = {.operations = &operations};
	struct st_profile profile;
	st_font_catalog_t *fonts = NULL;
	st_term_session_t *session = NULL;
	st_wren_connected_t *connected = NULL;
	st_console_snapshot_t *snapshot = NULL;
	struct st_transfer_inspection state;
	const char *filename = ST_WREN_TEST_DIRECTORY "/wren-xmodem-g.bin";
	st_profile_init(&profile);
	profile.connection_type = ST_CONNECTION_RAW;
	profile.download_directory = strdup(ST_WREN_TEST_DIRECTORY);
	profile.upload_directory = strdup(ST_WREN_TEST_DIRECTORY);
	CHECK(profile.download_directory != NULL && profile.upload_directory != NULL);
	atomic_init(&connection.connected, true);
	atomic_init(&connection.binary_mode, false);
	atomic_init(&connection.stopping, false);
	CHECK(st_mutex_init(&connection.input.mutex, ST_MUTEX_PLAIN) == ST_THREAD_SUCCESS);
	CHECK(st_condition_init(&connection.input.changed) == ST_THREAD_SUCCESS);
	CHECK(st_mutex_init(&connection.output.mutex, ST_MUTEX_PLAIN) == ST_THREAD_SUCCESS);
	CHECK(st_condition_init(&connection.output.changed) == ST_THREAD_SUCCESS);
	CHECK(st_console_snapshot_create(&snapshot, console) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	const struct st_term_config term_config = {.console = console,
	    .connection = &connection, .fonts = fonts, .hide_status = true,
	    .screen_mode = ST_SCREEN_MODE_80X25, .encoding = ST_CTERM_ENCODING_UTF8};
	CHECK(st_term_session_create(&session, &term_config) == 0);
	const struct st_wren_connected_config config = {.host = host,
	    .console = console, .profile = &profile, .session = session,
	    .connection = &connection, .cterm = st_term_session_cterm(session)};
	CHECK(st_wren_connected_create(&connected, &config) == 0);
	CHECK(transfer_interpret(NULL, connected,
	    "import \"syncterm\" for Transfer, TransferError, Error, IOErr\n"
	    "var held = null\n"
	    "var dirty = false\n"
	    "var invalid = Fiber.new { Transfer.upload(\"xmodem-g\", \"unused\", 0) }\n"
	    "invalid.try()\n"
	    "if (invalid.error == null || Transfer.active) Fiber.abort(\"XMODEM-G upload accepted\")\n") == 0);
	int *controls[] = {&st_wren_test_transfer_create_status,
	    &st_wren_test_transfer_claim_status, &st_wren_test_transfer_launch_status,
	    &st_wren_test_transfer_create_status};
	const int statuses[] = {IO_ERROR_OUT_OF_MEMORY, IO_ERROR_READ_ONLY,
	    IO_ERROR_INTERRUPTED, 12345};
	for (size_t i = 0; i < sizeof(controls) / sizeof(controls[0]); ++i) {
		char source[512];
		*controls[i] = statuses[i];
		CHECK(snprintf(source, sizeof(source),
		    "held = Transfer.download(\"xmodem-g\", \"wren-xmodem-g.bin\")\n"
		    "if (!(held is TransferError) || held.code != %d || Transfer.active) Fiber.abort(\"startup stage status lost\")\n",
		    statuses[i]) < (int)sizeof(source));
		CHECK(transfer_interpret(NULL, connected, source) == 0);
		*controls[i] = 0;
		bool reserved;
		CHECK(!st_term_session_inspect_transfer(session, &state, &reserved) && !reserved);
		CHECK(!st_connection_binary_mode(&connection));
	}
	bool safe_mode = host->config.safe_mode;
	host->config.safe_mode = true;
	CHECK(transfer_interpret(NULL, connected,
	    "var denied = Transfer.download(\"xmodem-g\", \"wren-xmodem-g.bin\")\n"
	    "if (!(denied is TransferError) || denied.code != IOErr.permissionDenied || Transfer.active) Fiber.abort(\"safe transfer started\")\n"
	    "if (Transfer.beginSession(\"fake\") != null) Fiber.abort(\"safe synthetic transfer denied\")\n"
	    "Transfer.endSession()\n") == 0);
	host->config.safe_mode = safe_mode;
	for (unsigned action = 0; action < 5; ++action) {
		int removed = fs_remove_file(filename);
		CHECK(removed == 0 || removed == IO_ERROR_NOT_FOUND);
		CHECK(transfer_interpret(NULL, connected,
		    "if (Transfer.download(\"xmodem-g\", \"wren-xmodem-g.bin\") != null) Fiber.abort(\"XMODEM-G startup failed\")\n"
		    "invalid = Fiber.new { Transfer.result }\n"
		    "invalid.try()\n"
		    "if (invalid.error == null) Fiber.abort(\"running protocol result accepted\")\n") == 0);
		CHECK(st_connection_binary_mode(&connection));
		CHECK(wait_transfer(session, false, 0, &state) == 0 && state.protocol == ST_TRANSFER_XMODEM_G);
		bool requested = false;
		for (unsigned i = 0; i < 1000 && !requested; ++i) {
			(void)st_mutex_lock(&connection.output.mutex);
			requested = connection.output.count != 0;
			if (requested) CHECK(connection.output.data[connection.output.head] == 'G');
			(void)st_mutex_unlock(&connection.output.mutex);
			if (!requested) (void)st_thread_sleep(&(struct timespec){.tv_nsec = 1000000L}, NULL);
		}
		CHECK(requested);
		if (action < 2) {
			uint8_t block[133] = {1, 1, 254};
			memset(block + 3, 'X', 128);
			/* CRC-16/XMODEM of 128 'X' bytes, independently calculated. */
			block[131] = 0x27; block[132] = action == 0 ? 0x3f : 0x3e;
			CHECK(feed_transfer(&connection, session, block, sizeof(block)) == 0);
			if (action == 0) {
				CHECK(wait_transfer(session, false, 128, &state) == 0);
				(void)st_mutex_lock(&connection.output.mutex);
				CHECK(connection.output.count == 1); /* No per-block ACK in G mode. */
				(void)st_mutex_unlock(&connection.output.mutex);
				const uint8_t eot = 4;
				CHECK(feed_transfer(&connection, session, &eot, 1) == 0);
			}
		} else if (action == 2) {
			const uint8_t cancel[] = {24, 24};
			CHECK(feed_transfer(&connection, session, cancel, sizeof(cancel)) == 0);
		} else if (action == 3) {
			CHECK(transfer_interpret(NULL, connected,
			    "if (!Transfer.requestAbort()) Fiber.abort(\"local abort not latched\")\n") == 0);
		} else {
			(void)st_mutex_lock(&connection.input.mutex);
			connection.input.eof = true;
			(void)st_mutex_unlock(&connection.input.mutex);
		}
		CHECK(wait_transfer(session, true, 0, &state) == 0);
		if (action == 0) CHECK(state.result == 0);
		else if (action == 1) CHECK(state.result == IO_ERROR_PROTOCOL);
		else if (action == 2 || action == 3) CHECK(state.result == IO_ERROR_CANCELLED);
		else CHECK(state.result != 0);
		CHECK(state.aborted == (action == 3));
		char source[1024];
		CHECK(snprintf(source, sizeof(source),
		    "dirty = Transfer.tickDirty\n"
		    "held = Transfer.result\n"
		    "if (%d == 0) {\n"
		    "  if (held != null || Transfer.result != null) Fiber.abort(\"successful result changed\")\n"
		    "} else {\n"
		    "  if (!(held is TransferError) || held.code != %d || Transfer.result.code != %d || !held.message.contains(\"completion\")) Fiber.abort(\"completion status lost\")\n"
		    "}\n"
		    "if (Transfer.tickDirty != dirty || Transfer.drainLog().count == 0) Fiber.abort(\"result consumed mailbox\")\n"
		    "Transfer.endSession()\n"
		    "System.gc()\n"
		    "if (held != null && held.code != %d) Fiber.abort(\"saved error lost\")\n",
		    state.result, state.result, state.result, state.result) < (int)sizeof(source));
		CHECK(transfer_interpret(NULL, connected, source) == 0);
		CHECK(!st_connection_binary_mode(&connection));
		if (action == 0) {
			uint8_t content[129];
			FILE *file = fopen(filename, "rb");
			CHECK(file != NULL && fread(content, 1, sizeof(content), file) == 128 && fclose(file) == 0);
			for (size_t i = 0; i < 128; ++i) CHECK(content[i] == 'X');
		}
		(void)st_mutex_lock(&connection.output.mutex);
		connection.output.head = connection.output.count = 0;
		(void)st_mutex_unlock(&connection.output.mutex);
		(void)st_mutex_lock(&connection.input.mutex);
		connection.input.eof = false;
		(void)st_mutex_unlock(&connection.input.mutex);
	}
	CHECK(write_file(filename, "wire failure") == 0);
	atomic_store(&connection.connected, false);
	(void)st_mutex_lock(&connection.output.mutex);
	connection.output.stopped = true;
	(void)st_mutex_unlock(&connection.output.mutex);
	CHECK(transfer_interpret(NULL, connected,
	    "if (Transfer.upload(\"raw\", \"" ST_WREN_TEST_DIRECTORY "/wren-xmodem-g.bin\", 0) != null) Fiber.abort(\"raw startup failed\")\n") == 0);
	CHECK(wait_transfer(session, true, 0, &state) == 0 && state.result == IO_ERROR_NOT_CONNECTED);
	CHECK(transfer_interpret(NULL, connected,
	    "if (Transfer.result.code != IOErr.notConnected || Transfer.aborted) Fiber.abort(\"I/O result coarsened\")\n"
	    "Transfer.endSession()\n"
	    "if (held == null || held.code == 0) Fiber.abort(\"prior error changed with new transfer\")\n") == 0);
	/* Actual flow UI: one startup error, no worker/progress owner, one dismissal. */
	atomic_store(&connection.connected, true);
	unsigned created = st_wren_test_transfer_create_calls;
	st_wren_test_transfer_create_status = IO_ERROR_OUT_OF_MEMORY;
	CHECK(transfer_interpret(NULL, connected,
	    "import \"transfer_pick\" for TransferFlow\n"
	    "import \"syncterm\" for Screen\n"
	    "TransferFlow.download()\n") == 0);
	CHECK(transfer_key(connected, 13) == 0);
	CHECK(st_wren_test_transfer_create_calls == created + 1);
	st_wren_test_transfer_create_status = 0;
	CHECK(transfer_interpret(NULL, connected,
	    "var size = Screen.size\n"
	    "var text = Screen.readRect(1, 1, size[0], size[1]).map {|c| c.ch }.join()\n"
	    "if (TransferFlow.busy || Transfer.active || !text.contains(\"Out of memory\")) Fiber.abort(\"startup failure UI missing\")\n") == 0);
	CHECK(transfer_key(connected, 13) == 0);
	CHECK(transfer_interpret(NULL, connected,
	    "if (TransferFlow.busy || Transfer.active) Fiber.abort(\"startup failure retained flow ownership\")\n") == 0);
	/* Inline cleanup captures the error before destroying the worker. */
	atomic_store(&connection.connected, false);
	CHECK(transfer_interpret(NULL, connected,
	    "TransferFlow.launch_(Fn.new {\n"
	    "  var error = Transfer.upload(\"raw\", \"" ST_WREN_TEST_DIRECTORY "/wren-xmodem-g.bin\", 0)\n"
	    "  if (error != null) Fiber.abort(error)\n"
	    "  TransferFlow.inline_()\n"
	    "})\n") == 0);
	CHECK(wait_transfer_notice(connected, console, "Not connected") == 0);
	CHECK(transfer_interpret(NULL, connected,
	    "text = Screen.readRect(1, 1, size[0], size[1]).map {|c| c.ch }.join()\n"
	    "if (Transfer.active || !TransferFlow.busy || !text.contains(\"Not connected\")) Fiber.abort(\"inline error lost after cleanup\")\n") == 0);
	CHECK(transfer_key(connected, 13) == 0);
	CHECK(transfer_interpret(NULL, connected,
	    "if (TransferFlow.busy || Transfer.active) Fiber.abort(\"inline failure retained flow ownership\")\n") == 0);
	CHECK(transfer_interpret(NULL, connected,
	    "import \"syncterm\" for Timer\n"
	    "TransferFlow.launch_(Fn.new {\n"
	    "  Timer.after(1).await()\n"
	    "  Fiber.abort(\"transfer text failure\")\n"
	    "})\n") == 0);
	CHECK(wait_transfer_notice(connected, console, "transfer text failure") == 0);
	CHECK(transfer_interpret(NULL, connected,
	    "text = Screen.readRect(1, 1, size[0], size[1]).map {|c| c.ch }.join()\n"
	    "if (TransferFlow.busy || Transfer.active || !text.contains(\"transfer text failure\")) Fiber.abort(\"resumed Fiber error escaped supervisor\")\n") == 0);
	CHECK(transfer_key(connected, 13) == 0);
	st_wren_connected_destroy(connected);
	st_term_session_destroy(&session);
	st_font_catalog_destroy(fonts);
	CHECK(st_console_snapshot_restore(console, snapshot, NULL) == 0);
	st_console_snapshot_destroy(snapshot);
	st_condition_destroy(&connection.output.changed); st_mutex_destroy(&connection.output.mutex);
	st_condition_destroy(&connection.input.changed); st_mutex_destroy(&connection.input.mutex);
	st_profile_dispose(&profile);
	CHECK(fs_remove_file(filename) == 0);
	return 0;
}

/* Exercise the real asynchronous progress App and VM-owned cancellation. */
static int
exercise_transfer_ui(st_wren_host_t *host, st_console_t *console,
	struct st_profile *profile)
{
	const struct st_wren_connected_config config = {.host = host,
	    .console = console, .profile = profile};
	for (unsigned aborting = 0; aborting < 2; ++aborting) {
		st_wren_connected_t *connected = NULL;
		CHECK(st_wren_connected_create(&connected, &config) == 0);
		CHECK(exercise_transfer_results(NULL, connected) == 0);
		CHECK(st_wren_connected_interpret(connected, "transfer_ui_probe",
		    "import \"syncterm\" for Transfer, Input, CTerm, IOErr\n"
		    "import \"transfer_app\" for TransferApp\n"
		    "class TransferProbe {\n"
		    "  static done { __done }\n"
		    "  static start() {\n"
		    "    __done = false\n"
		    "    if (Transfer.beginSession(\"fake\") != null) Fiber.abort(\"transfer startup must return null\")\n"
		    "    __fiber = Fiber.new {\n"
		    "      var app = TransferApp.new(\"Transfer test\")\n"
		    "      app.waitTimeoutMs = 0\n"
		    "      app.run()\n"
		    "      if (!Transfer.done || Transfer.snapshot().count != 6) Fiber.abort(\"completion shape\")\n"
		    "      var result = Transfer.result\n"
		    "      if (app.success != (result == null) || app.aborted != Transfer.aborted) Fiber.abort(\"App result disagrees\")\n"
		    "      if (result != null && result.code != IOErr.cancelled) Fiber.abort(\"completion code lost\")\n"
		    "      Transfer.endSession()\n"
		    "      System.gc()\n"
		    "      if (result != null && result.code != IOErr.cancelled) Fiber.abort(\"completion error lifetime\")\n"
		    "      __done = true\n"
		    "    }\n"
		    "    __fiber.call()\n"
		    "  }\n"
		    "}\n"
		    "TransferProbe.start()\n") == 0);
		if (aborting) {
			bool consumed;
			const struct st_console_event event = {.type = ST_CONSOLE_EVENT_KEY_CODE,
			    .data.key.code = 27};
			CHECK(st_wren_connected_handle_event(connected, &event, &consumed) == 0 && consumed);
		}
		const struct timespec delay = {.tv_nsec = 10000000L};
		for (unsigned i = 0; i < 150; ++i) {
			(void)st_thread_sleep(&delay, NULL);
			CHECK(st_wren_connected_pump(connected, 0) == 0);
		}
		CHECK(st_wren_connected_last_error(connected) == NULL);
		CHECK(st_wren_connected_interpret(connected, "transfer_ui_result",
		    "import \"transfer_ui_probe\" for TransferProbe\n"
		    "import \"syncterm\" for Transfer, CTerm\n"
		    "if (!TransferProbe.done || Transfer.active || CTerm.suspended) Fiber.abort(\"UI cleanup\")\n"
		    "Transfer.beginSession(\"fake\")\n") == 0);
		/* Teardown must join a still-running worker before releasing the VM. */
		st_wren_connected_destroy(connected);
	}
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
exercise_session_lease_contract(st_wren_host_t *host, st_console_t *console,
	struct st_profile *profile)
{
	const struct st_wren_connected_config config = {
		.host = host, .console = console, .profile = profile,
	};
	st_wren_connected_t *connected = NULL;
	st_console_snapshot_t *snapshot = NULL;
	WrenVM *isolated[] = {host->vm, host->picker_vm};

	for (size_t i = 0; i < sizeof(isolated) / sizeof(isolated[0]); ++i)
		CHECK(wrenInterpret(isolated[i], "no_current_session",
		    "import \"syncterm\" for Session\n"
		    "if (Session.current != null) Fiber.abort(\"session crossed VM modes\")\n")
		    == WREN_RESULT_SUCCESS);
	CHECK(st_console_snapshot_create(&snapshot, console) == 0);
	CHECK(st_wren_connected_create(&connected, &config) == 0);
	CHECK(!st_wren_connected_auxiliary_active(connected));
	CHECK(st_wren_connected_interpret(connected, "session_lease_contract",
	    "import \"syncterm\" for Session, SessionLease, ScriptError, Hook, Screen\n"
	    "var session = Session.current\n"
	    "if (!(session is Session) || session != Session.current || session.holdCount != 0) Fiber.abort(\"current session identity\")\n"
	    "var first = session.hold()\n"
	    "var second = session.hold()\n"
	    "if (!(first is SessionLease) || first.released || session.holdCount != 2) Fiber.abort(\"independent leases\")\n"
	    "if (!first.release() || first.release() || !first.released || second.released || session.holdCount != 1) Fiber.abort(\"release crossed lease owners\")\n"
	    ) == 0);
	CHECK(st_wren_connected_auxiliary_active(connected));
	CHECK(st_wren_connected_interpret(connected, "session_lease_contract",
	    "if (second.released || !second.release() || session.holdCount != 0) Fiber.abort(\"last lease release\")\n"
	    "var orphan = session.hold()\n"
	    "orphan = null\n"
	    "System.gc()\n"
	    "if (session.holdCount != 0) Fiber.abort(\"lease GC fallback\")\n"
	    "var result = session.hold(Fn.new {\n"
	    "  if (session.holdCount != 1) Fiber.abort(\"scoped hold missing\")\n"
	    "  return session.hold(Fn.new { 42 })\n"
	    "})\n"
	    "if (result != 42 || session.holdCount != 0) Fiber.abort(\"nested hold result/cleanup\")\n"
	    "var failure = ScriptError.new(41, \"expected lease failure\")\n"
	    "var failed = Fiber.new { session.hold(Fn.new { Fiber.abort(failure) }) }\n"
	    "failed.try()\n"
	    "if (failed.error != failure || session.holdCount != 0) Fiber.abort(\"failed hold cleanup\")\n"
	    "var worker = Fiber.new {\n"
	    "  session.hold(Fn.new {\n"
	    "    Fiber.yield(\"held across yield\")\n"
	    "    Fiber.abort(failure)\n"
	    "  })\n"
	    "}\n"
	    "if (worker.try() != \"held across yield\" || session.holdCount != 1) Fiber.abort(\"yielded hold\")\n"
	    ) == 0);
	CHECK(st_wren_connected_auxiliary_active(connected));
	CHECK(st_wren_connected_interpret(connected, "session_lease_contract",
	    "worker.try()\n"
	    "if (worker.error != failure || session.holdCount != 0) Fiber.abort(\"resumed abort hold cleanup\")\n"
	    "var bad = Fiber.new { session.hold(null) }\n"
	    "bad.try()\n"
	    "if (bad.error == null || session.holdCount != 0) Fiber.abort(\"invalid hold callback\")\n"
	    ) == 0);
	CHECK(!st_wren_connected_auxiliary_active(connected));
	CHECK(st_wren_connected_interpret(connected, "session_lease_contract",
	    "var retained = session.hold()\n"
	    "var parked = Fiber.new { session.hold(Fn.new { Fiber.yield() }) }\n"
	    "parked.call()\n"
	    "if (session.holdCount != 2) Fiber.abort(\"shutdown lease setup\")\n"
	    "Hook.onDisconnect {\n"
	    "  if (Session.current != null || session.holdCount != 0 || !retained.released) Fiber.abort(\"shutdown holds\")\n"
	    "  var late = Fiber.new { session.hold() }\n"
	    "  late.try()\n"
	    "  if (late.error == null) Fiber.abort(\"shutdown reacquired hold\")\n"
	    "  var size = Screen.size\n"
	    "  Screen.window.setBounds([1, 1, size[0], size[1]])\n"
	    "  Screen.window.setPosition([1, 1])\n"
	    "  Screen.window.print(\"LEASE-END\")\n"
	    "}\n"
	    ) == 0);
	CHECK(st_wren_connected_auxiliary_active(connected));
	st_wren_connected_destroy(connected);
	CHECK(console_has_text(console, 0, 0, "LEASE-END"));
	CHECK(st_console_snapshot_restore(console, snapshot, NULL) == 0);
	st_console_snapshot_destroy(snapshot);
	return 0;
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
	CHECK(st_wren_connected_interpret(connected, "operation_contract",
	    "import \"syncterm\" for Timer, Operation, OperationError, Wake\n"
	    "class OperationProbe {\n"
	    "  static events { __events }\n"
	    "  static events=(v) { __events = v }\n"
	    "  static waiter { __waiter }\n"
	    "  static waiter=(v) { __waiter = v }\n"
	    "  static pending { __pending }\n"
	    "  static pending=(v) { __pending = v }\n"
	    "}\n"
	    "OperationProbe.events = []\n"
	    "var first = Timer.after(0)\n"
	    "var second = Timer.after(0)\n"
	    "if (!(first is Operation) || first.isDone || first == second) Fiber.abort(\"operation identity\")\n"
	    "var pending = Timer.after(60000)\n"
	    "OperationProbe.pending = pending\n"
	    "OperationProbe.waiter = Fiber.new {\n"
	    "  if (first.await() != null || second.await() != null) Fiber.abort(\"timer completion\")\n"
	    "  OperationProbe.events.add(\"timers\")\n"
	    "  OperationProbe.events.add(Fiber.yield())\n"
	    "  OperationProbe.events.add(Fiber.yield())\n"
	    "}\n"
	    "OperationProbe.waiter.call()\n"
	    "Wake.post(OperationProbe.waiter, \"unrelated\")\n"
	    "Wake.post(OperationProbe.waiter, first)\n"
	    "var cancelled = Timer.after(60000)\n"
	    "if (!cancelled.cancel() || cancelled.cancel() || !cancelled.isDone || !cancelled.cancelled) Fiber.abort(\"cancel state\")\n"
	    "if (!(cancelled.await() is OperationError)) Fiber.abort(\"cancel result\")\n"
	    "for (bad in [-1, 0.5, 4294967296, \"delay\"]) {\n"
	    "  var f = Fiber.new { Timer.after(bad) }\n"
	    "  f.try()\n"
	    "  if (f.error == null) Fiber.abort(\"invalid delay accepted\")\n"
	    "}\n"
	    ) == 0);
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "operation_verify",
	    "import \"operation_contract\" for OperationProbe, first\n"
	    "import \"syncterm\" for Timer\n"
	    "if (OperationProbe.events.count != 3 || OperationProbe.events[2] != first || OperationProbe.events[0] != \"timers\" || OperationProbe.events[1] != \"unrelated\") Fiber.abort(\"unrelated wake stole a completion\")\n"
	    "OperationProbe.pending.cancel()\n"
	    "var live = []\n"
	    "for (i in 0...32) live.add(Timer.after(60000))\n"
	    "var full = Fiber.new { Timer.after(1) }\n"
	    "full.try()\n"
	    "if (full.error == null) Fiber.abort(\"timer limit\")\n"
	    "for (op in live) op.cancel()\n"
	    "for (i in 0...80) Timer.after(60000).cancel()\n"
	    "System.gc()\n"
	    ) == 0);
	CHECK(st_wren_connected_interpret(connected, "operation_owners",
	    "import \"syncterm\" for Timer, OperationError\n"
	    "var completed = 0\n"
	    "var shared = Timer.after(0)\n"
	    "shared.then(Fn.new {|r| completed = completed + 1 })\n"
	    "shared.then(Fn.new {|r| completed = completed + 1 })\n"
	    "var doomed = Timer.after(60000)\n"
	    "var owner = Fiber.new {\n"
	    "  doomed.await_(Fiber.current)\n"
	    "  Fiber.abort(\"abandoned owner\")\n"
	    "}\n"
	    "owner.try()\n"
	    "var cancelledObserved = false\n"
	    "var cancelled = Timer.after(60000)\n"
	    "cancelled.then(Fn.new {|r| cancelledObserved = r is OperationError })\n"
	    "cancelled.cancel()\n"
	    "for (bad in [null, 12, Fiber.new {}]) {\n"
	    "  var f = Fiber.new { shared.await_(bad) }\n"
	    "  f.try()\n"
	    "  if (f.error == null) Fiber.abort(\"invalid waiter accepted\")\n"
	    "}\n"
	    "var early = Fiber.new { shared.result }\n"
	    "early.try()\n"
	    "if (early.error == null) Fiber.abort(\"pending result accepted\")\n"
	    ) == 0);
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "operation_owners_verify",
	    "import \"operation_owners\" for completed, doomed, cancelledObserved, shared\n"
	    "if (completed != 2 || !doomed.cancelled || !cancelledObserved) Fiber.abort(\"operation ownership\")\n"
	    "var immediate = false\n"
	    "shared.then(Fn.new {|r| immediate = r == null })\n"
	    "if (!immediate) Fiber.abort(\"completed callback\")\n"
	    ) == 0);
	CHECK(st_wren_connected_interpret(connected, "operation_app_quit",
	    "import \"ui_app\" for App\n"
	    "var app = App.new()\n"
	    "var run = Fiber.new { app.run() }\n"
	    "run.call()\n"
	    "if (run.isDone) Fiber.abort(\"app did not park\")\n"
	    "app.quit()\n") == 0);
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "operation_app_quit_verify",
	    "import \"operation_app_quit\" for run\n"
	    "if (!run.isDone || run.error != null) Fiber.abort(\"app did not unwind after quit\")\n") == 0);
	CHECK(st_wren_connected_interpret(connected, "operation_wake_order",
	    "import \"syncterm\" for Timer, Wake\n"
	    "var order = []\n"
	    "var delay = Timer.after(60000)\n"
	    "var worker = Fiber.new {\n"
	    "  delay.await()\n"
	    "  order.add(Fiber.yield())\n"
	    "  order.add(Fiber.yield())\n"
	    "}\n"
	    "worker.call()\n"
	    "Wake.post(worker, \"earlier\")\n"
	    "delay.cancel()\n"
	    "Wake.post(worker, \"later\")\n"
	    ) == 0);
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "operation_wake_order_verify",
	    "import \"operation_wake_order\" for order, worker\n"
	    "if (!worker.isDone || order.count != 2 || order[0] != \"earlier\" || order[1] != \"later\") Fiber.abort(\"operation reordered queued wakes\")\n"
	    ) == 0);
	st_wren_connected_log_transfer(connected, ST_PROFILE_LOG_INFO, "info");
	st_wren_connected_log_transfer(connected, ST_PROFILE_LOG_DEBUG, "debug");
	CHECK(transfer_logs == 1);
	CHECK(st_wren_connected_render_status(connected, true) == 0);
	char *details = st_wren_connected_status_details(connected);
	CHECK(details != NULL && strstr(details, "BBS: Alpha") != NULL);
	free(details);
	CHECK(st_wren_connected_last_error(connected) == NULL);
	CHECK(st_wren_connected_interpret(connected, "live_log_level",
	    "import \"syncterm\" for Host, Session\n"
	    "if (Host.logLevel != Session.current.profile.xferLogLevel || Host.logLevelNames.count != 8 || Host.logLevelNames[7] != \"Debug\") Fiber.abort(\"initial log level\")\n"
	    "Host.logLevel = 99\n"
	    "if (Host.logLevel != 7 || Session.current.profile.xferLogLevel == 7) Fiber.abort(\"clamp/persistence\")\n"
	    "Host.logLevel = -4\n"
	    "if (Host.logLevel != 0) Fiber.abort(\"negative clamp\")\n") == 0);
	st_wren_connected_log_transfer(connected, ST_PROFILE_LOG_INFO, "hidden");
	st_wren_connected_log_transfer(connected, ST_PROFILE_LOG_EMERGENCY, "visible");
	CHECK(transfer_logs == 2);
	CHECK(wrenInterpret(host->vm, "menu_input_isolation",
	    "import \"syncterm\" for Input, KeyEvent\n"
	    "Input.unget(KeyEvent.text(\"é\", 5))\n") == WREN_RESULT_SUCCESS);
	CHECK(wrenInterpret(host->picker_vm, "picker_input_isolation",
	    "import \"syncterm\" for Input, Key, KeyEvent\n"
	    "Input.unget(KeyEvent.new(Key.up, 0x80000003))\n") == WREN_RESULT_SUCCESS);
	CHECK(st_wren_connected_interpret(connected, "connected_input_isolation",
	    "import \"syncterm\" for Input, KeyEvent\n"
	    "Input.unget(KeyEvent.text(\"😀\", 10))\n"
	    "var event = Input.poll()\n"
	    "if (event.key != 0x1f600 || event.text != \"😀\" || event.modifiers != 10) Fiber.abort(\"connected input isolation\")\n") == 0);
	CHECK(wrenInterpret(host->vm, "menu_input_isolation",
	    "var event = Input.poll()\n"
	    "if (event.key != 233 || event.text != \"é\" || event.modifiers != 5) Fiber.abort(\"menu input isolation\")\n") == WREN_RESULT_SUCCESS);
	CHECK(wrenInterpret(host->picker_vm, "picker_input_isolation",
	    "var event = Input.poll()\n"
	    "if (event.key != Key.up || event.codepoint != null || event.modifiers != 0x80000003) Fiber.abort(\"picker input isolation\")\n") == WREN_RESULT_SUCCESS);
	CHECK(st_wren_connected_interpret(connected, "foreign_type_contract",
	    foreign_type_contract) == 0);
	CHECK(st_wren_connected_interpret(connected, "no_sftp_provider",
	    "import \"syncterm\" for SFTP, SFTPError, Host, CTerm, Status, Conn, ConnError, ConnErr, Scrollback, Surface, Session\n"
	    "if (SFTP.available || Session.current.holdCount != 0 || Conn.connected) Fiber.abort(\"unexpected provider\")\n"
	    "if (Conn.sessionLog != null) Fiber.abort(\"unexpected session log provider\")\n"
	    "if (Host.sshPublicKey != null || !(SFTP.start().result is SFTPError)) Fiber.abort(\"unexpected SSH capability\")\n"
	    "if (Session.current.name != \"Alpha\" || Session.current.profile.addr != \"alpha.example\" || Session.current.lastConnected != 100 || Session.current.profile.palette.count > 16 || Session.current.profile.sshFingerprint.bytes.count > 32) Fiber.abort(\"connected profile view\")\n"
	    "if (Scrollback.width != 0 || Scrollback.height != 0 || Scrollback.copyRowsTo(Surface.new(2, 2), 0, 2, 0) != 0) Fiber.abort(\"empty scrollback\")\n"
	    "CTerm.suspended = true\n"
	    "if (!CTerm.suspended) Fiber.abort(\"suspend state\")\n"
	    "CTerm.suspended = false\n"
	    "CTerm.mouseDisabled = true\n"
	    "if (!CTerm.mouseDisabled) Fiber.abort(\"mouse state\")\n"
	    "CTerm.mouseDisabled = false\n"
	    "var sent = Conn.sendText(\"text\").result\n"
	    "if (!(sent is ConnError) || sent.code != ConnErr.notConnected || sent.partial) Fiber.abort(\"text send error\")\n"
	    "sent = Conn.sendBytes(\"raw\").result\n"
	    "if (!(sent is ConnError) || sent.code != ConnErr.notConnected) Fiber.abort(\"raw send error\")\n"
	    "if (Conn.paste() != null) Fiber.abort(\"disconnected paste\")\n")
	    == 0);
	CHECK(st_wren_connected_interpret(connected, "hook_reclamation",
	    "import \"syncterm\" for Hook\n"
	    "for (i in 0...600) {\n"
	    "  var h = Hook.onMatch(\"temporary\", Fn.new {|m| null })\n"
	    "  if (h == null || !h.remove() || h.remove()) Fiber.abort(\"reclaimed registration\")\n"
	    "}\n"
	    "var live = []\n"
	    "for (i in 0...256) {\n"
	    "  var h = Hook.onMatch(\"limit\", Fn.new {|m| null })\n"
	    "  if (h == null) Fiber.abort(\"premature live limit\")\n"
	    "  live.add(h)\n"
	    "}\n"
	    "if (Hook.onMatch(\"overflow\", Fn.new {|m| null }) != null) Fiber.abort(\"missing live limit\")\n"
	    "for (h in live) h.remove()\n"
	    "var otherHook = null\n"
	    "var selfHook = null\n"
	    "selfHook = Hook.onKey(0xfefe, Fn.new {|k|\n"
	    "  if (!selfHook.remove() || !otherHook.remove()) Fiber.abort(\"callback removal\")\n"
	    "  // Registration must not free the active dispatcher's iterator.\n"
	    "  for (i in 0...600) {\n"
	    "    var h = Hook.onMatch(\"nested\", Fn.new {|m| null })\n"
	    "    if (h == null || !h.remove()) Fiber.abort(\"nested registration\")\n"
	    "  }\n"
	    "  System.gc()\n"
	    "  return true\n"
	    "})\n"
	    "otherHook = Hook.onKey(0xfefe, Fn.new {|k| Fiber.abort(\"removed hook fired\") })\n"
	    "System.gc()\n"
	    ) == 0);
	CHECK(st_wren_connected_handle_event(connected, &(struct st_console_event){.type = ST_CONSOLE_EVENT_TEXT, .data.text.codepoint = 0xfefe}, &consumed) == 0 && consumed);
	CHECK(st_wren_connected_interpret(connected, "hook_reclamation_metrics",
	    "import \"hook_reclamation\" for selfHook, otherHook\n"
	    "if (selfHook.callCount != 1 || otherHook.callCount != 0) Fiber.abort(\"removed metrics\")\n"
	    "if (selfHook.remove() || otherHook.remove()) Fiber.abort(\"repeat removal\")\n"
	    "System.gc()\n"
	    ) == 0);
	CHECK(st_wren_connected_interpret(connected, "connected_key_test",
	    "import \"syncterm\" for Hook\n"
	    "class KeyHookState {\n"
	    "  static handle { __hook }\n"
	    "  static handle=(h) { __hook = h }\n"
	    "}\n"
	    "KeyHookState.handle = Hook.onKey(65, Fn.new {|k| k.key == 65 })\n"
	    "Hook.onKey(90, Fn.new {|k| k.key == 90 })\n"
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
	CHECK(st_wren_connected_handle_event(connected, &(struct st_console_event){.type = ST_CONSOLE_EVENT_TEXT, .data.text.codepoint = 66}, &consumed) == 0);
	CHECK(!consumed);
	CHECK(st_wren_connected_handle_event(connected, &(struct st_console_event){.type = ST_CONSOLE_EVENT_PHYSICAL_KEY, .data.physical_key = {.evdev = 24, .pressed = true, .modifiers = command_modifiers(host)}}, &consumed) == 0);
	CHECK(consumed);
	CHECK(st_wren_connected_interpret(connected, "mouse_toggle_on",
	    "import \"syncterm\" for CTerm\n"
	    "if (!CTerm.mouseDisabled) Fiber.abort(\"Alt-O did not disable mouse\")\n") == 0);
	CHECK(st_wren_connected_handle_event(connected, &(struct st_console_event){.type = ST_CONSOLE_EVENT_PHYSICAL_KEY, .data.physical_key = {.evdev = 24, .pressed = true, .modifiers = command_modifiers(host)}}, &consumed) == 0);
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
	    "Hook.onKey(0x11000d, 2, Fn.new {|key| true })\n") == 0);
	const struct st_console_event ctrl_enter = {
		.type = ST_CONSOLE_EVENT_KEY_CODE,
		.data.key = {.code = 13, .modifiers = ST_CONSOLE_MODIFIER_CONTROL},
	};
	CHECK(st_wren_connected_handle_event(connected, &ctrl_enter, &consumed) == 0);
	CHECK(consumed);
	CHECK(st_wren_connected_interpret(connected, "unicode_key_hook",
	    "import \"syncterm\" for Hook\n"
	    "var calls = 0\n"
	    "var hook = Hook.onKey(0x1f600, 15, Fn.new {|event|\n"
	    "  if (event.key != 0x1f600 || event.codepoint != 0x1f600 || event.text != \"😀\" || event.modifiers != 63) Fiber.abort(\"Unicode hook payload\")\n"
	    "  calls = calls + 1\n"
	    "  return true\n"
	    "})\n") == 0);
	struct st_console_event emoji = {.type = ST_CONSOLE_EVENT_TEXT,
	    .data.text = {.codepoint = UINT32_C(0x1f600), .modifiers = 14}};
	CHECK(st_wren_connected_handle_event(connected, &emoji, &consumed) == 0 && !consumed);
	emoji.data.text.modifiers = 63;
	CHECK(st_wren_connected_handle_event(connected, &emoji, &consumed) == 0 && consumed);
	CHECK(st_wren_connected_interpret(connected, "unicode_key_hook",
	    "if (calls != 1 || hook.callCount != 1 || !hook.remove()) Fiber.abort(\"Unicode hook filter\")\n") == 0);
	emoji.data.text.codepoint = UINT32_C(0xd800);
	CHECK(st_wren_connected_handle_event(connected, &emoji, &consumed)
	    == IO_ERROR_INVALID_ENCODING && !consumed);
	emoji.data.text.codepoint = UINT32_C(0x110000);
	CHECK(st_wren_connected_handle_event(connected, &emoji, &consumed)
	    == IO_ERROR_INVALID_ENCODING && !consumed);
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
			.modifiers = command_modifiers(host),
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
			.modifiers = command_modifiers(host),
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
	CHECK(st_wren_connected_handle_event(connected, &(struct st_console_event){.type = ST_CONSOLE_EVENT_TEXT, .data.text.codepoint = 65}, &consumed) == 0);
	CHECK(consumed);
	CHECK(st_wren_connected_interpret(connected, "connected_key_remove",
	    "import \"connected_key_test\" for KeyHookState\n"
	    "var h = KeyHookState.handle\n"
	    "if (h.callCount != 1 || h.totalRuntime < 0) Fiber.abort(\"hook metrics\")\n"
	    "if (!h.remove() || h.remove() || h.callCount != 1) Fiber.abort(\"hook removal\")\n") == 0);
	CHECK(st_wren_connected_handle_event(connected, &(struct st_console_event){.type = ST_CONSOLE_EVENT_TEXT, .data.text.codepoint = 65}, &consumed) == 0);
	CHECK(!consumed);
	CHECK(st_wren_connected_interpret(connected, "status_render_test",
	    "import \"syncterm\" for Status\n"
	    "if (!Status.enabled) Fiber.abort(\"status disabled\")\n"
	    "Status.callable = Fn.new {|s|\n"
	    "  s[0].ch = \"A\"\n"
	    "  s[1].ch = \"B\"\n"
	    "  s[2].ch = \"C\"\n"
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
	CHECK(st_wren_connected_interpret(connected, "connected_unknown_foreign",
	    "class Unknown { foreign static missing() }\n") == IO_ERROR_INVALID_FORMAT);
	CHECK(st_wren_connected_interpret(connected, "connected_forbidden_menu",
	    "import \"syncterm_menu\" for Menu\n") == IO_ERROR_INVALID_FORMAT);
	char *operation_cache = NULL;
	char operation_shutdown[1024];
	CHECK(st_bbs_cache_path_for_name(&operation_cache,
	    ((struct st_wren_host *)host)->config.paths->cache_directory, profile->name) == 0);
	CHECK(snprintf(operation_shutdown, sizeof(operation_shutdown), "%s/operation-shutdown", operation_cache)
	    < (int)sizeof(operation_shutdown));
	free(operation_cache);
	CHECK(st_wren_connected_interpret(connected, "operation_shutdown",
	    "import \"syncterm\" for Timer, Hook, Cache\n"
	    "if (Cache.replace(\"operation-shutdown\", \"armed\") != null) Fiber.abort(\"shutdown marker\")\n"
	    "var pending = Timer.after(60000)\n"
	    "pending.then(Fn.new {|result| Cache.replace(\"operation-shutdown\", \"resumed\") })\n"
	    "Hook.onDisconnect(Fn.new {\n"
	    "  Cache.replace(\"operation-shutdown\", pending.cancelled ? \"cancelled\" : \"pending\")\n"
	    "})\n") == 0);
	CHECK(file_equals(operation_shutdown, "armed"));
	st_wren_connected_destroy(connected);
	CHECK(file_equals(operation_shutdown, "cancelled"));
	CHECK(fs_remove_file(operation_shutdown) == 0);
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
	CHECK(st_wren_connected_handle_event(connected, &(struct st_console_event){.type = ST_CONSOLE_EVENT_PHYSICAL_KEY, .data.physical_key = {.evdev = 47, .pressed = true, .modifiers = command_modifiers(host)}}, &claimed) == 0 && claimed);
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
	CHECK(st_wren_connected_handle_event(connected, &(struct st_console_event){.type = ST_CONSOLE_EVENT_PHYSICAL_KEY, .data.physical_key = {.evdev = 44, .pressed = true, .modifiers = command_modifiers(host)}}, &claimed) == 0 && claimed);
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
	CHECK(st_wren_connected_handle_event(connected, &(struct st_console_event){.type = ST_CONSOLE_EVENT_PHYSICAL_KEY, .data.physical_key = {.evdev = 47, .pressed = true, .modifiers = command_modifiers(host)}}, &claimed) == 0 && claimed);
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
exercise_app_lifecycle(st_wren_host_t *host, st_console_t *console,
	const struct st_profile *profile)
{
	const struct st_wren_connected_config config = {
		.host = host, .console = console, .profile = profile,
	};
	st_wren_connected_t *connected = NULL;
	st_console_snapshot_t *snapshot = NULL;

	CHECK(st_console_snapshot_create(&snapshot, console) == 0);
	CHECK(st_wren_connected_create(&connected, &config) == 0);
	CHECK(st_wren_connected_interpret(connected, "app_lifecycle",
	    "import \"syncterm\" for Input, Screen, CustomCursor, Timer, Hook, Cleanup_, Key, KeyEvent\n"
	    "import \"ui_app\" for App\n"
	    "import \"ui_widget\" for Widget, Rect\n"
	    "var mouse = Input.mouseEvents\n"
	    "var cursor = CustomCursor.current\n"
	    "var attr = Screen.attr\n"
	    "var position = Screen.window.position\n"
	    "var error = [\"lifecycle error\"]\n"
	    "var Check = Fn.new {|app|\n"
	    "    if (app.running || Input.mouseEvents != mouse || Screen.attr != attr || CustomCursor.visible != cursor.visible || CustomCursor.blink != cursor.blink || CustomCursor.startLine != cursor.startLine || CustomCursor.endLine != cursor.endLine || CustomCursor.range != cursor.range || Screen.window.position[0] != position[0] || Screen.window.position[1] != position[1]) Fiber.abort(\"App state not restored\")\n"
	    "}\n"
	    "var sync = App.new()\n"
	    "sync.onLayout = Fn.new {|w, h|\n"
	    "  Screen.setAttr(71)\n"
	    "  CustomCursor.setVisible(!cursor.visible)\n"
	    "  Screen.window.setPosition([position[0] == 1 ? 2 : 1, 1])\n"
	    "  Fiber.abort(error)\n"
	    "}\n"
	    "var failed = Fiber.new { sync.runSync() }\n"
	    "failed.try()\n"
	    "if (failed.error != error) Fiber.abort(\"sync layout error identity\")\n"
	    "Check.call(sync)\n"
	    "var syncHandler = App.new()\n"
	    "syncHandler.bind(Key.f12, Fn.new {|key| Fiber.abort(\"handler error\") })\n"
	    "syncHandler.onError = Fn.new {|failed| Fiber.abort(error) }\n"
	    "Input.unget(KeyEvent.new(Key.f12))\n"
	    "failed = Fiber.new { syncHandler.runSync() }\n"
	    "failed.try()\n"
	    "if (failed.error != error) Fiber.abort(\"sync reporter error identity\")\n"
	    "Check.call(syncHandler)\n"
	    "var normal = App.new()\n"
	    "normal.onLayout = Fn.new {|w, h| normal.quit() }\n"
	    "normal.runSync()\n"
	    "Check.call(normal)\n"
	    "var app = App.new()\n"
	    "var done = false\n"
	    "app.onPost = Fn.new {|value| Fiber.abort(error) }\n"
	    "Fiber.new {\n"
	    "  var run = Fiber.new { app.run() }\n"
	    "  Cleanup_.try_(run)\n"
	    "  if (run.error != error) Fiber.abort(\"async handler error identity\")\n"
	    "  Check.call(app)\n"
	    "  done = true\n"
	    "}.call()\n"
	    "app.post(\"fail\")\n") == 0);
	for (unsigned i = 0; i < 4; ++i)
		CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "app_lifecycle",
	    "if (!done) Fiber.abort(\"async failure did not complete\")\n"
	    "var success = App.new()\n"
	    "var successDone = false\n"
	    "var pings = 0\n"
	    "success.onPost = Fn.new {|value|\n"
	    "  if (value == \"ping\") { pings = pings + 1 } else {\n"
	    "    var answer = success.runChild(Fn.new {\n"
	    "      Timer.after(0).await()\n"
	    "      return 42\n"
	    "    })\n"
	    "    if (answer != 42 || pings != 1) Fiber.abort(\"child result/event dispatch\")\n"
	    "    success.quit()\n"
	    "  }\n"
	    "}\n"
	    "Fiber.new {\n"
	    "  success.run()\n"
	    "  Check.call(success)\n"
	    "  successDone = true\n"
	    "}.call()\n"
	    "success.post(\"start\")\n"
	    "success.post(\"ping\")\n") == 0);
	for (unsigned i = 0; i < 8; ++i)
		CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "app_lifecycle",
	    "if (!successDone) Fiber.abort(\"runChild completion did not wake App\")\n"
	    "var childApp = App.new()\n"
	    "var childDone = false\n"
	    "childApp.onPost = Fn.new {|value|\n"
	    "  childApp.runChild(Fn.new {\n"
	    "    Timer.after(0).await()\n"
	    "    Fiber.abort(error)\n"
	    "  })\n"
	    "  Fiber.abort(\"child failure was swallowed\")\n"
	    "}\n"
	    "Fiber.new {\n"
	    "  var run = Fiber.new { childApp.run() }\n"
	    "  Cleanup_.try_(run)\n"
	    "  if (run.error != error) Fiber.abort(\"deferred child error identity\")\n"
	    "  Check.call(childApp)\n"
	    "  childDone = true\n"
	    "}.call()\n"
	    "childApp.post(\"fail child\")\n") == 0);
	for (unsigned i = 0; i < 8; ++i)
		CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "app_lifecycle",
	    "if (!childDone) Fiber.abort(\"child error not propagated\")\n"
	    "var stopping = App.new()\n"
	    "var stopped = false\n"
	    "var delay = null\n"
	    "var resumed = false\n"
	    "var closed = 0\n"
	    "stopping.onPost = Fn.new {|value|\n"
	    "  if (value == \"stop\") { stopping.quit() } else {\n"
	    "    stopping.runChild(Fn.new {\n"
	    "      Cleanup_.run(Fn.new {\n"
	    "        delay = Timer.after(60000)\n"
	    "        delay.await()\n"
	    "        resumed = true\n"
	    "      }, Fn.new { closed = closed + 1 })\n"
	    "    })\n"
	    "  }\n"
	    "}\n"
	    "Fiber.new {\n"
	    "  var run = Fiber.new { stopping.run() }\n"
	    "  Cleanup_.try_(run)\n"
	    "  if (run.error != \"App stopped before child completed\") Fiber.abort(\"child stop error\")\n"
	    "  Check.call(stopping)\n"
	    "  stopped = true\n"
	    "}.call()\n"
	    "stopping.post(\"start\")\n"
	    "stopping.post(\"stop\")\n") == 0);
	for (unsigned i = 0; i < 8; ++i)
		CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "app_lifecycle",
	    "if (!stopped || delay == null || !delay.cancelled || resumed || closed != 1) Fiber.abort(\"stopped child ownership\")\n"
	    "var boundary = App.new()\n"
	    "boundary.tickMs = 60000\n"
	    "var repeatOwner = Widget.new()\n"
	    "repeatOwner.bounds = Rect.new(1, 1, 1, 1)\n"
	    "boundary.root.add(repeatOwner)\n"
	    "var repeatCount = 0\n"
	    "var boundaryDone = false\n"
	    "boundary.onPost = Fn.new {|value|\n"
	    "  var executed = false\n"
	    "  var wrong = Fiber.new { boundary.runChild(Fn.new { executed = true }) }\n"
	    "  wrong.try()\n"
	    "  if (wrong.error == null || executed) Fiber.abort(\"foreign child caller accepted\")\n"
	    "  var reentry = Fiber.new { boundary.run() }\n"
	    "  reentry.try()\n"
	    "  if (reentry.error != \"App is already running\" || !boundary.running) Fiber.abort(\"App reentry state\")\n"
	    "  var immediate = Fiber.new { boundary.runChild(Fn.new { Fiber.abort(error) }) }\n"
	    "  Cleanup_.try_(immediate)\n"
	    "  if (immediate.error != error || !boundary.running) Fiber.abort(\"immediate child failure\")\n"
	    "  var inlineValue = boundary.runChild(Fn.new { 43 })\n"
	    "  if (inlineValue != 43) Fiber.abort(\"immediate child value\")\n"
	    "  var oldMouse = Input.mouseEvents\n"
	    "  var oldCursor = CustomCursor.current\n"
	    "  var inner = App.new()\n"
	    "  inner.onLayout = Fn.new {|w, h| inner.quit() }\n"
	    "  inner.runSync()\n"
	    "  if (inner.running || !boundary.running || Input.mouseEvents != oldMouse || CustomCursor.visible != oldCursor.visible || CustomCursor.startLine != oldCursor.startLine) Fiber.abort(\"nested App restoration\")\n"
	    "  boundary.startMouseRepeat_(repeatOwner, Fn.new { repeatCount = repeatCount + 1 })\n"
	    "  boundary.quit()\n"
	    "}\n"
	    "Fiber.new {\n"
	    "  boundary.run()\n"
	    "  Check.call(boundary)\n"
	    "  boundaryDone = true\n"
	    "}.call()\n"
	    "boundary.post(\"boundaries\")\n") == 0);
	for (unsigned i = 0; i < 4; ++i)
		CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "app_lifecycle",
	    "if (!boundaryDone || repeatCount != 0) Fiber.abort(\"boundary App did not stop\")\n"
	    "var rerunDone = false\n"
	    "boundary.onPost = Fn.new {|value| boundary.quit() }\n"
	    "Fiber.new {\n"
	    "  boundary.run()\n"
	    "  Check.call(boundary)\n"
	    "  rerunDone = true\n"
	    "}.call()\n"
	    "boundary.post(\"stop again\")\n") == 0);
	for (unsigned i = 0; i < 4; ++i)
		CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "app_lifecycle",
	    "if (!rerunDone) Fiber.abort(\"App could not run again\")\n"
	    "var timers = []\n"
	    "for (i in 0...32) {\n"
	    "  var timer = Timer.after(60000)\n"
	    "  if (timer.isDone) Fiber.abort(\"App leaked a repeat/tick timer\")\n"
	    "  timers.add(timer)\n"
	    "}\n"
	    "for (timer in timers) timer.cancel()\n"
	    "class TickFailure is App {\n"
	    "  construct new(error) {\n"
	    "    super()\n"
	    "    _error = error\n"
	    "    tickMs = 0\n"
	    "  }\n"
	    "  onTick_() { Fiber.abort(_error) }\n"
	    "}\n"
	    "var ticking = TickFailure.new(error)\n"
	    "var tickDone = false\n"
	    "Fiber.new {\n"
	    "  var run = Fiber.new { ticking.run() }\n"
	    "  Cleanup_.try_(run)\n"
	    "  if (run.error != error) Fiber.abort(\"tick error identity\")\n"
	    "  Check.call(ticking)\n"
	    "  tickDone = true\n"
	    "}.call()\n") == 0);
	for (unsigned i = 0; i < 8; ++i)
		CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "app_lifecycle",
	    "if (!tickDone) Fiber.abort(\"tick failure not cleaned up\")\n"
	    "var modalApp = App.new()\n"
	    "var modalReturned = false\n"
	    "var modalDone = false\n"
	    "modalApp.onPost = Fn.new {|value|\n"
	    "  if (value == \"stop\") { modalApp.quit() } else {\n"
	    "    var pane = Widget.new()\n"
	    "    pane.bounds = Rect.new(1, 1, 5, 3)\n"
	    "    modalApp.modal(pane)\n"
	    "    modalReturned = true\n"
	    "  }\n"
	    "}\n"
	    "Fiber.new {\n"
	    "  modalApp.run()\n"
	    "  Check.call(modalApp)\n"
	    "  modalDone = true\n"
	    "}.call()\n"
	    "modalApp.post(\"open\")\n"
	    "modalApp.post(\"stop\")\n") == 0);
	for (unsigned i = 0; i < 4; ++i)
		CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "app_lifecycle",
	    "if (!modalDone || !modalReturned) Fiber.abort(\"quit did not leave modal loop\")\n"
	    "var shutdown = App.new()\n"
	    "var shutdownDelay = null\n"
	    "var shutdownClosed = 0\n"
	    "var shutdownResumed = false\n"
	    "shutdown.onPost = Fn.new {|value|\n"
	    "  shutdown.runChild(Fn.new {\n"
	    "    Cleanup_.run(Fn.new {\n"
	    "      shutdownDelay = Timer.after(60000)\n"
	    "      shutdownDelay.await()\n"
	    "      shutdownResumed = true\n"
	    "      Fiber.abort(\"shutdown resumed user body\")\n"
	    "    }, Fn.new { shutdownClosed = shutdownClosed + 1 })\n"
	    "  })\n"
	    "}\n"
	    "Fiber.new { shutdown.run() }.call()\n"
	    "shutdown.post(\"pending\")\n") == 0);
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "app_lifecycle",
	    "Hook.onDisconnect {\n"
	    "  Check.call(shutdown)\n"
	    "  var ok = shutdownDelay != null && shutdownDelay.cancelled && shutdownClosed == 1 && !shutdownResumed\n"
	    "  Screen.window.setPosition([1, 1])\n"
	    "  Screen.window.print(ok ? \"+\" : \"-\")\n"
	    "}\n") == 0);
	st_wren_connected_destroy(connected);
	CHECK(console_has_text(console, 0, 0, "+"));
	/* Check the menu VM's native claim list directly: no input dispatch or
	 * GC can prune a stale claim and conceal missing lifecycle cleanup. */
	for (unsigned failing = 0; failing < 2; ++failing) {
		const char *module = failing ? "menu_app_error" : "menu_app_quit";
		CHECK(host->menu.claim_top == NULL);
		CHECK(wrenInterpret(host->vm, module,
		    "import \"syncterm\" for Cleanup_\n"
		    "import \"ui_app\" for App\n"
		    "var app = App.new()\n"
		    "var error = [\"menu App error\"]\n"
		    "var expected = null\n"
		    "var done = false\n"
		    "app.onPost = Fn.new {|command|\n"
		    "  if (command == \"stop\") { app.quit() } else { Fiber.abort(error) }\n"
		    "}\n"
		    "var run = Fiber.new { app.run() }\n"
		    "Fiber.new {\n"
		    "  Cleanup_.try_(run)\n"
		    "  if (run.error != expected) Fiber.abort(\"menu App result\")\n"
		    "  done = true\n"
		    "}.call()\n") == WREN_RESULT_SUCCESS);
		CHECK(host->menu.claim_top != NULL);
		CHECK(wrenInterpret(host->vm, module, failing
		    ? "expected = error\napp.post(\"fail\")\n"
		    : "app.post(\"stop\")\n") == WREN_RESULT_SUCCESS);
		CHECK(st_wren_wake_drain(host->vm) == 0);
		CHECK(host->menu.claim_top == NULL);
		CHECK(wrenInterpret(host->vm, module,
		    "if (!done || app.running) Fiber.abort(\"menu App did not finish\")\n")
		    == WREN_RESULT_SUCCESS);
	}
	CHECK(st_console_snapshot_restore(console, snapshot, NULL) == 0);
	st_console_snapshot_destroy(snapshot);
	return 0;
}

static int
exercise_cleanup_scopes(st_wren_host_t *host, st_console_t *console,
	const struct st_profile *profile)
{
	st_wren_connected_t *connected = NULL;
	st_console_snapshot_t *snapshot = NULL;
	const struct st_wren_connected_config config = {
		.host = host, .console = console, .profile = profile,
	};

	CHECK(st_console_snapshot_create(&snapshot, console) == 0);
	CHECK(st_wren_connected_create(&connected, &config) == 0);
	CHECK(st_wren_connected_interpret(connected, "ordinary_fiber_in_host",
	    "import \"syncterm\" for Cleanup_, Timer, Wake\n"
	    "var restored = false\n"
	    "var value = Cleanup_.run(Fn.new {\n"
	    "  var parent = Fiber.current\n"
	    "  var child = Fiber.new {\n"
	    "    var received = parent.transfer(13)\n"
	    "    return received\n"
	    "  }\n"
	    "  if (child.transfer() != 13 || child.call(17) != 17) Fiber.abort(\"ordinary transfer inside host callback\")\n"
	    "  return 19\n"
	    "}, Fn.new { restored = true })\n"
	    "if (value != 19 || !restored) Fiber.abort(\"host wrapper return\")\n"
	    "var cancelOperation = Timer.after(100000)\n"
	    "var cancelReceiver = null\n"
	    "var cancelResumed = false\n"
	    "var cancelRestores = 0\n"
	    "var cancelTask = Fiber.new {\n"
	    "  Cleanup_.run(Fn.new {\n"
	    "    cancelReceiver = Fiber.current\n"
	    "    cancelOperation.await()\n"
	    "    cancelResumed = true\n"
	    "  }, Fn.new { cancelRestores = cancelRestores + 1 })\n"
	    "}\n"
	    "cancelTask.call()\n"
	    "Wake.post(cancelReceiver, \"queued before cancellation\")\n"
	    "cancelOperation.cancel()\n"
	    "Cleanup_.abort_(cancelTask, \"cancelled host callback\")\n"
	    "if (cancelTask.isDone || cancelResumed || cancelRestores != 1) Fiber.abort(\"host cancellation changed Fiber lifecycle\")\n"
	    "System.gc()\n") == 0);
	for (unsigned i = 0; i < 4; ++i)
		CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "ordinary_fiber_in_host",
	    "if (cancelResumed || cancelRestores != 1) Fiber.abort(\"cancelled native wait resumed callback\")\n")
	    == 0);
	CHECK(st_wren_connected_interpret(connected, "cleanup_scopes",
	    "import \"syncterm\" for Screen, CTerm, CustomCursor, VideoFlags, Timer, Hook, Cleanup_, Wake\n"
	    "var attr = Screen.attr\n"
	    "var cursor = CustomCursor.current\n"
	    "var flags = VideoFlags.current\n"
	    "var fonts = (0..4).map {|i| Screen.font[i] }.toList\n"
	    "var error = [\"original error\"]\n"
	    "var outer = Fiber.new {\n"
	    "  Screen.modalRun(Fn.new {\n"
	    "    if (!CTerm.suspended) Fiber.abort(\"modal not suspended\")\n"
	    "    Screen.setAttr(19)\n"
	    "    CustomCursor.preserve(Fn.new {\n"
	    "      CustomCursor.setVisible(!cursor.visible)\n"
	    "      VideoFlags.preserve(Fn.new {\n"
	    "        VideoFlags.setNoBlink(!flags.noBlink)\n"
	    "        Screen.modalRun(Fn.new {\n"
	    "          Screen.setAttr(71)\n"
	    "          Fiber.abort(error)\n"
	    "        })\n"
	    "      })\n"
	    "    })\n"
	    "  })\n"
	    "}\n"
	    "outer.try()\n"
	    "if (outer.error != error || CTerm.suspended || Screen.attr != attr || CustomCursor.visible != cursor.visible || VideoFlags.noBlink != flags.noBlink) Fiber.abort(\"abort cleanup\")\n"
	    "for (i in 0..4) if (Screen.font[i] != fonts[i]) Fiber.abort(\"font cleanup\")\n"
	    "if (CustomCursor.preserve(Fn.new { 17 }) != 17 || VideoFlags.preserve(Fn.new { 19 }) != 19 || Screen.modalRun(Fn.new { 23 }) != 23) Fiber.abort(\"normal return\")\n"
	    "CTerm.suspended = true\n"
	    "Screen.modalRun(Fn.new { Screen.setAttr(29) })\n"
	    "if (!CTerm.suspended || Screen.attr != attr) Fiber.abort(\"previous suspension\")\n"
	    "CTerm.suspended = false\n"
	    "var paused = Fiber.new {\n"
	    "  return Screen.modalRun(Fn.new {\n"
	    "    Screen.setAttr(31)\n"
	    "    return Fiber.yield(\"parked\")\n"
	    "  })\n"
	    "}\n"
	    "if (paused.call() != \"parked\" || !CTerm.suspended || Screen.attr != 31) Fiber.abort(\"yield modal\")\n"
	    "if (paused.call(37) != 37 || CTerm.suspended || Screen.attr != attr) Fiber.abort(\"resume modal\")\n"
	    "var rejected = Fiber.new {\n"
	    "  Screen.update(Fn.new {\n"
	    "    Screen.modalRun(Fn.new {\n"
	    "      Screen.setAttr(71)\n"
	    "      Fiber.yield()\n"
	    "    })\n"
	    "  })\n"
	    "}\n"
	    "rejected.try()\n"
	    "if (rejected.error != \"Screen.update callback must not yield\" || CTerm.suspended || Screen.attr != attr) Fiber.abort(\"rejected yield cleanup\")\n"
	    "var asyncDone = false\n"
	    "Fiber.new {\n"
	    "  var body = Fiber.new {\n"
	    "    Screen.modalRun(Fn.new {\n"
	    "      CustomCursor.preserve(Fn.new {\n"
	    "        CustomCursor.setVisible(!cursor.visible)\n"
	    "        VideoFlags.preserve(Fn.new {\n"
	    "          VideoFlags.setNoBlink(!flags.noBlink)\n"
	    "          Timer.after(0).await()\n"
	    "          Fiber.abort(error)\n"
	    "        })\n"
	    "      })\n"
	    "    })\n"
	    "  }\n"
	    "  Cleanup_.try_(body)\n"
	    "  if (body.error != error || CTerm.suspended || CustomCursor.visible != cursor.visible || VideoFlags.noBlink != flags.noBlink) Fiber.abort(\"async abort cleanup\")\n"
	    "  asyncDone = true\n"
	    "}.call()\n") == 0);
	for (unsigned i = 0; i < 4; ++i)
		CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "cleanup_scopes",
	    "if (!asyncDone) Fiber.abort(\"async cleanup not resumed\")\n"
	    "var messages = []\n"
	    "var inner = null\n"
	    "var timer = Timer.after(0)\n"
	    "var receiver = Fiber.new {\n"
	    "  Screen.modalRun(Fn.new {\n"
	    "    inner = Fiber.current\n"
	    "    timer.await()\n"
	    "    messages.add(Fiber.yield())\n"
	    "    messages.add(Fiber.yield())\n"
	    "  })\n"
	    "}\n"
	    "receiver.call()\n"
	    "Wake.post(receiver, \"first\")\n"
	    "Wake.post(inner, \"second\")\n") == 0);
	for (unsigned i = 0; i < 4; ++i)
		CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "cleanup_scopes",
	    "if (!receiver.isDone || messages.join(\",\") != \"first,second\" || CTerm.suspended) Fiber.abort(\"scoped await wake ordering\")\n"
	    "var count = 0\n"
	    "var failed = Fiber.new {\n"
	    "  Cleanup_.run(Fn.new { Fiber.abort(error) }, Fn.new {\n"
	    "    count = count + 1\n"
	    "    Fiber.abort(\"cleanup failure\")\n"
	    "  })\n"
	    "}\n"
	    "failed.try()\n"
	    "if (failed.error != error || count != 1) Fiber.abort(\"original error priority\")\n"
	    "var closed = []\n"
	    "Fiber.new {\n"
	    "  Cleanup_.run(Fn.new {\n"
	    "    Screen.modalRun(Fn.new {\n"
	    "      Screen.setAttr(71)\n"
	    "      CustomCursor.preserve(Fn.new {\n"
	    "        CustomCursor.setVisible(!cursor.visible)\n"
	    "        VideoFlags.preserve(Fn.new {\n"
	    "          VideoFlags.setNoBlink(!flags.noBlink)\n"
	    "          Cleanup_.run(Fn.new {\n"
	    "            Timer.after(60000).await()\n"
	    "            Fiber.abort(\"shutdown resumed parked body\")\n"
	    "          }, Fn.new {\n"
	    "            closed.add(\"inner\")\n"
	    "            Fiber.abort(\"expected shutdown cleanup failure\")\n"
	    "          })\n"
	    "        })\n"
	    "      })\n"
	    "    })\n"
	    "  }, Fn.new { closed.add(\"outer\") })\n"
	    "}.call()\n"
	    "System.gc()\n"
	    "Hook.onDisconnect {\n"
	    "  var ok = closed.join(\",\") == \"inner,outer\" && !CTerm.suspended && Screen.attr == attr && CustomCursor.visible == cursor.visible && VideoFlags.noBlink == flags.noBlink\n"
	    "  Screen.window.setPosition([1, 1])\n"
	    "  Screen.window.print(ok ? \"+\" : \"-\")\n"
	    "}\n") == 0);
	st_wren_connected_destroy(connected);
	CHECK(console_has_text(console, 0, 0, "+"));
	CHECK(st_console_snapshot_restore(console, snapshot, NULL) == 0);
	st_console_snapshot_destroy(snapshot);
	return 0;
}

static int exercise_screen_connected_results(st_wren_connected_t *connected);
static int exercise_clipboard_url_capabilities(WrenVM *vm,
	st_wren_connected_t *connected, st_console_t *console);

static int
exercise_cterm_results(st_wren_connected_t *connected, st_console_t *console)
{
	CHECK(st_wren_connected_interpret(connected, "cterm_result_contract",
	    "import \"syncterm\" for CTerm, CTermError, Error, IOErr, Key, KeyEvent\n"
	    "var original = \"e\\u0301\"\n"
	    "var result = null\n"
	    "var savedWrite = null\n"
	    "var savedKey = null\n"
	    "var invalid = null\n") == 0);
	CHECK(st_wren_connected_interpret(connected, "cterm_result_contract",
	    "var savedReset = null\n") == 0);
	const int resets[] = {IO_ERROR_OUT_OF_MEMORY, IO_ERROR_IO, -271828, 271828};
	st_wren_test_cterm_reset_override = true;
	for (size_t i = 0; i < sizeof(resets) / sizeof(resets[0]); ++i) {
		char source[768];
		st_wren_test_cterm_reset_status = resets[i];
		CHECK(snprintf(source, sizeof(source),
		    "result = CTerm.reset()\n"
		    "if (!(result is CTermError) || !(result is Error) || result.code != %d || result.consumedBytes != null || result.handled != null) Fiber.abort(\"reset diagnostics lost\")\n"
		    "if (!result.message.contains(\"CTerm.reset\") || !result.toString.startsWith(\"CTermError\")) Fiber.abort(\"reset diagnostic text\")\n"
		    "savedReset = result\n", resets[i]) < (int)sizeof(source));
		unsigned before_reset = st_wren_test_cterm_reset_calls;
		CHECK(st_wren_connected_interpret(connected, "cterm_result_contract", source) == 0);
		CHECK(st_wren_test_cterm_reset_calls == before_reset + 1);
	}
	st_wren_test_cterm_reset_override = false;
	CHECK(st_wren_connected_interpret(connected, "cterm_result_contract",
	    "if (CTerm.reset() != null) Fiber.abort(\"native reset success\")\n"
	    "for (mutator in [Fn.new { savedReset.code = 0 }, Fn.new { savedReset.message = \"other\" }, Fn.new { savedReset.consumedBytes = 0 }, Fn.new { savedReset.handled = false }]) {\n"
	    "  var rejected = Fiber.new { mutator.call() }\n"
	    "  rejected.try()\n"
	    "  if (!(rejected.error is String)) Fiber.abort(\"mutable reset error\")\n"
	    "}\nSystem.gc()\n"
	    "if (savedReset.code != 271828 || savedReset.handled != null || savedReset.consumedBytes != null) Fiber.abort(\"reset error lifetime\")\n") == 0);
	const int codes[] = {IO_ERROR_NO_SPACE, IO_ERROR_IO, IO_ERROR_TIMED_OUT, 12345};
	const size_t counts[] = {0, 1, 2, 0};
	st_wren_test_cterm_write_override = true;
	for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i) {
		char source[1024];
		st_wren_test_cterm_write_status = codes[i];
		st_wren_test_cterm_write_consumed = counts[i];
		CHECK(snprintf(source, sizeof(source),
		    "result = CTerm.write(original)\n"
		    "if (!(result is CTermError) || !(result is Error) || result.code != %d || result.consumedBytes != %zu || result.handled != null) Fiber.abort(\"native write diagnostics lost\")\n"
		    "if (original.bytes.count != 3 || result.message.count == 0 || !result.toString.startsWith(\"CTermError\")) Fiber.abort(\"write error text/normalization\")\n"
		    "savedWrite = result\n", codes[i], counts[i]) < (int)sizeof(source));
		unsigned before = st_wren_test_cterm_write_calls;
		CHECK(st_wren_connected_interpret(connected, "cterm_result_contract", source) == 0);
		CHECK(st_wren_test_cterm_write_calls == before + 1 && st_wren_test_cterm_write_size == 2);
	}
	const size_t invalid_counts[] = {3, SIZE_MAX, 1};
	for (size_t i = 0; i < sizeof(invalid_counts) / sizeof(invalid_counts[0]); ++i) {
		st_wren_test_cterm_write_status = i == 2 ? 0 : IO_ERROR_IO;
		st_wren_test_cterm_write_consumed = invalid_counts[i];
		CHECK(st_wren_connected_interpret(connected, "cterm_result_contract",
		    "invalid = Fiber.new { CTerm.write(original) }\n"
		    "invalid.try()\n"
		    "if (!(invalid.error is String) || !invalid.error.contains(\"consumed count\")) Fiber.abort(\"invalid native count exposed\")\n") == 0);
	}
	st_wren_test_cterm_write_override = false;
	unsigned before = st_wren_test_cterm_write_calls;
	CHECK(st_wren_connected_interpret(connected, "cterm_result_contract",
	    "for (value in [null, 3, String.fromByte(255), savedWrite]) {\n"
	    "  var invalid = Fiber.new { CTerm.write(value) }\n"
	    "  invalid.try()\n"
	    "  if (invalid.error == null) Fiber.abort(\"invalid write text accepted\")\n"
	    "}\n"
	    "for (value in [null, 3, \"key\", savedWrite]) {\n"
	    "  var invalid = Fiber.new { CTerm.sendKey(value) }\n"
	    "  invalid.try()\n"
	    "  if (invalid.error == null) Fiber.abort(\"invalid key object accepted\")\n"
	    "}\n"
	    "for (mutator in [Fn.new { savedWrite.code = 0 }, Fn.new { savedWrite.consumedBytes = 0 }, Fn.new { savedWrite.handled = false }]) {\n"
	    "  var invalid = Fiber.new { mutator.call() }\n"
	    "  invalid.try()\n"
	    "  if (invalid.error == null) Fiber.abort(\"mutable terminal error\")\n"
	    "}\n") == 0);
	CHECK(st_wren_test_cterm_write_calls == before);
	st_wren_test_cterm_key_override = true;
	for (unsigned handled = 0; handled < 2; ++handled) {
		st_wren_test_cterm_key_status = handled ? IO_ERROR_IO : IO_ERROR_TIMED_OUT;
		st_wren_test_cterm_key_handled = handled != 0;
		char source[512];
		CHECK(snprintf(source, sizeof(source),
		    "result = CTerm.sendKey(KeyEvent.new(Key.enter))\n"
		    "if (!(result is CTermError) || result.code != %d || result.handled != %s || result.consumedBytes != null) Fiber.abort(\"native key diagnostics lost\")\n"
		    "savedKey = result\n", st_wren_test_cterm_key_status,
		    handled ? "true" : "false") < (int)sizeof(source));
		CHECK(st_wren_connected_interpret(connected, "cterm_result_contract", source) == 0);
		st_wren_test_cterm_key_status = 0;
		CHECK(st_wren_connected_interpret(connected, "cterm_result_contract",
		    "if (CTerm.sendKey(KeyEvent.new(Key.enter)) != savedKey.handled) Fiber.abort(\"successful native Bool changed\")\n") == 0);
	}
	st_wren_test_cterm_key_override = false;
	CHECK(st_wren_connected_interpret(connected, "cterm_result_contract",
	    "System.gc()\n"
	    "if (savedWrite.code != 12345 || savedWrite.consumedBytes != 0 || savedKey.code != IOErr.io || !savedKey.handled) Fiber.abort(\"terminal error lifetime\")\n"
	    "if (CTerm.write(\"\") != null || CTerm.write(\"\\e[2J\\e[HA\\u0000B\\r\") != null) Fiber.abort(\"valid control text rejected\")\n") == 0);
	CHECK(console_has_text(console, 0, 0, "AB"));
	CHECK(st_wren_connected_interpret(connected, "cterm_result_contract",
	    "if (CTerm.write(\"\\e[2J\\e[H\") != null) Fiber.abort(\"fixture clear failed\")\n") == 0);
	st_wren_test_cterm_write_override = true;
	st_wren_test_cterm_write_prefix = true;
	st_wren_test_cterm_write_status = IO_ERROR_NO_SPACE;
	st_wren_test_cterm_write_consumed = 2;
	before = st_wren_test_cterm_write_calls;
	CHECK(st_wren_connected_interpret(connected, "cterm_result_contract",
	    "result = CTerm.write(\"A\\rB\")\n"
	    "if (!(result is CTermError) || result.code != IOErr.noSpace || result.consumedBytes != 2 || CTerm.x != 1 || CTerm.y != 1) Fiber.abort(\"partial render result lost\")\n") == 0);
	CHECK(st_wren_test_cterm_write_calls == before + 1);
	CHECK(console_has_text(console, 0, 0, "A "));
	st_wren_test_cterm_write_override = st_wren_test_cterm_write_prefix = false;
	/* A real renderer failure can consume input before returning its error. */
	uint32_t width, height;
	CHECK(st_console_size(console, &width, &height) == 0);
	CHECK(st_console_resize(console, 1, 1) == 0);
	CHECK(st_wren_connected_interpret(connected, "cterm_result_contract",
	    "result = CTerm.write(\"X\")\n"
	    "if (!(result is CTermError)) Fiber.abort(\"real rendering failure was lost\")\n") == 0);
	CHECK(st_wren_test_cterm_write_last_status != 0);
	char expected[512];
	CHECK(snprintf(expected, sizeof(expected),
	    "if (result.code != %d || result.consumedBytes != %zu || result.handled != null) Fiber.abort(\"real renderer diagnostics changed\")\n",
	    st_wren_test_cterm_write_last_status,
	    st_wren_test_cterm_write_last_consumed) < (int)sizeof(expected));
	CHECK(st_wren_connected_interpret(connected, "cterm_result_contract", expected) == 0);
	CHECK(st_console_resize(console, width, height) == 0);
	CHECK(st_wren_connected_interpret(connected, "cterm_result_contract",
	    "CTerm.write(\"\")\n"
	    "if (CTerm.write(\"\\e[2J\\e[H\") != null) Fiber.abort(\"fixture recovery failed\")\n") == 0);
	return 0;
}

static int
exercise_cterm_boundaries(st_wren_host_t *host, st_console_t *console)
{
	const char *source =
	    "import \"syncterm\" for CTerm, CTermError, Key, KeyEvent\n"
	    "for (fn in [Fn.new { CTerm.write(\"text\") }, Fn.new { CTerm.reset() }, Fn.new { CTerm.sendKey(KeyEvent.new(Key.enter)) }, Fn.new { CTermError.new() }]) {\n"
	    "  var rejected = Fiber.new { fn.call() }\n"
	    "  rejected.try()\n"
	    "  if (!(rejected.error is String)) Fiber.abort(\"CTerm capability boundary crossed\")\n"
	    "}\n";
	unsigned writes = st_wren_test_cterm_write_calls;
	unsigned keys = st_wren_test_cterm_key_calls;
	unsigned resets = st_wren_test_cterm_reset_calls;
	CHECK(wrenInterpret(host->vm, "cterm_unavailable", source) == WREN_RESULT_SUCCESS);
	CHECK(wrenInterpret(host->picker_vm, "cterm_unavailable", source) == WREN_RESULT_SUCCESS);
	st_wren_connected_t *connected = NULL;
	struct st_profile profile;
	st_profile_init(&profile);
	const struct st_wren_connected_config config = {.host = host, .console = console,
	    .profile = &profile};
	CHECK(st_wren_connected_create(&connected, &config) == 0);
	CHECK(st_wren_connected_interpret(connected, "cterm_unavailable", source) == 0);
	st_wren_connected_destroy(connected);
	st_profile_dispose(&profile);
	CHECK(writes == st_wren_test_cterm_write_calls && keys == st_wren_test_cterm_key_calls
	    && resets == st_wren_test_cterm_reset_calls);
	return 0;
}

struct key_output_failure {
	uint8_t first;
	unsigned calls;
};
static ssize_t
key_output_failure(st_connection_t *connection, const void *bytes, size_t size,
	int64_t timeout, bool atomic_write, enum st_connection_send_kind kind)
{
	struct key_output_failure *failure = connection->transport;
	(void)timeout; (void)atomic_write; (void)kind;
	if (failure->calls++ != 0) return IO_ERROR_IO;
	CHECK(size != 0);
	failure->first = *(const uint8_t *)bytes;
	return 1;
}

/* A real native key can be accepted, emit a prefix and then fail output. */
static int
exercise_cterm_key_failure(st_wren_host_t *host, st_console_t *console,
	st_font_catalog_t *fonts, struct st_profile *profile,
	struct st_connection *connection, bool utf8)
{
	st_term_session_t *session = NULL;
	st_wren_connected_t *connected = NULL;
	struct key_output_failure failure = {0};
	connection->output.count = connection->output.head = 0;
	connection->send_filter = NULL;
	const struct st_term_config term = {.console = console, .connection = connection,
	    .fonts = fonts, .hide_status = true, .screen_mode = ST_SCREEN_MODE_80X25,
	    .encoding = utf8 ? ST_CTERM_ENCODING_UTF8 : ST_CTERM_ENCODING_CODEPAGE,
	    .codepage = ST_CODEPAGE_CP437};
	CHECK(st_term_session_create(&session, &term) == 0);
	const struct st_wren_connected_config config = {.host = host, .console = console,
	    .profile = profile, .connection = connection, .session = session,
	    .cterm = st_term_session_cterm(session)};
	CHECK(st_wren_connected_create(&connected, &config) == 0);
	CHECK(st_wren_connected_interpret(connected, "cterm_key_failure",
	    "import \"syncterm\" for CTerm, CTermError, IOErr, Key, KeyEvent, Mod, Session\n"
	    "import \"connected\" for Connected\n") == 0);
	/* The login helper must stop at the failing Return, without resending text. */
	st_wren_test_cterm_key_override = true;
	st_wren_test_cterm_key_status = IO_ERROR_IO;
	st_wren_test_cterm_key_handled = true;
	CHECK(st_wren_connected_interpret(connected, "cterm_key_failure",
	    "var login = Fiber.new { Connected.sendComponent_(\"name\") }\n"
	    "login.try()\n"
	    "if (!(login.error is CTermError) || login.error.code != IOErr.io || !login.error.handled) Fiber.abort(\"login ignored Return failure\")\n") == 0);
	st_wren_test_cterm_key_override = false;
	CHECK(connection->output.count == 4 && memcmp(connection->output.data, "name", 4) == 0);
	connection->output.count = connection->output.head = 0;
	atomic_store(&connection->connected, false);
	CHECK(st_wren_connected_interpret(connected, "cterm_key_failure",
	    "if (CTerm.sendKey(KeyEvent.new(Key.up)) != true) Fiber.abort(\"native disconnect suppression changed\")\n") == 0);
	CHECK(connection->output.count == 0);
	atomic_store(&connection->connected, true);
	connection->transport = &failure;
	connection->send_filter = key_output_failure;
	CHECK(st_wren_connected_interpret(connected, "cterm_key_failure",
	    "var result = CTerm.sendKey(KeyEvent.new(Key.up))\n"
	    "if (!(result is CTermError) || result.code != IOErr.io || !result.handled || result.consumedBytes != null) Fiber.abort(\"real key output failure lost\")\n"
	    "var saved = result\n"
	    "result = CTerm.sendKey(KeyEvent.new(Key.up))\n"
	    "if (!(result is CTermError) || result.code != IOErr.io || !result.handled) Fiber.abort(\"retained native output error changed\")\n"
	    "result = CTerm.sendKey(KeyEvent.new(Key.right, Mod.alt))\n"
	    "if (!(result is CTermError) || result.code != IOErr.io || result.handled) Fiber.abort(\"unhandled/error conflated\")\n"
	    "CTerm.suspended = true\n"
	    "var owner = Session.current.claimWire()\n"
	    "if (CTerm.sendKey(KeyEvent.new(Key.up)) != false) Fiber.abort(\"wire owner bypassed\")\n"
	    "if (CTerm.reset() != null || !CTerm.suspended || owner.released) Fiber.abort(\"reset changed wire/suspension policy\")\n"
	    "owner.release()\n"
	    "System.gc()\n"
	    "if (saved.code != IOErr.io || !saved.handled || saved.message.count == 0) Fiber.abort(\"key error lifetime\")\n") == 0);
	CHECK(failure.calls == 2 && failure.first == 27);
	st_wren_connected_destroy(connected);
	st_term_session_destroy(&session);
	connection->send_filter = NULL;
	connection->transport = NULL;
	return 0;
}

struct wire_handoff_test {
	st_term_session_t *session;
	bool enabled;
	int reentrant;
	uint8_t captured[64];
	size_t size;
};
static int
wire_handoff_filter(void *data, uint8_t byte, bool *consumed,
	uint8_t *replacement, size_t capacity, size_t *size)
{
	struct wire_handoff_test *test = data;
	(void)replacement; (void)capacity;
	*consumed = false; *size = 0;
	if (test->enabled && byte == 'A') {
		test->reentrant = st_term_session_claim_wire(test->session, test);
		st_term_session_set_suspended(test->session, true);
	}
	return 0;
}
static int
wire_handoff_capture(void *data, const uint8_t *bytes, size_t size)
{
	struct wire_handoff_test *test = data;
	if (!test->enabled) return 0;
	CHECK(size <= sizeof(test->captured) - test->size);
	memcpy(test->captured + test->size, bytes, size);
	test->size += size;
	return 0;
}

/* No worker drains this connection: each pump observes precisely the free
 * capacity arranged here, independent of scheduler or network timing. */
static int
exercise_owned_sends(st_wren_host_t *host, st_console_t *console)
{
	static const struct st_connection_transport_ops operations = {0};
	struct st_connection connection = {.operations = &operations};
	struct st_profile profile;
	st_font_catalog_t *fonts = NULL;
	st_console_snapshot_t *snapshot = NULL;

	CHECK(st_console_snapshot_create(&snapshot, console) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	st_profile_init(&profile);
	profile.screen_mode = ST_SCREEN_MODE_80X25;
	atomic_init(&connection.connected, true);
	atomic_init(&connection.binary_mode, false);
	atomic_init(&connection.stopping, false);
	CHECK(st_mutex_init(&connection.input.mutex, ST_MUTEX_PLAIN) == ST_THREAD_SUCCESS);
	CHECK(st_condition_init(&connection.input.changed) == ST_THREAD_SUCCESS);
	CHECK(st_mutex_init(&connection.output.mutex, ST_MUTEX_PLAIN) == ST_THREAD_SUCCESS);
	CHECK(st_condition_init(&connection.output.changed) == ST_THREAD_SUCCESS);
	for (unsigned mode = 0; mode < 2; ++mode) {
		st_term_session_t *session = NULL;
		st_wren_connected_t *connected = NULL;
		struct wire_handoff_test handoff = {0};
		const struct st_term_config term_config = {
			.console = console, .connection = &connection, .fonts = fonts,
			.screen_mode = ST_SCREEN_MODE_80X25, .hide_status = true,
			.encoding = mode == 0 ? ST_CTERM_ENCODING_UTF8 : ST_CTERM_ENCODING_CODEPAGE,
			.codepage = ST_CODEPAGE_CP437,
			.wire_filter = wire_handoff_filter, .wire_filter_context = &handoff,
			.wire_capture = wire_handoff_capture, .capture_context = &handoff,
		};
		CHECK(st_term_session_create(&session, &term_config) == 0);
		handoff.session = session;
		const struct st_wren_connected_config config = {
			.host = host, .console = console, .profile = &profile,
			.connection = &connection, .session = session,
			.cterm = st_term_session_cterm(session),
		};
		CHECK(st_wren_connected_create(&connected, &config) == 0);
		CHECK(st_wren_connected_interpret(connected, "profile_snapshot",
		    "import \"syncterm\" for Session, Profile, CTerm, Codepage, TerminalEncoding\n"
		    "var saved = Session.current.profile\n"
		    "if (!(saved is Profile) || !saved.readOnly || saved != Session.current.profile || saved.encoding != Codepage.cp437) Fiber.abort(\"session profile identity\")\n"
		    "var failed = Fiber.new { saved.addr = \"changed.example\" }\n"
		    "failed.try()\n"
		    "if (failed.error == null) Fiber.abort(\"read-only session snapshot\")\n"
		    "var editable = saved.copy()\n"
		    "editable.addr = \"changed.example\"\n"
		    "if (editable.readOnly || saved.addr == editable.addr) Fiber.abort(\"session copy isolation\")\n"
		    "var live = CTerm.encoding\n"
		    "if (CTerm.codepage != Codepage.cp437) Fiber.abort(\"live codepage\")\n") == 0);
		CHECK(st_cterm_set_encoding(st_term_session_cterm(session),
		    mode == 0 ? ST_CTERM_ENCODING_CODEPAGE : ST_CTERM_ENCODING_UTF8,
		    ST_CODEPAGE_CP437) == 0);
		CHECK(st_wren_connected_interpret(connected, "profile_snapshot",
		    "if (CTerm.encoding == live || saved.encoding != Codepage.cp437) Fiber.abort(\"configured/live encoding conflated\")\n") == 0);
		CHECK(st_cterm_set_encoding(st_term_session_cterm(session), term_config.encoding,
		    ST_CODEPAGE_CP437) == 0);
		CHECK(exercise_cterm_results(connected, console) == 0);
		CHECK(exercise_screen_connected_results(connected) == 0);
		CHECK(exercise_clipboard_url_capabilities(NULL, connected, console) == 0);

		connection.output.head = 0;
		connection.output.count = 0;
		CHECK(st_wren_connected_interpret(connected, "key_forward_binding",
	    "import \"syncterm\" for CTerm, Conn, Hook, Key, KeyEvent, Mod, PhysicalKeyEvent\n"
	    "var reentered = false\n"
	    "var hook = Hook.onKey(Fn.new {|event| reentered = true })\n"
	    "if (!CTerm.sendKey(KeyEvent.text(\"é\")) || !CTerm.sendKey(KeyEvent.new(Key.tab, Mod.shift))) Fiber.abort(\"forward key\")\n"
	    "if (CTerm.sendKey(KeyEvent.new(Key.right, Mod.alt))) Fiber.abort(\"unsupported Alt-Right\")\n"
	    "if (!CTerm.sendKey(KeyEvent.byte(0x82))) Fiber.abort(\"raw byte forwarding\")\n"
	    "if (CTerm.sendKey(PhysicalKeyEvent.new(30, true, 15, true))) Fiber.abort(\"disabled physical reporting\")\n"
	    "if (reentered) Fiber.abort(\"forwarding reentered hooks\")\n"
	    "hook.remove()\n"
	    "var bad = Conn.sendText(String.fromByte(255)).result\n"
	    "var rejected = Fiber.new { CTerm.sendKey(bad) }\n"
	    "rejected.try()\n"
	    "if (rejected.error == null) Fiber.abort(\"wrong foreign key payload accepted\")\n"
		    ) == 0);
		CHECK(connection.output.count == (mode == 0 ? 7 : 5));
		CHECK(memcmp(connection.output.data, mode == 0 ? "\xc3\xa9\x1b[Z\xc2\x82" : "\x82\x1b[Z\x82",
		    mode == 0 ? 7 : 5) == 0);
		connection.output.head = 0;
		connection.output.count = ST_CONNECTION_BUFFER_SIZE - 1;
		memset(connection.output.data, 'x', sizeof(connection.output.data));
		CHECK(st_wren_connected_interpret(connected, "owned_send",
		    "import \"syncterm\" for Session, Conn, ConnError, ConnErr, Operation, OperationError\n"
		    "class SendText {\n"
		    "  static repeat(ch, n) {\n"
		    "    var text = ch\n"
		    "    while (text.count < n) text = text + text\n"
		    "    return text[0...n]\n"
		    "  }\n"
		    "}\n"
		    "var send = Conn.sendText(\"e\\u0301\")\n"
		    "var later = Conn.sendText(\"Z\")\n"
		    "var received = false\n"
		    "var waiter = Fiber.new {\n"
		    "  if (send.await() != null || later.await() != null) Fiber.abort(\"await send\")\n"
		    "  received = true\n"
		    "}\n"
		    "waiter.call()\n") == 0);
		if (mode == 0) {
			CHECK(connection.output.count == ST_CONNECTION_BUFFER_SIZE - 1);
			CHECK(connection.output.data[ST_CONNECTION_BUFFER_SIZE - 1] == 'x');
		}
		else {
			CHECK(connection.output.count == ST_CONNECTION_BUFFER_SIZE);
			CHECK(connection.output.data[ST_CONNECTION_BUFFER_SIZE - 1] == 0x82);
		}
		/* A further native byte can never land between the two bytes of é. */
		CHECK(st_connection_send_raw(&connection, "K", 1, 0)
		    == (mode == 0 ? 1 : IO_ERROR_WOULD_BLOCK));
		connection.output.count = 0;
		CHECK(st_wren_connected_pump(connected, 0) == 0);
		CHECK(connection.output.count == (mode == 0 ? 3 : 1));
		CHECK(memcmp(connection.output.data, mode == 0 ? "\xc3\xa9Z" : "Z",
		    connection.output.count) == 0);
		CHECK(st_wren_connected_interpret(connected, "owned_send",
		    "if (!send.isDone || !later.isDone || !received || !waiter.isDone) Fiber.abort(\"completion identity\")\n"
		    "var bad = Conn.sendText(\"valid\" + String.fromByte(255)).result\n"
		    "if (!(bad is ConnError) || bad.code != ConnErr.invalidText || bad.partial) Fiber.abort(\"invalid text\")\n") == 0);
		CHECK(connection.output.count == (mode == 0 ? 3 : 1));
		if (mode == 1) {
			CHECK(st_wren_connected_interpret(connected, "owned_send",
			    "bad = Conn.sendText(\"prefix中\").result\n"
			    "if (!(bad is ConnError) || bad.code != ConnErr.invalidText || bad.partial) Fiber.abort(\"unmappable text\")\n") == 0);
			CHECK(connection.output.count == 1);
		}
		connection.output.count = 0;
		CHECK(st_wren_connected_interpret(connected, "owned_send",
		    "var raw = Conn.sendBytes(String.fromByte(255) + String.fromByte(0) + \"e\\u0301\\r\\n\")\n"
		    "if (!raw.isDone || raw.result != null) Fiber.abort(\"raw result\")\n") == 0);
		CHECK(connection.output.count == 7);
		CHECK(memcmp(connection.output.data, "\xff\0e\xcc\x81\r\n", 7) == 0);
		if (mode == 0) {
			connection.output.count = ST_CONNECTION_BUFFER_SIZE - 4096;
			CHECK(st_wren_connected_interpret(connected, "owned_send",
			    "var boundary = Conn.sendText(SendText.repeat(\"A\", 4095) + \"😀Z\")\n"
			    "if (boundary.isDone) Fiber.abort(\"expected queue pressure\")\n") == 0);
			CHECK(connection.output.count == ST_CONNECTION_BUFFER_SIZE - 1);
			for (size_t i = ST_CONNECTION_BUFFER_SIZE - 4096;
			    i < ST_CONNECTION_BUFFER_SIZE - 1; ++i)
				CHECK(connection.output.data[i] == 'A');
			connection.output.count = 0;
			CHECK(st_wren_connected_pump(connected, 0) == 0);
			CHECK(connection.output.count == 5);
			CHECK(memcmp(connection.output.data, "\xf0\x9f\x98\x80Z", 5) == 0);
			CHECK(st_wren_connected_interpret(connected, "owned_send",
			    "if (!boundary.isDone || boundary.result != null) Fiber.abort(\"boundary completion\")\n") == 0);
		}
		/* Cancellation releases queue storage without injecting its suffix. */
		connection.output.count = ST_CONNECTION_BUFFER_SIZE - 4096;
		CHECK(st_wren_connected_interpret(connected, "owned_send",
		    "var cancelled = Conn.sendBytes(SendText.repeat(\"C\", 5000))\n"
		    "if (cancelled.isDone || !cancelled.cancel() || cancelled.cancel()) Fiber.abort(\"cancel send\")\n"
		    "if (!(cancelled.result is OperationError)) Fiber.abort(\"cancel result\")\n") == 0);
		CHECK(connection.output.count == ST_CONNECTION_BUFFER_SIZE);
		connection.output.count = 0;
		CHECK(st_wren_connected_pump(connected, 0) == 0);
		CHECK(connection.output.count == 0);
		connection.output.count = ST_CONNECTION_BUFFER_SIZE;
		CHECK(st_wren_connected_interpret(connected, "owned_send",
		    "var pending = []\n"
		    "for (i in 0...64) pending.add(Conn.sendText(\"Q\"))\n"
		    "var full = Conn.sendText(\"overflow\").result\n"
		    "if (!(full is ConnError) || full.code != ConnErr.queueFull || full.partial) Fiber.abort(\"request limit\")\n"
		    "for (op in pending) if (!op.cancel()) Fiber.abort(\"release limit\")\n"
		    "var storage = Conn.sendBytes(SendText.repeat(\"a\", 600000))\n"
		    "full = Conn.sendBytes(SendText.repeat(\"b\", 500000)).result\n"
		    "if (!(full is ConnError) || full.code != ConnErr.queueFull || full.partial) Fiber.abort(\"storage limit\")\n"
		    "storage.cancel()\n"
		    "var oversized = Conn.sendBytes(SendText.repeat(\"c\", 1048576)).result\n"
		    "if (!(oversized is ConnError) || oversized.code != ConnErr.queueFull) Fiber.abort(\"input limit\")\n"
		    "var abandoned = Fiber.new {\n"
		    "  Conn.sendText(\"abandoned\").await_(Fiber.current)\n"
		    "  Fiber.abort(\"stop\")\n"
		    "}\n"
		    "abandoned.try()\n") == 0);
		CHECK(st_wren_connected_pump(connected, 0) == 0);
		connection.output.count = 0;
		CHECK(st_wren_connected_pump(connected, 0) == 0);
		CHECK(connection.output.count == 0);
		connection.output.count = ST_CONNECTION_BUFFER_SIZE;
		CHECK(st_wren_connected_interpret(connected, "owned_send",
		    "Conn.sendText(\"owned\" + \" string\")\n"
		    "System.gc()\n") == 0);
		connection.output.count = 0;
		CHECK(st_wren_connected_pump(connected, 0) == 0);
		CHECK(connection.output.count == 12);
		CHECK(memcmp(connection.output.data, "owned string", 12) == 0);
		connection.output.count = ST_CONNECTION_BUFFER_SIZE;
		CHECK(st_wren_connected_interpret(connected, "owned_send",
		    "var interrupted = Conn.sendText(\"pending before transfer\")\n") == 0);
		st_term_session_reserve_transfer(session, true);
		CHECK(st_wren_connected_interpret(connected, "owned_send",
		    "var busy = Conn.sendText(\"busy\").result\n"
		    "if (!(busy is ConnError) || busy.code != ConnErr.busy || busy.partial) Fiber.abort(\"transfer reservation\")\n") == 0);
		CHECK(st_wren_connected_pump(connected, 0) == 0);
		CHECK(st_wren_connected_interpret(connected, "owned_send",
		    "if (!interrupted.isDone || interrupted.result.code != ConnErr.busy || interrupted.result.partial) Fiber.abort(\"pending transfer reservation\")\n") == 0);
		st_term_session_reserve_transfer(session, false);
		/* Ownership includes terminal read-ahead, then the live queue; peek is
		 * nondestructive and local text flow control cannot stall protocol I/O. */
		connection.output.count = 0;
		size_t cleared;
		CHECK(st_cterm_write(st_term_session_cterm(session), "\x1b[2J\x1b[H", 7, &cleared) == 0);
		handoff.enabled = true;
		connection.input.head = 0;
		connection.input.count = 6;
		memcpy(connection.input.data, "AB\xff\0CD", 6);
		enum st_term_disposition disposition;
		bool finished;
		CHECK(st_term_session_pump(session, &disposition, &finished) == 0);
		CHECK(handoff.reentrant == IO_ERROR_BUSY && connection.input.count == 0);
		connection.input.data[0] = 'E'; connection.input.head = 0; connection.input.count = 1;
		connection.input.paused = true;
		CHECK(st_wren_connected_interpret(connected, "wire_owner",
		    "import \"syncterm\" for Session, WireLease, Conn, ConnError, ConnErr, CTerm, KeyEvent, Key, OperationError\n"
		    "CTerm.suspended = true\n"
		    "var owner = Session.current.claimWire()\n"
		    "if (!(owner is WireLease) || owner.released || owner.available != 6) Fiber.abort(\"wire owner\")\n"
		    "var expected = \"B\" + String.fromByte(255) + String.fromByte(0) + \"CDE\"\n"
		    "if (owner.peekBytes(64) != expected || owner.available != 6) Fiber.abort(\"wire peek ordering\")\n"
		    "var other = Session.current.claimWire()\n"
		    "if (!(other is ConnError) || other.code != ConnErr.busy) Fiber.abort(\"competing owner\")\n"
		    "if (Conn.sendText(\"blocked\").result.code != ConnErr.busy || Conn.sendBytes(\"blocked\").result.code != ConnErr.busy) Fiber.abort(\"unowned output\")\n"
		    "if (CTerm.sendKey(KeyEvent.new(Key.enter))) Fiber.abort(\"native key bypass\")\n"
		    "if (owner.readBytes(3) != \"B\" + String.fromByte(255) + String.fromByte(0)) Fiber.abort(\"wire read ahead\")\n"
		    "if (!owner.sendWire(String.fromByte(255) + String.fromByte(0)).isDone) Fiber.abort(\"owned output\")\n"
		    "if (owner.readBytes(0) != \"\") Fiber.abort(\"wire zero\")\n"
		    ) == 0);
		CHECK(st_term_session_reserve_transfer(session, true) == IO_ERROR_BUSY);
		CHECK(connection.output.count == 2 && memcmp(connection.output.data, "\xff\0", 2) == 0);
		CHECK(handoff.size == 4 && memcmp(handoff.captured, "AB\xff\0", 4) == 0);
		CHECK(st_term_session_pump(session, &disposition, &finished) == 0);
		CHECK(connection.input.count == 1);
		CHECK(st_wren_connected_interpret(connected, "wire_owner",
		    "if (!owner.release() || owner.release() || !owner.released || !CTerm.suspended) Fiber.abort(\"release state\")\n"
		    "if (!(owner.readBytes(1) is ConnError)) Fiber.abort(\"released read\")\n"
		    "owner = null\n"
		    "Session.current.claimWire()\n"
		    "System.gc()\n") == 0);
		CHECK(!st_term_session_wire_owned(session));
		connection.input.paused = false;
		st_term_session_set_suspended(session, false);
		CHECK(st_term_session_pump(session, &disposition, &finished) == 0);
		CHECK(st_term_session_pump(session, &disposition, &finished) == 0);
		CHECK(console_has_text(console, 0, 0, "ACDE"));
		CHECK(handoff.size == 7 && memcmp(handoff.captured, "AB\xff\0CDE", 7) == 0);
		handoff.enabled = false;
		connection.output.count = ST_CONNECTION_BUFFER_SIZE;
		CHECK(st_wren_connected_interpret(connected, "wire_owner",
		    "owner = Session.current.claimWire()\n"
		    "var queued = owner.sendWire(\"cancel on release\")\n"
		    "if (queued.isDone || !owner.release() || !(queued.result is OperationError)) Fiber.abort(\"owned send cancellation\")\n"
		    "var pending = Conn.sendBytes(\"wait\")\n"
		    "if (Session.current.claimWire().code != ConnErr.busy) Fiber.abort(\"pending send claim\")\n"
		    "pending.cancel()\n"
		    "CTerm.suspended = false\n"
		    "var failed = Fiber.new { Session.current.claimWire().use(Fn.new {|wire| Fiber.abort(\"scope failure\") }) }\n"
		    "failed.try()\n"
		    "if (failed.error != \"scope failure\") Fiber.abort(\"scope error\")\n") == 0);
		CHECK(!st_term_session_wire_owned(session));
		/* A disconnect after a prefix is diagnostic, never a retry offset. */
		connection.output.count = ST_CONNECTION_BUFFER_SIZE - 4096;
		CHECK(st_wren_connected_interpret(connected, "owned_send",
		    "var wire = Session.current.claimWire()\n"
		    "var partial = wire.sendWire(SendText.repeat(\"P\", 5000))\n") == 0);
		CHECK(connection.output.count == ST_CONNECTION_BUFFER_SIZE);
		atomic_store(&connection.connected, false);
		CHECK(st_wren_connected_pump(connected, 0) == 0);
		CHECK(st_wren_connected_interpret(connected, "owned_send",
		    "if (!wire.released) Fiber.abort(\"failed send retained wire\")\n"
		    "if (!partial.isDone || !(partial.result is ConnError) || !partial.result.partial || partial.result.code != ConnErr.notConnected) Fiber.abort(\"partial disconnect\")\n") == 0);
		atomic_store(&connection.connected, true);
		CHECK(st_wren_connected_interpret(connected, "owned_send",
		    "wire = Session.current.claimWire()\n"
		    "var shutdown = wire.sendWire(\"pending at shutdown\")\n"
		    "var observer = Fiber.new {\n"
		    "  shutdown.await()\n"
		    "  Fiber.abort(\"shutdown resumed send waiter\")\n"
		    "}\n"
		    "observer.call()\n") == 0);
		st_wren_connected_destroy(connected);
		CHECK(!st_term_session_wire_owned(session));
		st_term_session_destroy(&session);
		CHECK(exercise_cterm_key_failure(host, console, fonts, &profile,
		    &connection, mode == 0) == 0);
	}
	st_condition_destroy(&connection.output.changed);
	st_mutex_destroy(&connection.output.mutex);
	st_condition_destroy(&connection.input.changed);
	st_mutex_destroy(&connection.input.mutex);
	st_profile_dispose(&profile);
	st_font_catalog_destroy(fonts);
	CHECK(st_console_snapshot_restore(console, snapshot, NULL) == 0);
	st_console_snapshot_destroy(snapshot);
	return 0;
}

#if !SYNCTERM_PLATFORM_WINDOWS
static int auto_transfer_filter(void *context, uint8_t byte, bool *consumed,
	uint8_t *replacement, size_t capacity, size_t *size)
{
	st_wren_connected_t *connected = *(st_wren_connected_t **)context;
	return st_wren_connected_filter_wire_byte(connected, byte, consumed, replacement, capacity, size);
}
static int exercise_auto_transfer(st_wren_host_t *host, st_console_t *console)
{
	st_font_catalog_t *fonts = NULL;
	st_connection_t *connection = NULL;
	st_term_session_t *session = NULL;
	st_wren_connected_t *connected = NULL;
	struct st_profile profile;
	st_profile_init(&profile); profile.connection_type = ST_CONNECTION_SHELL;
	strcpy(profile.address, "sh ../../terminal/term/term-fixture.sh auto-transfer");
	profile.download_directory = strdup(ST_WREN_TEST_DIRECTORY);
	CHECK(profile.download_directory != NULL);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	CHECK(st_term_connect_profile(&connection, &profile, console, NULL, NULL) == 0);
	const struct st_term_config term_config = {.console = console, .connection = connection,
	    .fonts = fonts, .encoding = ST_CTERM_ENCODING_CODEPAGE, .codepage = ST_CODEPAGE_CP437,
	    .hide_status = true, .wire_filter = auto_transfer_filter, .wire_filter_context = &connected};
	CHECK(st_term_session_create(&session, &term_config) == 0);
	size_t logs = 0;
	const struct st_wren_connected_config wren_config = {.host = host, .console = console,
	    .profile = &profile, .connection = connection, .session = session,
	    .cterm = st_term_session_cterm(session), .transfer_log = count_transfer_logs,
	    .transfer_log_context = &logs};
	CHECK(st_wren_connected_create(&connected, &wren_config) == 0);
	bool finished = false; enum st_term_disposition disposition;
	uint64_t start = 0, now = 0; CHECK(st_time_monotonic_ms(&start) == 0);
	now = start;
	while (!finished && now - start < 10000) {
		CHECK(st_term_session_pump(session, &disposition, &finished) == 0);
		CHECK(st_wren_connected_pump(connected, 0) == 0);
		CHECK(st_wren_connected_last_error(connected) == NULL);
		CHECK(st_time_monotonic_ms(&now) == 0);
	}
	CHECK(finished && logs != 0);
	CHECK(st_wren_connected_interpret(connected, "auto_transfer_result",
	    "import \"syncterm\" for Transfer, CTerm, Screen\n"
	    "if (Transfer.active || CTerm.suspended) Fiber.abort(\"auto cleanup\")\n"
	    "var size = Screen.size\n"
	    "var text = Screen.readRect(1, 1, size[0], size[1]).map {|c| c.ch }.join()\n"
	    "if (!text.contains(\"BEFORE\") || !text.contains(\"AFTER\") || text.contains(\"0800000000\")) Fiber.abort(\"auto stream handoff\")\n") == 0);
	st_wren_connected_destroy(connected); st_term_session_destroy(&session);
	CHECK(st_connection_close(connection) == 0);
	st_font_catalog_destroy(fonts); st_profile_dispose(&profile);
	return 0;
}

#ifdef ST_RIP_TEST_PYTHON
struct rip_wire { uint8_t bytes[4096]; size_t size; };
static int capture_rip_wire(void *context, const uint8_t *bytes, size_t size)
{
	struct rip_wire *wire = context;
	if (size > sizeof(wire->bytes) - wire->size) return IO_ERROR_OUT_OF_RANGE;
	memcpy(wire->bytes + wire->size, bytes, size); wire->size += size;
	return 0;
}
static int rip_transfer_request(void *context, const struct st_cterm_rip_transfer_request *request)
{
	return st_wren_connected_rip_transfer(*(st_wren_connected_t **)context, request);
}
static int rip_transfer_event(void *context, const struct st_console_event *event, bool *consumed)
{
	return st_wren_connected_handle_event(*(st_wren_connected_t **)context, event, consumed);
}
static bool rip_confirmation_ready(st_console_t *console)
{
	uint32_t columns, rows;
	if (st_console_size(console, &columns, &rows) != 0 || columns < 7)
		return false;
	for (uint32_t y = 0; y < rows; ++y)
		for (uint32_t x = 0; x <= columns - 7; ++x)
			if (console_has_text(console, x, y, "Confirm"))
				return true;
	return false;
}
static int exercise_rip_transfer(st_wren_host_t *host, st_console_t *console)
{
	CHECK(setenv("ST_RIP_TEST_PYTHON", ST_RIP_TEST_PYTHON, 1) == 0);
	const char *cache = ST_WREN_TEST_DIRECTORY "/rip-cache";
	CHECK(fs_create_directories(cache) == 0);
	const char *files[] = {"upload.rip", "stored.rip", "asset.icn", "help.hlp", "scene.rip"};
	for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
		char path[1024]; snprintf(path, sizeof(path), "%s/%s", cache, files[i]);
		int removed = fs_remove_file(path);
		CHECK(removed == 0 || removed == IO_ERROR_NOT_FOUND);
	}
	CHECK(fs_replace_file(ST_WREN_TEST_DIRECTORY "/rip-cache/upload.rip", "SECRET", 6) == 0);
	for (unsigned mode = 0; mode < 8; ++mode) {
		st_font_catalog_t *fonts = NULL;
		st_connection_t *connection = NULL;
		st_term_session_t *session = NULL;
		st_wren_connected_t *connected = NULL;
		struct st_profile profile;
		struct rip_wire wire = {0};
		st_profile_init(&profile); profile.connection_type = ST_CONNECTION_SHELL;
		snprintf(profile.address, sizeof(profile.address), "sh ../../terminal/term/term-fixture.sh rip %u", mode);
		profile.download_directory = strdup(ST_WREN_TEST_DIRECTORY);
		CHECK(profile.download_directory != NULL);
		CHECK(st_font_catalog_create(&fonts, console) == 0);
		CHECK(st_term_connect_profile(&connection, &profile, console, NULL, NULL) == 0);
		bool binary = st_connection_binary_mode(connection);
		const struct st_term_config term_config = {.console = console, .connection = connection,
		    .fonts = fonts, .encoding = mode == 5 ? ST_CTERM_ENCODING_UTF8 : ST_CTERM_ENCODING_CODEPAGE,
		    .codepage = ST_CODEPAGE_CP437, .hide_status = true, .graphics_mode = ST_CTERM_GRAPHICS_RIP_3,
		    .rip_directory = cache, .rip_transfer = rip_transfer_request, .rip_transfer_context = &connected,
		    .wire_filter = auto_transfer_filter, .wire_filter_context = &connected,
		    .event_hook = rip_transfer_event, .event_hook_context = &connected,
		    .wire_capture = capture_rip_wire, .capture_context = &wire};
		CHECK(st_term_session_create(&session, &term_config) == 0);
		st_cterm_t *cterm = st_term_session_cterm(session);
		size_t logs = 0;
		const struct st_wren_connected_config config = {.host = host, .console = console,
		    .profile = &profile, .connection = connection, .session = session, .cterm = cterm,
		    .transfer_log = count_transfer_logs, .transfer_log_context = &logs};
		CHECK(st_wren_connected_create(&connected, &config) == 0);
		uint64_t start, now;
		CHECK(st_time_monotonic_ms(&start) == 0); now = start;
		bool finished = false, answered = false, streamed = false;
		enum st_term_disposition disposition;
		while (!finished && now - start < 10000) {
			CHECK(st_term_session_pump(session, &disposition, &finished) == 0);
			CHECK(st_wren_connected_pump(connected, 0) == 0);
			if (st_wren_connected_last_error(connected) != NULL) fprintf(stderr, "RIP mode %u: %s\n", mode, st_wren_connected_last_error(connected));
			CHECK(st_wren_connected_last_error(connected) == NULL);
			if (cterm->rip_engine->scene_depth != 0 && cterm->rip_engine->commands_seen > 1) streamed = true;
			/* Retain the peer's pre-approval silence interval, and wait for
			 * the actual input owner rather than guessing UI startup time. */
			if (mode >= 6 && !answered && now - start > 350 && rip_confirmation_ready(console)) {
				CHECK(queue_text(console, mode == 6 ? "y" : "n") == 0); answered = true;
			}
			CHECK(st_time_monotonic_ms(&now) == 0);
		}
		if (!finished || st_connection_binary_mode(connection) != binary) {
			fprintf(stderr, "RIP mode %u: finished=%d binary=%d/%d commands=%zu, wire:\n", mode, finished, st_connection_binary_mode(connection), binary, cterm->rip_engine->commands_seen);
		}
		CHECK(finished && st_connection_binary_mode(connection) == binary);
		CHECK(st_wren_connected_interpret(connected, "rip_cleanup",
		    "import \"syncterm\" for Transfer, CTerm\n"
		    "import \"transfer_pick\" for TransferFlow\n"
		    "if (Transfer.active || TransferFlow.busy || CTerm.suspended) Fiber.abort(\"RIP cleanup\")\n") == 0);
		CHECK(cterm->rip_engine->scene_depth == 0);
		if (streamed != (mode == 0 || mode == 5)) fprintf(stderr, "RIP mode %u: streamed=%d commands=%zu bytes=%zu\n", mode, streamed, cterm->rip_engine->commands_seen, wire.size);
		CHECK(streamed == (mode == 0 || mode == 5));
		if (mode == 0 || mode == 5) {
			struct st_pixel_buffer *pixels = NULL;
			const struct st_console_rect rect = {100, 100, 1, 1};
			CHECK(st_console_get_pixels(&pixels, console, &rect, false) == 0);
			CHECK(pixels->primary[0] == cterm->rip_engine->raster.palette[4]); st_pixel_buffer_destroy(pixels);
		}
		CHECK(wire.size >= 30 && memcmp(wire.bytes, "!|9\033", 4) == 0);
		CHECK(memcmp(wire.bytes + wire.size - 11, "\r\nAFTER-RIP", 11) == 0);
		/* Framing CRLF and binary SOH are retained exactly once in raw
		 * capture, even though the protocol input skips the framing LF. */
		if (mode < 6) {
			CHECK(logs != 0);
			size_t end = 0;
			while (end + 3 < wire.size && !(wire.bytes[end] == '<' && wire.bytes[end + 1] == '>')) ++end;
			CHECK(end + 4 < wire.size && memcmp(wire.bytes + end, "<>\r\n\1", 5) == 0);
		}
		struct fs_info info;
		if (mode == 0) CHECK(fs_get_info(&info, ST_WREN_TEST_DIRECTORY "/rip-cache/scene.rip") == IO_ERROR_NOT_FOUND);
		if (mode == 1) CHECK(fs_get_info(&info, ST_WREN_TEST_DIRECTORY "/rip-cache/stored.rip") == 0);
		if (mode == 2 || mode == 4 || mode == 5) {
			CHECK(fs_get_info(&info, ST_WREN_TEST_DIRECTORY "/rip-cache/asset.icn") == 0);
			size_t consumed;
			const char *load = "\r\n!|*|1I141400010ASSET.ICN\r";
			CHECK(st_cterm_write(cterm, load, strlen(load), &consumed) == 0 && consumed == strlen(load));
			struct st_pixel_buffer *pixels = NULL;
			const struct st_console_rect rect = {40, 40, 1, 1};
			CHECK(st_console_get_pixels(&pixels, console, &rect, false) == 0);
			if (pixels->primary[0] != 0x1f) fprintf(stderr, "RIP icon mode %u: pixel=%" PRIx32 "\n", mode, pixels->primary[0]);
			CHECK(pixels->primary[0] == 0x1f); st_pixel_buffer_destroy(pixels);
		}
		if (mode >= 4 && mode < 6) CHECK(fs_get_info(&info, ST_WREN_TEST_DIRECTORY "/rip-cache/scene.rip") == 0);
		CHECK(fs_get_info(&info, ST_WREN_TEST_DIRECTORY "/scene.rip") == IO_ERROR_NOT_FOUND);
		st_wren_connected_destroy(connected); st_term_session_destroy(&session);
		CHECK(st_connection_close(connection) == 0);
		st_font_catalog_destroy(fonts); st_profile_dispose(&profile);
	}
	for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
		char path[1024]; snprintf(path, sizeof(path), "%s/%s", cache, files[i]);
		CHECK(fs_remove_file(path) == 0);
	}
	CHECK(fs_remove_directory(cache) == 0);
	return 0;
}
#endif

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
	CHECK(st_wren_connected_interpret(connected, "ooii_binding_test",
	    "import \"syncterm\" for CTerm, Host\n"
	    "if (!Host.haveOOII || Host.maxOOIIMode != 3 || CTerm.ooiiMode != 0) Fiber.abort(\"OOII initial\")\n"
	    "CTerm.ooiiMode = 99\n"
	    "if (CTerm.ooiiMode != 3) Fiber.abort(\"OOII upper clamp\")\n"
	    "CTerm.ooiiMode = -1\n"
	    "if (CTerm.ooiiMode != 0) Fiber.abort(\"OOII lower clamp\")\n"
	    "CTerm.ooiiMode = 1\n"
	    "if (CTerm.ooiiMode != 1) Fiber.abort(\"OOII enable\")\n"
	    "CTerm.ooiiMode = 0\n") == 0);
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
	CHECK(st_wren_connected_handle_event(connected, &(struct st_console_event){.type = ST_CONSOLE_EVENT_KEY_CODE, .data.key = {.code = ST_CONSOLE_KEY_UP, .modifiers = command_modifiers(host)}}, &consumed) == 0 && consumed);
	CHECK(st_term_session_throttle_speed(session) == 300);
	consumed = false;
	CHECK(st_wren_connected_handle_event(connected, &(struct st_console_event){.type = ST_CONSOLE_EVENT_KEY_CODE, .data.key = {.code = ST_CONSOLE_KEY_DOWN, .modifiers = command_modifiers(host)}}, &consumed) == 0 && consumed);
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
	CHECK(st_wren_connected_handle_event(connected, &(struct st_console_event){.type = ST_CONSOLE_EVENT_KEY_CODE, .data.key = {.code = ST_CONSOLE_KEY_INSERT, .modifiers = ST_CONSOLE_MODIFIER_SHIFT}}, &consumed) == 0 && consumed);
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
exercise_font_api(st_wren_host_t *public_host, st_console_t *shared_console,
	const struct st_profile *profile)
{
	struct st_wren_host *host = public_host;
	const char path[] = ST_WREN_TEST_DIRECTORY "/font-api.f16";
	uint8_t bitmap[4096] = {0};
	/* Independent glyph patterns for later lookup/render assertions. */
	bitmap[0xc0 * 16] = 0x80;
	bitmap['A' * 16] = 0x40;
	for (unsigned shared = 0; shared < 2; ++shared) {
		st_console_t *console = shared_console;
		st_font_catalog_t *catalog = NULL;
		st_cterm_t *terminal = NULL;
		st_wren_connected_t *connected = NULL;
		struct st_profile session;
		const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
		const struct st_console_config geometry = {.columns = 80, .rows = 25, .cell_width = 8, .cell_height = 16};
		if (!shared) CHECK(st_console_create(&console, &display, &geometry) == 0);
		FILE *stream = fopen(path, "wb");
		CHECK(stream != NULL && fwrite(bitmap, 1, sizeof(bitmap), stream) == sizeof(bitmap));
		CHECK(fclose(stream) == 0);
		CHECK(st_font_catalog_create(&catalog, console) == 0);
		CHECK(st_font_catalog_load_custom_codepage(catalog, 46, path, ST_CODEPAGE_CP1251) == 0);
		const struct st_cterm_config config = {
			.console = console, .fonts = catalog, .emulation = ST_CTERM_ANSI_BBS,
			.encoding = ST_CTERM_ENCODING_UTF8, .codepage = ST_CODEPAGE_CP437,
			.font_selector = 0, .quiet = true,
		};
		CHECK(st_cterm_create(&terminal, &config) == 0);
		st_profile_init(&session);
		CHECK(st_profile_copy(&session, profile));
		free(session.download_directory);
		session.download_directory = strdup(ST_WREN_TEST_DIRECTORY);
		CHECK(session.download_directory != NULL);
		const struct st_wren_connected_config connected_config = {
			.host = host, .console = console, .profile = &session, .cterm = terminal,
		};
		CHECK(st_wren_connected_create(&connected, &connected_config) == 0);
		CHECK(st_wren_connected_interpret(connected, "font_api_setup",
		    "import \"syncterm\" for Font, Codepage, Screen, CTerm, Download, Error, FontError, FontErr, FileError, IOErr, CTermFontMask\n"
		    "class FontProbe {\n"
		    "  static check(ok) {\n"
		    "    if (!ok) Fiber.abort(\"font API contract\")\n"
		    "  }\n"
		    "  static bad(fn) {\n"
		    "    var f = Fiber.new(fn)\n"
		    "    f.try()\n"
		    "    check(f.error != null)\n"
		    "  }\n"
		    "}\n"
		    "var file = Download.list[\"font-api.f16\"]\n"
		    "FontProbe.check(file != null)\n"
		    "FontProbe.check(Font.codepageFor(46) == Codepage.cp1251)\n"
		    "Screen.font[0] = 46\n"
		    "FontProbe.check(Font.codepage == Codepage.cp1251 && Codepage.encodes_(\"А\"))\n"
		    "var old = (0...4).map {|i| CTerm.font[i] }.toList\n"
		    "FontProbe.check(file.open() == null)\n"
		    "file.offset = 17\n"
		    "var selector = Font.load(file)\n"
		    "FontProbe.check(selector is Num && file.isOpen && file.offset == 17)\n"
		    "file.close()\n"
		    "FontProbe.check(Font.codepageFor(selector) == Codepage.cp437)\n"
		    "for (i in 0...4) FontProbe.check(CTerm.font[i] == old[i])\n"
		    "var explicit = Font.load(file, Codepage.cp1251)\n"
		    "FontProbe.check(explicit is Num)\n"
		    "FontProbe.check(Font.available(explicit) && Font.name(explicit) != \"\" && Font.count > explicit)\n"
		    "FontProbe.check(Font.codepageFor(explicit) == Codepage.cp1251)\n"
		    "FontProbe.check(CTerm.setFont(explicit, CTermFontMask.bright | CTermFontMask.blink) == null)\n"
		    "FontProbe.check(CTerm.font[0] == old[0] && CTerm.font[1] == explicit && CTerm.font[2] == explicit && CTerm.font[3] == old[3])\n"
		    "CTerm.font[3] = selector\n"
		    "FontProbe.check(CTerm.font[3] == selector && CTerm.font[0] == old[0])\n"
		    "FontProbe.check(CTerm.setFont(explicit, CTermFontMask.all) == null)\n"
		    "for (i in 0...4) FontProbe.check(CTerm.font[i] == explicit)\n"
		    "var missing = CTerm.setFont(249, CTermFontMask.all)\n"
		    "FontProbe.check(missing is FontError && missing.code == FontErr.notLoaded && missing.nativeCode < 0)\n"
		    "var f = Fiber.new { CTerm.font[0] = 249 }\n"
		    "f.try()\n"
		    "FontProbe.check(f.error is FontError)\n"
		    "for (i in 0...4) FontProbe.check(CTerm.font[i] == explicit)\n"
		    "\n") == 0);
		CHECK(st_wren_connected_interpret(connected, "font_api_invalid",
		    "import \"font_api_setup\" for file, selector, explicit, old, FontProbe\n"
		    "import \"syncterm\" for Font, Codepage, Screen, CTerm\n"
		    "FontProbe.bad(Fn.new { CTerm.altFonts })\n"
		    "FontProbe.bad(Fn.new { CTerm.altFont })\n"
		    "FontProbe.bad(Fn.new { CTerm.altFont = 0 })\n"
		    "for (mask in [0, 16, -1, 0.5, \"1\", 9007199254740992]) FontProbe.bad(Fn.new { CTerm.setFont(selector, mask) })\n"
		    "for (index in [-1, 4, 0.5, null]) FontProbe.bad(Fn.new { CTerm.font[index] })\n"
		    "for (value in [-1, 256, 0.5, null]) FontProbe.bad(Fn.new { CTerm.font[0] = value })\n"
		    "for (value in [-1, 29, 0.5, null]) FontProbe.bad(Fn.new { Font.load(file, value) })\n"
		    "FontProbe.bad(Fn.new { Font.load(\"unapproved path\") })\n"
		    "FontProbe.check(Font.codepageFor(249) == null)\n"
		    "Screen.font[0] = 249\n"
		    "FontProbe.check(Font.codepage == null && !Codepage.encodes_(\"A\"))\n"
		    "Screen.font[0] = 0\n"
		    "for (i in 0...4) CTerm.font[i] = old[i]\n") == 0);
		CHECK(st_cterm_set_encoding(terminal, ST_CTERM_ENCODING_UTF8, ST_CODEPAGE_CP1251) == 0);
		CHECK(st_wren_connected_interpret(connected, "font_api_default_change",
		    "import \"font_api_setup\" for file, FontProbe\n"
		    "import \"syncterm\" for Font, Codepage, FontError, FontErr\n"
		    "var selector = Font.load(file)\n"
		    "FontProbe.check(selector is Num && Font.codepageFor(selector) == Codepage.cp1251)\n"
		    "var unsupported = Font.load(file, Codepage.cp1251_b)\n"
		    "FontProbe.check(unsupported is FontError && unsupported.code == FontErr.unsupportedCodepage)\n") == 0);
		/* Malformed bitmap and deleted pathname preserve already-loaded data. */
		CHECK(write_file(path, "bad") == 0);
		CHECK(st_wren_connected_interpret(connected, "font_api_bitmap_error",
		    "import \"font_api_setup\" for file, FontProbe\n"
		    "import \"syncterm\" for Font, FontError, FontErr, Error\n"
		    "var before = Font.count\n"
		    "var result = Font.load(file)\n"
		    "FontProbe.check(result is FontError && result is Error && result.code == FontErr.invalidBitmap && result.nativeCode < 0 && result.toString.startsWith(\"FontError\"))\n"
		    "FontProbe.check(Font.count == before)\n") == 0);
		CHECK(fs_remove_file(path) == 0);
		CHECK(st_wren_connected_interpret(connected, "font_api_file_error",
		    "import \"font_api_setup\" for file, FontProbe\n"
		    "import \"syncterm\" for Font, FileError, IOErr\n"
		    "var result = Font.load(file)\n"
		    "FontProbe.check(result is FileError && result.code == IOErr.notFound)\n") == 0);
		stream = fopen(path, "wb");
		CHECK(stream != NULL && fwrite(bitmap, 1, sizeof(bitmap), stream) == sizeof(bitmap));
		CHECK(fclose(stream) == 0);
		CHECK(st_wren_connected_interpret(connected, "font_api_capacity",
		    "import \"font_api_setup\" for file, FontProbe\n"
		    "import \"syncterm\" for Font, FontError, FontErr\n"
		    "var result = null\n"
		    "for (i in 0...256) {\n"
		    "  result = Font.load(file)\n"
		    "  if (result is FontError) break\n"
		    "}\n"
		    "FontProbe.check(result is FontError && result.code == FontErr.noSlots && result.nativeCode == 0)\n") == 0);
		st_wren_connected_destroy(connected);
		st_cterm_destroy(&terminal);
		st_font_catalog_destroy(catalog);
		st_profile_dispose(&session);
		if (!shared) st_console_destroy(console);
	}
	/* A grant is rooted in its original VM while the fixture changes that
	 * VM's current context: this must produce a permission failure, not I/O. */
	CHECK(wrenInterpret(host->vm, "font_grant_owner",
	    "import \"syncterm\" for Font, FontError, FontErr, FileError, IOErr\n"
	    "class GrantProbe {\n"
	    "  static check(file, permission) {\n"
	    "    var result = Font.load(file)\n"
	    "    if (permission) {\n"
	    "      if (!(result is FileError) || result.code != IOErr.permissionDenied) Fiber.abort(\"font grant authority\")\n"
	    "    } else {\n"
	    "      if (!(result is FontError) || result.code != FontErr.unavailable) Fiber.abort(\"font without terminal\")\n"
	    "    }\n"
	    "  }\n"
	    "}\n") == WREN_RESULT_SUCCESS);
	wrenEnsureSlots(host->vm, 3);
	CHECK(st_wren_file_push(host->vm, 1, path, ST_WREN_PICKER_DISPOSITION_READ, true));
	WrenHandle *grant = wrenGetSlotHandle(host->vm, 1);
	WrenHandle *call = wrenMakeCallHandle(host->vm, "check(_,_)");
	CHECK(call != NULL);
	for (unsigned permission = 0; permission < 2; ++permission) {
		wrenEnsureSlots(host->vm, 3);
		wrenGetVariable(host->vm, "font_grant_owner", "GrantProbe", 0);
		wrenSetSlotHandle(host->vm, 1, grant);
		wrenSetSlotBool(host->vm, 2, permission != 0);
		wrenSetUserData(host->vm, permission ? &host->picker : &host->menu);
		WrenInterpretResult result = wrenCall(host->vm, call);
		wrenSetUserData(host->vm, &host->menu);
		CHECK(result == WREN_RESULT_SUCCESS);
	}
	wrenReleaseHandle(host->vm, call);
	wrenReleaseHandle(host->vm, grant);
	CHECK(fs_remove_file(path) == 0);
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
		.graphics_mode = ST_CTERM_GRAPHICS_RIP_1,
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
	    "Screen.cursor.setVisible(false)\n"
	    "ScrollbackView.paint_(viewport, live, Scrollback.height, Scrollback.height, x, y)\n"
	    "if (Screen.window.position[0] != x || Screen.window.position[1] != y) Fiber.abort(\"scrollback cursor position\")\n"
	    "if (Screen.cursor.visible) Fiber.abort(\"scrollback hidden cursor\")\n"
	    "Screen.cursor.setVisible(true)\n"
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
	    "  var selector = Font.load(FontGrant.file)\n"
	    "  if (!(selector is Num)) Fiber.abort(\"font load\")\n"
	    "  if (!Font.available(selector) || Font.name(selector) == \"\") Fiber.abort(\"loaded font listing\")\n"
	    "  CTerm.font[0] = selector\n"
	    "  if (CTerm.font[0] != selector) Fiber.abort(\"loaded font select\")\n"
	    "  CTerm.font[0] = 0\n"
	    "}\n") == 0);
	CHECK(st_wren_connected_interpret(connected, "cterm_state_view",
	    "import \"syncterm\" for CTerm, Font, ScreenSupports\n"
	    "if (CTerm.x < 1 || CTerm.y < 1 || CTerm.width < 1 || CTerm.height < 1) Fiber.abort(\"cursor\")\n"
	    "if (CTerm.topMargin < 1 || CTerm.bottomMargin > CTerm.height || CTerm.leftMargin < 1 || CTerm.rightMargin > CTerm.width) Fiber.abort(\"margins\")\n"
	    "if (CTerm.attr < 0 || CTerm.fgColor < 0 || CTerm.bgColor < 0) Fiber.abort(\"colors\")\n"
	    "if (CTerm.fontSlot < 0 || CTerm.font[0] < 0) Fiber.abort(\"fonts\")\n"
	    "var invalidFont = Fiber.new { Font.load(\"not a File\") }\n"
	    "invalidFont.try()\n"
	    "if (invalidFont.error == null) Fiber.abort(\"font consent\")\n"
	    "if (!(Fiber.new { CTerm.saveScreenshot(\"not a File\", true) }.try() is String)) Fiber.abort(\"screenshot consent\")\n"
	    "if (ScreenSupports.fontSelection) {\n"
	    "  var before = CTerm.font[0]\n"
	    "  CTerm.font[0] = before\n"
	    "  if (CTerm.font[0] != before) Fiber.abort(\"font setter\")\n"
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
	CHECK(st_wren_connected_handle_event(connected, &(struct st_console_event){.type = ST_CONSOLE_EVENT_PHYSICAL_KEY, .data.physical_key = {.evdev = 48, .pressed = true, .modifiers = command_modifiers(host)}}, &claimed) == 0);
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
	CHECK(st_wren_connected_handle_event(connected, &(struct st_console_event){.type = ST_CONSOLE_EVENT_PHYSICAL_KEY, .data.physical_key = {.evdev = 46, .pressed = true, .modifiers = command_modifiers(host)}}, &claimed) == 0 && claimed);
	claimed = false;
	CHECK(st_wren_connected_handle_event(connected, &escape,
	    &claimed) == 0 && claimed);
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_last_error(connected) == NULL);
	CHECK(st_wren_connected_interpret(connected, "capture_modal_state",
	    "import \"syncterm\" for CTerm\n"
	    "if (CTerm.suspended) Fiber.abort(\"capture modal did not restore\")\n") == 0);
	claimed = false;
	CHECK(st_wren_connected_handle_event(connected, &(struct st_console_event){.type = ST_CONSOLE_EVENT_PHYSICAL_KEY, .data.physical_key = {.evdev = 44, .pressed = true, .modifiers = command_modifiers(host)}}, &claimed) == 0);
	CHECK(claimed);
	claimed = false;
	CHECK(st_wren_connected_handle_event(connected, &escape,
	    &claimed) == 0);
	CHECK(claimed);
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_last_error(connected) == NULL);
	claimed = false;
	CHECK(st_console_post_event(console, &escape) == 0);
	CHECK(st_wren_connected_handle_event(connected, &(struct st_console_event){.type = ST_CONSOLE_EVENT_PHYSICAL_KEY, .data.physical_key = {.evdev = 41, .pressed = true, .modifiers = ST_CONSOLE_MODIFIER_CONTROL}}, &claimed) == 0);
	CHECK(claimed && st_wren_connected_last_error(connected) == NULL);
	CHECK(st_wren_connected_interpret(connected, "cterm_write_probe",
	    "import \"syncterm\" for CTerm\n"
	    "var error = CTerm.write(String.fromCodePoint(27) + \"[HXY\")\n"
	    "if (error != null) Fiber.abort(error)\n") == 0);
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
	CHECK(st_wren_connected_capture_wire(connected,
	    (const uint8_t *)"A\x1b[31m\xc3\xa9", 8) == 0);
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
	st_wren_connected_capture_pause(connected);
	CHECK(st_wren_connected_capture_wire(connected, (const uint8_t *)"H", 1) == 0);
	CHECK(st_cterm_write(cterm, "H", 1, &consumed) == 0 && consumed == 1);
	st_wren_connected_capture_resume(connected);
	static const char raw_input[] = "D\x1b[32m\xc3\xa9\xff\xef\xbf\xbd\x00\xc3";
	for (size_t i = 0; i < sizeof(raw_input) - 1; ++i) {
		CHECK(st_wren_connected_capture_wire(connected,
		    (const uint8_t *)raw_input + i, 1) == 0);
		CHECK(st_cterm_write(cterm, raw_input + i, 1, &consumed) == 0 && consumed == 1);
	}
	CHECK(st_cterm_finish(cterm) == 0);
	CHECK(st_cterm_write_utf8(cterm, "injected", 8, &consumed) == 0 && consumed == 8);
	CHECK(fflush(raw) == 0 && fseek(raw, 0, SEEK_SET) == 0);
	CHECK(fread(captured, 1, sizeof(captured), raw) == sizeof(raw_input) - 1);
	CHECK(memcmp(captured, raw_input, sizeof(raw_input) - 1) == 0);
	st_wren_connected_destroy(connected);
	connected = NULL;
	st_cterm_destroy(&cterm);
	terminal_config.encoding = ST_CTERM_ENCODING_CODEPAGE;
	terminal_config.graphics_mode = ST_CTERM_GRAPHICS_OFF;
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
	    codepage_history, width, codepage_rows, ST_SCREEN_MODE_80X25, false, NULL) == 0);
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
	    saved_history, saved_columns, saved_rows, saved_mode, saved_status, NULL) == 0);
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
exercise_remote_shutdown(const struct st_wren_connected_config *config,
	struct fake_sftp *fake)
{
	st_wren_connected_t *connected = NULL;
	st_console_snapshot_t *snapshot = NULL;

	CHECK(st_console_snapshot_create(&snapshot, config->console) == 0);
	CHECK(st_wren_connected_create(&connected, config) == 0);
	CHECK(st_wren_connected_interpret(connected, "remote_shutdown",
	    "import \"syncterm\" for SFTP, FileFlag, Hook, Screen, OperationError\n"
	    "var first = SFTP.open(\"/parked\", FileFlag.read)\n"
	    "var second = SFTP.opendir(\"/closing\")\n") == 0);
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "remote_shutdown",
	    "var parked = first.result\n"
	    "var closing = second.result\n"
	    "var resumed = false\n"
	    "Fiber.new {\n"
	    "  parked.use(Fn.new {|file|\n"
	    "    file.readBytes(2, 0).await()\n"
	    "    resumed = true\n"
	    "  })\n"
	    "}.call()\n"
	    "var closeOperation = closing.close()\n"
	    "if (!closing.isClosing || parked.isClosed) Fiber.abort(\"shutdown resource setup\")\n"
	    "Hook.onDisconnect {\n"
	    "  if (resumed || !parked.isClosed || !closing.isClosed || closing.isClosing || !(closeOperation.result is OperationError)) Fiber.abort(\"resource shutdown state\")\n"
	    "  if (parked.close().result != null) Fiber.abort(\"shutdown repeated close\")\n"
	    "  var rejected = Fiber.new { parked.readBytes(1, 0) }\n"
	    "  rejected.try()\n"
	    "  if (rejected.error == null) Fiber.abort(\"shutdown resource reuse\")\n"
	    "  var size = Screen.size\n"
	    "  Screen.window.setBounds([1, 1, size[0], size[1]])\n"
	    "  Screen.window.setPosition([1, 1])\n"
	    "  Screen.window.print(\"REMOTE-END\")\n"
	    "}\n") == 0);
	uint64_t before = fake->next_id;
	st_wren_connected_destroy(connected);
	CHECK(fake->next_id == before);
	CHECK(console_has_text(config->console, 0, 0, "REMOTE-END"));
	CHECK(st_console_snapshot_restore(config->console, snapshot, NULL) == 0);
	st_console_snapshot_destroy(snapshot);
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
	CHECK(st_wren_connected_interpret(connected, "operation_sftp_cancel",
	    "import \"syncterm\" for SFTP, FileFlag, OperationError\n"
	    "var cancelledOpen = SFTP.open(\"/cancelled.bin\", FileFlag.read)\n"
	    "if (!cancelledOpen.cancel() || !(cancelledOpen.result is OperationError)) Fiber.abort(\"cancelled open\")\n"
	    ) == 0);
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(fake.close_count == 1);
	fake.submit_error = true;
	CHECK(st_wren_connected_interpret(connected, "operation_sftp_failure",
	    "import \"syncterm\" for SFTP, SFTPError\n"
	    "var failed = SFTP.stat(\"/failed\")\n"
	    "if (!failed.isDone || !(failed.await() is SFTPError)) Fiber.abort(\"immediate provider failure\")\n"
	    ) == 0);
	fake.submit_error = false;
	CHECK(st_wren_connected_interpret(connected, "operation_sftp_order",
	    "import \"syncterm\" for SFTP, SFTPError\n"
	    "class OperationSftpProbe {\n"
	    "  static values { __values }\n"
	    "  static values=(v) { __values = v }\n"
	    "}\n"
	    "var first = SFTP.stat(\"/first\")\n"
	    "var second = SFTP.stat(\"/second\")\n"
	    "Fiber.new {\n"
	    "  var a = first.await()\n"
	    "  var b = second.await()\n"
	    "  OperationSftpProbe.values = [a.mtime, b.mtime]\n"
	    "}.call()\n"
	    ) == 0);
	CHECK(fake.count == 2);
	struct fake_sftp_response first_response = fake.responses[fake.head];
	size_t second_response = (fake.head + 1) % (sizeof(fake.responses) / sizeof(fake.responses[0]));
	fake.responses[fake.head] = fake.responses[second_response];
	fake.responses[second_response] = first_response;
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "operation_sftp_order_verify",
	    "import \"operation_sftp_order\" for OperationSftpProbe\n"
	    "import \"syncterm\" for SFTP, SFTPError\n"
	    "if (OperationSftpProbe.values.count != 2 || OperationSftpProbe.values[0] != 111 || OperationSftpProbe.values[1] != 222) Fiber.abort(\"out of order SFTP results\")\n"
	    "var operations = []\n"
	    "for (i in 0...256) {\n"
	    "  var op = SFTP.stat(\"/limit\")\n"
	    "  if (op.isDone) Fiber.abort(\"premature request limit\")\n"
	    "  operations.add(op)\n"
	    "}\n"
	    "if (!(SFTP.stat(\"/overflow\").result is SFTPError)) Fiber.abort(\"unbounded requests\")\n"
	    "for (op in operations) op.cancel()\n"
	    "if (!(SFTP.stat(\"/cancelled-pending\").result is SFTPError)) Fiber.abort(\"cancelled native requests lost their bound\")\n"
	    ) == 0);
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "operation_sftp_abandoned",
	    "import \"syncterm\" for SFTP, FileFlag\n"
	    "SFTP.open(\"/unclaimed.bin\", FileFlag.read)\n"
	    "System.gc()\n") == 0);
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(st_wren_connected_interpret(connected, "operation_sftp_abandoned_collect",
	    "System.gc()\nSystem.gc()\n") == 0);
	CHECK(st_wren_connected_pump(connected, 0) == 0);
	CHECK(fake.close_count == 2);
	CHECK(exercise_sftp_integer_contract(connected, &fake) == 0);
	CHECK(exercise_remote_resource_contract(connected, &fake) == 0);
	CHECK(exercise_remote_outcomes(connected, &fake) == 0);
	CHECK(exercise_filesystem_outcomes(connected) == 0);
	CHECK(exercise_filesystem_path_handles(connected) == 0);
	CHECK(st_wren_connected_interpret(connected, "sftp_batch",
	    "import \"syncterm\" for SFTP\n"
	    "class Batch {\n"
	    "  static count { __count }\n"
	    "  static count=(n) { __count = n }\n"
	    "}\n"
	    "Batch.count = 0\n"
	    "for (i in 0...3) SFTP.stat(\"/remote.bin\").then(Fn.new {|r| Batch.count = Batch.count + 1 })\n") == 0);
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
	    "if (replaced is FileError || replaced != null) Fiber.abort(\"replace\")\n"
	    "var f = Cache.list[\"fs-test.won\"]\n"
	    "if (!(f is File) || (f.open() is FileError) || f.read() != \"atomic\" || (f.close() is FileError)) Fiber.abort(\"file round trip\")\n"
	    "if (Cache.delete(\"fs-test.won\") != null) Fiber.abort(\"delete\")\n") == 0);
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
	const struct { const char *field; const char *value; } invalid_queues[] = {
		{"nextId", "9007199254740992"},
		{"id", "9007199254740991"},
		{"total", "9007199254740992"},
		{"resume", "9007199254740992"},
		{"sourceSize", "9007199254740992"},
		{"sourceMtime", "9007199254740992"},
		{"sourceMtime", "-9007199254740992"},
		{"total", "0.5"},
	};
	for (size_t i = 0; i < sizeof(invalid_queues) / sizeof(invalid_queues[0]); ++i) {
		const char *keys[] = {"nextId", "id", "total", "resume", "sourceSize", "sourceMtime"};
		const char *values[] = {"2", "1", "1", "0", "1", "0"};
		for (size_t field = 0; field < sizeof(keys) / sizeof(keys[0]); ++field) {
			if (strcmp(keys[field], invalid_queues[i].field) == 0)
				values[field] = invalid_queues[i].value;
		}
		char document[512];
		CHECK(snprintf(document, sizeof(document),
		    "{\"version\":2,\"nextId\":%s,\"jobs\":[{\"id\":%s,\"dir\":\"down\","
		    "\"local\":\"invalid.bin\",\"remote\":\"/remote.bin\",\"total\":%s,"
		    "\"resume\":%s,\"sourceSize\":%s,\"sourceMtime\":%s,\"cancel\":false}]}",
		    values[0], values[1], values[2], values[3], values[4], values[5]) < (int)sizeof(document));
		CHECK(write_file(queue_path, document) == 0);
		CHECK(st_wren_connected_create(&connected, &config) == 0);
		CHECK(st_wren_connected_interpret(connected, "inexact_queue_result",
		    "import \"sftp_queue\" for SftpQueue\n"
		    "if (!SftpQueue.fatal) Fiber.abort(\"inexact persisted queue accepted\")\n") == 0);
		st_wren_connected_destroy(connected);
		connected = NULL;
		CHECK(file_equals(queue_path, document));
	}
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
	CHECK(exercise_remote_shutdown(&config, &fake) == 0);
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
	CHECK(wrenInterpret(host->vm, "unloaded_defaults",
	    "import \"syncterm_menu\" for Menu\n"
	    "if (Menu.defaults == null && (Menu.defaultsDirty || (Menu.saveDefaults() == null) != false)) Fiber.abort(\"unloaded defaults save\")\n")
	    == WREN_RESULT_SUCCESS);
	static const char source[] =
	    "import \"syncterm_menu\" for Menu\n"
	    "if (Menu.load(null) != null) Fiber.abort(\"load\")\n"
	    "if (Menu.applicationTitle != \"SyncTERM test - Headless\") Fiber.abort(\"application title\")\n"
	    "if (Menu.currentScreenMode != 0) Fiber.abort(\"current mode\")\n"
	    "if (Menu.setScreenMode(3) != null || Menu.currentScreenMode != 3 || Menu.setScreenMode(1) != null) Fiber.abort(\"set mode\")\n"
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
	    "s.midiModes = 0\n"
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
	    "if (!s.dirty || !(s.apply() == null)) Fiber.abort(\"settings\")\n"
	    "var b = Menu.entries[0]\n"
	    "b.profile.addr = \"alpha.example\"\n"
	    "b.profile.port = 2323\n"
	    "b.profile.connType = 4\n"
	    "b.profile.user = \"guest\"\n"
	    "b.profile.password = \"password\"\n"
	    "b.profile.syspass = \"system\"\n"
	    "b.profile.music = 0\n"
	    "b.profile.graphics = 0\n"
	    "b.profile.comment = \"comment\"\n"
	    "b.profile.addressFamily = 0\n"
	    "b.profile.termName = \"ansi\"\n"
	    "b.profile.screenMode = 1\n"
	    "b.profile.bpsRate = 0\n"
	    "b.profile.font = \"Codepage 437 English\"\n"
	    "b.profile.encoding = 255\n"
	    "if (b.profile.encoding != 255) Fiber.abort(\"bbs encoding\")\n"
	    "b.profile.noStatus = false\n"
	    "b.profile.hidePopups = false\n"
	    "b.profile.yellowIsYellow = true\n"
	    "b.profile.forceLcf = false\n"
	    "b.profile.dlDir = \"download\"\n"
	    "b.profile.ulDir = \"upload\"\n"
	    "b.profile.logFile = \"session.log\"\n"
	    "b.profile.appendLogFile = true\n"
	    "b.profile.xferLogLevel = 6\n"
	    "b.profile.protocolLogLevel = 6\n"
	    "b.profile.stopBits = 1\n"
	    "b.profile.dataBits = 8\n"
	    "b.profile.parity = 0\n"
	    "b.profile.flowControl = 0\n"
	    "b.profile.telnetNoBinary = false\n"
	    "b.profile.deferTelnetNegotiation = false\n"
	    "b.profile.tlsTrustWebPki = true\n"
	    "b.profile.tlsTrustedCert = \"server.pem\"\n"
	    "b.profile.tlsClientCert = \"client.pem\"\n"
	    "b.profile.tlsClientKey = \"client.key\"\n"
	    "b.profile.tlsPskIdentity = \"identity\"\n"
	    "b.profile.tlsPsk = \"secret\"\n"
	    "b.profile.tlsPskVersion = 1\n"
	    "b.profile.tlsVersionFloor = 1\n"
	    "b.profile.ghostProgram = \"ghost\"\n"
	    "b.profile.sftpPublicKey = true\n"
	    "b.profile.sshAllowAes128Cbc = false\n"
	    "b.profile.sshAcceptEarlyData = false\n"
	    "b.profile.palette = [0, 11184810]\n"
	    "b.profile.wrenScripts = [\"ui_list\", \"menu_bbs_editor\", \"ui_list\"]\n"
	    "b.sortOrder = 4\n"
	    "b.profile.lfExpand = false\n"
	    "if (!b.dirty || b.profile.connTypeName.count == 0 || b.profile.palette.count != 2 || b.profile.wrenScripts.count != 2) Fiber.abort(\"bbs\")\n"
	    "\n";
	static const char encryption_source[] =
	    "import \"syncterm_menu\" for Menu\n"
	    "if (Menu.encryptionAvailable) {\n"
	    "  if (!(Menu.setEncryption(6, 128, \"secret\") == null) || Menu.encryptionInfo.algorithm != 6 || Menu.encryptionInfo.keyBits != 128) Fiber.abort(\"encrypt\")\n"
	    "  if (!(Menu.setEncryption(7, 256, null) == null) || Menu.encryptionInfo.algorithm != 7) Fiber.abort(\"reencrypt\")\n"
	    "  if (!(Menu.setEncryption(0, 0, null) == null) || Menu.encryptionInfo.algorithm != 0) Fiber.abort(\"decrypt\")\n"
	    "}\n";
	static const char numeric_source[] =
	    "import \"syncterm_menu\" for Menu\n"
	    "var s = Menu.settings\n"
	    "s.audioModes = 4294967295\n"
	    "s.midiModes = 4294967295\n"
	    "if (s.midiModes != 4294967295) Fiber.abort(\"MIDI mask maximum\")\n"
	    "var badMidi = Fiber.new { s.midiModes = 4294967296 }\n"
	    "badMidi.try()\n"
	    "if (badMidi.error == null || s.midiModes != 4294967295) Fiber.abort(\"MIDI mask overflow\")\n"
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
	    "s.midiModes = 0\n"
	    "s.modemRate = 0\n"
	    "s.scrollbackLines = 1\n"
	    "if (s.audioModes != 0 || s.modemRate != 0 || s.scrollbackLines != 1) Fiber.abort(\"settings numeric minima\")\n"
	    "var b = Menu.entries[0]\n"
	    "var saved = [b.profile.port, b.profile.bpsRate, b.sortOrder]\n"
	    "b.profile.port = 65535\n"
	    "b.profile.bpsRate = 4294967295\n"
	    "b.sortOrder = -2147483648\n"
	    "if (b.profile.port != 65535 || b.profile.bpsRate != 4294967295 || b.sortOrder != -2147483648) Fiber.abort(\"bbs numeric bounds\")\n"
	    "bad = Fiber.new { b.profile.port = 65536 }\n"
	    "bad.try()\n"
	    "if (bad.error == null || b.profile.port != 65535) Fiber.abort(\"bbs port overflow\")\n"
	    "bad = Fiber.new { b.profile.bpsRate = 4294967296 }\n"
	    "bad.try()\n"
	    "if (bad.error == null || b.profile.bpsRate != 4294967295) Fiber.abort(\"bbs rate overflow\")\n"
	    "bad = Fiber.new { b.sortOrder = -2147483649 }\n"
	    "bad.try()\n"
	    "if (bad.error == null || b.sortOrder != -2147483648) Fiber.abort(\"bbs sort underflow\")\n"
	    "b.profile.port = 0\n"
	    "b.profile.bpsRate = 0\n"
	    "b.sortOrder = 2147483647\n"
	    "if (b.profile.port != 0 || b.profile.bpsRate != 0 || b.sortOrder != 2147483647) Fiber.abort(\"bbs numeric opposite bounds\")\n"
	    "b.profile.port = saved[0]\n"
	    "b.profile.bpsRate = saved[1]\n"
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
	    "var linked = {\"screenMode\": 1, \"encoding\": 0, \"font\": Menu.fontsCatalog[0][1], \"graphics\": 0, \"noStatus\": false}\n"
	    "BbsEditor.changeEncoding_(linked, 24)\n"
	    "if (linked[\"screenMode\"] != 17 || linked[\"font\"] != Menu.fontsCatalog[32][1]) Fiber.abort(\"PETSCII upper\")\n"
	    "BbsEditor.changeFont_(linked, Menu.fontsCatalog[35][1])\n"
	    "if (linked[\"screenMode\"] != 18 || linked[\"encoding\"] != 25) Fiber.abort(\"PETSCII lower\")\n"
	    "BbsEditor.changeEncoding_(linked, 255)\n"
	    "if (linked[\"screenMode\"] != 1 || linked[\"font\"] != Menu.fontsCatalog[0][1]) Fiber.abort(\"UTF-8 switch\")\n"
	    "BbsEditor.changeFont_(linked, Menu.fontsCatalog[44][1])\n"
	    "if (linked[\"screenMode\"] != 1 || linked[\"encoding\"] != 255) Fiber.abort(\"UTF-8 font\")\n"
	    "BbsEditor.changeGraphics_(linked, 1)\n"
	    "if (linked[\"screenMode\"] != 5 || linked[\"encoding\"] != 0 || linked[\"font\"] != Menu.fontsCatalog[45][1]) Fiber.abort(\"RIP v1\")\n"
	    "BbsEditor.changeScreen_(linked, 25)\n"
	    "if (linked[\"graphics\"] != 0 || linked[\"encoding\"] != 26 || linked[\"font\"] != Menu.fontsCatalog[43][1]) Fiber.abort(\"Prestel\")\n"
	    "for (defaults in [false, true]) {\n"
	    "  var bbs = defaults ? Menu.defaults : Menu.entries[0]\n"
	    "  for (encoding in [0, 12, 255]) {\n"
	    "    var draft = BbsEditor.draft_(bbs)\n"
	    "    draft[\"screenMode\"] = 1\n"
	    "    draft[\"graphics\"] = 0\n"
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
	CHECK(wrenInterpret(host->vm, "amiga_mode_contract",
	    "import \"syncterm_menu\" for Menu\n"
	    "import \"syncterm\" for ScreenMode\n"
	    "if (ScreenMode.amiga640x200 != 30 || ScreenMode.amiga640x400 != 31) Fiber.abort(\"Amiga mode constants\")\n"
	    "if (Menu.screenModes[31] != \"Amiga 640x400\" || Menu.canvasModes[32][0] != 31 || Menu.canvasModes[32][1] != \"640x400 (Amiga 640x400)\") Fiber.abort(\"Amiga mode catalogs\")\n"
	    "if (Menu.setScreenMode(ScreenMode.amiga640x400) != null || Menu.currentScreenMode != 31 || Menu.paletteDefaults(31)[0] != 16 || Menu.setScreenMode(1) != null) Fiber.abort(\"Amiga mode selection\")\n"
	    "var s = Menu.settings\n"
	    "s.startupMode = ScreenMode.amiga640x400\n"
	    "if (s.startupMode != 31) Fiber.abort(\"Amiga startup mode\")\n"
	    "var profile = Menu.entries[0].profile\n"
	    "profile.screenMode = ScreenMode.amiga640x400\n"
	    "if (profile.screenMode != 31) Fiber.abort(\"Amiga profile mode\")\n"
	    "profile.screenMode = 1\n") == WREN_RESULT_SUCCESS);
	CHECK(wrenInterpret(host->vm, "cga_mode_contract",
	    "import \"syncterm_menu\" for Menu\n"
	    "import \"syncterm\" for ScreenMode\n"
	    "if (ScreenMode.cga80x25 != 32 || ScreenMode.ega80x25 != 23 || ScreenMode.vga80x25 != 24) Fiber.abort(\"stable PC modes\")\n"
	    "var names = Menu.screenModes\n"
	    "var choices = Menu.screenModeChoices\n"
	    "var canvases = Menu.canvasModes\n"
	    "var order = [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,32,23,24,25,26,27,28,29,30,31]\n"
	    "if (names.count != 33 || names[32] != \"CGA 80x25\" || choices.count != 33 || canvases.count != 33) Fiber.abort(\"CGA catalogs\")\n"
	    "for (i in 0...order.count) {\n"
	    "  if (choices[i][0] != order[i] || choices[i][1] != names[order[i]] || canvases[i][0] != order[i]) Fiber.abort(\"CGA choice order\")\n"
	    "}\n"
	    "if (canvases[23][1] != \"640x200 (CGA 80x25)\") Fiber.abort(\"CGA canvas\")\n"
	    "choices.clear()\n"
	    "if (Menu.screenModeChoices.count != 33) Fiber.abort(\"mutable choice catalog\")\n"
	    "if (Menu.setScreenMode(32) != null || Menu.currentScreenMode != 32 || Menu.paletteDefaults(32)[0] != 16) Fiber.abort(\"CGA selection\")\n"
	    "var settings = Menu.settings\n"
	    "var priorStartup = settings.startupMode\n"
	    "settings.startupMode = 32\n"
	    "if (settings.save() != null || Menu.settings.startupMode != 32) Fiber.abort(\"CGA startup persistence\")\n"
	    "var profile = Menu.entries[0].profile\n"
	    "profile.screenMode = 32\n"
	    "if (profile.screenMode != 32) Fiber.abort(\"CGA profile\")\n"
	    "profile.screenMode = 1\n"
	    "settings.startupMode = priorStartup\n"
	    "if (settings.save() != null || Menu.setScreenMode(1) != null) Fiber.abort(\"CGA restore\")\n") == WREN_RESULT_SUCCESS);
	if (wrenInterpret(host->vm, "menu_encryption_contract", encryption_source)
	    != WREN_RESULT_SUCCESS) {
		fprintf(stderr, "encryption contract failed: %s\n", host->last_error);
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
	CHECK(wrenInterpret(host->vm, "amiga_font_contract",
	    "import \"syncterm_menu\" for Menu, MenuError, AmigaFontFamily\n"
	    "import \"syncterm\" for GraphicsMode\n"
	    "if (GraphicsMode.skypix13 != 3 || GraphicsMode.skypix31 != 4 || GraphicsMode.igs221 != 5 || Menu.graphicsModes.count != 6) Fiber.abort(\"graphics modes\")\n"
	    "var catalog = Menu.amigaFonts\n"
	    "if (catalog.count != 11) Fiber.abort(\"compiled Amiga families\")\n"
	    "var strikes = 0\n"
	    "for (family in catalog) {\n"
	    "  if (!(family is AmigaFontFamily) || family.source != \"bundled\" || family.error != null || family.installedSizes.count != 0) Fiber.abort(\"Amiga family metadata\")\n"
	    "  var sizes = family.sizes\n"
	    "  strikes = strikes + sizes.count\n"
	    "  sizes.clear()\n"
	    "  if (family.sizes.count == 0) Fiber.abort(\"mutable family metadata\")\n"
	    "}\n"
	    "if (strikes != 35 || !(Menu.removeAmigaFont(catalog[0]) is MenuError)) Fiber.abort(\"immutable bundled fonts\")\n"
	    "var mutate = Fiber.new { catalog[0].name = \"modified\" }\n"
	    "mutate.try()\n"
	    "if (mutate.error == null) Fiber.abort(\"writable Amiga metadata\")\n"
	    ) == WREN_RESULT_SUCCESS);
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
exercise_emoji_contract(st_wren_host_t *public_host)
{
	struct st_wren_host *host = public_host;
	static const char source[] =
	    "import \"syncterm\" for Emoji, Text, Key, KeyEvent, Host, Cell, Surface, Screen\n"
	    "import \"ui_input\" for TextInput\n"
	    "import \"ui_app\" for App\n"
	    "import \"ui_emoji_picker\" for EmojiPicker\n"
	    "import \"ui_draw\" for Painter\n"
	    "import \"ui_widget\" for Rect\n"
	    "import \"ui_style\" for Theme\n"
	    "var app = App.new()\n"
	    "if ((app.binding(112, 2) != null) != Host.localControlKeys) Fiber.abort(\"local shortcut policy\")\n"
	    "if (app.openEmojiPicker()) Fiber.abort(\"picker without focused field\")\n"
	    "var matches = Emoji.search(\"MAN SHRUGGING: MEDIUM SKIN TONE\")\n"
	    "var exact = matches.where {|e| e.text == \"🤷🏽‍♂️\" }.toList\n"
	    "if (exact.count != 1 || exact[0].text != \"🤷🏽‍♂️\") Fiber.abort(\"exact variant name search\")\n"
	    "var variant = exact[0]\n"
	    "if (variant.variants.count != 18) Fiber.abort(\"shrug gender and tone grouping\")\n"
	    "if (Emoji.entries.any {|e| e.text == variant.text }) Fiber.abort(\"variants leaked into base grid\")\n"
	    "if (Emoji.search(\"zzqq no emoji\").count != 0) Fiber.abort(\"empty search\")\n"
	    "var bikini = Emoji.search(\"bikini\").where {|e| e.text == \"👙\" }.toList[0]\n"
	    "if (!bikini.keywords.contains(\"pool\")) Fiber.abort(\"English search keywords\")\n"
	    "if (bikini.keywords.where {|k| k == \"pool\" }.count != 1) Fiber.abort(\"duplicate keywords\")\n"
	    "if (Text.graphemes(\"a\" + variant.text + \"b\").count != 3) Fiber.abort(\"joined segmentation\")\n"
	    "var field = TextInput.new()\n"
	    "field.bounds = Rect.new(1, 1, 30, 1)\n"
	    "field.value = \"ab\"\n"
	    "field.cursor = 1\n"
	    "var changes = 0\n"
	    "field.onChange = Fn.new {|s| changes = changes + 1 }\n"
	    "field.maxLen = 6\n"
	    "if (field.insertText(variant.text) || field.value != \"ab\" || field.cursor != 1 || changes != 0) Fiber.abort(\"non-atomic overflow\")\n"
	    "field.maxLen = 7\n"
	    "if (!field.insertText(variant.text) || field.value != \"a\" + variant.text + \"b\" || field.cursor != 6 || changes != 1) Fiber.abort(\"insert at cursor\")\n"
	    "if (field.columnsBetween_(0, 6) != 3) Fiber.abort(\"joined cursor width\")\n"
	    "if (field.indexAtColumn_(3) != 6) Fiber.abort(\"click after joined emoji\")\n"
	    "field.maxLen = 4\n"
	    "field.selectAll()\n"
	    "if (field.insertText(variant.text) || !field.allSelected || changes != 1) Fiber.abort(\"failed selection insertion\")\n"
	    "field.maxLen = 5\n"
	    "if (!field.insertText(variant.text) || field.value != variant.text || field.allSelected || changes != 2) Fiber.abort(\"selection replacement\")\n"
	    "field.value = \"abcdefg\"\n"
	    "field.cursor = 1\n"
	    "field.maxLen = 7\n"
	    "field.insertMode = false\n"
	    "if (!field.insertText(variant.text) || field.value != \"a\" + variant.text + \"g\" || changes != 3) Fiber.abort(\"overwrite\")\n"
	    "field.insertMode = true\n"
	    "if (field.insertText(\"\\n\") || changes != 3) Fiber.abort(\"control insertion\")\n"
	    "// This cluster has an independent mapping in the synthetic font.\n"
	    "var shaped = \"👩🏽‍❤️‍💋‍👩🏻\"\n"
	    "var sf = Surface.new(4, 1)\n"
	    "Painter.text(sf, 0, 0, shaped + \"!\", Theme.current.style(\"default\"), 4)\n"
	    "if (sf[0].ch != shaped || sf[0].columns != 2 || sf[2].ch != \"!\") Fiber.abort(\"joined text painting\")\n"
	    "var picker = EmojiPicker.new(null)\n"
	    "picker.draw()\n"
	    "picker.handle(KeyEvent.new(Key.f2))\n"
	    "picker.finish()\n";
	if (wrenInterpret(host->vm, "emoji_contract", source) != WREN_RESULT_SUCCESS) {
		fprintf(stderr, "emoji contract failed: %s\n", host->last_error);
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
	    "if (!Menu.offlineScrollbackHasStatus || !Menu.prepareOfflineScrollback()) Fiber.abort(\"history metadata\")\n"
	    "if (Menu.finishOfflineScrollback() != null) Fiber.abort(\"history finish\")\n";

	if (wrenInterpret(host->vm, "session_history_contract", source) != WREN_RESULT_SUCCESS
	    || host->session_mode_active)
		return -1;
	return 0;
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
	    "if (!(current.save() == null) || current.dirty) Fiber.abort(\"save failed\")\n"
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
	    "if (Menu.load(null) != null) Fiber.abort(\"initial load\")\n"
	    "BbsContractState.entry = Menu.entries[0]\n"
	    "BbsContractState.transient = Menu.quickConnect(\"raw://temporary.example:23\")\n";
	static const char failed_load_source[] =
	    "import \"syncterm_menu\" for Menu\n"
	    "import \"bbs_contract_state\" for BbsContractState\n"
	    "if (Menu.load(null) == null) Fiber.abort(\"failed load succeeded\")\n"
	    "if (BbsContractState.entry.name != \"Zulu\" || "
	    "BbsContractState.transient.addr != \"temporary.example\") "
	    "Fiber.abort(\"failed load changed model\")\n";
	static const char successful_load_source[] =
	    "import \"syncterm_menu\" for Menu\n"
	    "import \"bbs_contract_state\" for BbsContractState\n"
	    "if (Menu.load(null) != null) Fiber.abort(\"reload\")\n"
	    "var oldEntry = Fiber.new { BbsContractState.entry.name }.try()\n"
	    "var retainedProfile = BbsContractState.transient.addr\n"
	    "if (!(oldEntry is String) || retainedProfile != \"temporary.example\") "
	    "Fiber.abort(\"successful load retained old handle\")\n";
	static const char categories_source[] =
	    "import \"syncterm_menu\" for Menu, MenuError\n"
	    "class BbsLookup {\n"
	    "  static named(name) {\n"
	    "    for (entry in Menu.entries) {\n"
	    "      if (entry.name == name) return entry\n"
	    "    }\n"
	    "    return null\n"
	    "  }\n"
	    "}\n"
	    "if (Menu.load(null) != null) Fiber.abort(\"category load\")\n"
	    "var system = BbsLookup.named(\"System\")\n"
	    "if (system == null || !system.readOnly) Fiber.abort(\"system entry\")\n"
	    "var readOnly = Fiber.new { system.profile.comment = \"forbidden\" }.try()\n"
	    "if (!(readOnly is String) || (system.rename(\"Other\") == null) || "
	    "(system.save() == null) || (system.delete() == null)) Fiber.abort(\"system mutability\")\n"
	    "if (!Menu.nameAvailable(\"System\") || Menu.nameAvailable(\"Alpha\") "
	    "|| Menu.nameAvailable(\"\") || Menu.nameAvailable(\"bad[name\") || "
	    "Menu.nameAvailable(\"syncterm-system-cache\")) "
	    "Fiber.abort(\"name admission\")\n"
	    "if (!(Menu.create(\"Alpha\") is MenuError) || !(Menu.create(\"\") is MenuError) || "
	    "!(Menu.create(\"bad[name\") is MenuError)) Fiber.abort(\"create admission\")\n"
	    "var defaults = Menu.defaults\n"
	    "var defaultsAgain = Menu.defaults\n"
	    "defaults.comment = \"working defaults\"\n"
	    "if (defaultsAgain.comment != \"working defaults\" || !(Menu.saveDefaults() == null) "
	    "|| Menu.defaultsDirty) Fiber.abort(\"defaults sharing\")\n"
	    "var transient = Menu.quickConnect(\"raw://quick.example:23\")\n"
	    "transient.comment = \"allowed\"\n"
	    "if (transient.readOnly || transient.comment != \"allowed\") Fiber.abort(\"profile value rules\")\n"
	    "var count = Menu.entries.count\n"
	    "var shadow = Menu.create(\"System\")\n"
	    "if (shadow == null || shadow.readOnly || shadow.calls != 0 || "
	    "shadow.connected != 0 || Menu.entries.count != count) "
	    "Fiber.abort(\"system shadow\")\n"
	    "var retainedProfile = transient.addr\n"
	    "var systemStale = Fiber.new { system.name }.try()\n"
	    "var defaultsRetained = defaults.comment\n"
	    "if (retainedProfile != \"quick.example\" || !(systemStale is String) || defaultsRetained != \"working defaults\") Fiber.abort(\"create identity rules\")\n"
	    "var sameShadow = BbsLookup.named(\"System\")\n"
	    "var unrelated = BbsLookup.named(\"Alpha\")\n"
	    "shadow.profile.comment = \"shared record\"\n"
	    "if (sameShadow.profile.comment != \"shared record\") Fiber.abort(\"record sharing\")\n"
	    "if (!(shadow.delete() == null)) Fiber.abort(\"discard shadow\")\n"
	    "var shadowStale = Fiber.new { shadow.name }.try()\n"
	    "var unrelatedRetained = unrelated.name\n"
	    "var restored = BbsLookup.named(\"System\")\n"
	    "if (!(shadowStale is String) || unrelatedRetained != \"Alpha\" || "
	    "restored == null || !restored.readOnly "
	    "|| restored.profile.addr != \"system.example\") Fiber.abort(\"restore system\")\n"
	    "var scratch = Menu.create(\"Scratch\")\n"
	    "if (scratch == null || !(scratch.delete() == null)) Fiber.abort(\"discard draft\")\n"
	    "if (!(Fiber.new { scratch.name }.try() is String)) "
	    "Fiber.abort(\"deleted draft remained usable\")\n"
	    "var accent = Menu.create(\"Cafe\\u0301\")\n"
	    "if (accent == null || accent.name != \"Caf\\u00e9\" || "
	    "!(accent.delete() == null)) Fiber.abort(\"normalized Wren name\")\n";
	static const char capabilities_source[] =
	    "import \"syncterm_menu\" for Menu\n"
	    "import \"bbs_categories\" for BbsLookup, restored, transient, system\n"
	    "var retained = BbsLookup.named(\"Alpha\")\n"
	    "var view = retained.profile\n"
	    "var detached = view.copy()\n"
	    "var original = view.addr\n"
	    "detached.addr = \"detached.example\"\n"
	    "if (view.addr != original || detached.readOnly || restored.cache != null) Fiber.abort(\"profile isolation or system cache\")\n"
	    "for (i in 0...150) {\n"
	    "  if (view.copy().palette.count > 16 || view.copy().wrenScripts.count > 100) Fiber.abort(\"owned profile lists\")\n"
	    "}\n"
	    "System.gc()\n"
	    "var secret = \"e\\u0301\"\n"
	    "detached.password = secret\n"
	    "detached.dlDir = secret\n"
	    "if (detached.password != \"é\" || detached.dlDir != \"é\") Fiber.abort(\"configuration NFC boundary\")\n"
	    "var capability = Menu.create(\"Capability\")\n"
	    "if (capability.cache != null || !(capability.save() == null)) Fiber.abort(\"unsaved cache\")\n"
	    "if (retained.name != \"Alpha\" || view.addr != original) Fiber.abort(\"unrelated create invalidated identity\")\n"
	    "var cache = capability.cache\n"
	    "if (cache.list.containsKey(\"syncterm-cache-name.txt\")) Fiber.abort(\"cache metadata grant\")\n"
	    "for (name in [\"syncterm-cache-name.txt\", \"SYNCTERM-CACHE-NAME.TXT\"]) {\n"
	    "  if (!(Fiber.new { cache.create(name) }.try() is String)) Fiber.abort(\"cache metadata overwrite\")\n"
	    "}\n"
	    "var file = cache.create(\"owned.tmp\")\n"
	    "if (file.open() != null || file.writeBytes(\"original\") != null || file.close() != null) Fiber.abort(\"cache write\")\n"
	    "var listed = cache.list[\"owned.tmp\"]\n"
	    "var nested = cache.createDir(\"child\")\n"
	    "if (!(nested.list is Map)) Fiber.abort(\"child grant\")\n"
	    "if (!(capability.rename(\"Renamed Capability\") == null) || !cache.contains(\"owned.tmp\") || !(capability.save() == null)) Fiber.abort(\"cache rename policy\")\n"
	    "if (!(Fiber.new { cache.list }.try() is String) || !(Fiber.new { listed.open() }.try() is String) || !(Fiber.new { nested.list }.try() is String)) Fiber.abort(\"old cache grants survived rename\")\n"
	    "if (!(Fiber.new { listed.digest(1) }.try() is String)) Fiber.abort(\"stale cache digest survived rename\")\n"
	    "cache = capability.cache\n"
	    "file = cache.list[\"owned.tmp\"]\n"
	    "if (file.open() != null || file.read() != \"original\" || file.close() != null) Fiber.abort(\"renamed cache continuity\")\n"
	    "var removedView = capability.profile\n"
	    "if (!(capability.delete() == null) || file.close() != null) Fiber.abort(\"delete or owned close\")\n"
	    "var replacement = Menu.create(\"Renamed Capability\")\n"
	    "if (!(replacement.save() == null)) Fiber.abort(\"replacement save\")\n"
	    "if (!(Fiber.new { cache.create(\"forbidden.tmp\") }.try() is String) || !(Fiber.new { file.open() }.try() is String) || !(Fiber.new { removedView.addr }.try() is String)) Fiber.abort(\"deleted owner aliased replacement\")\n"
	    "var secondQuick = Menu.quickConnect(\"ssh://second.example\")\n"
	    "if (transient.addr != \"quick.example\" || secondQuick.addr != \"second.example\") Fiber.abort(\"independent quick profiles\")\n"
	    "if (!(replacement.delete() == null) || view.addr != original) Fiber.abort(\"unrelated deletion invalidated view\")\n"
	    "if (!(Fiber.new { system.name }.try() is String)) Fiber.abort(\"shadow restoration revived old identity\")\n";

	static const char save_shadow_source[] =
	    "import \"syncterm_menu\" for Menu\n"
	    "import \"bbs_contract_state\" for BbsContractState\n"
	    "var shadow = Menu.create(\"System\")\n"
	    "shadow.profile.comment = \"persisted shadow\"\n"
	    "if (!(shadow.save() == null) || shadow.dirty || shadow.profile.comment != "
	    "\"persisted shadow\") Fiber.abort(\"save shadow\")\n"
	    "BbsContractState.persisted = shadow\n";
	static const char delete_shadow_source[] =
	    "import \"syncterm_menu\" for Menu\n"
	    "import \"bbs_contract_state\" for BbsContractState\n"
	    "var shadow = BbsContractState.persisted\n"
	    "if (!(shadow.delete() == null)) Fiber.abort(\"delete shadow\")\n"
	    "if (!(Fiber.new { shadow.name }.try() is String)) "
	    "Fiber.abort(\"deleted shadow remained usable\")\n"
	    "var restored = null\n"
	    "for (entry in Menu.entries) {\n"
	    "  if (entry.name == \"System\") restored = entry\n"
	    "}\n"
	    "if (restored == null || !restored.readOnly) Fiber.abort(\"system not restored\")\n";
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
	    "copy.profile.comment = \"shared copy\"\n"
	    "if (same.profile.comment != \"shared copy\" || !(copy.rename(\"Aardvark\") == null) "
	    "|| same.name != \"Aardvark\" || same.searchKey != \"aardvark\" || !(Menu.sort() == null) || "
	    "same.name != \"Aardvark\" || !(copy.save() == null) || copy.dirty || "
	    "copy.searchKey != \"aardvark\") "
	    "Fiber.abort(\"copy lifecycle\")\n"
	    "BbsContractState.persisted = copy\n";
	static const char delete_copy_source[] =
	    "import \"bbs_contract_state\" for BbsContractState\n"
	    "var copy = BbsContractState.persisted\n"
	    "if (copy.name != \"Aardvark\" || !(copy.delete() == null)) "
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
	CHECK(interpret_bbs_contract(host, "bbs_capabilities", capabilities_source) == 0);
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
	CHECK(interpret_bbs_contract(host, "bbs_reload_ownership",
	    "import \"syncterm_menu\" for Menu\n"
	    "import \"bbs_categories\" for BbsLookup\n"
	    "var entry = BbsLookup.named(\"Alpha\")\n"
	    "var profile = entry.profile\n"
	    "var value = profile.copy()\n"
	    "var defaults = Menu.defaults\n"
	    "var cache = entry.cache\n"
	    "var file = cache.create(\"reload.tmp\")\n"
	    "if (Menu.load(null) != null) Fiber.abort(\"ownership reload\")\n"
	    "for (access in [Fn.new { entry.name }, Fn.new { profile.addr }, Fn.new { defaults.addr }, Fn.new { cache.list }, Fn.new { file.open() }]) {\n"
	    "  if (!(Fiber.new { access.call() }.try() is String)) Fiber.abort(\"reload kept borrowed capability\")\n"
	    "}\n"
	    "if (value.addr != \"alpha.example\") Fiber.abort(\"reload expired owned profile\")\n"
	    "var current = BbsLookup.named(\"Alpha\").cache\n"
	    "if (current.delete(\"reload.tmp\") != null) Fiber.abort(\"reload fixture cleanup\")\n") == 0);


	CHECK(write_file(personal,
	    "[Alpha]\nAddress=alpha.example\nLastConnected=100\n"
	    "[Zulu]\nAddress=zulu.example\nLastConnected=200\n") == 0);
	CHECK(write_file(shared, "") == 0);
	CHECK(interpret_bbs_contract(host, "bbs_contract_restore",
	    "import \"syncterm_menu\" for Menu\n"
	    "if (Menu.load(null) != null || Menu.entries.count != 2) "
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
	    "import \"syncterm\" for Host\n"
	    "import \"syncterm_menu\" for Menu\n"
	    "var rows = Menu.buildOptions\n"
	    "var options = {}\n"
	    "for (row in Menu.buildOptions) options[row[1]] = row[2]\n"
	    "if (Menu.buildOptions.count != 37 "
	    "|| !options.containsKey(\"DeuceSSH (SSH)\") "
	    "|| !options.containsKey(\"OpenSSL (TLS, symmetric ciphers)\") "
	    "|| !options.containsKey(\"Operation Overkill ][ Terminal\") "
	    "|| !options.containsKey(\"X11 Support\") "
	    "|| !options.containsKey(\"Wayland\") "
	    "|| !options.containsKey(\"Core Audio\") "
	    "|| !options.containsKey(\"libsndfile\")) "
	    "Fiber.abort(\"build options\")\n"
	    "if (!options[\"Operation Overkill ][ Terminal\"] || !Host.haveOOII || Host.maxOOIIMode != 3) "
	    "Fiber.abort(\"OOII build option\")\n"
	    "var sndfile = options[\"libsndfile\"]\n"
	    "if (options[\"Unifont (SyncTERM Unicode)\"] != "
	    ST_WREN_TEST_UNIFONT_ENABLED
	    ") Fiber.abort(\"Unifont build option\")\n";

	if (wrenInterpret(host->vm, "build_options_contract", source)
	    != WREN_RESULT_SUCCESS) {
		fprintf(stderr, "build options contract failed: %s\n",
		    host->last_error);
		return -1;
	}
	uint32_t console = st_console_build_features(), audio = audio_compiled_backends();
	const char *crypto = st_crypto_provider_name();
	const struct {
		const char *category, *name;
		bool enabled;
	} expected[] = {
		{"Fonts", "Unifont (SyncTERM Unicode)",
		    strcmp(ST_WREN_TEST_UNIFONT_ENABLED, "true") == 0},
		{"Fonts", "UNSCII", true},
		{"Fonts", "Noto Emoji", true},
		{"Fonts", "Noto Color Emoji", true},
		{"Crypto", "DeuceSSH (SSH)", strcmp(crypto, "none") != 0},
		{"Crypto", "OpenSSL (TLS, symmetric ciphers)", strcmp(crypto, "OpenSSL") == 0},
		{"Crypto", "Botan 3 (TLS, symmetric ciphers)", strcmp(crypto, "Botan") == 0},
		{"Crypto", "Encrypted BBS lists", st_list_crypto_encryption_available()},
		{"Terminal", "Operation Overkill ][ Terminal", true},
		{"Terminal", "JPEG XL support", st_pixel_image_jxl_compiled()},
		{"Video", "SDL", (console & ST_CONSOLE_BUILD_SDL) != 0},
		{"Video", "X11 Support", (console & ST_CONSOLE_BUILD_X11) != 0},
		{"Video", "Xinerama", (console & ST_CONSOLE_BUILD_XINERAMA) != 0},
		{"Video", "XRandR", (console & ST_CONSOLE_BUILD_XRANDR) != 0},
		{"Video", "XRender", (console & ST_CONSOLE_BUILD_XRENDER) != 0},
		{"Video", "GDI", (console & ST_CONSOLE_BUILD_GDI) != 0},
		{"Video", "Wayland", (console & ST_CONSOLE_BUILD_WAYLAND) != 0},
		{"Video", "Quartz", (console & ST_CONSOLE_BUILD_QUARTZ) != 0},
		{"Video", "Win32 Console", (console & ST_CONSOLE_BUILD_WIN32) != 0},
		{"Video", "ncurses", (console & ST_CONSOLE_BUILD_NCURSES) != 0},
		{"Video", "ANSI", true},
		{"Video", "libretro", true},
		{"Video", "Testing bitmap backend", (console & ST_CONSOLE_BUILD_TEST) != 0},
		{"Audio", "OSS", (audio & AUDIO_BACKEND_OSS) != 0},
		{"Audio", "SDL", (audio & AUDIO_BACKEND_SDL) != 0},
		{"Audio", "ALSA", (audio & AUDIO_BACKEND_ALSA) != 0},
		{"Audio", "WASAPI", (audio & AUDIO_BACKEND_WASAPI) != 0},
		{"Audio", "PortAudio", (audio & AUDIO_BACKEND_PORTAUDIO) != 0},
		{"Audio", "PipeWire", (audio & AUDIO_BACKEND_PIPEWIRE) != 0},
		{"Audio", "PulseAudio", (audio & AUDIO_BACKEND_PULSE_AUDIO) != 0},
		{"Audio", "Core Audio", (audio & AUDIO_BACKEND_CORE_AUDIO) != 0},
		{"Audio", "libsndfile", st_audio_file_supported()},
	};
	wrenEnsureSlots(host->vm, 3);
	wrenGetVariable(host->vm, "build_options_contract", "rows", 0);
	CHECK(wrenGetSlotType(host->vm, 0) == WREN_TYPE_LIST);
	CHECK(wrenGetListCount(host->vm, 0) == (int)(sizeof(expected) / sizeof(expected[0])) + 5);
	for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
		wrenGetListElement(host->vm, 0, (int)i, 1);
		CHECK(wrenGetSlotType(host->vm, 1) == WREN_TYPE_LIST && wrenGetListCount(host->vm, 1) == 3);
		wrenGetListElement(host->vm, 1, 0, 2);
		CHECK(wrenGetSlotType(host->vm, 2) == WREN_TYPE_STRING
		    && strcmp(wrenGetSlotString(host->vm, 2), expected[i].category) == 0);
		wrenGetListElement(host->vm, 1, 1, 2);
		CHECK(wrenGetSlotType(host->vm, 2) == WREN_TYPE_STRING
		    && strcmp(wrenGetSlotString(host->vm, 2), expected[i].name) == 0);
		wrenGetListElement(host->vm, 1, 2, 2);
		CHECK(wrenGetSlotType(host->vm, 2) == WREN_TYPE_BOOL
		    && wrenGetSlotBool(host->vm, 2) == expected[i].enabled);
	}
	const enum st_midi_backend midi[] = {ST_MIDI_WINMM, ST_MIDI_APPLE, ST_MIDI_ALSA, ST_MIDI_FLUIDSYNTH, ST_MIDI_ADLMIDI};
	const char *names[] = {"WinMM", "Apple DLS", "ALSA Sequencer", "FluidSynth", "libADLMIDI"};
	for (size_t i = 0; i < 5; ++i) {
		enum st_midi_linkage linkage = st_midi_backend_linkage(midi[i]);
		const char *suffix = linkage == ST_MIDI_RUNTIME ? " (runtime loaded)"
		    : linkage == ST_MIDI_STATIC ? " (static)" : linkage == ST_MIDI_SYSTEM ? " (system linked)"
		    : linkage == ST_MIDI_SHARED ? " (shared)" : "";
		char label[80];
		snprintf(label, sizeof(label), "%s%s", names[i], suffix);
		wrenGetListElement(host->vm, 0, (int)(sizeof(expected) / sizeof(expected[0]) + i), 1);
		CHECK(wrenGetSlotType(host->vm, 1) == WREN_TYPE_LIST && wrenGetListCount(host->vm, 1) == 3);
		wrenGetListElement(host->vm, 1, 0, 2);
		CHECK(strcmp(wrenGetSlotString(host->vm, 2), "MIDI") == 0);
		wrenGetListElement(host->vm, 1, 1, 2);
		CHECK(strcmp(wrenGetSlotString(host->vm, 2), label) == 0);
		wrenGetListElement(host->vm, 1, 2, 2);
		CHECK(wrenGetSlotBool(host->vm, 2) == (linkage != ST_MIDI_NOT_BUILT));
	}
	wrenGetVariable(host->vm, "build_options_contract", "sndfile", 0);
	CHECK(wrenGetSlotType(host->vm, 0) == WREN_TYPE_BOOL);
	CHECK(wrenGetSlotBool(host->vm, 0) == st_audio_file_supported());
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
check_language_preferences(WrenVM *vm, const char *module)
{
	struct st_language_preferences expected;

	CHECK(st_language_discover(&expected) == 0);
	wrenEnsureSlots(vm, 2);
	wrenGetVariable(vm, module, "languageResult", 0);
	CHECK(wrenGetSlotType(vm, 0) == WREN_TYPE_LIST);
	CHECK((size_t)wrenGetListCount(vm, 0) == expected.count);
	for (size_t i = 0; i < expected.count; i++) {
		wrenGetListElement(vm, 0, (int)i, 1);
		CHECK(wrenGetSlotType(vm, 1) == WREN_TYPE_STRING);
		CHECK(strcmp(wrenGetSlotString(vm, 1), expected.tags[i]) == 0);
	}
	st_language_free(&expected);
	return 0;
}

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
	static const struct catalog_row address_families[] = {
		{ST_ADDRESS_FAMILY_UNSPECIFIED, "As per DNS"},
		{ST_ADDRESS_FAMILY_IPV4, "IPv4 only"},
		{ST_ADDRESS_FAMILY_IPV6, "IPv6 only"},
		{ST_ADDRESS_FAMILY_HAPPY_EYEBALLS, "Happy Eyeballs"},
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
	    "var languageResult = Platform.languages\n"
	    "var scratchLanguages = Platform.languages\n"
	    "scratchLanguages.clear()\n"
	    "if (Platform.languages.count == 0) Fiber.abort(\"language list ownership\")\n"
	    "var ratesResult = Menu.rates\n"
	    "var missingRatesResult = Menu.serialRates(\"/no/such/syncterm-device\")\n"
	    "var noRtsResult = Menu.flowControlsNoRts\n"
	    "var addressFamiliesResult = Menu.addressFamilies\n"
	    "var outputModesResult = Menu.outputModes\n"
	    "if (!(Fiber.new { Menu.serialRates(123) }.try() is String)) Fiber.abort(\"serial type validation\")\n"
	    "if (!(Fiber.new { Menu.serialRates(\"bad\\u0000device\") }.try() is String)) Fiber.abort(\"serial NUL validation\")\n";
	static const char platform_source[] =
	    "import \"syncterm\" for Platform\n"
	    "var platformResult = Platform.name\n"
	    "var languageResult = Platform.languages\n";

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
	CHECK(check_language_preferences(host->vm, "menu_catalog_contract") == 0);
	CHECK(check_language_preferences(host->picker_vm,
	    "picker_platform_contract") == 0);
	CHECK(catalog_matches(host->vm, "menu_catalog_contract", "ratesResult",
	    rates, sizeof(rates) / sizeof(rates[0])));
	CHECK(catalog_matches(host->vm, "menu_catalog_contract",
	    "missingRatesResult", rates, sizeof(rates) / sizeof(rates[0])));
	CHECK(catalog_matches(host->vm, "menu_catalog_contract", "noRtsResult",
	    no_rts, sizeof(no_rts) / sizeof(no_rts[0])));
	CHECK(catalog_matches(host->vm, "menu_catalog_contract", "addressFamiliesResult",
	    address_families, sizeof(address_families) / sizeof(address_families[0])));
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
exercise_unicode_fallback_contract(st_wren_host_t *public_host)
{
	struct st_wren_host *host = public_host;
	static const char source[] =
	    "import \"syncterm\" for Cell, Surface\n"
	    "var cell = Cell.new()\n"
	    "cell.font = 46\n"
	    "cell.ch = \"\\u4e00\"\n"
	    "if (cell.columns != 2) Fiber.abort(\"Unicode fallback width\")\n"
	    "var surface = Surface.new(2, 1)\n"
	    "cell = surface[0]\n"
	    "var emoji = \"👩🏽‍❤️‍💋‍👩🏻\"\n"
	    "cell.ch = emoji\n"
	    "cell.font = 46\n"
	    "if (cell.ch != emoji || cell.columns != 2 || surface[1].columns != 0) Fiber.abort(\"cluster font change\")\n"
	    "cell.font = 1\n"
	    "if (cell.ch != emoji || cell.columns != 2 || surface[1].columns != 0) Fiber.abort(\"cluster font round trip\")\n";

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
	    "import \"syncterm\" for Cell, Screen, Surface, Error\n"
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
	    "import \"syncterm\" for Cell, Screen, Surface, Error\n"
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
	    "if (!(Screen.readRect(0, 1, 1, 1) is Error) || !(Screen.readRect(-1, 1, 1, 1) is Error)) Fiber.abort(\"invalid read origin\")\n"
	    "if (!(Screen.readRect(2, 1, 1, 1) is Error) || !(Screen.readRect(1, 2, 1, 1) is Error)) Fiber.abort(\"inverted read\")\n"
	    "if (!(Screen.readRect(81, 1, 81, 1) is Error) || !(Screen.readRect(1, 26, 1, 26) is Error)) Fiber.abort(\"out-of-bounds read\")\n"
	    "if (!(Screen.readRect(1, 1, 4294967296, 1) is Error)) Fiber.abort(\"unrepresentable read\")\n"
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
	    "import \"syncterm\" for Cell, Screen, Surface, Error\n"
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
	    "if (single.ch != \"\\u0000\" || single.columns != 1) Fiber.abort(\"explicit NUL mapping\")\n"
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
exercise_integer_contract(st_wren_host_t *host)
{
	WrenVM *vms[] = {host->vm, host->picker_vm};
	const double invalid[] = {NAN, INFINITY, -INFINITY, 0.5, -0.5,
	    0x1p53, -0x1p53, 0x1p63, -0x1p63, 0x1p64};
	const int64_t valid[] = {0, 1, -1, INT32_MAX, (int64_t)UINT32_MAX,
	    (int64_t)UINT32_MAX + 1, (int64_t)ST_WREN_MAX_INTEGER,
	    -(int64_t)ST_WREN_MAX_INTEGER};

	CHECK(st_wren_u64_exact(ST_WREN_MAX_INTEGER));
	CHECK(!st_wren_u64_exact(ST_WREN_MAX_INTEGER + 1));
	CHECK(!st_wren_u64_exact(UINT64_MAX));
	CHECK(st_wren_i64_exact(-(int64_t)ST_WREN_MAX_INTEGER));
	CHECK(!st_wren_i64_exact(-(int64_t)ST_WREN_MAX_INTEGER - 1));
	CHECK(!st_wren_i64_exact(INT64_MIN));
	CHECK(!st_wren_i64_exact(INT64_MAX));
	for (size_t mode = 0; mode < sizeof(vms) / sizeof(vms[0]); ++mode) {
		WrenVM *vm = vms[mode];
		wrenEnsureSlots(vm, 1);
		for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
			int64_t signed_value = 17;
			uint64_t unsigned_value = 19;
			wrenSetSlotDouble(vm, 0, invalid[i]);
			CHECK(!st_wren_number_i64(vm, 0, &signed_value));
			CHECK(!st_wren_number_u64(vm, 0, &unsigned_value));
			CHECK(signed_value == 17 && unsigned_value == 19);
		}
		for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i) {
			int64_t signed_value = 17;
			uint64_t unsigned_value = 19;
			st_wren_return_i64(vm, valid[i]);
			CHECK(st_wren_number_i64(vm, 0, &signed_value));
			CHECK(signed_value == valid[i]);
			CHECK(st_wren_number_u64(vm, 0, &unsigned_value) == (valid[i] >= 0));
			if (valid[i] >= 0) {
				CHECK(unsigned_value == (uint64_t)valid[i]);
				st_wren_return_u64(vm, unsigned_value);
				CHECK(wrenGetSlotDouble(vm, 0) == (double)valid[i]);
			}
			else
				CHECK(unsigned_value == 19);
		}
		wrenSetSlotNull(vm, 0);
		int64_t signed_value = 17;
		CHECK(!st_wren_number_i64(vm, 0, &signed_value) && signed_value == 17);
	}
	return 0;
}

static int
exercise_foreign_type_contract(st_wren_host_t *public_host)
{
	struct st_wren_host *host = public_host;
	WrenVM *vms[] = {host->vm, host->picker_vm};
	static const uint64_t collisions[] = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 0x101, 0x102, 0x103, 0x104,
		UINT32_C(0x50425546), UINT32_C(0x504d4153), UINT32_C(0x50424c54),
		UINT32_C(0x5354464c), UINT32_C(0x53544452), UINT32_C(0x53544648),
	};

	for (size_t i = 0; i < sizeof(vms) / sizeof(vms[0]); ++i) {
		WrenVM *vm = vms[i];
		CHECK(wrenInterpret(vm, "foreign_type_contract", foreign_type_contract)
		    == WREN_RESULT_SUCCESS);
		WrenHandle *call = wrenMakeCallHandle(vm, "run(_)");
		for (size_t j = 0; j < sizeof(collisions) / sizeof(collisions[0]); ++j) {
			wrenEnsureSlots(vm, 3);
			wrenGetVariable(vm, "foreign_type_contract", "ForeignTypeProbe", 0);
			wrenGetVariable(vm, "syncterm", "SFTPStat", 2);
			struct st_sftp_stat *stat = wrenSetSlotNewForeign(vm, 1, 2,
			    sizeof(*stat));
			/* Valid stat values can resemble another payload's header,
			 * followed by a small timestamp misread as a native pointer. */
			*stat = (struct st_sftp_stat){.size = collisions[j], .modified_time = 1};
			int count = wrenGetSlotCount(vm);
			Value *slots = vm->apiStack;
			CHECK(st_wren_foreign_is(vm, 1, "syncterm", "SFTPStat"));
			CHECK(!st_wren_foreign_is(vm, 1, "missing_module", "SFTPStat"));
			CHECK(!st_wren_foreign_is(vm, 1, "syncterm", "MissingClass"));
			CHECK(st_wren_file_read_path(vm, 1) == NULL);
			CHECK(st_wren_directory_path(vm, 1) == NULL);
			struct st_console_cell *cells = NULL;
			uint32_t width = 17, height = 19;
			CHECK(!st_wren_surface_view(vm, 1, &cells, &width, &height));
			CHECK(cells == NULL && width == 17 && height == 19);
			struct st_profile profile;
			st_profile_init(&profile);
			CHECK(!st_wren_menu_copy_profile(vm, 1, &profile));
			st_profile_dispose(&profile);
			CHECK(wrenGetSlotCount(vm) == count && vm->apiStack == slots);
			CHECK(wrenCall(vm, call) == WREN_RESULT_SUCCESS);
		}
		CHECK(wrenInterpret(vm, "foreign_shadow_contract",
		    "foreign class Surface {}\n") == WREN_RESULT_SUCCESS);
		wrenEnsureSlots(vm, 3);
		wrenGetVariable(vm, "foreign_type_contract", "ForeignTypeProbe", 0);
		wrenGetVariable(vm, "foreign_shadow_contract", "Surface", 2);
		(void)wrenSetSlotNewForeign(vm, 1, 2, 0);
		CHECK(!st_wren_foreign_is(vm, 1, "syncterm", "Surface"));
		CHECK(wrenCall(vm, call) == WREN_RESULT_SUCCESS);
		wrenReleaseHandle(vm, call);
	}
	return 0;
}

static WrenFinalizerFn snapshot_finalizer;
static size_t snapshot_finalizations;

static void
observe_snapshot_finalizer(void *data)
{
	++snapshot_finalizations;
	snapshot_finalizer(data);
}

static int
exercise_snapshot_lifetime_contract(st_wren_host_t *public_host)
{
	struct st_wren_host *host = public_host;
	WrenVM *vms[] = {host->vm, host->picker_vm};

	for (size_t i = 0; i < sizeof(vms) / sizeof(vms[0]); ++i) {
		WrenVM *vm = vms[i];
		wrenCollectGarbage(vm);
		CHECK(wrenInterpret(vm, "snapshot_lifetime_contract",
		    "import \"syncterm\" for Screen\n"
		    "var saved = Screen.save()\n") == WREN_RESULT_SUCCESS);
		wrenEnsureSlots(vm, 1);
		wrenGetVariable(vm, "snapshot_lifetime_contract", "saved", 0);
		CHECK(wrenGetSlotType(vm, 0) == WREN_TYPE_FOREIGN);
		ObjClass *class_obj = wrenGetClass(vm, vm->apiStack[0]);
		/* Native resources require a foreign class with its own finalizer,
		 * even when no constructor is exposed to scripts. */
		CHECK(class_obj->numFields == -1);
		CHECK(strcmp(class_obj->name->value, "ScreenSnapshot") == 0);
		int symbol = wrenSymbolTableFind(&vm->methodNames, "<finalize>", 10);
		CHECK(symbol >= 0 && symbol < class_obj->methods.count);
		Method original = class_obj->methods.data[symbol];
		CHECK(original.type == METHOD_FOREIGN);
		snapshot_finalizer = (WrenFinalizerFn)original.as.foreign;
		snapshot_finalizations = 0;
		class_obj->methods.data[symbol].as.foreign =
		    (WrenForeignMethodFn)observe_snapshot_finalizer;
		wrenSetSlotNull(vm, 0);
		wrenCollectGarbage(vm);
		CHECK(snapshot_finalizations == 0);
		CHECK(wrenInterpret(vm, "snapshot_lifetime_contract",
		    "Screen.restore(saved)\n"
		    "Screen.restore(saved)\n"
		    "saved = null\n"
		    "System.gc()\n") == WREN_RESULT_SUCCESS);
		CHECK(snapshot_finalizations == 1);
		CHECK(wrenInterpret(vm, "snapshot_lifetime_contract",
		    "for (i in 0...12) {\n"
		    "  var temporary = Screen.save()\n"
		    "  Screen.restore(temporary)\n"
		    "}\n"
		    "System.gc()\n") == WREN_RESULT_SUCCESS);
		CHECK(snapshot_finalizations == 13);
		wrenCollectGarbage(vm);
		CHECK(snapshot_finalizations == 13);
		class_obj->methods.data[symbol] = original;
	}
	return 0;
}

struct update_presentations {
	size_t count;
	uint32_t colours[4];
};

static int
record_update_presentation(st_console_t *console)
{
	struct update_presentations *state = console->display->backend_state;

	CHECK(state->count < 4);
	state->colours[state->count++] = console->pixels->primary[0];
	return 0;
}

static int
exercise_screen_update_contract(st_wren_host_t *public_host,
	st_console_t *console)
{
	struct st_wren_host *host = public_host;
	struct update_presentations state = {0};
	const struct st_console_backend_ops *previous_ops = console->display->backend_ops;
	void *previous_state = console->display->backend_state;
	static const struct st_console_backend_ops ops = {
		.presents_pixels = true,
		.present = record_update_presentation,
	};
	static const char source[] =
	    "import \"syncterm\" for Screen, ScreenUpdate_, Surface, PixelColor\n"
	    "var s = Surface.new(1, 1)\n"
	    "s.cellAt(0, 0).ch = \"T\"\n"
	    "var result = Screen.update {\n"
	    "  Screen.putRect(s, 1, 1)\n"
	    "  Screen.update { Screen.setPixel(0, 0, PixelColor.rgb(0x123456)) }\n"
	    "  return 42\n"
	    "}\n"
	    "if (result != 42 || Screen.readRect(1, 1, 1, 1)[0].ch != \"T\") Fiber.abort(\"update result/cells\")\n"
	    "var failed = Fiber.new {\n"
	    "  Screen.update {\n"
	    "    Screen.putRect(s, 1, 1)\n"
	    "    Screen.setPixel(0, 0, PixelColor.rgb(0x234567))\n"
	    "    Fiber.abort(\"expected update failure\")\n"
	    "  }\n"
	    "}\n"
	    "failed.try()\n"
	    "if (failed.error != \"expected update failure\") Fiber.abort(\"update error propagation\")\n"
	    "var yielded = Fiber.new {\n"
	    "  Screen.update {\n"
	    "    Screen.setPixel(0, 0, PixelColor.rgb(0x345678))\n"
	    "    Fiber.yield()\n"
	    "  }\n"
	    "}\n"
	    "yielded.try()\n"
	    "if (yielded.error != \"Screen.update callback must not yield\") Fiber.abort(\"update yield guard\")\n"
	    "var invalid = Fiber.new { Screen.update(null) }\n"
	    "invalid.try()\n"
	    "if (invalid.error == null) Fiber.abort(\"update function validation\")\n"
	    "var closed = ScreenUpdate_.new()\n"
	    "closed.finish_()\n"
	    "closed.finish_()\n"
	    "var abandoned = ScreenUpdate_.new()\n"
	    "Screen.setPixel(0, 0, PixelColor.rgb(0x456789))\n"
	    "abandoned = null\n"
	    "System.gc()\n";
	WrenInterpretResult result;

	CHECK(!console->display->presentation_thread_started);
	CHECK(st_console_present(console) == 0);
	console->display->backend_ops = &ops;
	console->display->backend_state = &state;
	result = wrenInterpret(host->vm, "screen_update_contract", source);
	console->display->backend_ops = previous_ops;
	console->display->backend_state = previous_state;
	if (result != WREN_RESULT_SUCCESS)
		fprintf(stderr, "Screen.update contract failed: %s\n", host->last_error);
	CHECK(result == WREN_RESULT_SUCCESS);
	CHECK(console->update_depth == 0);
	CHECK(state.count == 4);
	for (size_t i = 0; i < 4; ++i)
		CHECK(state.colours[i] == (UINT32_C(0x80000000)
		    | (UINT32_C(0x123456) + (uint32_t)i * UINT32_C(0x111111))));
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
	    "right.ch = \"\\u2514\"\n"
	    "if (right.ch != \"\\u2514\") Fiber.abort(\"box drawing character\")\n"
	    "right.ch = \"\\u00a0\"\n"
	    "right.font = 1\n"
	    "if (right.ch != \"\\u00a0\") Fiber.abort(\"font Unicode preservation\")\n"
	    "right.font = 0\n"
	    "if (right.ch != \"\\u00a0\") Fiber.abort(\"font Unicode round trip\")\n"
	    "right.ch = \"\\u263a\"\n"
	    "if (right.ch != \"\\u263a\") Fiber.abort(\"smiley character\")\n"
	    "var pixel = Screen.readRect(1, 1, 1, 1).cellAt(0, 0)\n"
	    "if (!pixel.pixelGraphics || pixel.eqContent(pixel) || left.eqContent(pixel)) Fiber.abort(\"pixel cell equality\")\n"
	    "var surface = Surface.new(1, 1)\n"
	    "var below = Fiber.new { surface.iterate(9007199254740991) }\n"
	    "var belowResult = below.try()\n"
	    "if (below.error != null || belowResult != false) Fiber.abort(\"largest exact iterator integer\")\n"
	    "for (n in [9007199254740992, 9223372036854774784]) {\n"
	    "  var inexact = Fiber.new { surface.iterate(n) }\n"
	    "  inexact.try()\n"
	    "  if (inexact.error == null) Fiber.abort(\"inexact iterator integer\")\n"
	    "}\n"
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
	    "Screen.window.setBounds([3, 4, 6, 7])\n"
	    "Screen.window.setPosition([2, 3])\n"
	    "if (Screen.window.position[0] != 2 || Screen.window.position[1] != 3) Fiber.abort(\"relative window position\")\n"
	    "Screen.window.putChar(88)\n"
	    "if (Screen.readRect(4, 6, 4, 6).cellAt(0, 0).ch != \"X\") Fiber.abort(\"relative window output\")\n"
	    "var badPosition = Fiber.new { Screen.window.setPosition([5, 1]) }\n"
	    "badPosition.try()\n"
	    "if (badPosition.error == null || Screen.window.position[0] != 3 || Screen.window.position[1] != 3) Fiber.abort(\"invalid window position\")\n"
	    "var badBounds = Fiber.new { Screen.window.setBounds([3, 4, 2, 7]) }\n"
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

struct capability_probe {
	size_t queries, operations;
};

static uint32_t
probe_capabilities(const st_console_t *console)
{
	struct capability_probe *probe = console->display->backend_state;
	++probe->queries;
	return 0;
}

static int
probe_clipboard_set(st_console_display_t *display, const char *text)
{
	struct capability_probe *probe = display->backend_state;
	(void)text;
	++probe->operations;
	return IO_ERROR_NOT_SUPPORTED;
}

static int
probe_clipboard_get(st_console_display_t *display, char **text)
{
	(void)text;
	return probe_clipboard_set(display, NULL);
}

static bool
capability_interpret(WrenVM *vm, st_wren_connected_t *connected,
	const char *source)
{
	return connected != NULL
	    ? st_wren_connected_interpret(connected, "clipboard_url_capabilities", source) == 0
	    : wrenInterpret(vm, "clipboard_url_capabilities", source) == WREN_RESULT_SUCCESS;
}

static bool
pixel_blit_interpret(WrenVM *vm, st_wren_connected_t *connected,
	const char *source)
{
	return connected != NULL
	    ? st_wren_connected_interpret(connected, "pixel_blit_contract", source) == 0
	    : wrenInterpret(vm, "pixel_blit_contract", source) == WREN_RESULT_SUCCESS;
}

static int
exercise_pixel_blit_vm(WrenVM *vm, st_wren_connected_t *connected)
{
	const char *source =
	    "import \"pixel_vm_test\" for PixelVmTest\n"
	    "var result = PixelVmTest.runOffscreen()\n"
	    "if (result[0] < 30 || result[1] != 0) Fiber.abort(\"offscreen tests did not run\")\n";
	/* Pure buffer operations must not query or require a pixel backend. */
	st_wren_test_screen_fault = (struct st_wren_screen_fault){
	    .name = "st_console_capabilities", .status = IO_ERROR_NOT_SUPPORTED};
	bool passed = pixel_blit_interpret(vm, connected, source);
	size_t calls = st_wren_test_screen_fault.calls;
	st_wren_test_screen_fault = (struct st_wren_screen_fault){0};
	CHECK(passed && calls == 0);
	CHECK(pixel_blit_interpret(vm, connected,
	    "import \"syncterm\" for PixelBuffer, PixelMask, PixelBlit, PixelError, Error, ScreenError\n"
	    "var errors = []\nvar src = PixelBuffer.new(2, 1, 3)\n"
	    "src.fillAlternate(4)\nvar dst = PixelBuffer.new(2, 1, 9)\n"
	    "var op = PixelBlit.new()\nvar mask = PixelMask.new(2, 1, true)\n"));
	const int statuses[] = {IO_ERROR_OUT_OF_MEMORY, IO_ERROR_OUT_OF_RANGE,
	    IO_ERROR_INVALID_ARGUMENT, -271828, 271828};
	for (size_t i = 0; i < sizeof(statuses) / sizeof(statuses[0]); ++i) {
		st_wren_test_screen_fault = (struct st_wren_screen_fault){
		    .name = "st_pixel_buffer_blit", .status = statuses[i]};
		char script[1024];
		(void)snprintf(script, sizeof(script),
		    "for (e in [dst.blit(src, op), dst.blit(src, mask, op)]) {\n"
		    "  if (!(e is PixelError) || !(e is Error) || e is ScreenError || e.code != %d) Fiber.abort(\"lost native pixel result\")\n"
		    "  errors.add(e)\n}\n"
		    "if (dst.hasAlternate || dst[0] != 9 || src[0] != 3 || src.alternateAt(0) != 4 || op.sourceWidth != 0) Fiber.abort(\"failed blit changed state\")\n",
		    statuses[i]);
		passed = pixel_blit_interpret(vm, connected, script);
		calls = st_wren_test_screen_fault.calls;
		st_wren_test_screen_fault = (struct st_wren_screen_fault){0};
		CHECK(passed && calls == 2);
	}
	CHECK(pixel_blit_interpret(vm, connected,
	    "src = null\ndst = null\nop = null\nmask = null\nSystem.gc()\n"
	    "for (e in errors) {\n"
	    "  if (!e.message.contains(\"PixelBuffer.blit\") || !e.toString.startsWith(\"PixelError\")) Fiber.abort(\"pixel error lifetime\")\n"
	    "}\n"));
	return 0;
}

static int
exercise_pixel_blit_contract(struct st_wren_host *host)
{
	CHECK(exercise_pixel_blit_vm(host->vm, NULL) == 0);
	CHECK(exercise_pixel_blit_vm(host->picker_vm, NULL) == 0);
	struct st_profile profile;
	st_profile_init(&profile);
	const struct st_wren_connected_config config = {
	    .host = host, .console = host->config.console, .profile = &profile};
	st_wren_connected_t *connected = NULL;
	CHECK(st_wren_connected_create(&connected, &config) == 0);
	CHECK(exercise_pixel_blit_vm(NULL, connected) == 0);
	st_wren_connected_destroy(connected);
	st_profile_dispose(&profile);
	return 0;
}

static int
exercise_clipboard_url_capabilities(WrenVM *vm,
	st_wren_connected_t *connected, st_console_t *console)
{
	struct capability_probe probe = {0};
	const struct st_console_backend_ops *previous_ops = console->display->backend_ops;
	void *previous_state = console->display->backend_state;
	struct st_console_backend_ops ops = {.capabilities = probe_capabilities};
	bool passed = capability_interpret(vm, connected,
	    "import \"syncterm\" for Screen, ScreenSupports, ScreenError\n");
	console->display->backend_ops = &ops;
	console->display->backend_state = &probe;
	for (unsigned bits = 0; passed && bits < 4; ++bits) {
		char source[1024];
		ops.set_clipboard_text = bits & 1 ? probe_clipboard_set : NULL;
		ops.get_clipboard_text = bits & 1 ? probe_clipboard_get : NULL;
		ops.open_url = bits & 2 ? probe_clipboard_set : NULL;
		probe.queries = 0;
		(void)snprintf(source, sizeof(source),
		    "if (Screen.supports.clipboard != %s || ScreenSupports.clipboard != %s || "
		    "Screen.supports.openUrl != %s || ScreenSupports.openUrl != %s) "
		    "Fiber.abort(\"clipboard/URL capability bits\")\n",
		    bits & 1 ? "true" : "false", bits & 1 ? "true" : "false",
		    bits & 2 ? "true" : "false", bits & 2 ? "true" : "false");
		passed = capability_interpret(vm, connected, source)
		    && probe.queries == 4 && probe.operations == 0;
	}
	if (passed) passed = capability_interpret(vm, connected,
	    "for (assign in [Fn.new { Screen.supports.clipboard = true }, Fn.new { ScreenSupports.openUrl = false }]) {\n"
	    "  var bad = Fiber.new { assign.call() }\n"
	    "  bad.try()\n"
	    "  if (bad.error == null) Fiber.abort(\"writable capability\")\n"
	    "}\n");
	const int statuses[] = {IO_ERROR_IO, -271828, 271828};
	for (size_t i = 0; passed && i < sizeof(statuses) / sizeof(*statuses); ++i) {
		char source[1024];
		st_wren_test_screen_fault = (struct st_wren_screen_fault){
		    .name = "st_console_capabilities", .status = statuses[i]};
		(void)snprintf(source, sizeof(source),
		    "{\nvar clipboard = Screen.supports.clipboard\nvar url = Screen.supports.openUrl\n"
		    "for (error in [clipboard, url]) {\n"
		    "  if (!(error is ScreenError) || error.code != %d) Fiber.abort(\"capability failure became Bool\")\n"
		    "}\n}\n", statuses[i]);
		passed = capability_interpret(vm, connected, source)
		    && st_wren_test_screen_fault.calls == 2 && probe.operations == 0;
		st_wren_test_screen_fault = (struct st_wren_screen_fault){0};
	}
	console->display->backend_ops = previous_ops;
	console->display->backend_state = previous_state;
	CHECK(passed);
	return 0;
}

static int
exercise_screen_results(st_wren_host_t *public_host)
{
	struct st_wren_host *host = public_host;
	CHECK(exercise_clipboard_url_capabilities(host->vm, NULL,
	    st_wren_context_from_vm(host->vm)->console) == 0);
	CHECK(exercise_clipboard_url_capabilities(host->picker_vm, NULL,
	    st_wren_context_from_vm(host->picker_vm)->console) == 0);
	/* A real native rejection must be a returned value, not a String abort. */
	CHECK(wrenInterpret(host->vm, "screen_results",
	    "import \"syncterm\" for CustomCursor, Error, IOErr\n"
	    "var invalid = CustomCursor.current\n"
	    "invalid.range = 0\n"
	    "var failed = Fiber.new { invalid.apply() }\n"
	    "var result = failed.try()\n"
	    "if (failed.error != null || !(result is Error) || result.code != IOErr.invalidArgument) "
	    "Fiber.abort(\"native cursor failure was not returned\")\n")
	    == WREN_RESULT_SUCCESS);
	return 0;
}

static int
exercise_screen_failure_boundaries(st_wren_host_t *public_host)
{
	struct st_wren_host *host = public_host;
	static const struct { const char *native; const char *expression; } cases[] = {
		{"size", "Screen.size"},
		{"get_style", "Screen.hyperlinkId"},
		{"set_style", "Screen.setHyperlinkId(0)"},
		{"read_cells", "Screen.readRect(1, 1, 1, 1)"},
		{"write_complete_cells", "Screen.putRect(Surface.new(1, 1), 1, 1)"},
		{"write_complete_cells", "Screen.putRect(Surface.new(1, 1), Rect.new(0, 0, 1, 1), 1, 1)"},
		{"get_custom_cursor", "CustomCursor.startLine"},
		{"get_custom_cursor", "CustomCursor.visible"},
		{"set_custom_cursor", "CustomCursor.setStartLine(1)"},
		{"get_video_flags", "VideoFlags.noBlink"},
		{"get_video_flags", "VideoFlags.expand"},
		{"get_palette", "Color.fromAttr(7)"},
		{"get_palette", "Color.toLegacyAttr(0, 0)"},
		{"get_palette", "PixelColor.toRgb(0)"},
		{"get_video_flags", "Screen.attr"},
		{"capabilities", "Host.textTerminal"},
		{"capabilities", "Host.localControlKeys"},
		{"capabilities", "Screen.supports.pixels"},
		{"capabilities", "Screen.supports.clipboard"},
		{"capabilities", "Screen.supports.openUrl"},
		{"get_style", "Screen.attr"},
		{"set_style", "Screen.setAttr(7)"},
		{"get_position", "Screen.window.position"},
		{"set_position", "Screen.window.setPosition([1, 1])"},
		{"get_window", "Screen.window.bounds"},
		{"set_window", "Screen.window.setBounds([1, 1, 2, 2])"},
		{"put_codepoint", "Screen.window.putChar(65)"},
		{"write_utf8", "Screen.window.print(\"e\\u0301\")"},
		{"clear_window", "Screen.window.clear()"},
		{"clear_line", "Screen.window.clearToLineEnd()"},
		{"delete_lines", "Screen.window.deleteLine()"},
		{"insert_lines", "Screen.window.insertLine()"},
		{"scroll", "Screen.window.scroll()"},
		{"get_palette", "Palette[0]"},
		{"set_palette", "Palette.set(0, 0x123456)"},
		{"get_custom_cursor", "CustomCursor.current"},
		{"get_default_cursor", "CustomCursor.normal"},
		{"set_custom_cursor", "CustomCursor.setVisible(true)"},
		{"get_video_flags", "VideoFlags.current"},
		{"set_video_flags", "VideoFlags.setNoBlink(true)"},
		{"pixel_size", "Screen.pixelSize"},
		{"cell_pixel_size", "Screen.cellPixelSize"},
		{"get_pixels", "Screen.readPixels(0, 0, 1, 1)"},
		{"set_pixel", "Screen.setPixel(0, 0, PixelColor.rgb(0))"},
		{"blit_pixels", "Screen.blitPixels(PixelBuffer.new(1, 1), PixelBlit.new())"},
		{"snapshot_create", "Screen.save()"},
		{"snapshot_restore", "Screen.restore(saved)"},
		{"begin_update", "Screen.update(Fn.new { 42 })"},
		{"get_clipboard_text", "Clipboard.text"},
		{"set_clipboard_text", "Clipboard.setText(\"test\")"},
		{"set_pointer_visible", "Input.setMouseVisible(true)"},
		{"set_pointer_shape", "Menu.setHyperlinkHover(0)"},
		{"set_title", "Menu.showEntry(null)"},
		{"open_url", "Menu.openUrl(\"https://example.invalid/\")"},
		{"st_wren_host_set_screen_mode", "Menu.setScreenMode(1)"},
		{"st_hyperlink_registry_copy", "Menu.openHyperlink(link)"},
		{"st_hyperlink_registry_contains", "Menu.setHyperlinkHover(link)"},
		{"open_url", "Menu.openHyperlink(link)"},
	};
	CHECK(wrenInterpret(host->vm, "screen_failures",
	    "import \"syncterm\" for Screen, Surface, ScreenError, Error, IOErr, Palette, CustomCursor, VideoFlags, Clipboard, Input, PixelColor, PixelBuffer, PixelBlit, Hyperlinks, Color, Host\n"
	    "import \"syncterm_menu\" for Menu\n"
	    "import \"ui_widget\" for Rect\n"
	    "var saved = Screen.save()\n"
	    "var link = Hyperlinks.add(\"https://example.invalid/\", null)\n"
	    "var errors = []\n") == WREN_RESULT_SUCCESS);
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
		char native[96], source[1024];
		(void)snprintf(native, sizeof(native), "%s%s", strncmp(cases[i].native, "st_", 3) == 0 ? "" : "st_console_", cases[i].native);
		for (unsigned unknown = 0; unknown < 3; ++unknown) {
			int code = unknown == 2 ? 271828 : unknown ? -271828 : IO_ERROR_IO;
			st_wren_test_screen_fault = (struct st_wren_screen_fault){.name = native, .status = code};
			(void)snprintf(source, sizeof(source),
			    "errors.add(%s)\n"
			    "if (!(errors[-1] is ScreenError) || !(errors[-1] is Error) || errors[-1].code != %d) Fiber.abort(\"lost %s result\")\n",
			    cases[i].expression, code, cases[i].native);
			CHECK(wrenInterpret(host->vm, "screen_failures", source) == WREN_RESULT_SUCCESS);
			CHECK(st_wren_test_screen_fault.calls == 1);
			st_wren_test_screen_fault = (struct st_wren_screen_fault){0};
		}
	}
	CHECK(wrenInterpret(host->vm, "screen_failures",
	    "System.gc()\n"
	    "for (error in errors) {\n"
	    "  if (!(error.message is String) || !error.toString.startsWith(\"ScreenError\")) Fiber.abort(\"error lifetime\")\n"
	    "}\n"
	    "var held = errors[0]\n"
	    "var immutable = Fiber.new { held.code = 1 }\n"
	    "immutable.try()\n"
	    "if (immutable.error == null || held.code != IOErr.io) Fiber.abort(\"mutable screen error\")\n"
	    "if (!(Screen.readRect(0, 1, 1, 1) is ScreenError)) Fiber.abort(\"range failure missing\")\n"
	    "if (Screen.putRect(Surface.new(1, 1), -100, -100) != null) Fiber.abort(\"clipped success\")\n") == WREN_RESULT_SUCCESS);
	return 0;
}

static int
exercise_screen_cleanup_results(st_wren_host_t *public_host)
{
	struct st_wren_host *host = public_host;
	CHECK(wrenInterpret(host->vm, "screen_cleanup",
	    "import \"syncterm\" for Screen, Error, ScreenError, CustomCursor, VideoFlags, IOErr\n"
	    "var calls = 0\n"
	    "var previous = Screen.save()\n"
	    "var first = CustomCursor.current\n"
	    "first.range = 0\n"
	    "first = first.apply()\n") == WREN_RESULT_SUCCESS);
	st_wren_test_screen_fault = (struct st_wren_screen_fault){.name = "st_console_begin_update", .status = -271828};
	CHECK(wrenInterpret(host->vm, "screen_cleanup",
	    "var denied = Screen.update(Fn.new { calls = calls + 1 })\n"
	    "if (!(denied is ScreenError) || calls != 0) Fiber.abort(\"begin failure ran callback\")\n") == WREN_RESULT_SUCCESS);
	st_wren_test_screen_fault = (struct st_wren_screen_fault){.name = "st_console_end_update", .status = IO_ERROR_IO, .after = true};
	CHECK(wrenInterpret(host->vm, "screen_cleanup",
	    "var finished = Screen.update(Fn.new { 42 })\n"
	    "if (!(finished is ScreenError) || finished.code != IOErr.io) Fiber.abort(\"end failure lost\")\n"
	    "if (Screen.update(Fn.new { first }) != first) Fiber.abort(\"end replaced returned error\")\n"
	    "var aborted = Fiber.new { Screen.update(Fn.new { Fiber.abort(first) }) }\n"
	    "aborted.try()\n"
	    "if (aborted.error != first) Fiber.abort(\"end replaced callback abort\")\n") == WREN_RESULT_SUCCESS);
	CHECK(st_wren_test_screen_fault.calls == 3);
	st_wren_test_screen_fault = (struct st_wren_screen_fault){.name = "st_console_set_custom_cursor", .status = IO_ERROR_IO};
	CHECK(wrenInterpret(host->vm, "screen_cleanup",
	    "var restore = CustomCursor.preserve(Fn.new { 42 })\n"
	    "if (!(restore is ScreenError) || restore.code != IOErr.io) Fiber.abort(\"cursor cleanup lost\")\n"
	    "if (CustomCursor.preserve(Fn.new { first }) != first) Fiber.abort(\"cleanup replaced returned error\")\n"
	    "var cursorAborted = Fiber.new { CustomCursor.preserve(Fn.new { Fiber.abort(first) }) }\n"
	    "cursorAborted.try()\n"
	    "if (cursorAborted.error != first) Fiber.abort(\"cleanup replaced callback abort\")\n") == WREN_RESULT_SUCCESS);
	CHECK(st_wren_test_screen_fault.calls == 3);
	st_wren_test_screen_fault = (struct st_wren_screen_fault){.name = "st_console_snapshot_restore", .status = IO_ERROR_IO};
	CHECK(wrenInterpret(host->vm, "screen_cleanup",
	    "Screen.font[0] = 2\n"
	    "var rejected = Screen.restore(previous)\n"
	    "if (!(rejected is ScreenError) || Screen.font[0] != 2) Fiber.abort(\"rejected restore changed slots\")\n") == WREN_RESULT_SUCCESS);
	st_wren_test_screen_fault.after = true;
	CHECK(wrenInterpret(host->vm, "screen_cleanup",
	    "var applied = Screen.restore(previous)\n"
	    "if (!(applied is ScreenError) || Screen.font[0] == 2) Fiber.abort(\"applied restore did not synchronize slots\")\n") == WREN_RESULT_SUCCESS);
	st_wren_test_screen_fault = (struct st_wren_screen_fault){0};
	st_wren_test_no_pixels = true;
	CHECK(wrenInterpret(host->vm, "screen_cleanup",
	    "if (Screen.supports.pixels) Fiber.abort(\"absent capability\")\n"
	    "var unsupported = Screen.readPixels(0, 0, 1, 1)\n"
	    "if (!(unsupported is ScreenError) || unsupported.code != IOErr.notSupported) Fiber.abort(\"unsupported capture\")\n") == WREN_RESULT_SUCCESS);
	st_wren_test_no_pixels = false;
	CHECK(wrenInterpret(host->vm, "screen_cleanup",
	    "Screen.window.setBounds([1, 1, 4, 4])\n"
	    "Screen.window.setPosition([1, 1])\n") == WREN_RESULT_SUCCESS);
	st_wren_test_screen_fault = (struct st_wren_screen_fault){.name = "st_console_write_utf8", .status = IO_ERROR_IO, .after = true};
	CHECK(wrenInterpret(host->vm, "screen_cleanup",
	    "var committed = Screen.window.print(\"AB\")\n"
	    "if (!(committed is ScreenError) || Screen.window.position[0] != 3) Fiber.abort(\"drawing effects lost\")\n") == WREN_RESULT_SUCCESS);
	CHECK(st_wren_test_screen_fault.calls == 1);
	st_wren_test_screen_fault = (struct st_wren_screen_fault){.name = "binding.captureStorage"};
	CHECK(wrenInterpret(host->vm, "screen_cleanup",
	    "var allocation = Screen.readRect(1, 1, 1, 1)\n"
	    "if (!(allocation is ScreenError) || allocation.code != IOErr.outOfMemory) Fiber.abort(\"binding allocation failure lost\")\n") == WREN_RESULT_SUCCESS);
	CHECK(st_wren_test_screen_fault.calls == 1);
	st_wren_test_screen_fault = (struct st_wren_screen_fault){.name = "st_console_set_video_flags", .status = IO_ERROR_IO};
	CHECK(wrenInterpret(host->vm, "screen_cleanup",
	    "var flagsCleanup = VideoFlags.preserve(Fn.new { 42 })\n"
	    "if (!(flagsCleanup is ScreenError) || VideoFlags.preserve(Fn.new { first }) != first) Fiber.abort(\"flags cleanup lost\")\n") == WREN_RESULT_SUCCESS);
	st_wren_test_screen_fault = (struct st_wren_screen_fault){0};
	CHECK(wrenInterpret(host->vm, "screen_cleanup", "Screen.restore(previous)\n") == WREN_RESULT_SUCCESS);
	return 0;
}

static int
exercise_screen_ui_cleanup(st_wren_host_t *public_host)
{
	struct st_wren_host *host = public_host;
	CHECK(wrenInterpret(host->vm, "screen_ui_cleanup",
	    "import \"syncterm\" for Screen, ScreenError, CustomCursor, VideoFlags, Input, Error, IOErr, KeyEvent\n"
	    "import \"ui_app\" for App\n"
	    "var app = App.new()\n"
	    "var mouse = Input.mouseEvents\n"
	    "var visible = CustomCursor.visible\n"
	    "app.setupCursor_()\n"
	    "CustomCursor.setVisible(!visible)\n"
	    "Input.mouseEvents = 0\n") == WREN_RESULT_SUCCESS);
	st_wren_test_screen_fault = (struct st_wren_screen_fault){.name = "st_console_set_style", .status = IO_ERROR_IO};
	CHECK(wrenInterpret(host->vm, "screen_ui_cleanup",
	    "var cleanup = Fiber.new { app.finishRun_(mouse) }\n"
	    "cleanup.try()\n"
	    "if (!(cleanup.error is ScreenError) || Input.mouseEvents != mouse || CustomCursor.visible != visible) Fiber.abort(\"UI cleanup stopped at first failure\")\n") == WREN_RESULT_SUCCESS);
	CHECK(st_wren_test_screen_fault.calls == 1);
	st_wren_test_screen_fault = (struct st_wren_screen_fault){.name = "st_console_clear_window", .status = IO_ERROR_IO};
	CHECK(wrenInterpret(host->vm, "screen_ui_cleanup",
	    "app.bind(65, Fn.new {|key| Screen.checked_(Screen.window.clear()) })\n"
	    "var handler = Fiber.new { app.dispatchSync_(KeyEvent.new(65, 0)) }\n"
	    "handler.try()\n"
	    "if (!(handler.error is ScreenError)) Fiber.abort(\"UI swallowed unhandled drawing failure\")\n") == WREN_RESULT_SUCCESS);
	CHECK(st_wren_test_screen_fault.calls == 1);
	st_wren_test_screen_fault = (struct st_wren_screen_fault){.name = "st_console_get_custom_cursor", .status = IO_ERROR_IO};
	CHECK(wrenInterpret(host->vm, "screen_ui_cleanup",
	    "var calls = 0\n"
	    "var denied = CustomCursor.preserve(Fn.new { calls = calls + 1 })\n"
	    "if (!(denied is ScreenError) || calls != 0) Fiber.abort(\"cursor acquisition ran callback\")\n") == WREN_RESULT_SUCCESS);
	st_wren_test_screen_fault = (struct st_wren_screen_fault){.name = "st_console_get_video_flags", .status = IO_ERROR_IO};
	CHECK(wrenInterpret(host->vm, "screen_ui_cleanup",
	    "var deniedFlags = VideoFlags.preserve(Fn.new { calls = calls + 1 })\n"
	    "if (!(deniedFlags is ScreenError) || calls != 0) Fiber.abort(\"flags acquisition ran callback\")\n") == WREN_RESULT_SUCCESS);
	st_wren_test_screen_fault = (struct st_wren_screen_fault){0};
	CHECK(wrenInterpret(host->vm, "screen_ui_cleanup",
	    "for (fn in [Fn.new { Screen.attr = 7 }, Fn.new { Screen.window.position = [1, 1] },\n"
	    "    Fn.new { Screen.palette[0] = 0 }, Fn.new { CustomCursor.visible = true },\n"
	    "    Fn.new { VideoFlags.noBlink = true }, Fn.new { Input.mouseVisible = true }]) {\n"
	    "  var obsolete = Fiber.new { fn.call() }\n"
	    "  obsolete.try()\n"
	    "  if (obsolete.error == null) Fiber.abort(\"obsolete setter retained\")\n"
	    "}\n") == WREN_RESULT_SUCCESS);
	return 0;
}

static int
exercise_screen_connected_results(st_wren_connected_t *connected)
{
	CHECK(st_wren_connected_interpret(connected, "screen_connected_results",
	    "import \"syncterm\" for Screen, ScreenError, CustomCursor, CTerm, IOErr\n"
	    "var calls = 0\n"
	    "var first = CustomCursor.current\n"
	    "first.range = 0\n"
	    "first = first.apply()\n") == 0);
	st_wren_test_screen_fault = (struct st_wren_screen_fault){.name = "st_console_snapshot_create", .status = IO_ERROR_IO};
	CHECK(st_wren_connected_interpret(connected, "screen_connected_results",
	    "var denied = Screen.modalRun(Fn.new { calls = calls + 1 })\n"
	    "if (!(denied is ScreenError) || calls != 0 || CTerm.suspended) Fiber.abort(\"modal acquisition ran callback\")\n") == 0);
	st_wren_test_screen_fault = (struct st_wren_screen_fault){.name = "st_console_snapshot_restore", .status = IO_ERROR_IO, .after = true};
	CHECK(st_wren_connected_interpret(connected, "screen_connected_results",
	    "var restored = Screen.modalRun(Fn.new { 42 })\n"
	    "if (!(restored is ScreenError) || restored.code != IOErr.io || CTerm.suspended) Fiber.abort(\"modal restore failure lost\")\n"
	    "if (Screen.modalRun(Fn.new { first }) != first) Fiber.abort(\"modal cleanup replaced returned error\")\n"
	    "var aborted = Fiber.new { Screen.modalRun(Fn.new { Fiber.abort(first) }) }\n"
	    "aborted.try()\n"
	    "if (aborted.error != first || CTerm.suspended) Fiber.abort(\"modal cleanup replaced abort\")\n") == 0);
	CHECK(st_wren_test_screen_fault.calls == 3);
	st_wren_test_screen_fault = (struct st_wren_screen_fault){0};
	return 0;
}

static int
exercise_cursor_contract(st_wren_host_t *public_host)
{
	struct st_wren_host *host = public_host;
	static const char source[] =
	    "import \"syncterm\" for CustomCursor, Error\n"
	    "var saved = CustomCursor.current\n"
	    "var normal = CustomCursor.normal\n"
	    "if (normal.startLine != 14 || normal.endLine != 15 || normal.range != 16 || !normal.visible || !normal.blink) Fiber.abort(\"normal cursor\")\n"
	    "var solid = CustomCursor.solid\n"
	    "if (solid.startLine != 0 || solid.endLine != 15 || solid.range != 16 || !solid.visible || solid.blink) Fiber.abort(\"solid cursor\")\n"
	    "CustomCursor.setStartLine(1)\n"
	    "CustomCursor.setEndLine(3)\n"
	    "CustomCursor.setRange(32)\n"
	    "CustomCursor.setBlink(false)\n"
	    "CustomCursor.setVisible(false)\n"
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
	    "var rejected = invalid.apply()\n"
	    "if (!(rejected is Error) || CustomCursor.range != 16) Fiber.abort(\"invalid cursor\")\n"
	    "var wrongType = Fiber.new { CustomCursor.setVisible(1) }\n"
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
	    "VideoFlags.setAltChars(false)\n"
	    "VideoFlags.setNoBright(false)\n"
	    "VideoFlags.setBgBright(false)\n"
	    "VideoFlags.setBlinkAltChars(false)\n"
	    "VideoFlags.setNoBlink(false)\n"
	    "VideoFlags.setLineGraphicsExpand(false)\n"
	    "if (VideoFlags.altChars || VideoFlags.noBright || VideoFlags.bgBright || VideoFlags.blinkAltChars || VideoFlags.noBlink || VideoFlags.lineGraphicsExpand || VideoFlags.expand != expansion) Fiber.abort(\"live video flags\")\n"
	    "Screen.font[2] = 65535\n"
	    "VideoFlags.setAltChars(true)\n"
	    "VideoFlags.setAltChars(false)\n"
	    "Screen.font[1] = 0\n"
	    "Screen.font[2] = 1\n"
	    "Screen.font[3] = 2\n"
	    "Screen.font[4] = 3\n"
	    "VideoFlags.setAltChars(true)\n"
	    "VideoFlags.setBlinkAltChars(true)\n"
	    "Screen.window.setPosition([1, 1])\n"
	    "Screen.setAttr(0x08)\n"
	    "Screen.window.putChar(65)\n"
	    "Screen.setAttr(0x80)\n"
	    "Screen.window.putChar(65)\n"
	    "Screen.setAttr(0x88)\n"
	    "Screen.window.putChar(65)\n"
	    "var selected = Screen.readRect(1, 1, 3, 1)\n"
	    "if (selected.cellAt(0, 0).font != 1 || selected.cellAt(1, 0).font != 2 || selected.cellAt(2, 0).font != 3) Fiber.abort(\"attribute font selection\")\n"
	    "VideoFlags.setAltChars(false)\n"
	    "VideoFlags.setBlinkAltChars(false)\n"
	    "if (Screen.readRect(1, 1, 1, 1).cellAt(0, 0).font != 1) Fiber.abort(\"stored font identity\")\n"
	    "Screen.restore(savedScreen)\n"
	    "if (Screen.font[1] != 0 || Screen.font[2] != 0 || Screen.font[3] != 0 || Screen.font[4] != 0 || VideoFlags.altChars || VideoFlags.blinkAltChars) Fiber.abort(\"screen display-state restore\")\n"
	    "var staged = VideoFlags.current\n"
	    "staged.noBright = true\n"
	    "staged.bgBright = true\n"
	    "staged.noBlink = true\n"
	    "staged.apply()\n"
	    "if (!VideoFlags.noBright || !VideoFlags.bgBright || !VideoFlags.noBlink || VideoFlags.expand != expansion) Fiber.abort(\"staged video flags\")\n"
	    "VideoFlags.setNoBright(false)\n"
	    "VideoFlags.setBgBright(false)\n"
	    "VideoFlags.setNoBlink(false)\n"
	    "var colours = Color.fromAttr(0x1f)\n"
	    "if (colours[0] != 0xffffff || colours[1] != 0x0000a8) Fiber.abort(\"attribute RGB conversion\")\n"
	    "if (Color.toLegacyAttr(0xffffff, 0) != 0x0f) Fiber.abort(\"RGB attribute conversion\")\n"
	    "var old4 = Screen.palette[4]\n"
	    "var old12 = Screen.palette[12]\n"
	    "Screen.palette.set(4, 0x123456)\n"
	    "Screen.palette.set(12, 0x654321)\n"
	    "if (Color.fromAttr(0x01)[0] != 0x123456 || Color.toLegacyAttr(0x123456, 0) != 0x01) Fiber.abort(\"active foreground palette conversion\")\n"
	    "VideoFlags.setBgBright(true)\n"
	    "if (Color.fromAttr(0x90)[1] != 0x654321 || Color.toLegacyAttr(0, 0x654321) != 0x90) Fiber.abort(\"active background palette conversion\")\n"
	    "VideoFlags.setBgBright(false)\n"
	    "Screen.palette.set(4, old4)\n"
	    "Screen.palette.set(12, old12)\n"
	    "var cell = Cell.new()\n"
	    "cell.legacyAttr = 0x9f\n"
	    "if (cell.legacyAttr != 0x9f || !cell.bright || !cell.blink) Fiber.abort(\"legacy cell state\")\n"
	    "cell.fgRgb = 0xa8a8a8\n"
	    "if (!cell.bright || !cell.blink || cell.fgRgb != 0xa8a8a8) Fiber.abort(\"explicit RGB state\")\n"
	    "if (!ScreenSupports.altBlinkFont || !ScreenSupports.altBoldFont || !ScreenSupports.brightBackground) Fiber.abort(\"screen capabilities\")\n"
	    "var wrongType = Fiber.new { VideoFlags.setNoBlink(1) }\n"
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
	    "VideoFlags.setAltChars(false)\n"
	    "VideoFlags.setBlinkAltChars(false)\n"
	    "Screen.window.setBounds([1, 2, 5, 6])\n"
	    "Screen.font[1] = 0\n"
	    "Screen.setAttr(0x07)\n"
	    "Screen.font[1] = 4\n"
	    "Screen.window.clear()\n"
	    "if (Screen.readRect(1, 2, 1, 2).cellAt(0, 0).font != 4) Fiber.abort(\"clear font selection\")\n"
	    "VideoFlags.setAltChars(true)\n"
	    "Screen.font[2] = 0\n"
	    "Screen.setAttr(0x08)\n"
	    "Screen.font[2] = 5\n"
	    "Screen.window.setPosition([2, 2])\n"
	    "Screen.window.clearToLineEnd()\n"
	    "if (Screen.readRect(2, 3, 2, 3).cellAt(0, 0).font != 5) Fiber.abort(\"clear-line font selection\")\n"
	    "VideoFlags.setBlinkAltChars(true)\n"
	    "Screen.font[3] = 0\n"
	    "Screen.setAttr(0x80)\n"
	    "Screen.font[3] = 6\n"
	    "Screen.window.setPosition([1, 3])\n"
	    "Screen.window.insertLine()\n"
	    "if (Screen.readRect(1, 4, 1, 4).cellAt(0, 0).font != 6) Fiber.abort(\"insert-line font selection\")\n"
	    "Screen.font[4] = 0\n"
	    "Screen.setAttr(0x88)\n"
	    "Screen.font[4] = 7\n"
	    "Screen.window.setPosition([1, 3])\n"
	    "Screen.window.deleteLine()\n"
	    "if (Screen.readRect(1, 6, 1, 6).cellAt(0, 0).font != 7) Fiber.abort(\"delete-line font selection\")\n"
	    "VideoFlags.setAltChars(false)\n"
	    "VideoFlags.setBlinkAltChars(false)\n"
	    "Screen.font[1] = 0\n"
	    "Screen.setAttr(0x07)\n"
	    "Screen.font[1] = 8\n"
	    "Screen.window.scroll()\n"
	    "if (Screen.readRect(1, 6, 1, 6).cellAt(0, 0).font != 8) Fiber.abort(\"scroll font selection\")\n"
	    "Screen.font[1] = 10\n"
	    "if (Screen.readRect(1, 6, 1, 6).cellAt(0, 0).font != 8) Fiber.abort(\"retained font identity\")\n"
	    "VideoFlags.setAltChars(true)\n"
	    "VideoFlags.setBlinkAltChars(true)\n"
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
	    "for (pair in [[0, \"\\u0000\"], [1, \"☺\"], [0x7f, \"⌂\"], [0xb3, \"│\"], [0xff, \"\\u00a0\"]]) {\n"
	    "  if (Codepage.decodeByte(Codepage.cp437, pair[0]) != pair[1]) Fiber.abort(\"CP437 display decoding\")\n"
	    "}\n"
	    "if (Codepage.decodeByte(Codepage.cp1251, 0xc0) != \"А\" || Codepage.decodeByte(Codepage.cp437, 0xc0) != \"└\") Fiber.abort(\"explicit codepage decoding\")\n"
	    "for (pair in [[-1, 65], [Codepage.atariSt + 1, 65], [0.5, 65], [\"0\", 65], [0, -1], [0, 256], [0, 0.5], [0, null]]) {\n"
	    "  var invalid = Fiber.new { Codepage.decodeByte(pair[0], pair[1]) }\n"
	    "  invalid.try()\n"
	    "  if (invalid.error == null) Fiber.abort(\"invalid codepage or byte accepted\")\n"
	    "}\n"
	    "if (Font.name(0) != \"Codepage 437 English\" || Font.name(43) != \"Prestel\" || Font.name(200) != \"\") Fiber.abort(\"font names\")\n"
	    "if (Font.count != 47 || !Font.available(0) || Font.available(45)) Fiber.abort(\"font catalog\")\n"
	    "Screen.font[0] = 1\n"
	    "if (!Codepage.encodes_(\"\\u0410\") || Codepage.encodes_(\"\\u2514\")) Fiber.abort(\"selected codepage encoding\")\n"
	    "if (Codepage.decodeByte(Codepage.cp437, 0xb3) != \"│\") Fiber.abort(\"decoder depends on selected font\")\n"
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
	    "if (!(Menu.setActiveSortProfile(0) == null) || Menu.activeSortProfile != 0) Fiber.abort(\"sort select\")\n"
	    "if (!(Menu.addSortProfile(2, \"Calls\", [29, 4, 1]) == null)) Fiber.abort(\"sort add\")\n"
	    "if (!(Menu.updateSortProfile(2, \"Call Count\", [29, -4, 1]) == null)) Fiber.abort(\"sort update\")\n"
	    "if (!(Menu.deleteSortProfile(2) == null) || !(Menu.saveSortProfiles() == null)) Fiber.abort(\"sort delete\")\n"
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
	    "if (customFont.name != \"Renamed\" || !(customFont.clearFile(2) == null)) Fiber.abort(\"font edit\")\n"
	    "if (!(customFont.delete() == null) || Menu.fonts.count != 1 || !(Menu.saveFonts() == null)) Fiber.abort(\"font delete\")\n"
	    "var unsaved = Menu.createFont(\"Unsaved\", 1)\n"
	    "if (unsaved == null || !(Menu.reloadFonts() == null) || Menu.fonts.count != 1 || Menu.fontsDirty) Fiber.abort(\"font reload\")\n"
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
	    "if (saved != null || document.dirty || document.canUndo || document.canRedo) Fiber.abort(\"document save\")\n"
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
	    "import \"syncterm_menu\" for Menu, MenuError\n"
	    "var failure = Menu.deleteTheme(\"host-theme.ini\")\n"
	    "if (!(failure is MenuError) || failure.code == null || failure.code != failure.nativeCode || !failure.message.contains(\"Classic Theme was selected and saved\")) "
	    "Fiber.abort(\"local delete lost its native failure or completed Classic save\")\n"
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
	    "import \"syncterm_menu\" for Menu, MenuError\n"
	    "var failure = Menu.deleteCloudTheme(\"official/blue\")\n"
	    "if (!(failure is MenuError) || failure.code == null || failure.code != failure.nativeCode || !failure.message.contains(\"Classic Theme was selected and saved\")) "
	    "Fiber.abort(\"cloud delete lost its native failure or completed Classic save\")\n"
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
		unsigned char byte = (unsigned char)*text;
		struct st_console_event event = {
			.type = ST_CONSOLE_EVENT_TEXT,
			.data.text.codepoint = byte,
		};
		if (byte < 32 || byte == 127) {
			event = (struct st_console_event){
				.type = ST_CONSOLE_EVENT_KEY_CODE,
				.data.key.code = byte,
			};
			if (byte >= 1 && byte <= 26 && byte != 8 && byte != 9 && byte != 13)
				event.data.key.modifiers = ST_CONSOLE_MODIFIER_CONTROL;
		}
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
		.type = ST_CONSOLE_EVENT_KEY_CODE,
		.data.key.code = (uint16_t)poster->finish,
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
		.type = ST_CONSOLE_EVENT_KEY_CODE, .data.key.code = ST_CONSOLE_KEY_F1,
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
	    "Screen.setAttr(0x1f)\n"
	    "WrenConsole.run(\"syncterm\")\n"
	    "if (Screen.attr != 0x1f) Fiber.abort(\"REPL did not restore caller attribute\")\n"
	    "if (!correct) Fiber.abort(\"REPL inherited editor attribute\")\n"
	    "Screen.restore(saved)\n"
	    "WrenConsole.unregister(\"style-test\")\n";
	struct st_console_event enter = {
		.type = ST_CONSOLE_EVENT_KEY_CODE,
		.data.key.code = '\r',
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
	static const char strict[] =
	    "import \"syncterm\" for Input, Key, KeyEvent, Mod\n"
	    "for (scalar in [0, 0x100, 0x301, 0x1f600, 0x10ffff]) {\n"
	    "  var text = String.fromCodePoint(scalar)\n"
	    "  var event = KeyEvent.text(text, 0x8000003f)\n"
	    "  if (event.key != scalar || event.codepoint != scalar || event.text != text || event.modifiers != 0x8000003f || event.byte != null || event.scanCode != null) Fiber.abort(\"strict scalar constructor\")\n"
	    "  Input.unget(event)\n"
	    "  var returned = Input.poll()\n"
	    "  if (returned.key != scalar || returned.text != text || returned.modifiers != 0x8000003f) Fiber.abort(\"scalar round trip\")\n"
	    "}\n"
	    "if (KeyEvent.text(String.fromCodePoint(0x100)).key == Key.up) Fiber.abort(\"text and named namespace collision\")\n"
	    "for (bad in [\"\", \"ab\", \"e\" + String.fromCodePoint(0x301), String.fromByte(255), String.fromByte(128), String.fromByte(192) + String.fromByte(128), String.fromCodePoint(0) + \"a\", 42, null]) {\n"
	    "  var attempt = Fiber.new { KeyEvent.text(bad) }\n"
	    "  attempt.try()\n"
	    "  if (attempt.error == null) Fiber.abort(\"invalid scalar accepted\")\n"
	    "}\n"
	    "for (bad in [-1, 0xd800, 0xdfff, 0x120001, 0.5]) {\n"
	    "  var attempt = Fiber.new { KeyEvent.new(bad) }\n"
	    "  attempt.try()\n"
	    "  if (attempt.error == null) Fiber.abort(\"invalid key accepted\")\n"
	    "}\n"
	    "var chord = KeyEvent.new(Key.left, Mod.control | Mod.shift | Mod.capsLock)\n"
	    "if (!chord.matches(Key.left, Mod.control | Mod.shift) || chord.matches(Key.left, Mod.control) || chord.matches(Key.left, Mod.shift)) Fiber.abort(\"combined modifier matching\")\n"
	    "var alt = KeyEvent.new(Key.right, Mod.alt)\n"
	    "if (alt.key != Key.right || alt.key == Key.quit || alt.matches(Key.right, Mod.gui)) Fiber.abort(\"unsupported modifier preserved\")\n"
	    "var future = KeyEvent.new(Key.up, 0x80000000)\n"
	    "if (future.matches(Key.up, 0)) Fiber.abort(\"unknown modifier dropped\")\n"
	    "for (bad in [-1, 0.5, 0x100000000]) {\n"
	    "  var attempt = Fiber.new { future.matches(Key.up, bad) }\n"
	    "  attempt.try()\n"
	    "  if (attempt.error == null) Fiber.abort(\"invalid match modifier\")\n"
	    "}\n"
	    "var quit = Fiber.new { KeyEvent.new(Key.quit, Mod.alt) }\n"
	    "quit.try()\n"
	    "if (quit.error == null) Fiber.abort(\"modified quit accepted\")\n"
	    "var raw = KeyEvent.byte(0)\n"
	    "Input.unget(raw)\n"
	    "var got = Input.poll()\n"
	    "if (got.byte != 0 || got.key != null || got.codepoint != null || got.text != \"\") Fiber.abort(\"zero byte round trip\")\n"
	    "raw = KeyEvent.doorway(0x48, true)\n"
	    "Input.unget(raw)\n"
	    "got = Input.poll()\n"
	    "if (got.scanCode != 0x48 || !got.enhanced || got.key != null || got.byte != null) Fiber.abort(\"DoorWay round trip\")\n";
	static const char values[] =
	    "import \"syncterm\" for Input, Key, KeyEvent, Mouse, MouseEvent, PhysicalKeyEvent\n"
	    "var key = KeyEvent.new(Key.f12)\n"
	    "if (key.codepoint != null || key.text != \"\") Fiber.abort(\"extended key text\")\n"
	    "Input.unget(key)\n"
	    "var gotKey = Input.poll()\n"
	    "if (!(gotKey is KeyEvent) || gotKey.key != Key.f12 || gotKey.codepoint != null || gotKey.text != \"\") Fiber.abort(\"key unget\")\n"
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
	    "var text = KeyEvent.text(\"À\", 15)\n"
	    "if (text.key != 0xc0 || text.codepoint != 0xc0 || text.text != \"À\" || text.modifiers != 15) Fiber.abort(\"constructor font independence\")\n"
	    "var escape = KeyEvent.new(Key.escape)\n"
	    "if (escape.key != Key.escape || escape.codepoint != null || escape.text != \"\") Fiber.abort(\"named escape\")\n"
	    "var encodable = Input.poll()\n"
	    "if (!(encodable is KeyEvent) || encodable.key != 0x0410 || encodable.codepoint != 0x0410 || encodable.text != \"А\") Fiber.abort(\"Cyrillic text\")\n"
	    "Input.unget(encodable)\n"
	    "var returned = Input.poll()\n"
	    "if (returned.key != 0x0410 || returned.text != \"А\") Fiber.abort(\"unicode unget\")\n"
	    "var unencodable = Input.poll()\n"
	    "if (unencodable.key != 0x4e00 || unencodable.codepoint != 0x4e00 || unencodable.text != \"一\") Fiber.abort(\"CJK text\")\n"
	    "var named = Input.poll()\n"
	    "if (named.key != 0x1100c0 || named.codepoint != null || named.text != \"\") Fiber.abort(\"unrecognized native key\")\n"
	    "var numeric = Input.poll()\n"
	    "if (numeric.byte != 0xc0 || numeric.key != null || numeric.codepoint != null || numeric.text != \"\") Fiber.abort(\"raw numeric entry\")\n"
	    "Input.unget(numeric)\n"
	    "if (Input.poll().byte != 0xc0) Fiber.abort(\"byte unget\")\n"
	    "var byte255 = Input.poll()\n"
	    "var unicode255 = Input.poll()\n"
	    "if (byte255.byte != 255 || byte255.codepoint != null || unicode255.key != 255 || unicode255.text != \"ÿ\" || unicode255.byte != null) Fiber.abort(\"byte and Unicode separation\")\n"
	    "Screen.font[0] = original\n";
	const struct st_console_event numeric = {
		.type = ST_CONSOLE_EVENT_CODEPAGE_BYTE,
		.data.codepage_byte = 0xc0,
	};
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

	CHECK(wrenInterpret(host->vm, "strict_input_contract", strict)
	    == WREN_RESULT_SUCCESS);
	CHECK(wrenInterpret(host->vm, "input_value_contract", values)
	    == WREN_RESULT_SUCCESS);
	CHECK(st_console_post_event(console, &encodable) == 0);
	CHECK(st_console_post_event(console, &unencodable) == 0);
	CHECK(st_console_post_event(console, &compatibility) == 0);
	CHECK(st_console_post_event(console, &numeric) == 0);
	CHECK(st_console_post_event(console, &(struct st_console_event){
	    .type = ST_CONSOLE_EVENT_CODEPAGE_BYTE, .data.codepage_byte = 255}) == 0);
	CHECK(st_console_post_event(console, &(struct st_console_event){
	    .type = ST_CONSOLE_EVENT_TEXT, .data.text.codepoint = 255}) == 0);
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
	} modified[] = {
		{13, ST_CONSOLE_MODIFIER_CONTROL},
		{8, ST_CONSOLE_MODIFIER_CONTROL},
		{ST_CONSOLE_KEY_HOME, ST_CONSOLE_MODIFIER_SHIFT},
		{ST_CONSOLE_KEY_HOME, ST_CONSOLE_MODIFIER_CONTROL},
		{ST_CONSOLE_KEY_PAGE_UP, ST_CONSOLE_MODIFIER_SHIFT},
		{ST_CONSOLE_KEY_PAGE_UP, ST_CONSOLE_MODIFIER_CONTROL},
		{ST_CONSOLE_KEY_PAGE_DOWN, ST_CONSOLE_MODIFIER_SHIFT},
		{ST_CONSOLE_KEY_PAGE_DOWN, ST_CONSOLE_MODIFIER_CONTROL},
		{ST_CONSOLE_KEY_RIGHT, UINT32_C(0x8000000f)},
		{UINT16_MAX, ST_CONSOLE_MODIFIER_ALT},
	};
	for (size_t i = 0; i < sizeof(modified) / sizeof(modified[0]); ++i) {
		const struct st_console_event event = {
			.type = ST_CONSOLE_EVENT_KEY_CODE,
			.data.key = {.code = modified[i].key, .modifiers = modified[i].modifiers},
		};
		CHECK(st_console_post_event(console, &event) == 0);
	}
	CHECK(wrenInterpret(host->vm, "modified_input_contract",
	    "import \"syncterm\" for Input, Key\n"
	    "var keys = [Key.enter, Key.backspace, Key.home, Key.home, Key.pageUp, Key.pageUp, Key.pageDown, Key.pageDown, Key.right, 0x11ffff]\n"
	    "var modifiers = [2, 2, 1, 2, 1, 2, 1, 2, 0x8000000f, 4]\n"
	    "for (i in 0...keys.count) {\n"
	    "  var event = Input.poll()\n"
	    "  if (event.key != keys[i] || event.modifiers != modifiers[i] || event.codepoint != null) Fiber.abort(\"modified key identity\")\n"
	    "  Input.unget(event)\n"
	    "  var returned = Input.poll()\n"
	    "  if (returned.key != keys[i] || returned.modifiers != modifiers[i]) Fiber.abort(\"modified unget\")\n"
	    "}\n") == WREN_RESULT_SUCCESS);
	return 0;
}

static int
exercise_numeric_encoding_contract(struct st_wren_host *host,
	st_console_t *console)
{
	st_font_catalog_t *fonts = NULL;
	st_cterm_t *terminal = NULL;
	CHECK(host->menu.cterm == NULL);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	const struct st_cterm_config config = {
		.console = console, .fonts = fonts, .emulation = ST_CTERM_ANSI_BBS,
		.encoding = ST_CTERM_ENCODING_UTF8, .codepage = ST_CODEPAGE_CP1251,
		.quiet = true,
	};
	CHECK(st_cterm_create(&terminal, &config) == 0);
	host->menu.cterm = terminal;
	CHECK(wrenInterpret(host->vm, "numeric_encoding_contract",
	    "import \"syncterm\" for Input, KeyEvent, Screen\n"
	    "class NumericProbe {\n"
	    "  static start() {\n"
	    "    __font = Screen.font[0]\n"
	    "    Screen.font[0] = 0\n"
	    "    __worker = Fiber.new {\n"
	    "      var claim = Input.pushClaim(Fn.new {|event|\n"
	    "        NumericProbe.check(event)\n"
	    "        return true\n"
	    "      })\n"
	    "      Fiber.yield()\n"
	    "      claim.pop()\n"
	    "    }\n"
	    "    __worker.call()\n"
	    "  }\n"
	    "  static expect() {\n"
		    "    __seen = 0\n"
	    "  }\n"
	    "  static check(event) {\n"
	    "    if (!(event is KeyEvent) || event.byte != 224 || event.key != null || event.codepoint != null || event.text != \"\") Fiber.abort(\"numeric input encoding\")\n"
	    "    __seen = __seen + 1\n"
	    "  }\n"
	    "  static poll() { check(Input.poll()) }\n"
	    "  static verify() {\n"
	    "    if (__seen != 2) Fiber.abort(\"numeric queue and dispatch\")\n"
	    "  }\n"
	    "  static finish() {\n"
	    "    __worker.call()\n"
	    "    Screen.font[0] = __font\n"
	    "  }\n"
	    "}\n"
	    "NumericProbe.start()\n") == WREN_RESULT_SUCCESS);
	const struct {
		enum st_cterm_encoding encoding;
		enum st_codepage codepage;
	} cases[] = {
		{ST_CTERM_ENCODING_UTF8, ST_CODEPAGE_CP1251},
		{ST_CTERM_ENCODING_CODEPAGE, ST_CODEPAGE_CP1251},
		{ST_CTERM_ENCODING_CODEPAGE, ST_CODEPAGE_KOI8_R},
		{ST_CTERM_ENCODING_CODEPAGE, ST_CODEPAGE_CP850},
		{ST_CTERM_ENCODING_CODEPAGE, ST_CODEPAGE_ISO_8859_1},
		{ST_CTERM_ENCODING_CODEPAGE, ST_CODEPAGE_CP437},
		{ST_CTERM_ENCODING_UTF8, ST_CODEPAGE_CP437},
	};
	const struct st_console_event event = {
		.type = ST_CONSOLE_EVENT_CODEPAGE_BYTE, .data.codepage_byte = 224,
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		CHECK(st_cterm_set_encoding(terminal, cases[i].encoding, cases[i].codepage) == 0);
		CHECK(wrenInterpret(host->vm, "numeric_encoding_contract", "NumericProbe.expect()\n") == WREN_RESULT_SUCCESS);
		CHECK(st_console_post_event(console, &event) == 0);
		CHECK(wrenInterpret(host->vm, "numeric_encoding_contract", "NumericProbe.poll()\n") == WREN_RESULT_SUCCESS);
		bool consumed = false;
		CHECK(st_wren_input_dispatch_console_event(host->vm, &event, &consumed) == 0 && consumed);
		CHECK(wrenInterpret(host->vm, "numeric_encoding_contract", "NumericProbe.verify()\n") == WREN_RESULT_SUCCESS);
	}
	CHECK(wrenInterpret(host->vm, "numeric_encoding_contract", "NumericProbe.finish()\n") == WREN_RESULT_SUCCESS);
	host->menu.cterm = NULL;
	st_cterm_destroy(&terminal);
	st_font_catalog_destroy(fonts);
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
	    "        __seen.add(event.key)\n"
	    "        return event.key == 2\n"
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
		.data.key.native = {.type = ST_CONSOLE_EVENT_TEXT, .data.text.codepoint = 1},
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
	event.data.key.native.data.text.codepoint = 2;
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
exercise_key_click_setting(struct st_wren_host *host)
{
	st_console_t *console = NULL;
	st_font_catalog_t *fonts = NULL;
	st_cterm_t *terminal = NULL;
	st_wren_connected_t *connected = NULL;
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {.columns = 80, .rows = 25, .cell_width = 8, .cell_height = 8};
	CHECK(st_console_create(&console, &display, &geometry) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	const struct st_cterm_config config = {.console = console, .fonts = fonts,
		.emulation = ST_CTERM_ATARI_ST_VT52, .encoding = ST_CTERM_ENCODING_CODEPAGE,
		.codepage = ST_CODEPAGE_ATARI_ST, .graphics_mode = ST_CTERM_GRAPHICS_IGS_221, .quiet = true};
	CHECK(st_cterm_create(&terminal, &config) == 0);
	struct st_profile profile;
	st_profile_init(&profile);
	const struct st_wren_connected_config wren_config = {
		.host = host, .console = console, .cterm = terminal, .profile = &profile};
	CHECK(st_wren_connected_create(&connected, &wren_config) == 0);
	st_profile_dispose(&profile);
	CHECK(wrenInterpret(host->vm, "key_click_setting",
		"import \"syncterm_menu\" for Menu\n"
		"var settings = Menu.settings\n"
		"var previous = settings.atariStKeyClicks\n"
		"settings.atariStKeyClicks = false\n"
		"if (settings.apply() != null) Fiber.abort(\"disable clicks\")\n") == WREN_RESULT_SUCCESS);
	CHECK(!terminal->atari_st_key_clicks);
	CHECK(wrenInterpret(host->vm, "key_click_setting",
		"settings.atariStKeyClicks = true\n"
		"if (settings.apply() != null) Fiber.abort(\"enable clicks\")\n") == WREN_RESULT_SUCCESS);
	CHECK(terminal->atari_st_key_clicks);
	CHECK(wrenInterpret(host->vm, "key_click_setting",
		"settings.atariStKeyClicks = previous\n"
		"if (settings.apply() != null) Fiber.abort(\"restore clicks\")\n") == WREN_RESULT_SUCCESS);
	st_wren_connected_destroy(connected);
	st_cterm_destroy(&terminal);
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
	return 0;
}

static int exercise_skypix_menu_audio(struct st_wren_host *host)
{
	st_console_t *console=NULL;
	st_font_catalog_t *fonts=NULL;
	st_cterm_t *terminal=NULL;
	st_audio_runtime_t *runtime=NULL;
	st_wren_connected_t *connected=NULL;
	const struct st_console_display_config display={.backend=ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry={.columns=80,.rows=25,.cell_width=8,.cell_height=8,.wrap=true};
	CHECK(st_console_create(&console,&display,&geometry)==0);
	CHECK(st_font_catalog_create(&fonts,console)==0);
	CHECK(st_audio_runtime_create_pull(&runtime)==0);
	const struct st_cterm_config config={.console=console,.fonts=fonts,.audio=runtime,
		.emulation=ST_CTERM_ANSI_BBS,.encoding=ST_CTERM_ENCODING_UTF8,
		.codepage=ST_CODEPAGE_CP437,.graphics_mode=ST_CTERM_GRAPHICS_SKYPIX_31,.quiet=true};
	CHECK(st_cterm_create(&terminal,&config)==0);
	uint8_t *sample=calloc(20006,1);CHECK(sample!=NULL);
	sample[2]=0x4e;sample[3]=0x20;sample[4]=0x1f;sample[5]=0x40;
	CHECK(st_cterm_skypix_load_sample(terminal,sample,20006)==0);free(sample);
	struct st_profile profile;st_profile_init(&profile);
	const struct st_wren_connected_config wren_config={.host=host,.console=console,.cterm=terminal,.profile=&profile};
	CHECK(st_wren_connected_create(&connected,&wren_config)==0);
	st_profile_dispose(&profile);
	size_t used;
	CHECK(st_cterm_write(terminal,"\x1b[9;0;0;0;1!",12,&used)==0 && used==12);
	int16_t frames[4096*2];
	CHECK(st_audio_runtime_pull(runtime,frames,4096)==0);
	CHECK(audio_stream_buffered_frames(terminal->fx_stream)==0);
	const struct st_console_pixel_cursor hidden={0};
	CHECK(st_console_set_pixel_cursor(console,&hidden)==0);
	CHECK(st_wren_connected_interpret(connected,"skypix_menu_audio",
		"import \"syncterm\" for Input\n"
		"if (Input.next(45) != null) Fiber.abort(\"unexpected input\")\n")==0);
	CHECK(audio_stream_buffered_frames(terminal->fx_stream)>0);
	CHECK(audio_stream_buffered_frames(terminal->fx_stream)<=8192);
	CHECK(st_cterm_input_paused(terminal));
	CHECK(st_audio_runtime_pull(runtime,frames,4096)==0);
	CHECK(st_audio_runtime_pull(runtime,frames,4096)==0);
	CHECK(audio_stream_buffered_frames(terminal->fx_stream)==0);
	CHECK(wrenInterpret(host->vm,"skypix_menu_owner_audio",
		"import \"syncterm\" for Input\n"
		"if (Input.next(45) != null) Fiber.abort(\"unexpected menu input\")\n")==WREN_RESULT_SUCCESS);
	CHECK(audio_stream_buffered_frames(terminal->fx_stream)>0);
	CHECK(audio_stream_buffered_frames(terminal->fx_stream)<=8192);
	struct st_connection ended={0};atomic_init(&ended.connected,false);
	host->poll_contexts->connection=&ended;
	CHECK(wrenInterpret(host->vm,"skypix_menu_eof_audio",
		"import \"syncterm\" for Input\n"
		"if (Input.next(0) != null) Fiber.abort(\"unexpected EOF input\")\n")==WREN_RESULT_SUCCESS);
	CHECK(!st_cterm_input_paused(terminal) && audio_stream_buffered_frames(terminal->fx_stream)==0);
	host->poll_contexts->connection=NULL;
	st_wren_connected_destroy(connected);
	CHECK(host->poll_contexts==NULL);
	st_cterm_destroy(&terminal);
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
	st_audio_runtime_destroy(runtime);
	return 0;
}

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
	    "import \"syncterm\" for Input, Key, Mod\n"
	    "var first = Input.nextForWidget_(false, 100)\n"
	    "if (first == null || first.key != Key.quit) Fiber.abort(\"initial quit\")\n"
	    "if (Input.nextForWidget_(true, 0) != null) Fiber.abort(\"at-exit timeout\")\n"
	    "for (i in 0...3) {\n"
	    "  var event = Input.poll()\n"
	    "  if (event == null || event.key != Key.quit) Fiber.abort(\"quit did not persist\")\n"
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
	    "import \"syncterm\" for Host, Emoji\n"
	    "import \"syncterm_menu\" for Menu, MenuError\n"
	    "if (!Host.safeMode || Menu.load(null) != null) Fiber.abort(\"safe setup\")\n"
	    "var grin = Emoji.search(\"grinning face\")[0]\n"
	    "if (Emoji.recordUse(grin) != null || Emoji.recent[0].text != grin.text) Fiber.abort(\"safe emoji recent\")\n"
	    "var count = Menu.entries.count\n"
	    "var entry = Menu.entries[0]\n"
	    "entry.profile.comment = \"unsaved edit\"\n"
	    "if (!(entry.rename(\"Renamed\") == null) || (entry.save() == null) || !entry.dirty) Fiber.abort(\"entry save\")\n"
	    "if ((entry.delete() == null) || Menu.entries.count != count || Menu.entries[0].name != \"Renamed\") Fiber.abort(\"entry delete\")\n"
	    "var fresh = Menu.create(\"Fresh\")\n"
	    "fresh.profile.comment = \"unsaved new entry\"\n"
	    "if ((fresh.save() == null) || !fresh.dirty || Menu.entries.count != count + 1) Fiber.abort(\"new entry save\")\n"
	    "if (!(fresh.delete() == null) || Menu.entries.count != count) Fiber.abort(\"new entry discard\")\n"
	    "var defaults = Menu.defaults\n"
	    "defaults.comment = \"unsaved defaults\"\n"
	    "if ((Menu.saveDefaults() == null) || !Menu.defaultsDirty || Menu.defaults.comment != \"unsaved defaults\") Fiber.abort(\"defaults save\")\n"
	    "var snapshot = Menu.settings\n"
	    "var confirm = snapshot.confirmClose\n"
	    "snapshot.confirmClose = !confirm\n"
	    "if ((snapshot.save() == null) || !snapshot.dirty || Menu.settings.confirmClose != confirm) Fiber.abort(\"settings save\")\n"
	    "if (!(Menu.updateWebList(0, \"https://example.invalid/changed\") == null) || !Menu.webListsDirty) Fiber.abort(\"web edit\")\n"
	    "if ((Menu.saveWebLists() == null) || !Menu.webListsDirty) Fiber.abort(\"web save\")\n"
	    "if (Menu.canCreateFont || !(Menu.createFont(\"Blocked\", 0) is MenuError) || (Menu.saveFonts() == null)) Fiber.abort(\"font safe mode\")\n"
	    "if (!(Menu.reloadFonts() == null) || Menu.fontsDirty) Fiber.abort(\"font safe reload\")\n"
	    "var algorithm = Menu.encryptionInfo.algorithm\n"
	    "var keySize = Menu.encryptionInfo.keyBits\n"
	    "if ((Menu.setEncryption(6, 128, \"secret\") == null)) Fiber.abort(\"encryption save\")\n"
	    "if (Menu.encryptionInfo.algorithm != algorithm || Menu.encryptionInfo.keyBits != keySize) Fiber.abort(\"encryption state\")\n";
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
	const char *directory, const char *fixture, uint32_t command_modifier);
#endif
int
main(int argc, char **argv)
{
	bool menu_only = argc == 2 && strcmp(argv[1], "--menu-only") == 0;
	static const uint8_t default_bitmap[128 * 16] = {0};
	static const uint8_t default_wide_bitmap[128] = {0};
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
		{UINT32_C(0x00b3), 29},
		{UINT32_C(0x00e9), 31},
		{UINT32_C(0x03a9), 127},
		{UINT32_C(0x203c), 25},
		{UINT32_C(0x2060), 129},
		{UINT32_C(0x2191), 24},
		{UINT32_C(0x2193), 23},
		{UINT32_C(0x2502), 26},
		{UINT32_C(0x4e00), 128},
		{UINT32_C(0x4e16), 130},
		{UINT32_C(0xfeff), 30},
	};
	static const struct st_console_font_cluster_mapping default_clusters[] = {
		{.text = "👩🏽‍❤️‍💋‍👩🏻", .text_size = sizeof("👩🏽‍❤️‍💋‍👩🏻") - 1,
		    .glyph = 131},
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
		.wide_glyph_count = 4,
		.wide_row_stride = 2,
		.wide_glyph_stride = 32,
		.mapping = default_mapping,
		.mapping_count = sizeof(default_mapping) / sizeof(default_mapping[0]),
		.cluster_mapping = default_clusters,
		.cluster_mapping_count = sizeof(default_clusters) / sizeof(default_clusters[0]),
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
		.data.key = {.code = 0x11, .modifiers = ST_CONSOLE_MODIFIER_CONTROL},
	};
	struct st_console_event ctrl_s = {
		.type = ST_CONSOLE_EVENT_KEY_CODE,
		.data.key = {.code = 0x13, .modifiers = ST_CONSOLE_MODIFIER_CONTROL},
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
	/* Keep the diagnostic fixture as the designated fallback, matching
	 * the application's explicit installation of its presentation font. */
	CHECK(st_console_set_fallback_font(console, default_identifier) == 0);
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
	CHECK(st_audio_runtime_create(&audio_runtime, 0) == 0);
	CHECK(st_session_history_publish(session_history, history_cells, 2, 2,
	    ST_SCREEN_MODE_CURRENT, true, NULL) == 0);
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
	CHECK(exercise_numeric_encoding_contract(host, console) == 0);
	CHECK(exercise_claim_wake_contract(host) == 0);
	CHECK(exercise_input_deadline_contract(host, console) == 0);
	CHECK(exercise_key_click_setting(host) == 0);
	CHECK(exercise_skypix_menu_audio(host) == 0);
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
	    "if (ordinary == null || ordinary.scanCode != 46 || ordinary.enhanced || ordinary.key != null || "
	    "enhanced == null || enhanced.scanCode != 28 || !enhanced.enhanced || enhanced.key != null) "
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
	    "event != null) "
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
	CHECK(exercise_integer_contract(host) == 0);
	CHECK(exercise_foreign_type_contract(host) == 0);
	CHECK(exercise_filesystem_failures(host) == 0);
	CHECK(exercise_snapshot_lifetime_contract(host) == 0);
	CHECK(exercise_screen_update_contract(host, console) == 0);
	CHECK(exercise_zero_width_contract(host) == 0);
	CHECK(exercise_web_refresh_overlay(host, console) == 0);
	CHECK(exercise_pixel_blit_contract(host) == 0);
	CHECK(exercise_screen_results(host) == 0);
	CHECK(exercise_screen_failure_boundaries(host) == 0);
	CHECK(exercise_screen_cleanup_results(host) == 0);
	CHECK(exercise_screen_ui_cleanup(host) == 0);
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
	CHECK(exercise_emoji_contract(host) == 0);
	CHECK(exercise_emoji_result_contract(host, host->vm, NULL) == 0);
	CHECK(exercise_emoji_result_contract(host, host->picker_vm, NULL) == 0);
	CHECK(exercise_transfer_results(host->vm, NULL) == 0);
	CHECK(exercise_transfer_results(host->picker_vm, NULL) == 0);
	CHECK(exercise_cterm_boundaries(host, console) == 0);
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
		CHECK(exercise_session_lease_contract(host, console, &selected) == 0);
		CHECK(exercise_connected_without_sftp(host, console, &selected) == 0);
		CHECK(exercise_app_lifecycle(host, console, &selected) == 0);
		CHECK(exercise_cleanup_scopes(host, console, &selected) == 0);
		CHECK(exercise_owned_sends(host, console) == 0);
		CHECK(exercise_upload_consent(host, console) == 0);
		CHECK(exercise_transfer_wire(host, console) == 0);
		CHECK(exercise_transfer_ui(host, console, &selected) == 0);
		CHECK(exercise_connected_session_log(host, console, &selected) == 0);
		CHECK(exercise_connected_link_hover(host, console, &selected) == 0);
#if !SYNCTERM_PLATFORM_WINDOWS
		CHECK(exercise_connected_throttle(host, console) == 0);
		CHECK(exercise_auto_transfer(host, console) == 0);
#ifdef ST_RIP_TEST_PYTHON
		CHECK(exercise_rip_transfer(host, console) == 0);
#endif
#endif
		CHECK(exercise_connected_sftp(host, console, &selected) == 0);
#ifdef ST_WREN_HAVE_LIVE_SFTP
		if (argc == 2) CHECK(st_wren_test_live_sftp(host, console,
		    ST_WREN_TEST_DIRECTORY, argv[1], command_modifiers(host)) == 0);
#endif
		CHECK(exercise_font_api(host, console, &selected) == 0);
	CHECK(exercise_connected_scrollback(host, console, &selected) == 0);
	}
	st_profile_dispose(&selected);
	CHECK(wrenInterpret(((struct st_wren_host *)host)->vm,
	    "alt_shortcut_contract",
	    "import \"syncterm\" for Key\n"
	    "import \"main_menu\" for MainMenu\n"
	    "MainMenu.registerCommand(121, 4, \"Alt-Y\", \"Test\", "
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
	console_config.cell_width = 0;
	console_config.cell_height = 0;
	CHECK(st_console_create(&console, &console_config_display, &console_config) == 0);
	uint32_t text_capabilities;
	CHECK(st_console_capabilities(console, &text_capabilities) == 0);
	CHECK((text_capabilities & ST_CONSOLE_CAP_TEXT_TERMINAL) != 0);
	CHECK((text_capabilities & ST_CONSOLE_CAP_LOCAL_CONTROL_KEYS) != 0);
	/* Text consoles use a Unicode presentation font, not a byte-indexed
	 * codepage font. Keep its glyph indices distinct from CP437 bytes. */
	CHECK(st_console_register_static_font(console, &default_font,
	    &default_identifier) == 0 && default_identifier == 0);
	host_config.console = console;
	CHECK(st_wren_host_create(&host, &host_config) == 0);
	CHECK(wrenInterpret(((struct st_wren_host *)host)->vm,
	    "text_cell_characters",
	    "import \"syncterm\" for Surface\n"
	    "var c = Surface.new(1, 1)[0]\n"
	    "c.ch = \"│\"\n"
	    "if (c.ch != \"│\") Fiber.abort(\"separator character\")\n"
	    "c.font = 0\n"
	    "if (c.ch != \"│\") Fiber.abort(\"font character preservation\")\n"
	    "c.ch = \"\\u00b3\"\n"
	    "if (c.ch != \"\\u00b3\") Fiber.abort(\"superscript character\")\n"
	    "for (ch in [\"↑\", \"↓\", \"‼\"]) {\n"
	    "  c.ch = ch\n"
	    "  if (c.ch != ch) Fiber.abort(\"indicator character\")\n"
	    "}\n") == WREN_RESULT_SUCCESS);
	st_profile_init(&selected);
	struct st_wren_connected_config text_connected_config = {
		.host = host, .console = console, .profile = &selected,
		.status_enabled = true,
	};
	(void)snprintf(selected.name, sizeof(selected.name), "é一");
	selected.encoding = ST_PROFILE_ENCODING_UTF8;
	/* Text backends expose terminal state but cannot load/select bitmaps. */
	st_font_catalog_t *text_fonts = NULL;
	st_cterm_t *text_terminal = NULL;
	st_wren_connected_t *font_text_connected = NULL;
	CHECK(st_font_catalog_create(&text_fonts, console) == 0);
	const struct st_cterm_config text_terminal_config = {
		.console = console, .fonts = text_fonts,
		.emulation = ST_CTERM_ANSI_BBS, .encoding = ST_CTERM_ENCODING_UTF8,
		.codepage = ST_CODEPAGE_CP437, .quiet = true,
	};
	CHECK(st_cterm_create(&text_terminal, &text_terminal_config) == 0);
	free(selected.download_directory);
	selected.download_directory = strdup(ST_WREN_TEST_DIRECTORY);
	CHECK(selected.download_directory != NULL);
	struct st_wren_connected_config font_text_config = {
		.host = host, .console = console, .profile = &selected, .cterm = text_terminal,
	};
	CHECK(st_wren_connected_create(&font_text_connected, &font_text_config) == 0);
	CHECK(st_wren_connected_interpret(font_text_connected, "font_text_capability",
	    "import \"syncterm\" for Font, FontError, FontErr, Download, CTerm, CTermFontMask\n"
	    "var file = Download.create(\"font-unavailable.f16\")\n"
	    "var result = Font.load(file)\n"
	    "if (!(result is FontError) || result.code != FontErr.unavailable) Fiber.abort(\"text font loading\")\n"
	    "result = CTerm.setFont(0, CTermFontMask.all)\n"
	    "if (!(result is FontError) || result.code != FontErr.unavailable) Fiber.abort(\"text font selection\")\n"
	    "var failed = Fiber.new { CTerm.font[0] = 0 }\n"
	    "failed.try()\n"
	    "if (!(failed.error is FontError)) Fiber.abort(\"text font assignment\")\n"
	    "Download.delete(\"font-unavailable.f16\")\n") == 0);
	st_wren_connected_destroy(font_text_connected);
	st_cterm_destroy(&text_terminal);
	st_font_catalog_destroy(text_fonts);
	st_wren_connected_t *text_connected = NULL;
	CHECK(st_wren_connected_create(&text_connected, &text_connected_config) == 0);
	CHECK(st_wren_connected_render_status(text_connected, true) == 0);
	struct st_console_cell status_cells[80];
	const struct st_console_rect status_row = {0, 24, 80, 1};
	CHECK(st_console_read_cells(console, &status_row, status_cells, 80) == 0);
	CHECK(status_cells[1].glyph == 31 && status_cells[2].glyph == 128
	    && (status_cells[3].flags & ST_CONSOLE_CELL_CONTINUATION) != 0);
	size_t separators = 0;
	for (size_t i = 0; i < 80; ++i) {
		char text[4];
		size_t size;
		CHECK(st_console_font_glyph_text(console, status_cells[i].glyph_font,
		    status_cells[i].glyph, text, sizeof(text), &size) == 0);
		CHECK(size != 2 || memcmp(text, "\xc2\xb3", 2) != 0);
		if (size == 3 && memcmp(text, "\xe2\x94\x82", 3) == 0)
			++separators;
	}
	CHECK(separators == 3);
	st_wren_connected_destroy(text_connected);
	st_profile_dispose(&selected);
	CHECK(wrenInterpret(((struct st_wren_host *)host)->vm,
	    "ansi_host_capabilities",
	    "import \"syncterm\" for Host, PixelBlit, PixelBuffer, Screen, Key, Error\n"
	    "import \"ui_app\" for App\n"
	    "if (App.new().binding(112, 2) == null) Fiber.abort(\"ANSI emoji shortcut\")\n"
	    "if (!Host.textTerminal || !Host.localControlKeys) "
	    "Fiber.abort(\"ANSI host capabilities\")\n"
	    "if (Screen.supports.pixels || !(Screen.pixelSize is Error) || "
	    "!(Screen.cellPixelSize is Error)) Fiber.abort(\"ANSI pixel capability\")\n"
	    "if (!(Screen.setPixel(0, 0, 0) is Error) || "
	    "!(Screen.blitPixels(PixelBuffer.new(1, 1), PixelBlit.new()) is Error)) "
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
	console_config.cell_width = 8;
	console_config.cell_height = 16;
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
