/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */
#include "st_connection.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
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
run(const char *fixture, unsigned test)
{
	int pipefd[2], child_status;
	pid_t child;
	char port[16];
	size_t used = 0;
	struct st_profile profile;
	struct ui_state ui = {.selection = test == 3 ? 2 : test == 4 ? 1 : 0, .profile = &profile, .test = test};
	struct st_connection_hooks hooks = {
		.choice = choice, .remember_hostkey = save_key, .ui_context = &ui, .alert = alert,
		.prompt = prompt,
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
		    : test == 14 ? "retry" : "password", (char *)NULL);
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
	profile.port = (uint16_t)strtoul(port, NULL, 10);
	if (test >= 3 && test <= 5) {
		memset(profile.ssh_fingerprint, 0xa5, 32);
		profile.ssh_fingerprint_size = 32;
	}
	int status = st_connection_connect_ssh(&connection, &profile, &screen, &hooks, &error);
	if ((test >= 5 && test <= 7) || test == 13 || test == 15) CHECK(status < 0 && connection == NULL);
	else {
		if (status != 0) fprintf(stderr, "connect %u: %d %s\n", test, status, error.detail);
		CHECK(status == 0 && connection != NULL);
		CHECK(read_text(connection, "hello") == 0);
		CHECK(st_connection_update_terminal_size(connection, &resized) == 0);
		CHECK(st_connection_send(connection, keys, sizeof(keys), 5000) == (ssize_t)sizeof(keys));
		CHECK(read_text(connection, "bye") == 0);
		CHECK(st_connection_close(connection) == 0);
	}
	CHECK(waitpid(child, &child_status, 0) == child
	    && WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0);
	fixture_child = 0;
	CHECK(!ui.invalid);
	CHECK(ui.choices == ((test >= 3 && test <= 5) || test == 12 || test == 13 ? 1 : 0));
	CHECK(ui.saves == (test == 3 || test == 5 || test == 13 || test == 15 ? 0 : 1));
	CHECK(ui.prompts == (test == 9 || test == 10 ? 1 : test == 14 ? 2 : test == 6 ? 1 : 0));
	CHECK(ui.alerts == (test == 2 || test == 9 || test == 11 ? 1 : test == 10 ? 2 : 0));
	CHECK(ui.errors == (test == 6 || test == 7 ? 1 : 0));
	st_profile_dispose(&profile);
	return 0;
}
int
main(int argc, char **argv)
{
	(void)signal(SIGPIPE, SIG_IGN);
	CHECK(atexit(cleanup_fixture) == 0);
	CHECK(argc == 2);
	for (unsigned test = 0; test < 16; ++test) {
		fprintf(stderr, "SSH fixture %u\n", test);
		CHECK(run(argv[1], test) == 0);
	}
	return 0;
}
