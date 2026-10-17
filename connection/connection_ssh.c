/* SSH shell transport using the external DeuceSSH library.
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */
#include "connection_stream.h"
#include "st_time.h"
#include "string_compat.h"
#include "xp_crypto.h"
#include "xp_digest.h"
#include "client.h"
#include <deucessh.h>
#include <deucessh-algorithms.h>
#include <deucessh-auth.h>
#include <deucessh-conn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ssh_notice {
	struct ssh_notice *next;
	char title[32];
	char message[];
};

struct ssh_transport {
	struct st_connection_stream stream;
	dssh_session session;
	dssh_channel channel;
	dssh_channel sftp_channel; /* SFTP worker only; session cleanup owns storage */
	struct st_sftp_client *sftp;
	const struct st_profile *profile; /* setup only */
	const struct st_connection_hooks *hooks; /* setup only */
	atomic_bool setup_complete;
	atomic_bool hostkey_verified;
	struct st_profile identity;
	struct st_connection_hooks ui_hooks;
	st_mutex_t request_mutex;
	st_condition_t request_changed;
	bool request_initialized, request_pending, request_accepted, stopping;
	uint8_t pending_fingerprint[32];
	char pending_algorithm[65];
	unsigned pending_bits;
	bool legacy_match;
	bool cancelled;
	uint8_t fingerprint[32];
	char algorithm[65];
	unsigned key_bits;
	bool used_saved_password;
	bool hide_popups;
	struct ssh_notice *notices, *last_notice;
	size_t notice_count;
};

struct ssh_pty_mode {
	uint8_t  op;
	uint32_t val;
};

static const struct ssh_pty_mode default_pty_modes[] = {
	/* Special characters — 255 means "none". */
	{   1,     3 },	/* VINTR    ^C → SIGINT                  */
	{   2,    28 },	/* VQUIT    ^\ → SIGQUIT                 */
	{   3,     8 },	/* VERASE   ^H (DECBKM-set default)      */
	{   4,    21 },	/* VKILL    ^U                           */
	{   5,     4 },	/* VEOF     ^D                           */
	{   6,   255 },	/* VEOL     none                         */
	{   7,   255 },	/* VEOL2    none                         */
	{   8,    17 },	/* VSTART   ^Q (moot; IXON=0)            */
	{   9,    19 },	/* VSTOP    ^S (moot)                    */
	{  10,    26 },	/* VSUSP    ^Z                           */
	{  11,    25 },	/* VDSUSP   ^Y (BSD; ignored elsewhere)  */
	{  12,    18 },	/* VREPRINT ^R                           */
	{  13,    23 },	/* VWERASE  ^W                           */
	{  14,    22 },	/* VLNEXT   ^V                           */
	{  15,    15 },	/* VFLUSH   ^O                           */
	{  16,   255 },	/* VSWTCH   none                         */
	{  17,    20 },	/* VSTATUS  ^T (BSD; ignored elsewhere)  */
	{  18,    15 },	/* VDISCARD ^O                           */
	/* Input flags */
	{  30,     1 },	/* IGNPAR   no parity over SSH           */
	{  31,     0 },	/* PARMRK                                */
	{  32,     0 },	/* INPCK                                 */
	{  33,     0 },	/* ISTRIP   8-bit clean                  */
	{  34,     0 },	/* INLCR                                 */
	{  35,     0 },	/* IGNCR                                 */
	{  36,     1 },	/* ICRNL    Enter→LF for cooked progs    */
	{  37,     0 },	/* IUCLC                                 */
	{  38,     0 },	/* IXON     would corrupt zmodem         */
	{  39,     0 },	/* IXANY                                 */
	{  40,     0 },	/* IXOFF                                 */
	{  41,     1 },	/* IMAXBEL                               */
	{  42,     0 },	/* IUTF8    set when emulation is UTF-8  */
	/* Local flags */
	{  50,     1 },	/* ISIG     ^C reaches the shell         */
	{  51,     1 },	/* ICANON                                */
	{  52,     0 },	/* XCASE                                 */
	{  53,     1 },	/* ECHO                                  */
	{  54,     1 },	/* ECHOE                                 */
	{  55,     1 },	/* ECHOK                                 */
	{  56,     0 },	/* ECHONL                                */
	{  57,     0 },	/* NOFLSH                                */
	{  58,     0 },	/* TOSTOP                                */
	{  59,     1 },	/* IEXTEN                                */
	{  60,     1 },	/* ECHOCTL                               */
	{  61,     1 },	/* ECHOKE                                */
	{  62,     0 },	/* PENDIN                                */
	/* Output flags */
	{  70,     1 },	/* OPOST                                 */
	{  71,     0 },	/* OLCUC                                 */
	{  72,     1 },	/* ONLCR    server-side LF→CRLF          */
	{  73,     0 },	/* OCRNL                                 */
	{  74,     0 },	/* ONOCR                                 */
	{  75,     0 },	/* ONLRET                                */
	/* Control flags */
	{  90,     0 },	/* CS7                                   */
	{  91,     1 },	/* CS8      8-bit                        */
	{  92,     0 },	/* PARENB                                */
	{  93,     0 },	/* PARODD                                */
	/* Line speed (bps) — advisory; some apps key behavior off it. */
	{ 128, 38400 },	/* TTY_OP_ISPEED                         */
	{ 129, 38400 },	/* TTY_OP_OSPEED                         */
};

static st_once_flag_t init_once = ST_ONCE_FLAG_INIT;
static st_mutex_t init_mutex;
static bool init_mutex_valid, algorithms_ready;
static int initialization_status;

static int
ssh_error(int status)
{
	switch (status) {
	case 0: return 0;
	case DSSH_ERROR_ALLOC: return IO_ERROR_OUT_OF_MEMORY;
	case DSSH_ERROR_TIMEOUT: return IO_ERROR_TIMED_OUT;
	case DSSH_ERROR_TERMINATED: return IO_ERROR_CONNECTION_RESET;
	case DSSH_ERROR_REJECTED:
	case DSSH_ERROR_AUTH_REJECTED: return IO_ERROR_PERMISSION_DENIED;
	case DSSH_ERROR_INIT: return IO_ERROR_IO;
	default: return IO_ERROR_PROTOCOL;
	}
}
static void
initialize_mutex(void)
{
	init_mutex_valid = st_mutex_init(&init_mutex, ST_MUTEX_PLAIN) == ST_THREAD_SUCCESS;
}

static int
initialize_algorithms(const char *key_file)
{
	int status = 0;
	st_call_once(&init_once, initialize_mutex);
	if (!init_mutex_valid) return IO_ERROR_OUT_OF_MEMORY;
	(void)st_mutex_lock(&init_mutex);
	if (!algorithms_ready) {
		int (*const functions[])(void) = {
			dssh_register_mlkem768x25519_sha256,
			dssh_register_sntrup761x25519_sha512,
			dssh_register_curve25519_sha256,
			dssh_register_dh_gex_sha256,
			dssh_register_aes256_ctr, dssh_register_aes128_cbc,
			dssh_register_hmac_sha2_256, dssh_register_hmac_sha2_512,
			dssh_register_none_comp, dssh_register_ssh_ed25519,
			dssh_register_rsa_sha2_256, dssh_register_rsa_sha2_512,
		};
		for (size_t i = 0; i < sizeof(functions) / sizeof(functions[0])
		    && status == 0; ++i) status = functions[i]();
		if (status == 0 && (key_file == NULL
		    || dssh_ed25519_load_key_file(key_file, NULL, NULL) != 0)) {
			status = dssh_ed25519_generate_key();
			if (status == 0 && key_file != NULL)
				(void)dssh_ed25519_save_key_file(key_file, NULL, NULL);
		}
		initialization_status = status;
		algorithms_ready = true;
	}
	status = initialization_status;
	(void)st_mutex_unlock(&init_mutex);
	return status;
}

static int
ssh_io(uint8_t *data, size_t size, dssh_session session, void *context, bool sending)
{
	struct ssh_transport *t = context;
	uint64_t start = 0, now;
	size_t done = 0;
	(void)st_time_monotonic_ms(&start);
	while (done < size) {
		ssize_t n;
		if (dssh_session_is_terminated(session)) return DSSH_ERROR_TERMINATED;
		if (!atomic_load(&t->setup_complete)) {
			if (st_connection_cancelled(t->hooks, ST_CONNECTION_SETUP_NEGOTIATING)) {
				t->cancelled = true;
				return DSSH_ERROR_TERMINATED;
			}
			if (st_time_monotonic_ms(&now) == 0 && now - start >= 60000)
				return DSSH_ERROR_TIMEOUT;
		}
		n = sending ? st_connection_stream_write(&t->stream, data + done, size - done, 100)
		    : st_connection_stream_read(&t->stream, data + done, size - done, 100);
		if (n == IO_ERROR_WOULD_BLOCK || n == IO_ERROR_TIMED_OUT) continue;
		if (n <= 0) return DSSH_ERROR_TERMINATED;
		done += (size_t)n;
	}
	return 0;
}
static int
ssh_tx(uint8_t *data, size_t size, dssh_session session, void *context)
{
	return ssh_io(data, size, session, context, true);
}
static int
ssh_rx(uint8_t *data, size_t size, dssh_session session, void *context)
{
	return ssh_io(data, size, session, context, false);
}
static void
ssh_terminated(dssh_session session, void *context)
{
	struct ssh_transport *t = context;
	(void)session;
	st_connection_stream_interrupt(&t->stream);
}
static int confirm_hostkey(struct ssh_transport *t);

static void
ssh_notice(struct ssh_transport *t, const char *title, const uint8_t *message, size_t size)
{
	if (size == 0 || t->hide_popups) return;
	if (size > SIZE_MAX - sizeof(struct ssh_notice) - 1) return;
	struct ssh_notice *notice = malloc(sizeof(*notice) + size + 1);
	if (notice == NULL) return;
	notice->next = NULL;
	(void)snprintf(notice->title, sizeof(notice->title), "%s", title);
	memcpy(notice->message, message, size);
	notice->message[size] = '\0';
	if (!atomic_load(&t->setup_complete)) {
		st_connection_notice(t->hooks, title, notice->message);
		free(notice);
		return;
	}
	(void)st_mutex_lock(&t->request_mutex);
	if (!t->stopping && t->notice_count < 64) {
		if (t->last_notice != NULL) t->last_notice->next = notice;
		else t->notices = notice;
		t->last_notice = notice;
		++t->notice_count;
		notice = NULL;
	}
	(void)st_mutex_unlock(&t->request_mutex);
	free(notice);
}

static void
auth_banner(const uint8_t *message, size_t size, const uint8_t *language,
	size_t language_size, void *context)
{
	(void)language; (void)language_size;
	ssh_notice(context, "SSH Banner", message, size);
}

static void
debug_message(bool always, const uint8_t *message, size_t size, void *context)
{
	if (always) ssh_notice(context, "SSH Debug", message, size);
}

static dssh_log_action
ssh_diagnostic(const struct dssh_log_record *record, void *context)
{
	struct ssh_transport *t = context;
	if (t->hooks != NULL && t->hooks->log != NULL) {
		char message[DSSH_LOG_MESSAGE_MAX + 1];
		size_t size = record->message_len < sizeof(message) - 1
		    ? record->message_len : sizeof(message) - 1;
		memcpy(message, record->message, size); message[size] = '\0';
		t->hooks->log(t->hooks->log_context, record->level == DSSH_LOG_ERROR
		    ? ST_CONNECTION_LOG_ERROR : record->level == DSSH_LOG_WARNING
		    ? ST_CONNECTION_LOG_WARNING : ST_CONNECTION_LOG_DEBUG, "SSH", message);
	}
	return DSSH_LOG_LOCAL_ONLY;
}

static dssh_hostkey_decision
verify_hostkey(const char *algorithm, unsigned bits, const uint8_t *fingerprint,
	const uint8_t *blob, size_t size, void *context)
{
	struct ssh_transport *t = context;
	(void)blob;
	(void)size;
	if (atomic_load(&t->hostkey_verified)) {
		bool accepted;
		if (memcmp(t->fingerprint, fingerprint, sizeof(t->fingerprint)) == 0)
			return DSSH_HOSTKEY_ACCEPT;
		if (!atomic_load(&t->setup_complete)) {
			struct st_profile identity = {0};
			struct ssh_transport candidate = {0};
			memcpy(identity.ssh_fingerprint, t->fingerprint, 32);
			identity.ssh_fingerprint_size = 32;
			identity.source = t->profile->source;
			strcpy(identity.address, t->profile->address);
			candidate.profile = &identity;
			candidate.hooks = t->hooks;
			memcpy(candidate.fingerprint, fingerprint, 32);
			(void)snprintf(candidate.algorithm, sizeof(candidate.algorithm), "%s", algorithm);
			candidate.key_bits = bits;
			if (confirm_hostkey(&candidate) != 0) return DSSH_HOSTKEY_REJECT;
			memcpy(t->fingerprint, fingerprint, 32);
			return DSSH_HOSTKEY_ACCEPT;
		}
		(void)st_mutex_lock(&t->request_mutex);
		memcpy(t->pending_fingerprint, fingerprint, 32);
		(void)snprintf(t->pending_algorithm, sizeof(t->pending_algorithm), "%s", algorithm);
		t->pending_bits = bits;
		t->request_pending = true;
		t->request_accepted = false;
		while (t->request_pending && !t->stopping)
			(void)st_condition_wait(&t->request_changed, &t->request_mutex);
		accepted = !t->stopping && t->request_accepted;
		(void)st_mutex_unlock(&t->request_mutex);
		return accepted ? DSSH_HOSTKEY_ACCEPT : DSSH_HOSTKEY_REJECT;
	}
	memcpy(t->fingerprint, fingerprint, sizeof(t->fingerprint));
	(void)snprintf(t->algorithm, sizeof(t->algorithm), "%s", algorithm);
	t->key_bits = bits;
	if (t->profile->ssh_fingerprint_size == 20 && size > 4) {
		uint32_t name_size = ((uint32_t)blob[0] << 24) | ((uint32_t)blob[1] << 16)
		    | ((uint32_t)blob[2] << 8) | blob[3];
		uint8_t legacy[20] = {0};
		if (name_size < size - 4
		    && xp_digest(XP_DIGEST_MD5, blob + 4 + name_size,
		    size - 4 - name_size, legacy, 16) == XP_CRYPTO_OK)
			t->legacy_match = memcmp(t->profile->ssh_fingerprint, legacy, 20) == 0;
	}
	/* Confirm on the connecting thread after handshake, before credentials. */
	return DSSH_HOSTKEY_ACCEPT;
}

static int
confirm_hostkey(struct ssh_transport *t)
{
	const struct st_profile *profile = t->profile;
	const struct st_connection_hooks *hooks = t->hooks;
	bool weak = strstr(t->algorithm, "rsa") != NULL
	    && t->key_bits != 0 && t->key_bits < 2048;
	char hex[65], previous[65], message[768];
	static const char digits[] = "0123456789abcdef";
	const char *choices[] = {"Disconnect", "Update", "Ignore"};
	int choice;

	if (profile->ssh_fingerprint_size == 32
	    && memcmp(profile->ssh_fingerprint, t->fingerprint, 32) == 0) return 0;
	if (t->legacy_match || (profile->ssh_fingerprint_size == 0 && !weak))
		return hooks != NULL && hooks->remember_hostkey != NULL
		    ? hooks->remember_hostkey(hooks->ui_context, t->fingerprint, false) : 0;
	for (size_t i = 0; i < 32; ++i) {
		hex[2 * i] = digits[t->fingerprint[i] >> 4];
		hex[2 * i + 1] = digits[t->fingerprint[i] & 15];
	}
	hex[64] = '\0';
	if (profile->ssh_fingerprint_size == 0 && weak) {
		const char *weak_choices[] = {"Disconnect", "Accept"};
		char title[128];
		(void)snprintf(title, sizeof(title), "Weak host key (%u-bit %s)", t->key_bits, t->algorithm);
		(void)snprintf(message, sizeof(message), "The server presented a %u-bit %s host key. "
		    "Keys below 2048 bits are considered weak today and within reach of a well-resourced attacker.\n\n"
		    "This is the first time SyncTERM has seen this host, so there is no prior fingerprint to compare against. "
		    "A genuine weak key and an active downgrade cannot be distinguished here.\n\nFingerprint: %s\n",
		    t->key_bits, t->algorithm, hex);
		if (hooks == NULL || hooks->choice == NULL || profile->hide_popups)
			return IO_ERROR_PERMISSION_DENIED;
		choice = hooks->choice(hooks->ui_context, title, message, weak_choices, 2);
		if (choice != 1) return choice < 0 ? choice : IO_ERROR_PERMISSION_DENIED;
		return hooks->remember_hostkey == NULL ? 0
		    : hooks->remember_hostkey(hooks->ui_context, t->fingerprint, false);
	}
	for (size_t i = 0; i < profile->ssh_fingerprint_size && i < 32; ++i) {
		previous[2 * i] = digits[profile->ssh_fingerprint[i] >> 4];
		previous[2 * i + 1] = digits[profile->ssh_fingerprint[i] & 15];
	}
	previous[2 * (profile->ssh_fingerprint_size < 32 ? profile->ssh_fingerprint_size : 32)] = '\0';
	if (hooks == NULL || hooks->choice == NULL || profile->hide_popups) return IO_ERROR_PERMISSION_DENIED;
	(void)snprintf(message, sizeof(message),
	    "%s\nThis may indicate eavesdropping, or the server may have changed its key."
	    "\nHost: %s\nKey: %s (%u bits)\nLast known fingerprint: %s\nFingerprint sent now (SHA-256): %s%s",
	    profile->ssh_fingerprint_size == 0 ? "Unknown SSH host key"
	    : "SSH HOST KEY HAS CHANGED or uses a legacy fingerprint",
	    profile->address, t->algorithm, t->key_bits, previous, hex,
	    weak ? "\nWARNING: this RSA key is weaker than 2048 bits." : "");
	char title[128] = "Fingerprint Changed";
	if (weak) (void)snprintf(title, sizeof(title), "Fingerprint Changed - WEAK %u-bit %s key",
	    t->key_bits, t->algorithm);
	choice = hooks->choice(hooks->ui_context, title, message, choices, 3);
	if (choice == 1)
		return hooks->remember_hostkey == NULL ? 0
		    : hooks->remember_hostkey(hooks->ui_context, t->fingerprint, true);
	return choice == 2 ? 0 : choice < 0 ? choice : IO_ERROR_PERMISSION_DENIED;
}

static int
ssh_process_requests(void *state)
{
	struct ssh_transport *t = state;
	struct ssh_transport candidate = {0};
	int status;
	(void)st_mutex_lock(&t->request_mutex);
	/* New arrivals wait for the next owner tick, so a stream of peer debug
	 * messages cannot indefinitely postpone input/close handling. */
	size_t notices = t->notice_count;
	while (t->notices != NULL && !t->stopping && notices-- != 0) {
		struct ssh_notice *notice = t->notices;
		t->notices = notice->next;
		if (t->notices == NULL) t->last_notice = NULL;
		--t->notice_count;
		(void)st_mutex_unlock(&t->request_mutex);
		st_connection_notice(&t->ui_hooks, notice->title, notice->message);
		free(notice);
		(void)st_mutex_lock(&t->request_mutex);
	}
	if (!t->request_pending || t->stopping) {
		(void)st_mutex_unlock(&t->request_mutex);
		return 0;
	}
	candidate.profile = &t->identity;
	candidate.hooks = &t->ui_hooks;
	memcpy(candidate.fingerprint, t->pending_fingerprint, 32);
	memcpy(candidate.algorithm, t->pending_algorithm, sizeof(candidate.algorithm));
	candidate.key_bits = t->pending_bits;
	(void)st_mutex_unlock(&t->request_mutex);
	status = confirm_hostkey(&candidate);
	(void)st_mutex_lock(&t->request_mutex);
	t->request_accepted = status == 0;
	if (status == 0) {
		memcpy(t->fingerprint, candidate.fingerprint, 32);
		memcpy(t->identity.ssh_fingerprint, candidate.fingerprint, 32);
		t->identity.ssh_fingerprint_size = 32;
	}
	t->request_pending = false;
	(void)st_condition_broadcast(&t->request_changed);
	(void)st_mutex_unlock(&t->request_mutex);
	return 0;
}

static int
prompt(struct ssh_transport *t, const char *title, const char *message,
	size_t limit, bool masked, char **answer)
{
	*answer = NULL;
	if (t->hide_popups || t->hooks == NULL || t->hooks->prompt == NULL)
		return IO_ERROR_PASSWORD_REQUIRED;
	int status = t->hooks->prompt(t->hooks->ui_context, title,
	    message, "", limit, masked, answer);
	if (status != 0) {
		if (status == IO_ERROR_CANCELLED) t->cancelled = true;
		return status;
	}
	if (*answer == NULL) { t->cancelled = true; return IO_ERROR_CANCELLED; }
	return 0;
}

static int
keyboard_interactive(const uint8_t *name, size_t name_size,
	const uint8_t *instruction, size_t instruction_size, uint32_t count,
	const uint8_t **prompts, const size_t *prompt_sizes, const bool *echo,
	uint8_t **responses, size_t *response_sizes, void *context)
{
	struct ssh_transport *t = context;
	(void)name;
	(void)name_size;
	if (instruction_size != 0)
		ssh_notice(t, "SSH Keyboard-Interactive", instruction, instruction_size);
	for (uint32_t i = 0; i < count; ++i) {
		char question[2048], *answer = NULL;
		int status;
		(void)snprintf(question, sizeof(question), "%.*s",
		    (int)(prompt_sizes[i] < 1000 ? prompt_sizes[i] : 1000), prompts[i]);
		if (count == 1 && !echo[i] && !t->used_saved_password
		    && t->profile->password[0] != '\0' && prompt_sizes[i] == 10
		    && memcmp(prompts[i], "Password: ", 10) == 0) {
			answer = strdup(t->profile->password);
			t->used_saved_password = true;
		} else {
			status = prompt(t, "SSH Keyboard-Interactive", question, ST_PROFILE_PASSWORD_MAX, !echo[i], &answer);
			if (status != 0) {
				for (uint32_t j = 0; j < i; ++j) {
					xp_crypto_clear(responses[j], response_sizes[j]);
					free(responses[j]); responses[j] = NULL; response_sizes[j] = 0;
				}
				return DSSH_ERROR_AUTH_REJECTED;
			}
		}
		if (answer == NULL) return DSSH_ERROR_ALLOC;
		responses[i] = (uint8_t *)answer;
		response_sizes[i] = strlen(answer);
	}
	return 0;
}

static int
authenticate(struct ssh_transport *t)
{
	char *username = NULL, *password = NULL, methods[512];
	int status;
	if (t->profile->username[0] != '\0')
		username = strdup(t->profile->username);
	else if (t->profile->connection_type == ST_CONNECTION_SSH_NO_AUTH)
		username = strdup("");
	else {
		status = prompt(t, "SSH User ID", "No user ID is stored for this entry.",
		    ST_PROFILE_USER_MAX, false, &username);
		if (status != 0) return status;
	}
	if (username == NULL) return IO_ERROR_OUT_OF_MEMORY;
	if (username[0] == '\0' && t->profile->connection_type != ST_CONNECTION_SSH_NO_AUTH) {
		free(username); t->cancelled = true; return IO_ERROR_CANCELLED;
	}
	status = dssh_auth_get_methods(t->session, username, methods, sizeof(methods));
	if (status == DSSH_AUTH_NONE_ACCEPTED) { free(username); return 0; }
	if (status < 0 || t->profile->connection_type == ST_CONNECTION_SSH_NO_AUTH) {
		free(username);
		return IO_ERROR_PERMISSION_DENIED;
	}
	status = DSSH_ERROR_AUTH_REJECTED;
	if (strstr(methods, "publickey") != NULL)
		status = dssh_auth_publickey(t->session, username, "ssh-ed25519");
	if (status != 0 && strstr(methods, "password") != NULL) {
		password = strdup(t->profile->password);
		if (password == NULL) { free(username); return IO_ERROR_OUT_OF_MEMORY; }
		if (password[0] != '\0')
			status = dssh_auth_password(t->session, username, password, NULL, NULL);
		for (int attempt = 0; attempt < 3 && status != 0; ++attempt) {
			xp_crypto_clear(password, strlen(password)); free(password); password = NULL;
			if (prompt(t, "SSH Password", attempt == 0 ? "Enter password."
			    : "Incorrect password. Try again.", ST_PROFILE_PASSWORD_MAX, true, &password) != 0) break;
			if (password[0] == '\0') { t->cancelled = true; break; }
			status = dssh_auth_password(t->session, username, password, NULL, NULL);
		}
		if (password != NULL) { xp_crypto_clear(password, strlen(password)); free(password); }
	}
	if (status != 0 && !t->cancelled && strstr(methods, "keyboard-interactive") != NULL)
		status = dssh_auth_keyboard_interactive(t->session, username, keyboard_interactive, t);
	free(username);
	return status == 0 ? 0 : t->cancelled ? IO_ERROR_CANCELLED : IO_ERROR_PERMISSION_DENIED;
}

static ssize_t
ssh_read(void *state, void *data, size_t size, int64_t timeout)
{
	struct ssh_transport *t = state;
	int events = dssh_chan_poll(t->channel, DSSH_POLL_READ | DSSH_POLL_READEXT, timeout);
	if (events < 0) return IO_ERROR_CONNECTION_RESET;
	if ((events & DSSH_POLL_READEXT) != 0) {
		uint8_t discarded[4096];
		(void)dssh_chan_read(t->channel, 1, discarded, sizeof(discarded));
	}
	if ((events & DSSH_POLL_READ) != 0) {
		int64_t n = dssh_chan_read(t->channel, 0, data, size);
		return n == DSSH_ERROR_NOMORE ? IO_ERROR_WOULD_BLOCK
		    : n < 0 ? IO_ERROR_CONNECTION_RESET : (ssize_t)n;
	}
	return IO_ERROR_WOULD_BLOCK;
}
static ssize_t
ssh_write(void *state, const void *data, size_t size, int64_t timeout)
{
	struct ssh_transport *t = state;
	int events = dssh_chan_poll(t->channel, DSSH_POLL_WRITE, timeout);
	if (events < 0) return IO_ERROR_CONNECTION_RESET;
	if ((events & DSSH_POLL_WRITE) != 0) {
		int64_t n = dssh_chan_write(t->channel, 0, data, size);
		return n == DSSH_ERROR_NOMORE ? IO_ERROR_WOULD_BLOCK
		    : n < 0 ? IO_ERROR_CONNECTION_RESET : (ssize_t)n;
	}
	return IO_ERROR_WOULD_BLOCK;
}
static void
ssh_interrupt(void *state)
{
	struct ssh_transport *t = state;
	if (t->request_initialized) {
		(void)st_mutex_lock(&t->request_mutex);
		t->stopping = true;
		(void)st_condition_broadcast(&t->request_changed);
		(void)st_mutex_unlock(&t->request_mutex);
	}
	if (t->session != NULL) (void)dssh_session_terminate(t->session);
	st_connection_stream_interrupt(&t->stream);
}
static void
ssh_destroy(void *state)
{
	struct ssh_transport *t = state;
	ssh_interrupt(t);
	st_sftp_client_destroy(t->sftp);
	if (t->session != NULL) dssh_session_cleanup(t->session);
	while (t->notices != NULL) {
		struct ssh_notice *next = t->notices->next;
		free(t->notices); t->notices = next;
	}
	st_connection_stream_close(&t->stream);
	if (t->request_initialized) {
		st_condition_destroy(&t->request_changed);
		st_mutex_destroy(&t->request_mutex);
	}
	free(t);
}
static int
ssh_resize(void *state, const struct st_screen_geometry *geometry)
{
	struct ssh_transport *t = state;
	return dssh_chan_send_window_change(t->channel, geometry->columns,
	    geometry->rows, geometry->pixel_width, geometry->pixel_height) == 0
	    ? 0 : IO_ERROR_IO;
}
static int
sftp_open_channel(void *context)
{
	struct ssh_transport *t = context;
	struct dssh_chan_params params = {0};
	int status = dssh_chan_params_init(&params, DSSH_CHAN_SUBSYSTEM);
	if (status == 0) status = dssh_chan_params_set_subsystem(&params, "sftp");
	if (status == 0) t->sftp_channel = dssh_chan_open(t->session, &params);
	dssh_chan_params_free(&params);
	return t->sftp_channel == NULL ? IO_ERROR_NOT_SUPPORTED : 0;
}
static ssize_t
sftp_read_channel(void *context, void *data, size_t size, int timeout)
{
	struct ssh_transport *t = context;
	int events = dssh_chan_poll(t->sftp_channel, DSSH_POLL_READ, timeout);
	if (events < 0) return IO_ERROR_CONNECTION_RESET;
	if ((events & DSSH_POLL_READ) == 0) return IO_ERROR_WOULD_BLOCK;
	int64_t n = dssh_chan_read(t->sftp_channel, 0, data, size);
	return n == DSSH_ERROR_NOMORE ? IO_ERROR_WOULD_BLOCK
	    : n < 0 ? IO_ERROR_CONNECTION_RESET : (ssize_t)n;
}
static ssize_t
sftp_send_channel(void *context, const void *data, size_t size)
{
	struct ssh_transport *t = context;
	int events = dssh_chan_poll(t->sftp_channel, DSSH_POLL_WRITE, 0);
	if (events < 0) return IO_ERROR_CONNECTION_RESET;
	if ((events & DSSH_POLL_WRITE) == 0) return IO_ERROR_WOULD_BLOCK;
	int64_t n = dssh_chan_write(t->sftp_channel, 0, data, size);
	return n < 0 ? IO_ERROR_CONNECTION_RESET : (ssize_t)n;
}
static void
sftp_close_channel(void *context)
{
	struct ssh_transport *t = context;
	(void)dssh_chan_close(t->sftp_channel, -1);
	t->sftp_channel = NULL;
}
static void
sftp_log(void *context, const char *message)
{
	struct ssh_transport *t = context;
	st_connection_log_hooks(&t->ui_hooks, "SFTP", ST_CONNECTION_LOG_DEBUG, "%s", message);
}
static const struct st_sftp_provider *
ssh_sftp(void *state)
{ return st_sftp_client_provider(((struct ssh_transport *)state)->sftp); }
static bool
ssh_auxiliary_alive(void *state)
{ return !dssh_session_is_terminated(((struct ssh_transport *)state)->session); }
char *
st_connection_ssh_public_key(void)
{
	st_call_once(&init_once, initialize_mutex);
	if (!init_mutex_valid) return NULL;
	(void)st_mutex_lock(&init_mutex);
	if (!algorithms_ready || initialization_status != 0) {
		(void)st_mutex_unlock(&init_mutex);
		return NULL;
	}
	int size = dssh_ed25519_get_pub_str(NULL, 0);
	char *key = size <= 0 ? NULL : malloc((size_t)size + 1);
	if (key != NULL) {
		if (dssh_ed25519_get_pub_str(key, (size_t)size + 1) < 0) { free(key); key = NULL; }
		else key[size] = '\0';
	}
	(void)st_mutex_unlock(&init_mutex);
	return key;
}
static const struct st_connection_transport_ops operations = {
	.retain_after_eof = true, .sftp = ssh_sftp,
	.auxiliary_alive = ssh_auxiliary_alive,
	.read = ssh_read, .write = ssh_write, .interrupt = ssh_interrupt,
	.destroy = ssh_destroy, .update_terminal_size = ssh_resize,
	.process_requests = ssh_process_requests,
};

struct open_channel {
	struct ssh_transport *transport;
	const struct dssh_chan_params *params;
	atomic_bool done;
	dssh_channel channel;
};

static int
open_channel_worker(void *context)
{
	struct open_channel *open = context;
	open->channel = dssh_chan_open(open->transport->session, open->params);
	atomic_store(&open->done, true);
	return 0;
}

static int
open_shell(struct ssh_transport *t, const struct dssh_chan_params *params)
{
	struct open_channel open = {.transport = t, .params = params};
	st_thread_t thread;
	const struct timespec delay = {0, 10000000};
	int result;
	atomic_init(&open.done, false);
	if (st_thread_create(&thread, open_channel_worker, &open) != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	while (!atomic_load(&open.done)) {
		(void)ssh_process_requests(t);
		if (st_connection_cancelled(t->hooks, ST_CONNECTION_SETUP_NEGOTIATING)) {
			t->cancelled = true;
			ssh_interrupt(t);
		}
		(void)st_thread_sleep(&delay, NULL);
	}
	if (st_thread_join(thread, &result) != ST_THREAD_SUCCESS) return IO_ERROR_IO;
	t->channel = open.channel;
	return open.channel != NULL ? 0 : IO_ERROR_PROTOCOL;
}

int
st_connection_connect_ssh(st_connection_t **result, const struct st_profile *profile,
	const struct st_screen_geometry *current, const struct st_connection_hooks *hooks,
	struct st_connection_error *error)
{
	struct ssh_transport *t;
	struct st_screen_geometry geometry;
	struct dssh_chan_params params = {0};
	int status;
	bool key_rejected = false;
	const char *detail = "SSH transport negotiation failed";
	enum st_connection_setup_phase phase = ST_CONNECTION_SETUP_NEGOTIATING;
	const char *allowed[] = {"aes256-ctr"};
	if (result == NULL || profile == NULL || profile->address[0] == '\0'
	    || (profile->connection_type != ST_CONNECTION_SSH
	    && profile->connection_type != ST_CONNECTION_SSH_NO_AUTH)
	    || (profile->ssh_fingerprint_size != 0 && profile->ssh_fingerprint_size != 20
	    && profile->ssh_fingerprint_size != 32))
		return st_connection_fail(error, IO_ERROR_INVALID_ARGUMENT,
		    ST_CONNECTION_SETUP_NONE, "invalid SSH profile");
	status = initialize_algorithms(hooks == NULL ? NULL : hooks->ssh_key_file);
	if (status != 0) return st_connection_fail(error, IO_ERROR_IO,
	    ST_CONNECTION_SETUP_NONE, "DeuceSSH initialization failed");
	/* Callback registration is immutable once sessions exist. */
	status = dssh_transport_set_callbacks(ssh_tx, ssh_rx, NULL, NULL);
	if (status != 0 && status != DSSH_ERROR_TOOLATE) return IO_ERROR_IO;
	t = calloc(1, sizeof(*t));
	if (t == NULL) return IO_ERROR_OUT_OF_MEMORY;
	if (st_mutex_init(&t->request_mutex, ST_MUTEX_PLAIN) != ST_THREAD_SUCCESS) {
		free(t);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	if (st_condition_init(&t->request_changed) != ST_THREAD_SUCCESS) {
		st_mutex_destroy(&t->request_mutex);
		free(t);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	t->request_initialized = true;
	atomic_init(&t->setup_complete, false);
	atomic_init(&t->hostkey_verified, false);
	t->profile = profile;
	t->hide_popups = profile->hide_popups;
	t->hooks = hooks;
	status = st_connection_profile_geometry(profile, current, &geometry);
	if (status == 0) status = st_connection_stream_open(&t->stream, profile,
	    false, NULL, hooks, error);
	if (status != 0) { ssh_destroy(t); return status; }
	t->session = dssh_session_init(true, 0);
	if (t->session == NULL) { ssh_destroy(t); return IO_ERROR_OUT_OF_MEMORY; }
	status = dssh_session_set_timeout(t->session, 60000);
	if (status == 0) status = dssh_session_set_cbdata(t->session, t, t, t, t);
	if (status == 0) status = dssh_session_set_hostkey_verify_cb(t->session, verify_hostkey, t);
	if (status == 0) status = dssh_session_set_terminate_cb(t->session, ssh_terminated, t);
	if (status == 0) status = dssh_session_set_banner_cb(t->session, auth_banner, t);
	if (status == 0) status = dssh_session_set_debug_cb(t->session, debug_message, t);
	if (status == 0) status = dssh_session_set_log_cb(t->session, ssh_diagnostic, t);
	if (status == 0) status = dssh_session_set_log_level(t->session,
	    profile->protocol_log_level <= ST_PROFILE_LOG_ERROR ? DSSH_LOG_ERROR
	    : profile->protocol_log_level < ST_PROFILE_LOG_DEBUG ? DSSH_LOG_WARNING : DSSH_LOG_DEBUG);
	if (status == 0 && !profile->ssh_allow_aes128_cbc)
		status = dssh_session_set_enc_filter(t->session, allowed, 1);
	if (status == 0) status = dssh_transport_handshake(t->session);
	status = ssh_error(status);
	if (status == 0) {
		detail = "SSH host key was not accepted";
		status = confirm_hostkey(t);
		key_rejected = status != 0;
	}
	if (status == 0) atomic_store(&t->hostkey_verified, true);
	if (status == 0 && st_connection_cancelled(hooks, ST_CONNECTION_SETUP_AUTHENTICATING))
		status = IO_ERROR_CANCELLED;
	if (status == 0) {
		phase = ST_CONNECTION_SETUP_AUTHENTICATING;
		detail = "SSH authentication failed";
		status = authenticate(t);
	}
	if (status == 0) {
		memcpy(t->identity.ssh_fingerprint, t->fingerprint, 32);
		t->identity.ssh_fingerprint_size = 32;
		t->identity.source = profile->source;
		t->identity.hide_popups = profile->hide_popups;
		strcpy(t->identity.address, profile->address);
		if (hooks != NULL) t->ui_hooks = *hooks;
		t->hooks = &t->ui_hooks;
		atomic_store(&t->setup_complete, true);
		status = ssh_error(dssh_session_start(t->session));
	}
	if (status == 0) {
		phase = ST_CONNECTION_SETUP_NEGOTIATING;
		detail = "SSH PTY/shell setup failed";
		status = dssh_chan_params_init(&params, DSSH_CHAN_SHELL);
		if (status == 0) status = dssh_chan_params_set_pty(&params, true);
		if (status == 0) status = dssh_chan_params_set_term(&params,
		    st_connection_terminal_name(profile));
		if (status == 0) status = dssh_chan_params_add_env(&params, "TERM",
		    st_connection_terminal_name(profile));
		if (status == 0) status = dssh_chan_params_set_size(&params,
		    geometry.columns, geometry.rows, geometry.pixel_width, geometry.pixel_height);
		for (size_t i = 0; status == 0
		    && i < sizeof(default_pty_modes) / sizeof(default_pty_modes[0]); ++i)
			status = dssh_chan_params_set_mode(&params, default_pty_modes[i].op,
			    default_pty_modes[i].val);
		if (status == 0 && profile->encoding == ST_CODEPAGE_ATASCII)
			status = dssh_chan_params_set_mode(&params, 3, 0x7e);
		if (status == 0 && profile->encoding == ST_CODEPAGE_ATASCII)
			status = dssh_chan_params_set_mode(&params, 6, 0x9b);
		if (status == 0 && (profile->encoding == ST_CODEPAGE_PETSCII_UPPER
		    || profile->encoding == ST_CODEPAGE_PETSCII_LOWER))
			status = dssh_chan_params_set_mode(&params, 3, 0x14);
		if (status == 0) status = dssh_chan_params_set_mode(&params, 42,
		    profile->encoding == ST_PROFILE_ENCODING_UTF8 ? 1 : 0);
		if (status == 0) status = dssh_chan_params_set_accept_early_data(&params,
		    profile->ssh_accept_early_data);
		status = ssh_error(status);
		if (status == 0) status = open_shell(t, &params);
		dssh_chan_params_free(&params);
		if (status == 0 && t->channel == NULL) status = IO_ERROR_PROTOCOL;
	}
	if (status != 0) {
		bool cancelled = t->cancelled;
		ssh_destroy(t);
		int result_status = st_connection_fail(error, cancelled ? IO_ERROR_CANCELLED : status, phase, detail);
		bool presented = key_rejected || (!cancelled && status != IO_ERROR_CANCELLED
		    && st_connection_error_dialog(hooks, "SSH error", detail));
		if (error != NULL) error->presented = presented;
		return result_status;
	}
	t->profile = &t->identity;
	const struct st_sftp_io sftp_io = {.context = t, .open = sftp_open_channel,
	    .read = sftp_read_channel, .write = sftp_send_channel,
	    .close = sftp_close_channel, .log = sftp_log};
	status = st_sftp_client_create(&t->sftp, &sftp_io);
	if (status != 0) { ssh_destroy(t); return status; }
	return st_connection_start(result, t, &operations, "SSH", hooks, NULL, error);
}
