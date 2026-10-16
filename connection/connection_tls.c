/* TLS connection decisions, before any terminal bytes are exchanged.
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */
#include "connection_stream.h"
#include "string_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct captured_chain {
	struct st_tls_peer_certificate *certificates;
	size_t count;
};

static void
free_chain(struct captured_chain *chain)
{
	for (size_t i = 0; i < chain->count; ++i) {
		free((void *)chain->certificates[i].subject);
		free((void *)chain->certificates[i].issuer);
		free((void *)chain->certificates[i].not_before);
		free((void *)chain->certificates[i].not_after);
		free((void *)chain->certificates[i].fingerprint_sha256);
		free((void *)chain->certificates[i].pem);
	}
	free(chain->certificates);
	memset(chain, 0, sizeof(*chain));
}

static void
capture_chain(const struct st_tls_peer_certificate *certificates, size_t count,
	void *context)
{
	struct captured_chain *chain = context;
	free_chain(chain);
	chain->certificates = calloc(count, sizeof(*chain->certificates));
	if (chain->certificates == NULL) return;
	chain->count = count;
	for (size_t i = 0; i < count; ++i) {
		struct st_tls_peer_certificate *copy = &chain->certificates[i];
		copy->subject = strdup(certificates[i].subject);
		copy->issuer = strdup(certificates[i].issuer);
		copy->not_before = strdup(certificates[i].not_before);
		copy->not_after = strdup(certificates[i].not_after);
		copy->fingerprint_sha256 = strdup(certificates[i].fingerprint_sha256);
		copy->pem = strdup(certificates[i].pem);
		if (copy->subject == NULL || copy->issuer == NULL || copy->not_before == NULL
		    || copy->not_after == NULL || copy->fingerprint_sha256 == NULL || copy->pem == NULL) {
			free_chain(chain);
			return;
		}
	}
}

static bool
certificate_failure(const struct st_connection_hooks *hooks,
	const struct captured_chain *chain, const char *error)
{
	char **labels;
	char intro[1400];
	bool shown = false;
	if (chain->count == 0 || hooks->choice == NULL) return false;
	labels = calloc(chain->count + 2, sizeof(*labels));
	if (labels == NULL) return false;
	for (size_t i = 0; i < chain->count; ++i) {
		labels[i] = malloc(256);
		if (labels[i] == NULL) goto finished;
		(void)snprintf(labels[i], 256, "Certificate %zu (%s): %.160s", i + 1,
		    i == 0 ? "server" : "issuer", chain->certificates[i].subject);
	}
	labels[chain->count] = "View Protocol Log";
	labels[chain->count + 1] = "Disconnect";
	(void)snprintf(intro, sizeof(intro), "Certificate verification failed:\n\n%s\n\n"
	    "The peer presented %zu certificate%s. Select one to view it.",
	    error, chain->count, chain->count == 1 ? "" : "s");
	if (hooks->status != NULL) hooks->status(hooks->ui_context, NULL);
	for (;;) {
		int selected = hooks->choice(hooks->ui_context, "TLS Certificate Error", intro,
		    (const char *const *)labels, chain->count + 2);
		shown = true;
		if (selected >= 0 && (size_t)selected == chain->count) {
			if (hooks->help != NULL) hooks->help(hooks->ui_context, "TLS Certificate Error", error);
			continue;
		}
		if (selected < 0 || (size_t)selected >= chain->count) break;
		const struct st_tls_peer_certificate *certificate = &chain->certificates[selected];
		size_t capacity = strlen(certificate->subject) + strlen(certificate->issuer)
		    + strlen(certificate->not_before) + strlen(certificate->not_after)
		    + strlen(certificate->fingerprint_sha256) + 512;
		char *details = malloc(capacity);
		if (details == NULL) break;
		(void)snprintf(details, capacity, "Certificate %d of %zu\n\nSubject:\n%s\n\nIssuer:\n%s\n\n"
		    "Valid from: %s\nValid until: %s\n\nSHA-256 fingerprint:\n%s\n\n"
		    "This chain was supplied by a peer whose certificate could not be authenticated.",
		    selected + 1, chain->count, certificate->subject, certificate->issuer,
		    certificate->not_before, certificate->not_after, certificate->fingerprint_sha256);
		const char *choices[] = {"Back", "Trust for This Connection"};
		int action = hooks->choice(hooks->ui_context, "Presented Certificate", details,
		    choices, hooks->trust_tls_certificate == NULL ? 1 : 2);
		free(details);
		if (action == 1 && hooks->trust_tls_certificate != NULL) {
			char *filename = NULL;
			int status = hooks->trust_tls_certificate(hooks->ui_context, certificate->pem, &filename);
			if (status != 0 || filename == NULL) {
				free(filename);
				st_connection_notice(hooks, "Certificate not installed",
				    "The certificate file or dialing-directory entry could not be saved.");
				continue;
			}
			size_t size = strlen(filename) + 160;
			char *message = malloc(size);
			if (message != NULL) {
				(void)snprintf(message, size, "This connection now trusts only the selected certificate.\n\n"
				    "Saved as:\n%s\n\nReconnect to try again.", filename);
				st_connection_notice(hooks, "Certificate installed", message);
				free(message);
			}
			free(filename);
			break;
		}
	}
finished:
	for (size_t i = 0; i < chain->count; ++i) free(labels[i]);
	free(labels);
	return shown;
}

static const char *
version_name(int version)
{
	return version == ST_TLS_VERSION_1_3 ? "TLS 1.3"
	    : version == ST_TLS_VERSION_1_2 ? "TLS 1.2" : "an unknown TLS version";
}

static void
remember_version(const struct st_connection_hooks *hooks, int version)
{
	if (hooks->remember_tls_version != NULL
	    && hooks->remember_tls_version(hooks->ui_context, version) != 0)
		st_connection_notice(hooks, "TLS version not saved",
		    "The authenticated TLS version could not be saved to the dialing directory. "
		    "The protection will apply only until SyncTERM exits.");
}

int
st_connection_stream_start_tls(struct st_connection_stream *stream,
	const struct st_profile *profile, struct st_tls_client_options *options,
	struct st_connection_error *error)
{
	struct captured_chain chain = {0};
	const struct st_connection_hooks *hooks = &stream->hooks;
	bool presented = false;
	bool authenticated = options->psk == NULL && options->server_auth != ST_TLS_SERVER_AUTH_NONE;
	options->peer_chain = capture_chain;
	options->peer_chain_context = &chain;
	int status = st_tls_client_open(&stream->tls, stream->socket, options);
	if (status != 0) {
		const char *detail = stream->diagnostic[0] == '\0'
		    ? "TLS negotiation failed" : stream->diagnostic;
		/* MQTT's speculative PSK leg may retry with certificates: its caller
		 * presents a final error only after both legs have failed. */
		if (profile->connection_type != ST_CONNECTION_MQTT) {
			presented = certificate_failure(hooks, &chain, detail);
			if (!presented) presented = st_connection_error_dialog(hooks, "Error activating session", detail);
		}
		st_connection_fail(error, status, ST_CONNECTION_SETUP_NEGOTIATING, detail);
		if (error != NULL) error->presented = presented;
	} else if (authenticated) {
		enum st_tls_protocol_version negotiated = st_tls_protocol_version(stream->tls);
		int version = negotiated == ST_TLS_PROTOCOL_1_3 ? ST_TLS_VERSION_1_3
		    : negotiated == ST_TLS_PROTOCOL_1_2 ? ST_TLS_VERSION_1_2 : ST_TLS_VERSION_UNKNOWN;
		if (version != ST_TLS_VERSION_UNKNOWN && version < profile->tls_version_floor) {
			const char *choices[] = {"Disconnect", "Accept Once", "Accept and Remember", "View Protocol Log"};
			char message[640];
			int choice = 0;
			(void)snprintf(message, sizeof(message), "This server previously authenticated using %s, "
			    "but this connection authenticated using %s. This can be caused by a server "
			    "configuration change or an active downgrade attack.\n\nSyncTERM has not started Telnet I/O.\n",
			    version_name(profile->tls_version_floor), version_name(version));
			if (hooks->status != NULL) hooks->status(hooks->ui_context, NULL);
			if (hooks->choice != NULL) {
				do {
					choice = hooks->choice(hooks->ui_context, "TLS version downgraded", message, choices, 4);
					if (choice == 3 && hooks->help != NULL)
						hooks->help(hooks->ui_context, "TLS version downgraded", message);
				} while (choice == 3);
			}
			if (choice == 2) remember_version(hooks, version);
			if (choice != 1 && choice != 2) {
				status = st_connection_fail(error, IO_ERROR_CANCELLED,
				    ST_CONNECTION_SETUP_NEGOTIATING, "TLS version downgrade rejected");
				if (error != NULL) error->presented = true;
			}
		} else if (version > profile->tls_version_floor)
			remember_version(hooks, version);
	}
	free_chain(&chain);
	if (status == 0) stream->tls_ready = true;
	return status;
}

int
st_connection_stream_process_requests(struct st_connection_stream *stream)
{
	char message[sizeof(stream->diagnostic)];
	char title[sizeof(stream->error_title)];
	bool pending;
	(void)st_mutex_lock(&stream->mutex);
	pending = stream->error_pending;
	stream->error_pending = false;
	memcpy(message, stream->diagnostic, sizeof(message));
	memcpy(title, stream->error_title, sizeof(title));
	(void)st_mutex_unlock(&stream->mutex);
	if (pending)
		(void)st_connection_error_dialog(&stream->hooks, title, message);
	return 0;
}
