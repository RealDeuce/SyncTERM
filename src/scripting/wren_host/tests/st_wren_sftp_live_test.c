/* Real SSH/SFTP integration for the shipped connected scripts.
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */
#include "wren_host_internal.h"
#include "st_connection.h"
#include "st_term.h"
#include "st_threads.h"
#include "st_time.h"
#include "filesystem.h"
#include "string_compat.h"
#include "st_app_paths.h"
#include "st_bbs_list.h"
#include "st_consent_token.h"
#include "st_list_crypto.h"
#include "st_settings.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "Live SFTP %d: %s\n", __LINE__, #e); return 1; } } while (0)
static pid_t child;
static void cleanup_child(void)
{ if (child > 0) { (void)kill(child, SIGTERM); (void)waitpid(child, NULL, 0); child = 0; } }
static int rpc(const struct st_sftp_provider *provider, struct st_sftp_request request,
	struct st_sftp_response *response)
{
	uint64_t id;
	CHECK(provider->submit(provider->context, &request, &id) == 0);
	for (unsigned i = 0; i < 400; ++i) {
		int result = provider->poll(provider->context, response, 25);
		CHECK(result >= 0);
		if (result == 1 && response->request_id == id) return 0;
	}
	CHECK(false); return 1;
}
static int installed_key(st_connection_t *connection, char **line)
{
	const struct st_sftp_provider *p = st_connection_sftp(connection);
	struct st_sftp_response response;
	CHECK(rpc(p, (struct st_sftp_request){.operation = ST_SFTP_OPEN,
	    .path = ".ssh/authorized_keys", .flags = 1}, &response) == 0 && response.kind == ST_SFTP_RESULT_HANDLE);
	uint64_t handle = response.handle;
	CHECK(rpc(p, (struct st_sftp_request){.operation = ST_SFTP_READ, .handle = handle,
	    .size = 4096}, &response) == 0 && response.kind == ST_SFTP_RESULT_BYTES);
	*line = malloc(response.size + 1); CHECK(*line != NULL);
	memcpy(*line, response.data, response.size); (*line)[response.size] = 0;
	CHECK(rpc(p, (struct st_sftp_request){.operation = ST_SFTP_CLOSE, .handle = handle}, &response) == 0);
	return 0;
}
static int reconnect_with_key(const char *fixture, struct st_profile *profile, const char *line)
{
	int descriptors[2], result; CHECK(pipe(descriptors) == 0);
	child = fork(); CHECK(child >= 0);
	if (child == 0) {
		close(descriptors[0]); (void)dup2(descriptors[1], STDOUT_FILENO); close(descriptors[1]);
		execl(fixture, fixture, "publickey-auth", line, (char *)NULL); _exit(127);
	}
	close(descriptors[1]); FILE *stream = fdopen(descriptors[0], "r"); CHECK(stream != NULL);
	unsigned port; CHECK(fscanf(stream, "%u", &port) == 1); fclose(stream);
	profile->port = (uint16_t)port; profile->password[0] = 0;
	st_connection_t *connection = NULL;
	const struct st_screen_geometry geometry = {80,25,640,400};
	struct st_connection_error error = {0};
	CHECK(st_connection_connect_ssh(&connection, profile, &geometry, NULL, &error) == 0);
	char greeting[5]; size_t used = 0;
	while (used < sizeof(greeting)) {
		ssize_t n = st_connection_receive(connection, greeting + used, sizeof(greeting) - used, 5000);
		CHECK(n > 0); used += (size_t)n;
	}
	CHECK(memcmp(greeting, "hello", 5) == 0);
	CHECK(st_connection_send(connection, "q", 1, 1000) == 1);
	used = 0;
	while (used < 2) {
		ssize_t n = st_connection_receive(connection, greeting + used, 2 - used, 5000);
		CHECK(n > 0); used += (size_t)n;
	}
	CHECK(memcmp(greeting, "ok", 2) == 0);
	CHECK(st_connection_close(connection) == 0);
	CHECK(waitpid(child, &result, 0) == child && WIFEXITED(result) && WEXITSTATUS(result) == 0);
	child = 0; return 0;
}
static int drain(st_wren_connected_t *vm)
{
	uint64_t start, now, quiet_since = 0;
	CHECK(st_time_monotonic_ms(&start) == 0);
	do {
		CHECK(st_wren_connected_pump(vm, 5) == 0);
		CHECK(st_wren_connected_last_error(vm) == NULL);
		/* UI children also wake from timers after their last remote reply. */
		CHECK(st_time_monotonic_ms(&now) == 0);
		if (st_wren_connected_auxiliary_active(vm)) quiet_since = 0;
		else if (quiet_since == 0) quiet_since = now;
		else if (now - quiet_since >= 250) {
			/* A reply callback can run past a UI timer's deadline after
			 * pump has dispatched timers. Dispatch those overdue timers
			 * before treating the auxiliary quiet interval as completion. */
			CHECK(st_wren_connected_pump(vm, 0) == 0);
			CHECK(st_wren_connected_last_error(vm) == NULL);
			if (!st_wren_connected_auxiliary_active(vm)) return 0;
			quiet_since = 0;
		}
	} while (now - start < 30000);
	CHECK(false); return 1;
}
static int wait_browser(st_wren_connected_t *vm, st_console_t *console)
{
	uint32_t columns, rows;
	uint64_t start, now;
	CHECK(st_console_size(console, &columns, &rows) == 0);
	size_t count = (size_t)columns * rows;
	struct st_console_cell *cells = calloc(count, sizeof(*cells));
	char *text = malloc(count + 1); CHECK(cells != NULL && text != NULL);
	const struct st_console_rect area = {0,0,columns,rows};
	CHECK(st_time_monotonic_ms(&start) == 0);
	do {
		CHECK(st_wren_connected_pump(vm, 10) == 0);
		CHECK(st_wren_connected_last_error(vm) == NULL);
		CHECK(st_console_read_cells(console, &area, cells, columns) == 0);
		for (size_t i = 0; i < count; ++i) {
			char glyph[8]; size_t size;
			text[i] = st_console_font_glyph_text(console, cells[i].glyph_font,
			    cells[i].glyph, glyph, sizeof(glyph), &size) == 0 && size == 1 ? glyph[0] : ' ';
		}
		text[count] = 0;
		if (strstr(text, "remote.bin") != NULL) { free(cells); free(text); return 0; }
		CHECK(st_time_monotonic_ms(&now) == 0);
	} while (now - start < 15000);
	fprintf(stderr, "Browser screen: %s\n", text); free(cells); free(text);
	CHECK(false); return 1;
}
static int upload(st_wren_host_t *host, st_wren_connected_t *vm, const char *directory)
{
	char local[1024], list_path[1024], source[4096];
	(void)snprintf(local, sizeof(local), "%s/sftp-upload.bin", directory);
	(void)snprintf(list_path, sizeof(list_path), "%s/sftp-upload.lst", directory);
	FILE *file = fopen(local, "wb"); CHECK(file != NULL);
	for (size_t i = 0; i < 90000; ++i) CHECK(fputc((int)(uint8_t)(i * 13), file) != EOF);
	CHECK(fclose(file) == 0);
	file = fopen(list_path, "wb"); CHECK(file != NULL && fclose(file) == 0);
	struct st_wren_host *internal = host;
	struct st_app_paths paths = *internal->config.paths;
	paths.personal_list = list_path;
	struct st_settings settings = st_settings;
	settings.list_path = list_path;
	settings.download_system_list = false;
	st_bbs_list_t *list = NULL;
	CHECK(st_bbs_list_open_replacement(&list, NULL, &paths, &settings) == 0);
	struct st_list_crypto_options options = ST_LIST_CRYPTO_OPTIONS_AES256;
	options.cost_log2 = 8;
	CHECK(st_bbs_list_rewrite_encryption(list, &options, "test", 4) == 0);
	uint8_t key[32]; bool available = false;
	CHECK(st_bbs_list_picker_key(list, key, &available) == 0 && available);
	char *token = NULL;
	CHECK(st_consent_token_sign(&token, key, local) == 0 && token != NULL);
	st_bbs_list_t *saved = internal->bbs_list; internal->bbs_list = list;
	CHECK(snprintf(source, sizeof(source),
	    "import \"sftp_queue\" for SftpQueue\n"
	    "if (SftpQueue.enqueue(SftpQueue.UPLOAD, \"%s\", \"/upload.bin\", 90000, \"%s\") == null) Fiber.abort(\"upload enqueue\")\n",
	    local, token) < (int)sizeof(source));
	free(token);
	CHECK(st_wren_connected_interpret(vm, "live_upload", source) == 0);
	CHECK(drain(vm) == 0);
	CHECK(st_wren_connected_interpret(vm, "upload_verify",
	    "import \"syncterm\" for SFTP, SFTPError, FileFlag, Session\n"
	    "import \"sftp_queue\" for SftpQueue\n"
		    "if (SftpQueue.snapshot[1].status != SftpQueue.DONE) Fiber.abort(\"upload failed: %(SftpQueue.snapshot[1].errMsg)\")\n"
	    "Fiber.new {\n"
	    "  var lease = Session.current.hold()\n"
	    "  var h = SFTP.open(\"/upload.bin\", FileFlag.read).await()\n"
	    "  if (h is SFTPError) Fiber.abort(h.toString)\n"
	    "  var pos = 0\n"
	    "  while (pos < 90000) {\n"
	    "    var bytes = h.readBytes(4096, pos).await()\n"
	    "    if (!(bytes is String) || bytes.bytes.count == 0) Fiber.abort(\"upload truncated\")\n"
	    "    for (b in bytes.bytes) {\n"
	    "      if (b != (pos * 13) % 256) Fiber.abort(\"upload corrupted\")\n"
	    "      pos = pos + 1\n"
	    "    }\n"
	    "  }\n"
	    "  h.close().await()\n"
	    "  lease.release()\n"
	    "}.call()\n") == 0);
	CHECK(drain(vm) == 0);
	internal->bbs_list = saved;
	st_bbs_list_close(list);
	CHECK(fs_remove_file(local) == 0 && fs_remove_file(list_path) == 0);
	return 0;
}
static int run_case(st_wren_host_t *host, st_console_t *console,
	const char *directory, const char *fixture, unsigned test)
{
	const char *modes[] = {"sftp", "sftp", "sftp", "sftp-denied", "sftp-missing-dir",
	    "sftp-write-error", "sftp-close-error", "sftp-read-error", "sftp-shell-close", "sftp-drop"};
	int descriptors[2], result;
	CHECK(pipe(descriptors) == 0);
	child = fork(); CHECK(child >= 0);
	if (child == 0) {
		close(descriptors[0]); (void)dup2(descriptors[1], STDOUT_FILENO); close(descriptors[1]);
		execl(fixture, fixture, modes[test], (char *)NULL); _exit(127);
	}
	close(descriptors[1]);
	FILE *port_stream = fdopen(descriptors[0], "r"); CHECK(port_stream != NULL);
	unsigned port; CHECK(fscanf(port_stream, "%u", &port) == 1); fclose(port_stream);
	struct st_profile profile;
	st_profile_init(&profile);
	(void)snprintf(profile.name, sizeof(profile.name), "SFTP Live %u", test);
	profile.connection_type = ST_CONNECTION_SSH; profile.encoding = ST_PROFILE_ENCODING_UTF8;
	profile.screen_mode = ST_SCREEN_MODE_80X25; profile.port = (uint16_t)port;
	profile.sftp_public_key = test != 0 && test != 9;
	strcpy(profile.address, "127.0.0.1"); strcpy(profile.username, "guest"); strcpy(profile.password, "secret"); strcpy(profile.terminal_name, "ansi");
	free(profile.download_directory); profile.download_directory = strdup(directory);
	st_connection_t *connection = NULL;
	const struct st_screen_geometry geometry = {80,25,640,400};
	struct st_connection_error error = {0};
	CHECK(st_connection_connect_ssh(&connection, &profile, &geometry, NULL, &error) == 0);
	char greeting[9]; size_t used = 0;
	while (used < sizeof(greeting)) {
		ssize_t n = st_connection_receive(connection, greeting + used, sizeof(greeting) - used, 5000);
		CHECK(n > 0); used += (size_t)n;
	}
	CHECK(memcmp(greeting, "helloidle", 9) == 0);
	struct st_wren_host *internal = host;
	internal->config.safe_mode = test == 2;
	st_wren_connected_t *vm = NULL;
	char *key_line = NULL;
	struct st_wren_connected_config config = {.host = host, .console = console,
	    .profile = &profile, .connection = connection, .sftp = st_connection_sftp(connection)};
	CHECK(st_wren_connected_create(&vm, &config) == 0);
	if (test == 0 || test == 2) {
		CHECK(st_wren_connected_interpret(vm, "lazy_probe",
		    "import \"syncterm\" for SFTP, Host\n"
		    "if (SFTP.available || SFTP.lname || SFTP.pubdir != null) Fiber.abort(\"not lazy\")\n"
		    "if (!(Host.sshPublicKey is Map)) Fiber.abort(\"missing public key\")\n") == 0);
		bool consumed = false;
		CHECK(st_wren_connected_handle_event(vm, &(struct st_console_event){.type = ST_CONSOLE_EVENT_PHYSICAL_KEY, .data.physical_key = {.evdev = 16, .pressed = true, .modifiers = ST_CONSOLE_MODIFIER_ALT}}, &consumed) == 0 && consumed);
		CHECK(st_wren_connected_interpret(vm, "empty_queue",
		    "import \"syncterm\" for SFTP\n"
		    "import \"sftp_app\" for SftpApp\n"
		    "if (SFTP.available || SftpApp.current == null || SftpApp.current.mode != \"queue\") Fiber.abort(\"empty queue negotiated SFTP\")\n"
		    "SftpApp.current.quit()\n") == 0);
		CHECK(drain(vm) == 0);
		CHECK(st_wren_connected_interpret(vm, "still_lazy",
		    "import \"syncterm\" for SFTP\n"
		    "if (SFTP.available) Fiber.abort(\"empty queue opened a subsystem\")\n") == 0);
	}
	CHECK(drain(vm) == 0);
	if (test == 9) {
		bool consumed = false;
		CHECK(st_wren_connected_handle_event(vm, &(struct st_console_event){.type = ST_CONSOLE_EVENT_PHYSICAL_KEY, .data.physical_key = {.evdev = 16, .pressed = true, .modifiers = ST_CONSOLE_MODIFIER_ALT}}, &consumed) == 0 && consumed);
		CHECK(st_wren_connected_interpret(vm, "connection_loss",
		    "import \"sftp_queue\" for SftpQueue\n"
		    "SftpQueue.enqueue(SftpQueue.DOWNLOAD, \"sftp-loss.bin\", \"/remote.bin\", 90000)\n") == 0);
		CHECK(drain(vm) == 0);
		CHECK(st_wren_connected_interpret(vm, "connection_loss_result",
		    "import \"syncterm\" for CTerm, Session\n"
		    "import \"sftp_app\" for SftpApp\n"
		    "import \"sftp_queue\" for SftpQueue\n"
		    "if (!SftpQueue.suspended || Session.current.holdCount > 0 || CTerm.suspended || SftpApp.current != null) Fiber.abort(\"network loss state: queue suspended=%(SftpQueue.suspended), active=%(Session.current.holdCount), terminal suspended=%(CTerm.suspended), app=%(SftpApp.current)\")\n") == 0);
	} else if (test >= 3 && test <= 7) {
		CHECK(st_wren_connected_interpret(vm, "failure_cleanup",
		    "import \"syncterm\" for CTerm, SFTP, Session\n"
		    "if (Session.current.holdCount > 0 || !SFTP.available) Fiber.abort(\"key failure leaked activity\")\n") == 0);
	} else {
		CHECK(st_wren_connected_interpret(vm, "live_contract",
		    "import \"syncterm\" for Session, CTerm, SFTP, SFTPError, SFTPStat, FileFlag, Host\n"
		    "class Live {\n"
		    "  static done { __done }\n"
		    "  static run() {\n"
		    "    var lease = Session.current.hold()\n"
		    "    var ready = SFTP.start().await()\n"
		    "    if (ready != true || !SFTP.lname || SFTP.pubdir != \"/public\") Fiber.abort(\"startup\")\n"
		    "    var d = SFTP.opendir(\"/\").await()\n"
		    "    var entries = d.readEntries().await()\n"
		    "    var e = entries[0]\n"
		    "    if (e.longname != \"Friendly fixture\" || e.toString != e.longname || e.hash.bytes.count != 20 || !e.hasLongDesc) Fiber.abort(\"entry metadata\")\n"
		    "    var metadata = e.stat\n"
		    "    if (!(metadata is SFTPStat) || metadata.size != 90000 || metadata.mtime != 1700000123 || metadata.atime != 1700000123 || metadata.mode != 33188 || metadata.uid != 0 || metadata.gid != 0) Fiber.abort(\"entry stat\")\n"
		    "    d.close().await()\n"
		    "    var missing = SFTP.stat(\"/absent\").await()\n"
		    "    if (!(missing is SFTPError) || missing.code != 0 || missing.serverStatus != 2 || !missing.toString.startsWith(\"SFTPError: No Such File\")) Fiber.abort(\"error representation\")\n"
		    "    var h = SFTP.open(\".ssh/authorized_keys\", FileFlag.read).await()\n"
		    "    if (h is SFTPError) Fiber.abort(h.toString)\n"
		    "    var content = h.readBytes(4096, 0).await()\n"
		    "    if (Host.safeMode || !Session.current.profile.sftpPublicKey) {\n"
		    "      if (content != null) Fiber.abort(\"unexpected key write\")\n"
		    "    } else {\n"
		    "      var key = Host.sshPublicKey\n"
		    "      var expected = \"%(key[\"algo\"]) %(key[\"blob\"]) Added by SyncTERM\\n\"\n"
		    "      if (content != expected) Fiber.abort(\"authorized_keys content\")\n"
		    "    }\n"
		    "    h.close().await()\n"
		    "    __done = true\n"
		    "    lease.release()\n"
		    "  }\n"
		    "}\n"
		    "Fiber.new { Live.run() }.call()\n") == 0);
		CHECK(drain(vm) == 0);
		CHECK(st_wren_connected_interpret(vm, "live_result",
		    "import \"live_contract\" for Live\nif (!Live.done) Fiber.abort(\"live contract unfinished\")\n") == 0);
		if (test == 1) {
			CHECK(st_wren_connected_interpret(vm, "duplicate_key",
			    "import \"sftp_pubkey\" for SftpPubkey\nSftpPubkey.run()\n") == 0);
			CHECK(drain(vm) == 0);
			CHECK(st_wren_connected_interpret(vm, "duplicate_check",
			    "import \"live_contract\" for Live\nFiber.new { Live.run() }.call()\n") == 0);
			CHECK(drain(vm) == 0);
		}
		char local[1024]; (void)snprintf(local, sizeof(local), "%s/sftp-live.bin", directory);
		(void)fs_remove_file(local);
		CHECK(st_wren_connected_interpret(vm, "live_download",
		    "import \"sftp_queue\" for SftpQueue\n"
		    "if (SftpQueue.enqueue(SftpQueue.DOWNLOAD, \"sftp-live.bin\", \"/remote.bin\", 90000) == null) Fiber.abort(\"download enqueue\")\n") == 0);
		CHECK(drain(vm) == 0);
		CHECK(st_wren_connected_interpret(vm, "download_result",
		    "import \"sftp_queue\" for SftpQueue\n"
		    "if (SftpQueue.snapshot[0].status != SftpQueue.DONE) Fiber.abort(\"download failed\")\n") == 0);
		FILE *file = fopen(local, "rb"); CHECK(file != NULL);
		for (size_t i = 0; i < 90000; ++i) CHECK(fgetc(file) == (int)(uint8_t)(i * 13));
		CHECK(fgetc(file) == EOF && fclose(file) == 0);
		CHECK(fs_remove_file(local) == 0);
		if (test == 0) {
			CHECK(upload(host, vm, directory) == 0);
			bool consumed = false;
			CHECK(st_wren_connected_handle_event(vm, &(struct st_console_event){.type = ST_CONSOLE_EVENT_PHYSICAL_KEY, .data.physical_key = {.evdev = 31, .pressed = true, .modifiers = ST_CONSOLE_MODIFIER_ALT}}, &consumed) == 0 && consumed);
			CHECK(wait_browser(vm, console) == 0);
			CHECK(st_wren_connected_interpret(vm, "browser_result",
			    "import \"sftp_app\" for SftpApp\n"
			    "import \"syncterm\" for Screen\n"
			    "if (SftpApp.current == null || SftpApp.current.mode != \"browser\") Fiber.abort(\"browser did not open\")\n"
			    "var size = Screen.size\n"
			    "var cells = Screen.readRect(1, 1, size[0], size[1])\n"
			    "if (!cells.map {|c| c.ch }.join().contains(\"remote.bin\")) Fiber.abort(\"browser rows not rendered\")\n"
			    "SftpApp.current.quit()\n") == 0);
			CHECK(drain(vm) == 0);
		}
		if (test == 8) CHECK(!st_connection_is_connected(connection) && st_connection_auxiliary_alive(connection));
	}
	if (test == 1) CHECK(installed_key(connection, &key_line) == 0);
	st_wren_connected_destroy(vm);
	CHECK(st_connection_close(connection) == 0);
	CHECK(waitpid(child, &result, 0) == child && WIFEXITED(result) && WEXITSTATUS(result) == 0);
	child = 0; internal->config.safe_mode = false;
	if (test == 1) CHECK(reconnect_with_key(fixture, &profile, key_line) == 0);
	free(key_line);
	st_profile_dispose(&profile); return 0;
}
int st_wren_test_live_sftp(st_wren_host_t *host, st_console_t *console,
	const char *directory, const char *fixture)
{
	CHECK(atexit(cleanup_child) == 0);
	for (unsigned test = 0; test < 10; ++test) {
		fprintf(stderr, "Live SFTP case %u\n", test);
		CHECK(run_case(host, console, directory, fixture, test) == 0);
	}
	fprintf(stderr, "Live SFTP sessions closed\n");
	return 0;
}
