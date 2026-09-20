/* POSIX pseudo-terminal shell transport. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "connection_internal.h"

#include "io_error.h"
#include "st_connection_config.h"
#include "st_settings.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#if ST_CONNECTION_HAVE_TTYDEFAULTS
	#define TTYDEFCHARS
	#include <sys/ttydefaults.h>
#endif

#if ST_CONNECTION_FORKPTY_HEADER_PTY_H
	#include <pty.h>
#elif ST_CONNECTION_FORKPTY_HEADER_UTIL_H
	#include <util.h>
#elif ST_CONNECTION_FORKPTY_HEADER_LIBUTIL_H
	#include <libutil.h>
#endif

extern char **environ;

struct shell_transport {
	st_mutex_t mutex;
	int descriptor;
	pid_t child;
};

static ssize_t
shell_read_transport(void *state, void *data, size_t size,
	int64_t timeout_ms)
{
	struct shell_transport *transport = state;
	struct pollfd item = {transport->descriptor, POLLIN, 0};
	int timeout = timeout_ms < 0 ? -1
	    : timeout_ms > INT32_MAX ? INT32_MAX : (int)timeout_ms;
	int result;

	do {
		result = poll(&item, 1, timeout);
	} while (result < 0 && errno == EINTR);
	if (result == 0)
		return IO_ERROR_TIMED_OUT;
	if (result < 0)
		return IO_ERROR_IO;
	if ((item.revents & POLLNVAL) != 0)
		return IO_ERROR_NOT_CONNECTED;
	if ((item.revents & (POLLIN | POLLHUP)) != 0) {
		ssize_t count;

		do {
			count = read(transport->descriptor, data, size);
		} while (count < 0 && errno == EINTR);
		if (count < 0 && errno == EIO && (item.revents & POLLHUP) != 0)
			return 0;
		return count < 0 ? IO_ERROR_IO : count;
	}
	return IO_ERROR_IO;
}

static ssize_t
shell_write_transport(void *state, const void *data, size_t size,
	int64_t timeout_ms)
{
	struct shell_transport *transport = state;
	struct pollfd item = {transport->descriptor, POLLOUT, 0};
	int timeout = timeout_ms < 0 ? -1
	    : timeout_ms > INT32_MAX ? INT32_MAX : (int)timeout_ms;
	int result;
	ssize_t count;

	do {
		result = poll(&item, 1, timeout);
	} while (result < 0 && errno == EINTR);
	if (result == 0)
		return IO_ERROR_TIMED_OUT;
	if (result < 0 || (item.revents & (POLLERR | POLLNVAL | POLLHUP)) != 0)
		return IO_ERROR_IO;
	do {
		count = write(transport->descriptor, data, size);
	} while (count < 0 && errno == EINTR);
	return count < 0 ? IO_ERROR_IO : count;
}

static void
shell_interrupt(void *state)
{
	struct shell_transport *transport = state;

	(void)kill(transport->child, SIGHUP);
}

static int
shell_update_terminal_size(void *state,
	const struct st_screen_geometry *geometry)
{
	struct shell_transport *transport = state;
	struct winsize size;
	int result;

	if (geometry->columns > UINT16_MAX || geometry->rows > UINT16_MAX
	    || geometry->pixel_width > UINT16_MAX
	    || geometry->pixel_height > UINT16_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	memset(&size, 0, sizeof(size));
	size.ws_col = (unsigned short)geometry->columns;
	size.ws_row = (unsigned short)geometry->rows;
	size.ws_xpixel = (unsigned short)geometry->pixel_width;
	size.ws_ypixel = (unsigned short)geometry->pixel_height;
	(void)st_mutex_lock(&transport->mutex);
	result = ioctl(transport->descriptor, TIOCSWINSZ, &size);
	(void)st_mutex_unlock(&transport->mutex);
	return result == 0 ? 0 : IO_ERROR_IO;
}

static void
shell_destroy(void *state)
{
	struct shell_transport *transport = state;
	unsigned attempts;
	int status;
	pid_t waited;
	struct timespec pause = {0, 100000000L};

	for (attempts = 0; attempts < 100; ++attempts) {
		pid_t result = waitpid(transport->child, &status, WNOHANG);

		if (result == transport->child || (result < 0 && errno == ECHILD))
			break;
		(void)st_thread_sleep(&pause, NULL);
	}
	if (attempts == 100) {
		(void)kill(transport->child, SIGKILL);
		do {
			waited = waitpid(transport->child, &status, 0);
		} while (waited < 0 && errno == EINTR);
	}
	(void)close(transport->descriptor);
	st_mutex_destroy(&transport->mutex);
	free(transport);
}

static const struct st_connection_transport_ops shell_operations = {
	.bind = NULL,
	.read = shell_read_transport,
	.write = shell_write_transport,
	.interrupt = shell_interrupt,
	.update_terminal_size = shell_update_terminal_size,
	.destroy = shell_destroy,
};

static void
configure_shell_terminal(void)
{
	struct termios attributes;

	if (tcgetattr(STDIN_FILENO, &attributes) != 0)
		return;
#if ST_CONNECTION_HAVE_TTYDEFAULTS
	attributes.c_iflag = TTYDEF_IFLAG;
	attributes.c_oflag = TTYDEF_OFLAG;
	attributes.c_lflag = TTYDEF_LFLAG;
	attributes.c_cflag = TTYDEF_CFLAG;
	memcpy(attributes.c_cc, ttydefchars, sizeof(attributes.c_cc));
#endif
	attributes.c_iflag &= ~(IXON | IXOFF);
#if ST_CONNECTION_HAVE_IXANY
	attributes.c_iflag &= ~IXANY;
#endif
	attributes.c_cc[VERASE] = '\b';
#if ST_CONNECTION_HAVE_VERASE2
	attributes.c_cc[VERASE2] = '\b';
#endif
#if ST_CONNECTION_HAVE_B115200
	(void)cfsetispeed(&attributes, B115200);
	(void)cfsetospeed(&attributes, B115200);
#elif ST_CONNECTION_HAVE_TTYDEFAULTS
	(void)cfsetispeed(&attributes, TTYDEF_SPEED);
	(void)cfsetospeed(&attributes, TTYDEF_SPEED);
#endif
	(void)tcsetattr(STDIN_FILENO, TCSANOW, &attributes);
}

static char *
environment_value(const char *name, const char *value)
{
	size_t name_length = strlen(name);
	size_t value_length = strlen(value);
	char *result = malloc(name_length + value_length + 2);

	if (result != NULL)
		(void)snprintf(result, name_length + value_length + 2, "%s=%s",
		    name, value);
	return result;
}

static bool
environment_matches(const char *entry, const char *name)
{
	size_t length = strlen(name);

	return strncmp(entry, name, length) == 0 && entry[length] == '=';
}

static char **
make_environment(const struct st_profile *profile,
	const struct st_screen_geometry *geometry, char **owned)
{
	static const char *names[] = {"TERM", "COLUMNS", "LINES"};
	char columns[32];
	char rows[32];
	const char *values[3];
	size_t original_count = 0;
	size_t count = 0;
	size_t i;
	char **result;

	while (environ[original_count] != NULL)
		++original_count;
	(void)snprintf(columns, sizeof(columns), "%u", geometry->columns);
	(void)snprintf(rows, sizeof(rows), "%u", geometry->rows);
	values[0] = profile->terminal_name[0] != '\0'
	    ? profile->terminal_name
	    : st_settings.terminal_type[0] != '\0'
	    ? st_settings.terminal_type : "ansi";
	values[1] = columns;
	values[2] = rows;
	result = calloc(original_count + 4, sizeof(*result));
	if (result == NULL)
		return NULL;
	for (i = 0; i < original_count; ++i) {
		size_t replacement;

		for (replacement = 0; replacement < 3; ++replacement) {
			if (environment_matches(environ[i], names[replacement]))
				break;
		}
		if (replacement == 3)
			result[count++] = environ[i];
	}
	for (i = 0; i < 3; ++i) {
		owned[i] = environment_value(names[i], values[i]);
		if (owned[i] == NULL) {
			while (i > 0)
				free(owned[--i]);
			free(result);
			return NULL;
		}
		result[count++] = owned[i];
	}
	return result;
}

int
st_connection_connect_shell(st_connection_t **result,
	const struct st_profile *profile,
	const struct st_screen_geometry *current_screen,
	const struct st_connection_hooks *hooks,
	struct st_connection_error *error)
{
	struct st_screen_geometry geometry;
	struct shell_transport *transport;
	struct winsize window;
	char *owned_environment[3] = {NULL, NULL, NULL};
	char **environment;
	const char *command;
	pid_t child;
	int master;
	int status;
	size_t i;

	if (result == NULL || profile == NULL
	    || profile->connection_type != ST_CONNECTION_SHELL)
		return st_connection_fail(error, IO_ERROR_INVALID_ARGUMENT,
		    ST_CONNECTION_SETUP_NONE, "profile is not a shell connection");
	status = st_connection_profile_geometry(profile, current_screen,
	    &geometry);
	if (status != 0)
		return st_connection_fail(error, status, ST_CONNECTION_SETUP_NONE,
		    "invalid initial terminal geometry");
	if (geometry.columns > UINT16_MAX || geometry.rows > UINT16_MAX
	    || geometry.pixel_width > UINT16_MAX
	    || geometry.pixel_height > UINT16_MAX)
		return st_connection_fail(error, IO_ERROR_OUT_OF_RANGE,
		    ST_CONNECTION_SETUP_STARTING_SHELL,
		    "terminal geometry exceeds the pseudo-terminal limit");
	if (st_connection_cancelled(hooks,
	    ST_CONNECTION_SETUP_STARTING_SHELL))
		return st_connection_fail(error, IO_ERROR_CANCELLED,
		    ST_CONNECTION_SETUP_STARTING_SHELL, "connection cancelled");
	command = profile->address[0] != '\0' ? profile->address : getenv("SHELL");
	if (command == NULL || command[0] == '\0')
		command = "/bin/sh";
	environment = make_environment(profile, &geometry, owned_environment);
	if (environment == NULL)
		return st_connection_fail(error, IO_ERROR_OUT_OF_MEMORY,
		    ST_CONNECTION_SETUP_STARTING_SHELL,
		    "unable to create shell environment");
	memset(&window, 0, sizeof(window));
	window.ws_col = (unsigned short)geometry.columns;
	window.ws_row = (unsigned short)geometry.rows;
	window.ws_xpixel = (unsigned short)geometry.pixel_width;
	window.ws_ypixel = (unsigned short)geometry.pixel_height;
	child = forkpty(&master, NULL, NULL, &window);
	if (child == 0) {
		configure_shell_terminal();
		execle("/bin/sh", "sh", "-c", command, (char *)NULL,
		    environment);
		_exit(127);
	}
	for (i = 0; i < 3; ++i)
		free(owned_environment[i]);
	free(environment);
	if (child < 0)
		return st_connection_fail(error, IO_ERROR_IO,
		    ST_CONNECTION_SETUP_STARTING_SHELL,
		    "unable to create pseudo-terminal");
	(void)fcntl(master, F_SETFD, fcntl(master, F_GETFD) | FD_CLOEXEC);
	transport = calloc(1, sizeof(*transport));
	if (transport == NULL) {
		(void)kill(child, SIGHUP);
		(void)close(master);
		(void)waitpid(child, NULL, 0);
		return st_connection_fail(error, IO_ERROR_OUT_OF_MEMORY,
		    ST_CONNECTION_SETUP_STARTING_SHELL,
		    "unable to allocate shell transport");
	}
	transport->descriptor = master;
	transport->child = child;
	if (st_mutex_init(&transport->mutex, ST_MUTEX_PLAIN)
	    != ST_THREAD_SUCCESS) {
		(void)kill(child, SIGHUP);
		(void)close(master);
		(void)waitpid(child, NULL, 0);
		free(transport);
		return st_connection_fail(error, IO_ERROR_OUT_OF_MEMORY,
		    ST_CONNECTION_SETUP_STARTING_SHELL,
		    "unable to initialize shell transport");
	}
	status = st_connection_start(result, transport, &shell_operations,
	    "Shell", hooks, NULL, error);
	if (status == 0)
		st_connection_log(*result, ST_CONNECTION_LOG_INFO,
		    "session established");
	return status;
}
