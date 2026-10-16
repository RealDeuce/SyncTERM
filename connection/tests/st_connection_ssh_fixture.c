/* A separate-process SSH server keeps DeuceSSH's global callbacks isolated.
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */
#include <deucessh.h>
#include <deucessh-algorithms.h>
#include <deucessh-auth.h>
#include <deucessh-conn.h>
#include "st_threads.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "SSH fixture %d: %s\n", __LINE__, #e); return 1; } } while (0)
static int descriptor = -1;
static const char *mode;
static unsigned auth_round, password_attempts;
static int
io(uint8_t *data, size_t size, dssh_session session, bool sending)
{
	for (size_t used = 0; used < size;) {
		struct pollfd p = {.fd = descriptor, .events = sending ? POLLOUT : POLLIN};
		ssize_t n;
		if (dssh_session_is_terminated(session)) return DSSH_ERROR_TERMINATED;
		int status = poll(&p, 1, 100);
		if (status < 0) return DSSH_ERROR_TERMINATED;
		if (status == 0) continue;
		n = sending ? send(descriptor, data + used, size - used, 0)
		    : recv(descriptor, data + used, size - used, 0);
		if (n <= 0) return DSSH_ERROR_TERMINATED;
		used += (size_t)n;
	}
	return 0;
}
static int
tx(uint8_t *data, size_t size, dssh_session session, void *context)
{
	(void)context; return io(data, size, session, true);
}
static int
rx(uint8_t *data, size_t size, dssh_session session, void *context)
{
	(void)context; return io(data, size, session, false);
}
static void
terminate(dssh_session session, void *context)
{
	(void)session; (void)context; (void)shutdown(descriptor, SHUT_RDWR);
}
static int
auth_none(const uint8_t *user, size_t size, void *context)
{
	(void)user; (void)size; (void)context;
	return strcmp(mode, "none") == 0 ? DSSH_AUTH_SUCCESS : DSSH_AUTH_FAILURE;
}
static int
auth_password(const uint8_t *user, size_t user_size, const uint8_t *password,
	size_t password_size, uint8_t **change, size_t *change_size, void *context)
{
	(void)change; (void)change_size; (void)context;
	if (strcmp(mode, "retry") == 0 && ++password_attempts <= 2) return DSSH_AUTH_FAILURE;
	return user_size == 5 && memcmp(user, "guest", 5) == 0
	    && password_size == 6 && memcmp(password, "secret", 6) == 0
	    ? DSSH_AUTH_SUCCESS : DSSH_AUTH_FAILURE;
}
static char *
copy_text(const char *text)
{
	size_t size = strlen(text) + 1;
	char *copy = malloc(size);
	if (copy != NULL) memcpy(copy, text, size);
	return copy;
}
static int
auth_keyboard(const uint8_t *user, size_t user_size, uint32_t count,
	const uint8_t **responses, const size_t *sizes, char **name,
	char **instruction, uint32_t *prompt_count, char ***prompts, bool **echo,
	void *context)
{
	(void)user; (void)user_size; (void)context;
	if (count != 0) {
		const char *expected = strcmp(mode, "keyboard-otp") == 0 || auth_round != 0 ? "654321" : "secret";
		if (count != 1 || sizes[0] != 6 || memcmp(responses[0], expected, 6) != 0)
			return DSSH_AUTH_FAILURE;
		if (strcmp(mode, "keyboard-repeat") != 0 || auth_round++ != 0) return DSSH_AUTH_SUCCESS;
	}
	*name = copy_text("Fixture");
	*instruction = copy_text("Authentication");
	*prompts = calloc(1, sizeof(**prompts));
	*echo = calloc(1, sizeof(**echo));
	if (*prompts != NULL) (*prompts)[0] = copy_text(strcmp(mode, "keyboard-otp") == 0 ? "OTP: " : "Password: ");
	*prompt_count = 1;
	return *name != NULL && *instruction != NULL && *prompts != NULL
	    && (*prompts)[0] != NULL && *echo != NULL ? DSSH_AUTH_KBI_PROMPT : DSSH_AUTH_FAILURE;
}
static int
pty(dssh_channel ch, const struct dssh_chan_params *p, void *context)
{
	bool utf8 = false, flow = false;
	(void)ch; (void)context;
	if (strcmp(mode, "log-ui") == 0)
		return p->cols == 80 && p->rows == 24 ? 0 : -1;
	if (p->cols != 80 || p->rows != 24 || p->term == NULL || strcmp(p->term, "ansi") != 0)
		return -1;
	for (size_t i = 0; i < p->mode_count; ++i) {
		if (p->modes[i].opcode == 42 && p->modes[i].value == 1) utf8 = true;
		if (p->modes[i].opcode == 38 && p->modes[i].value == 0) flow = true;
	}
	return utf8 && flow ? 0 : -1;
}
static int
environment(dssh_channel ch, const struct dssh_chan_params *p, void *context)
{
	(void)ch; (void)p; (void)context; return 0;
}
static int
shell(dssh_channel ch, const struct dssh_chan_params *p,
	struct dssh_chan_accept_result *result, void *context)
{
	(void)ch; (void)p; (void)result; (void)context; return 0;
}

static int
send_text(dssh_channel channel, const char *text)
{
	size_t size = strlen(text), used = 0;
	while (used < size) {
		CHECK((dssh_chan_poll(channel, DSSH_POLL_WRITE, 10000) & DSSH_POLL_WRITE) != 0);
		int64_t n = dssh_chan_write(channel, 0, (const uint8_t *)text + used, size - used);
		CHECK(n > 0);
		used += (size_t)n;
	}
	return 0;
}
int
main(int argc, char **argv)
{
	int listener, status;
	struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
	socklen_t length = sizeof(address);
	dssh_session session;
	dssh_channel channel;
	struct dssh_auth_server_cbs auth = {
		.none_cb = auth_none, .password_cb = auth_password,
		.keyboard_interactive_cb = auth_keyboard,
	};
	const struct dssh_chan_accept_cbs accept_callbacks = {
		.pty_req = pty, .env = environment, .shell = shell,
	};
	bool resized = false;
	uint8_t received[5];
	size_t used = 0;
	static const uint8_t expected[] = {'x',3,255,'\r','\n'};
	CHECK(argc == 2); mode = argv[1];
	(void)signal(SIGPIPE, SIG_IGN);
	CHECK(dssh_register_curve25519_sha256() == 0);
	bool weak = strncmp(mode, "weak", 4) == 0;
	CHECK((weak ? dssh_register_rsa_sha2_256() : dssh_register_ssh_ed25519()) == 0);
	CHECK(dssh_register_aes256_ctr() == 0);
	CHECK(dssh_register_hmac_sha2_256() == 0);
	CHECK(dssh_register_none_comp() == 0);
	CHECK((weak ? dssh_rsa_sha2_256_generate_key(1024) : dssh_ed25519_generate_key()) == 0);
	CHECK(dssh_transport_set_callbacks(tx, rx, NULL, NULL) == 0);
	listener = socket(AF_INET, SOCK_STREAM, 0);
	CHECK(listener >= 0 && bind(listener, (struct sockaddr *)&address, sizeof(address)) == 0
	    && listen(listener, 1) == 0
	    && getsockname(listener, (struct sockaddr *)&address, &length) == 0);
	printf("%u\n", (unsigned)ntohs(address.sin_port)); fflush(stdout);
	descriptor = accept(listener, NULL, NULL); CHECK(descriptor >= 0);
	close(listener);
	if (strcmp(mode, "bad-handshake") == 0) {
		static const char invalid[] = "SSH-1.5-fixture\r\n";
		CHECK(send(descriptor, invalid, sizeof(invalid) - 1, 0) == (ssize_t)sizeof(invalid) - 1);
		close(descriptor);
		return 0;
	}
	session = dssh_session_init(false, 0); CHECK(session != NULL);
	CHECK(dssh_session_set_timeout(session, 10000) == 0);
	if (strcmp(mode, "rekey") == 0)
		CHECK(dssh_session_set_rekey_seconds(session, 1) == 0);
	CHECK(dssh_session_set_terminate_cb(session, terminate, NULL) == 0);
	CHECK(dssh_transport_handshake(session) == 0);
	if (strcmp(mode, "banner") == 0) CHECK(dssh_auth_set_banner(session, "Fixture banner", "en") == 0);
	auth.methods_str = strncmp(mode, "keyboard", 8) == 0 ? "keyboard-interactive" : "password";
	status = dssh_auth_server(session, &auth, NULL, NULL);
	if (status != 0) {
		dssh_session_cleanup(session); close(descriptor);
		return 0; /* client-side rejection cases */
	}
	CHECK(dssh_session_start(session) == 0);
	channel = dssh_chan_accept(session, &accept_callbacks, 10000);
	CHECK(channel != NULL);
	CHECK(send_text(channel, "hello") == 0);
	if (strcmp(mode, "log-ui") == 0) {
		/* Let the application finish constructing its terminal before the
		 * peer closes. The UI driver acknowledges the rendered greeting. */
		uint8_t acknowledged;
		CHECK((dssh_chan_poll(channel, DSSH_POLL_READ, 10000) & DSSH_POLL_READ) != 0);
		CHECK(dssh_chan_read(channel, 0, &acknowledged, 1) == 1);
		CHECK(dssh_chan_close(channel, 0) == 0);
		dssh_session_cleanup(session); close(descriptor);
		return 0;
	}
	if (strcmp(mode, "rekey") == 0) {
		const struct timespec delay = {1, 200000000};
		(void)st_thread_sleep(&delay, NULL);
	}
	for (unsigned attempts = 0; attempts < 100 && (used < sizeof(received) || !resized); ++attempts) {
		int events = dssh_chan_poll(channel, DSSH_POLL_READ | DSSH_POLL_EVENT, 100);
		CHECK(events >= 0);
		if ((events & DSSH_POLL_EVENT) != 0) {
			struct dssh_chan_event event;
			while (dssh_chan_read_event(channel, &event) == 0)
				if (event.type == DSSH_EVENT_WINDOW_CHANGE
				    && event.window_change.cols == 120 && event.window_change.rows == 40)
					resized = true;
		}
		if ((events & DSSH_POLL_READ) != 0 && used < sizeof(received)) {
			int64_t n = dssh_chan_read(channel, 0, received + used, sizeof(received) - used);
			CHECK(n > 0); used += (size_t)n;
		}
	}
	CHECK(resized && used == sizeof(received) && memcmp(received, expected, used) == 0);
	CHECK(send_text(channel, "bye") == 0);
	CHECK(dssh_chan_close(channel, 0) == 0);
	dssh_session_cleanup(session); close(descriptor);
	return 0;
}
