/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */
#include "st_connection.h"
#include "st_sftp.h"
#include "st_threads.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "string_compat.h"

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "SSH client %d: %s\n", __LINE__, #e); return 1; } } while (0)
struct ui_state {
	int selection, choices, saves, alerts, prompts, errors;
	unsigned test;
	bool invalid;
	struct st_profile *profile;
};

struct log_capture {
	atomic_uint errors, warnings, debug, handshakes, authentications, demux_starts;
	atomic_bool invalid;
};

static void
capture_log(void *context, enum st_connection_log_level level,
	const char *source, const char *message)
{
	struct log_capture *log = context;
	if (source == NULL || strcmp(source, "SSH") != 0) return;
	if (message == NULL || strstr(message, "secret") != NULL
	    || strstr(message, "654321") != NULL) {
		atomic_store(&log->invalid, true);
		return;
	}
	if (level == ST_CONNECTION_LOG_ERROR) atomic_fetch_add(&log->errors, 1);
	else if (level == ST_CONNECTION_LOG_WARNING) atomic_fetch_add(&log->warnings, 1);
	else if (level == ST_CONNECTION_LOG_DEBUG) atomic_fetch_add(&log->debug, 1);
	else atomic_store(&log->invalid, true);
	if (strstr(message, "Handshake complete:") != NULL)
		atomic_fetch_add(&log->handshakes, 1);
	if (strstr(message, "Authentication operation completed successfully") != NULL)
		atomic_fetch_add(&log->authentications, 1);
	if (strstr(message, "Connection demultiplexer started") != NULL)
		atomic_fetch_add(&log->demux_starts, 1);
}
static void
alert(void *context, const char *title, const char *message)
{
	struct ui_state *ui = context;
	++ui->alerts;
	if ((strcmp(title, "SSH Keyboard-Interactive") != 0 || strcmp(message, "Authentication") != 0)
	    && (strcmp(title, "SSH Banner") != 0 || strcmp(message, "Fixture banner") != 0))
		ui->invalid = true;
}
static int
prompt(void *context, const char *title, const char *message, const char *initial,
	size_t limit, bool masked, char **result)
{
	struct ui_state *ui = context;
	(void)initial; (void)limit;
	++ui->prompts;
	if (!masked) ui->invalid = true;
	if (ui->test == 9 || ui->test == 10) {
		if (strcmp(title, "SSH Keyboard-Interactive") != 0
		    || strcmp(message, ui->test == 9 ? "OTP: " : "Password: ") != 0) ui->invalid = true;
		*result = strdup("654321");
	} else if (ui->test == 14) {
		if (strcmp(title, "SSH Password") != 0 || strcmp(message, ui->prompts == 1
		    ? "Enter password." : "Incorrect password. Try again.") != 0) ui->invalid = true;
		*result = strdup(ui->prompts == 1 ? "wrong" : "secret");
	} else return IO_ERROR_PASSWORD_REQUIRED;
	return *result == NULL ? IO_ERROR_OUT_OF_MEMORY : 0;
}
static pid_t fixture_child;
static void
cleanup_fixture(void)
{
	if (fixture_child > 0) {
		(void)kill(fixture_child, SIGTERM);
		(void)waitpid(fixture_child, NULL, 0);
	}
}
static int
choice(void *context, const char *title, const char *message,
	const char *const *choices, size_t count)
{
	struct ui_state *ui = context;
	(void)message;
	if (strcmp(title, "SSH error") == 0) { ++ui->errors; return 0; }
	++ui->choices;
	if (ui->test == 12 || ui->test == 13) {
		CHECK(strstr(title, "Weak host key") != NULL && count == 2
		    && strcmp(choices[0], "Disconnect") == 0 && strcmp(choices[1], "Accept") == 0);
		return ui->test == 12 ? 1 : 0;
	}
	CHECK(strcmp(title, "Fingerprint Changed") == 0 && count == 3
	    && strcmp(choices[0], "Disconnect") == 0 && strcmp(choices[1], "Update") == 0
	    && strcmp(choices[2], "Ignore") == 0);
	return ui->selection;
}
static int
save_key(void *context, const uint8_t key[32], bool explicit_update)
{
	struct ui_state *ui = context;
	CHECK(explicit_update == (ui->test == 4));
	++ui->saves;
	memcpy(ui->profile->ssh_fingerprint, key, 32);
	ui->profile->ssh_fingerprint_size = 32;
	return 0;
}
static int
read_text(st_connection_t *connection, const char *expected)
{
	char buffer[16];
	size_t size = strlen(expected), used = 0;
	while (used < size) {
		CHECK(st_connection_process_requests(connection) == 0);
		ssize_t n = st_connection_receive(connection, buffer + used, size - used, 5000);
		CHECK(n > 0); used += (size_t)n;
	}
	CHECK(memcmp(buffer, expected, size) == 0);
	return 0;
}
static int
await_sftp(st_connection_t *connection, const struct st_sftp_provider *p,
	struct st_sftp_response *response)
{
	for (unsigned i = 0; i < 200; ++i) {
		CHECK(st_connection_process_requests(connection) == 0);
		int n = p->poll(p->context, response, 25);
		CHECK(n >= 0);
		if (n == 1) return 0;
	}
	CHECK(false); return 1;
}
static int
sftp_rpc(st_connection_t *connection, const struct st_sftp_provider *p,
	struct st_sftp_request request, struct st_sftp_response *response)
{
	uint64_t id;
	CHECK(p->submit(p->context, &request, &id) == 0);
	CHECK(await_sftp(connection, p, response) == 0);
	CHECK(response->request_id == id);
	return 0;
}
static int
exercise_sftp(st_connection_t *connection, unsigned test)
{
	const struct st_sftp_provider *p = st_connection_sftp(connection);
	struct st_sftp_response r;
	uint64_t first, second, handle;
	CHECK(p != NULL && !p->available(p->context));
	CHECK(p->public_directory(p->context) == NULL && !p->long_name_extension(p->context));
	/* The server rejects any subsystem request during this idle window. */
	CHECK(read_text(connection, "idle") == 0);
	char *key = st_connection_ssh_public_key();
	CHECK(key != NULL && strncmp(key, "ssh-ed25519 ", 12) == 0); free(key);
	struct st_sftp_request q = {.operation = ST_SFTP_START};
	CHECK(p->submit(p->context, &q, &first) == 0);
	if (test == 20) {
		CHECK(read_text(connection, "init") == 0);
		p->cancel_all(p->context);
		CHECK(!p->available(p->context));
		return 0;
	}
	if (test != 19) CHECK(p->submit(p->context, &q, &second) == 0);
	CHECK(await_sftp(connection, p, &r) == 0 && r.request_id == first);
	if (test == 19) {
		CHECK(r.kind == ST_SFTP_RESULT_ERROR && !p->available(p->context));
		CHECK(p->submit(p->context, &q, &second) == IO_ERROR_NOT_CONNECTED);
		CHECK(read_text(connection, "usable") == 0);
		CHECK(st_connection_send(connection, "q", 1, 1000) == 1);
		return 0;
	}
	CHECK(r.kind == ST_SFTP_RESULT_STARTED);
	CHECK(await_sftp(connection, p, &r) == 0 && r.request_id == second && r.kind == ST_SFTP_RESULT_STARTED);
	CHECK(p->available(p->context) && p->long_name_extension(p->context));
	CHECK(strcmp(p->public_directory(p->context), "/public") == 0);
	if (test == 21) {
		CHECK(sftp_rpc(connection, p, (struct st_sftp_request){.operation = ST_SFTP_OPEN, .path = "/remote.bin", .flags = 1}, &r) == 0 && r.kind == ST_SFTP_RESULT_HANDLE);
		handle = r.handle;
		CHECK(sftp_rpc(connection, p, (struct st_sftp_request){.operation = ST_SFTP_READ, .handle = handle, .size = 32}, &r) == 0);
		CHECK(r.kind == ST_SFTP_RESULT_ERROR && r.transient);
		return 0;
	}
	CHECK(sftp_rpc(connection, p, (struct st_sftp_request){.operation = ST_SFTP_REALPATH, .path = "."}, &r) == 0);
	CHECK(r.kind == ST_SFTP_RESULT_STRING && r.size == 1 && memcmp(r.data, "/", 1) == 0);
	CHECK(sftp_rpc(connection, p, (struct st_sftp_request){.operation = ST_SFTP_OPENDIR, .path = "/"}, &r) == 0);
	CHECK(r.kind == ST_SFTP_RESULT_HANDLE); handle = r.handle;
	CHECK(sftp_rpc(connection, p, (struct st_sftp_request){.operation = ST_SFTP_READDIR, .handle = handle}, &r) == 0);
	CHECK(r.kind == ST_SFTP_RESULT_ENTRIES && r.entry_count == 1);
	CHECK(strcmp(r.entries[0].name, "remote.bin") == 0 && strcmp(r.entries[0].long_name, "Friendly fixture") == 0);
	CHECK(r.entries[0].hash_size == 20 && r.entries[0].has_long_description && r.entries[0].stat.size == 90000);
	CHECK(sftp_rpc(connection, p, (struct st_sftp_request){.operation = ST_SFTP_READDIR, .handle = handle}, &r) == 0 && r.kind == ST_SFTP_RESULT_EOF);
	CHECK(sftp_rpc(connection, p, (struct st_sftp_request){.operation = ST_SFTP_CLOSE, .handle = handle}, &r) == 0 && r.kind == ST_SFTP_RESULT_OK);
	CHECK(sftp_rpc(connection, p, (struct st_sftp_request){.operation = ST_SFTP_READDIR, .handle = handle}, &r) == 0 && r.kind == ST_SFTP_RESULT_ERROR);
	CHECK(sftp_rpc(connection, p, (struct st_sftp_request){.operation = ST_SFTP_DESCRIPTIONS, .path = "/remote.bin"}, &r) == 0);
	CHECK(r.kind == ST_SFTP_RESULT_STRING && r.size == 16 && memcmp(r.data, "Long description", 16) == 0);
	CHECK(sftp_rpc(connection, p, (struct st_sftp_request){.operation = ST_SFTP_OPEN, .path = "/new.bin", .flags = 1|2|8|16}, &r) == 0 && r.kind == ST_SFTP_RESULT_HANDLE);
	handle = r.handle;
	const uint8_t bytes[] = {0,255,127,'x'};
	CHECK(sftp_rpc(connection, p, (struct st_sftp_request){.operation = ST_SFTP_WRITE, .handle = handle, .data = bytes, .size = sizeof(bytes)}, &r) == 0 && r.kind == ST_SFTP_RESULT_OK);
	CHECK(sftp_rpc(connection, p, (struct st_sftp_request){.operation = ST_SFTP_READ, .handle = handle, .size = 32}, &r) == 0);
	CHECK(r.kind == ST_SFTP_RESULT_BYTES && r.size == sizeof(bytes) && memcmp(r.data, bytes, sizeof(bytes)) == 0);
	CHECK(sftp_rpc(connection, p, (struct st_sftp_request){.operation = ST_SFTP_CLOSE, .handle = handle}, &r) == 0);
	CHECK(sftp_rpc(connection, p, (struct st_sftp_request){.operation = ST_SFTP_SET_MTIME, .path = "/new.bin", .modified_time = 123456}, &r) == 0 && r.kind == ST_SFTP_RESULT_OK);
	CHECK(sftp_rpc(connection, p, (struct st_sftp_request){.operation = ST_SFTP_STAT, .path = "/new.bin"}, &r) == 0 && r.stat.modified_time == 123456 && r.stat.size == 4);
	CHECK(sftp_rpc(connection, p, (struct st_sftp_request){.operation = ST_SFTP_RENAME, .path = "/new.bin", .second_path = "/renamed.bin"}, &r) == 0 && r.kind == ST_SFTP_RESULT_OK);
	CHECK(sftp_rpc(connection, p, (struct st_sftp_request){.operation = ST_SFTP_REMOVE, .path = "/renamed.bin"}, &r) == 0 && r.kind == ST_SFTP_RESULT_OK);
	CHECK(sftp_rpc(connection, p, (struct st_sftp_request){.operation = ST_SFTP_STAT, .path = "/renamed.bin"}, &r) == 0 && r.kind == ST_SFTP_RESULT_ERROR && r.error_code == 0 && r.server_status == 2);
	CHECK(sftp_rpc(connection, p, (struct st_sftp_request){.operation = ST_SFTP_MKDIR, .path = "/dir"}, &r) == 0 && r.kind == ST_SFTP_RESULT_OK);
	CHECK(sftp_rpc(connection, p, (struct st_sftp_request){.operation = ST_SFTP_RMDIR, .path = "/dir"}, &r) == 0 && r.kind == ST_SFTP_RESULT_OK);
	if (test == 18) {
		for (unsigned i = 0; i < 100 && st_connection_is_connected(connection); ++i) {
			const struct timespec delay = {.tv_nsec = 10000000}; (void)st_thread_sleep(&delay, NULL);
		}
		CHECK(!st_connection_is_connected(connection) && st_connection_auxiliary_alive(connection));
	}
	return 0;
}
static int
run(const char *fixture, unsigned test, int log_level)
{
	int pipefd[2], child_status;
	pid_t child;
	char port[16];
	size_t used = 0;
	struct st_profile profile;
	struct log_capture log;
	atomic_init(&log.errors, 0);
	atomic_init(&log.warnings, 0);
	atomic_init(&log.debug, 0);
	atomic_init(&log.handshakes, 0);
	atomic_init(&log.authentications, 0);
	atomic_init(&log.demux_starts, 0);
	atomic_init(&log.invalid, false);
	struct ui_state ui = {.selection = test == 3 ? 2 : test == 4 ? 1 : 0, .profile = &profile, .test = test};
	struct st_connection_hooks hooks = {
		.choice = choice, .remember_hostkey = save_key, .ui_context = &ui, .alert = alert,
		.prompt = prompt,
		.log = capture_log, .log_context = &log,
	};
	const struct st_screen_geometry screen = {80,25,640,400}, resized = {120,40,960,640};
	struct st_connection_error error = {0};
	st_connection_t *connection = NULL;
	static const uint8_t keys[] = {'x',3,255,'\r','\n'};
	CHECK(pipe(pipefd) == 0);
	child = fork(); CHECK(child >= 0);
	if (child == 0) {
		close(pipefd[0]); (void)dup2(pipefd[1], STDOUT_FILENO); close(pipefd[1]);
		execl(fixture, fixture, test == 1 ? "none" : test == 2 ? "keyboard"
		    : test == 8 ? "rekey" : test == 9 ? "keyboard-otp" : test == 10 ? "keyboard-repeat"
		    : test == 11 ? "banner" : test == 12 || test == 13 || test == 15 ? "weak"
		    : test == 14 ? "retry" : test == 16 ? "bad-handshake"
		    : test == 17 ? "sftp" : test == 18 ? "sftp-shell-close"
		    : test == 19 ? "sftp-unsupported" : test == 20 ? "sftp-no-version"
		    : test == 21 ? "sftp-drop" : "password", (char *)NULL);
		_exit(127);
	}
	fixture_child = child;
	close(pipefd[1]);
	for (;;) {
		struct pollfd p = {.fd = pipefd[0], .events = POLLIN};
		CHECK(used + 1 < sizeof(port) && poll(&p, 1, 15000) > 0);
		CHECK(read(pipefd[0], port + used, 1) == 1);
		if (port[used++] == '\n') break;
	}
	close(pipefd[0]); port[used] = '\0';
	st_profile_init(&profile);
	profile.connection_type = test == 1 || test == 7 ? ST_CONNECTION_SSH_NO_AUTH : ST_CONNECTION_SSH;
	profile.encoding = ST_PROFILE_ENCODING_UTF8;
	strcpy(profile.address, "127.0.0.1"); strcpy(profile.username, "guest");
	strcpy(profile.terminal_name, "ansi");
	strcpy(profile.password, test == 6 || test == 14 ? "wrong" : "secret");
	profile.hide_popups = test == 15;
	profile.protocol_log_level = log_level;
	profile.port = (uint16_t)strtoul(port, NULL, 10);
	if (test >= 3 && test <= 5) {
		memset(profile.ssh_fingerprint, 0xa5, 32);
		profile.ssh_fingerprint_size = 32;
	}
	int status = st_connection_connect_ssh(&connection, &profile, &screen, &hooks, &error);
	if ((test >= 5 && test <= 7) || test == 13 || test == 15 || test == 16)
		CHECK(status < 0 && connection == NULL);
	else {
		if (status != 0) fprintf(stderr, "connect %u: %d %s\n", test, status, error.detail);
		CHECK(status == 0 && connection != NULL);
		if (log_level == ST_PROFILE_LOG_DEBUG) {
			CHECK(atomic_load(&log.handshakes) > 0);
			CHECK(atomic_load(&log.authentications) > 0);
			CHECK(atomic_load(&log.demux_starts) > 0);
		}
		CHECK(read_text(connection, "hello") == 0);
		if (test >= 17) {
			CHECK(exercise_sftp(connection, test) == 0);
		} else {
		CHECK(st_connection_update_terminal_size(connection, &resized) == 0);
		CHECK(st_connection_send(connection, keys, sizeof(keys), 5000) == (ssize_t)sizeof(keys));
		CHECK(read_text(connection, "bye") == 0);
		}
		CHECK(st_connection_close(connection) == 0);
	}
	CHECK(waitpid(child, &child_status, 0) == child
	    && WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0);
	fixture_child = 0;
	CHECK(!ui.invalid);
	CHECK(ui.choices == ((test >= 3 && test <= 5) || test == 12 || test == 13 ? 1 : 0));
	CHECK(ui.saves == (test == 3 || test == 5 || test == 13 || test == 15 || test == 16 ? 0 : 1));
	CHECK(ui.prompts == (test == 9 || test == 10 ? 1 : test == 14 ? 2 : test == 6 ? 1 : 0));
	CHECK(ui.alerts == (test == 2 || test == 9 || test == 11 ? 1 : test == 10 ? 2 : 0));
	CHECK(ui.errors == (test == 6 || test == 7 || test == 16 ? 1 : 0));
	CHECK(!atomic_load(&log.invalid));
	if (log_level < ST_PROFILE_LOG_DEBUG) CHECK(atomic_load(&log.debug) == 0);
	if (log_level <= ST_PROFILE_LOG_ERROR) CHECK(atomic_load(&log.warnings) == 0);
	else if (test == 14) CHECK(atomic_load(&log.warnings) > 0);
	if (test == 6 && log_level >= ST_PROFILE_LOG_WARNING) CHECK(atomic_load(&log.warnings) > 0);
	if (test == 16) CHECK(atomic_load(&log.errors) > 0);
	st_profile_dispose(&profile);
	return 0;
}
int
main(int argc, char **argv)
{
	(void)signal(SIGPIPE, SIG_IGN);
	CHECK(atexit(cleanup_fixture) == 0);
	CHECK(argc == 2);
	for (unsigned test = 0; test < 22; ++test) {
		fprintf(stderr, "SSH fixture %u\n", test);
		CHECK(run(argv[1], test, ST_PROFILE_LOG_DEBUG) == 0);
	}
	const int quieter_levels[] = {ST_PROFILE_LOG_ERROR, ST_PROFILE_LOG_WARNING,
		ST_PROFILE_LOG_NOTICE, ST_PROFILE_LOG_INFO};
	for (size_t i = 0; i < sizeof(quieter_levels) / sizeof(quieter_levels[0]); ++i) {
		fprintf(stderr, "SSH logging threshold %d\n", quieter_levels[i]);
		CHECK(run(argv[1], 14, quieter_levels[i]) == 0);
		CHECK(run(argv[1], 6, quieter_levels[i]) == 0);
		CHECK(run(argv[1], 16, quieter_levels[i]) == 0);
	}
	return 0;
}
