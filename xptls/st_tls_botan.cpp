/* Botan client-only TLS sessions for SyncTERM. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "st_tls.h"

#include "io_error.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <botan/auto_rng.h>
#include <botan/certstor.h>
#include <botan/certstor_system.h>
#include <botan/credentials_manager.h>
#include <botan/data_src.h>
#include <botan/pem.h>
#include <botan/pkcs8.h>
#include <botan/secmem.h>
#include <botan/tls_alert.h>
#include <botan/tls_callbacks.h>
#include <botan/tls_client.h>
#include <botan/tls_external_psk.h>
#include <botan/tls_policy.h>
#include <botan/tls_server_info.h>
#include <botan/tls_session.h>
#include <botan/tls_session_manager_noop.h>
#include <botan/x509path.h>

class ClientPolicy : public Botan::TLS::Policy {
public:
	ClientPolicy(enum st_tls_protocol_version minimum,
	    enum st_tls_protocol_version maximum)
	    : minimum_(minimum == ST_TLS_PROTOCOL_DEFAULT
	        ? ST_TLS_PROTOCOL_1_2 : minimum), maximum_(maximum) {}
	bool require_cert_revocation_info() const override { return false; }
	bool allow_tls12() const override
	{
		return minimum_ <= ST_TLS_PROTOCOL_1_2;
	}
	bool allow_tls13() const override
	{
		return minimum_ <= ST_TLS_PROTOCOL_1_3
		    && (maximum_ == ST_TLS_PROTOCOL_DEFAULT
		    || maximum_ >= ST_TLS_PROTOCOL_1_3);
	}
private:
	enum st_tls_protocol_version minimum_;
	enum st_tls_protocol_version maximum_;
};

class PskPolicy : public Botan::TLS::Policy {
public:
	PskPolicy(enum st_tls_protocol_version version,
	    enum st_tls_psk_policy policy)
	    : version_(version), policy_(policy) {}
	bool allow_tls12() const override { return version_ == ST_TLS_PROTOCOL_1_2; }
	bool allow_tls13() const override { return version_ == ST_TLS_PROTOCOL_1_3; }
	Botan::TLS::Protocol_Version latest_supported_version(bool datagram) const override
	{
		if (version_ == ST_TLS_PROTOCOL_1_3)
			return datagram ? Botan::TLS::Protocol_Version::DTLS_V13
			    : Botan::TLS::Protocol_Version::TLS_V13;
		return datagram ? Botan::TLS::Protocol_Version::DTLS_V12
		    : Botan::TLS::Protocol_Version::TLS_V12;
	}
	std::vector<std::string> allowed_key_exchange_methods() const override
	{
		if (version_ == ST_TLS_PROTOCOL_1_3)
			return Botan::TLS::Policy::allowed_key_exchange_methods();
		if (policy_ == ST_TLS_PSK_TLS12_COMPATIBILITY)
			return { "DHE_PSK", "ECDHE_PSK", "PSK" };
		return { "DHE_PSK", "ECDHE_PSK" };
	}
	std::vector<std::string> allowed_signature_methods() const override
	{
		return version_ == ST_TLS_PROTOCOL_1_3
		    ? Botan::TLS::Policy::allowed_signature_methods()
		    : std::vector<std::string>{ "IMPLICIT" };
	}
	std::vector<Botan::TLS::Group_Params> key_exchange_groups_to_offer() const override
	{
		if (version_ == ST_TLS_PROTOCOL_1_3)
			return { Botan::TLS::Group_Params::X25519,
			    Botan::TLS::Group_Params::SECP256R1 };
		return Botan::TLS::Policy::key_exchange_groups_to_offer();
	}
	std::vector<std::string> allowed_ciphers() const override
	{
		if (policy_ == ST_TLS_PSK_MODERN)
			return { "AES-128/GCM", "AES-256/GCM", "ChaCha20Poly1305" };
		return { "AES-128", "AES-256", "AES-128/GCM", "AES-256/GCM",
		    "ChaCha20Poly1305" };
	}
	std::vector<std::string> allowed_macs() const override
	{
		return policy_ == ST_TLS_PSK_MODERN
		    ? std::vector<std::string>{ "AEAD" }
		    : std::vector<std::string>{ "SHA-256", "SHA-384", "AEAD",
		      "SHA-1" };
	}
private:
	enum st_tls_protocol_version version_;
	enum st_tls_psk_policy policy_;
};

class ClientCredentials : public Botan::Credentials_Manager {
public:
	ClientCredentials(const struct st_tls_client_options& options)
	{
		if (options.psk != nullptr) {
			psk_identity_ = options.psk_identity;
			const auto *secret = static_cast<const uint8_t *>(options.psk);
			psk_.assign(secret, secret + options.psk_size);
		} else {
			if (options.server_auth == ST_TLS_SERVER_AUTH_WEB_PKI)
				system_store_ =
				    std::make_unique<Botan::System_Certificate_Store>();
			else if (options.server_auth
			    == ST_TLS_SERVER_AUTH_CERTIFICATE) {
				custom_certificate_.emplace(
				    options.trusted_certificate_file);
				custom_store_ = std::make_unique<
				    Botan::Certificate_Store_In_Memory>(
				    *custom_certificate_);
			}
			if (options.client_certificate_file != nullptr) {
				Botan::DataSource_Stream source(
				    options.client_certificate_file);
				while (Botan::PEM_Code::matches(source))
					client_chain_.emplace_back(source);
				if (client_chain_.empty())
					throw std::runtime_error(
					    "client certificate file is empty");
				Botan::DataSource_Stream key_source(
				    options.client_key_file);
				client_key_ = Botan::PKCS8::load_key(key_source);
			}
		}
	}

	std::vector<Botan::Certificate_Store *> trusted_certificate_authorities(
	    const std::string&, const std::string&) override
	{
		std::vector<Botan::Certificate_Store *> result;
		if (system_store_)
			result.push_back(system_store_.get());
		if (custom_store_)
			result.push_back(custom_store_.get());
		return result;
	}

	std::vector<Botan::X509_Certificate> find_cert_chain(
	    const std::vector<std::string>& key_types,
	    const std::vector<Botan::AlgorithmIdentifier>&,
	    const std::vector<Botan::X509_DN>&, const std::string& type,
	    const std::string&) override
	{
		if (type != "tls-client" || !client_key_)
			return {};
		if (!key_types.empty() && std::find(key_types.begin(), key_types.end(),
		    client_key_->algo_name()) == key_types.end())
			return {};
		return client_chain_;
	}

	std::shared_ptr<Botan::Private_Key> private_key_for(
	    const Botan::X509_Certificate&, const std::string& type,
	    const std::string&) override
	{
		return type == "tls-client" ? client_key_ : nullptr;
	}

	std::string psk_identity(const std::string&, const std::string&,
	    const std::string&) override { return psk_identity_; }
	Botan::SymmetricKey psk(const std::string&, const std::string&,
	    const std::string&) override
	{
		return Botan::SymmetricKey(psk_.data(), psk_.size());
	}
	std::vector<Botan::TLS::ExternalPSK> find_preshared_keys(
	    std::string_view, Botan::TLS::Connection_Side side,
	    const std::vector<std::string>& identities,
	    const std::optional<std::string>& prf) override
	{
		if (psk_.empty() || side != Botan::TLS::Connection_Side::Client
		    || (prf && *prf != "SHA-256")
		    || (!identities.empty() && std::find(identities.begin(),
		    identities.end(), psk_identity_) == identities.end()))
			return {};
		Botan::secure_vector<uint8_t> secret(psk_.begin(), psk_.end());
		std::vector<Botan::TLS::ExternalPSK> result;
		result.emplace_back(psk_identity_, "SHA-256", std::move(secret));
		return result;
	}
private:
	std::unique_ptr<Botan::System_Certificate_Store> system_store_;
	std::unique_ptr<Botan::Certificate_Store_In_Memory> custom_store_;
	std::optional<Botan::X509_Certificate> custom_certificate_;
	std::vector<Botan::X509_Certificate> client_chain_;
	std::shared_ptr<Botan::Private_Key> client_key_;
	std::string psk_identity_;
	Botan::secure_vector<uint8_t> psk_;
};

struct st_tls_session {
	socket_handle_t socket;
	std::deque<uint8_t> plaintext;
	std::deque<uint8_t> ciphertext;
	std::shared_ptr<Botan::AutoSeeded_RNG> random;
	std::shared_ptr<Botan::TLS::Session_Manager> sessions;
	std::shared_ptr<Botan::Credentials_Manager> credentials;
	std::shared_ptr<Botan::TLS::Policy> policy;
	std::shared_ptr<Botan::TLS::Callbacks> callbacks;
	std::unique_ptr<Botan::TLS::Channel> channel;
	std::mutex read_mutex;
	std::mutex write_mutex;
	std::mutex provider_mutex;
	std::atomic_bool terminating{false};
	bool peer_closed = false;
	bool fatal = false;
	bool psk_used = false;
	enum st_tls_protocol_version version = ST_TLS_PROTOCOL_DEFAULT;
	std::string cipher;
	std::string error;
	std::vector<std::vector<uint8_t>> peer_certificates;
	st_tls_diagnostic_fn diagnostic = nullptr;
	void *diagnostic_context = nullptr;
	st_tls_peer_chain_fn peer_chain = nullptr;
	void *peer_chain_context = nullptr;
};

static void
report(st_tls_session *session, int status, bool fatal, const char *message)
{
	session->error = message;
	if (session->diagnostic != nullptr) {
		const struct st_tls_diagnostic record = {
			status, 0, fatal, "Botan", session->error.c_str()
		};
		session->diagnostic(&record, session->diagnostic_context);
	}
}

static std::string
readable_time(const Botan::ASN1_Time& time)
{
	auto text = time.readable_string();
	if (text.size() >= 10 && text[4] == '/' && text[7] == '/') {
		text[4] = '-'; text[7] = '-';
	}
	return text;
}

class Callbacks : public Botan::TLS::Callbacks {
public:
	Callbacks(st_tls_session *session, bool verify, bool partial_chain)
	    : session_(session), verify_(verify), partial_chain_(partial_chain) {}
	void tls_emit_data(std::span<const uint8_t> data) override
	{
		session_->ciphertext.insert(session_->ciphertext.end(), data.begin(),
		    data.end());
	}
	void tls_record_received(uint64_t, std::span<const uint8_t> data) override
	{
		session_->plaintext.insert(session_->plaintext.end(), data.begin(),
		    data.end());
	}
	void tls_session_established(const Botan::TLS::Session_Summary& summary) override
	{
		session_->psk_used = summary.psk_used();
		session_->cipher = summary.ciphersuite().to_string();
		if (summary.version() == Botan::TLS::Protocol_Version::TLS_V12)
			session_->version = ST_TLS_PROTOCOL_1_2;
		else if (summary.version() == Botan::TLS::Protocol_Version::TLS_V13)
			session_->version = ST_TLS_PROTOCOL_1_3;
	}
	void tls_alert(Botan::TLS::Alert alert) override
	{
		if (alert.type() == Botan::TLS::AlertType::CloseNotify)
			session_->peer_closed = true;
		if (alert.is_fatal()) {
			session_->fatal = true;
			report(session_, IO_ERROR_PROTOCOL, true,
			    alert.type_string().c_str());
		}
	}
	void tls_verify_cert_chain(
	    const std::vector<Botan::X509_Certificate>& certificates,
	    const std::vector<std::optional<Botan::OCSP::Response>>& ocsp,
	    const std::vector<Botan::Certificate_Store *>& roots,
	    Botan::Usage_Type usage, std::string_view hostname,
	    const Botan::TLS::Policy& policy) override
	{
		session_->peer_certificates.clear();
		session_->peer_certificates.reserve(certificates.size());
		for (const auto& certificate : certificates)
			session_->peer_certificates.push_back(certificate.BER_encode());
		if (session_->peer_chain != nullptr) {
			struct Text { std::string subject, issuer, before, after, fingerprint, pem; };
			std::vector<Text> text;
			std::vector<struct st_tls_peer_certificate> records;
			text.reserve(certificates.size());
			records.reserve(certificates.size());
			for (const auto& certificate : certificates)
				text.push_back({certificate.subject_dn().to_string(),
				    certificate.issuer_dn().to_string(),
				    readable_time(certificate.not_before()), readable_time(certificate.not_after()),
				    certificate.fingerprint("SHA-256"), certificate.PEM_encode()});
			for (const auto& item : text)
				records.push_back({item.subject.c_str(), item.issuer.c_str(),
				    item.before.c_str(), item.after.c_str(), item.fingerprint.c_str(), item.pem.c_str()});
			session_->peer_chain(records.data(), records.size(), session_->peer_chain_context);
		}
		if (verify_ && partial_chain_) {
			Botan::Path_Validation_Restrictions restrictions(
			    policy.require_cert_revocation_info(), policy.minimum_signature_strength(),
			    false, std::chrono::seconds::zero(),
			    std::make_unique<Botan::Certificate_Store_In_Memory>(), false, false);
			auto result = Botan::x509_path_validate(certificates, restrictions, roots, hostname,
			    usage, std::chrono::system_clock::now(), std::chrono::milliseconds(0), ocsp);
			if (!result.successful_validation()) throw std::runtime_error(result.result_string());
		} else if (verify_)
			Botan::TLS::Callbacks::tls_verify_cert_chain(certificates, ocsp,
			    roots, usage, hostname, policy);
	}
private:
	st_tls_session *session_;
	bool verify_;
	bool partial_chain_;
};

class Deadline {
public:
	explicit Deadline(int64_t milliseconds)
	    : infinite_(milliseconds < 0), end_(std::chrono::steady_clock::now())
	{
		if (!infinite_)
			end_ += std::chrono::milliseconds(milliseconds);
	}
	int64_t remaining() const
	{
		if (infinite_)
			return -1;
		auto value = std::chrono::duration_cast<std::chrono::milliseconds>(
		    end_ - std::chrono::steady_clock::now()).count();
		return value <= 0 ? 0 : value;
	}
private:
	bool infinite_;
	std::chrono::steady_clock::time_point end_;
};

static int
flush_pending(st_tls_session *session, Deadline& deadline)
{
	while (!session->ciphertext.empty()) {
		unsigned events;
		int status = socket_wait(session->socket, SOCKET_WAIT_WRITABLE,
		    deadline.remaining(), &events);
		if (status < 0)
			return status;
		std::vector<uint8_t> pending(session->ciphertext.begin(),
		    session->ciphertext.end());
		ssize_t sent = socket_send(session->socket, pending.data(),
		    pending.size());
		if (sent == IO_ERROR_WOULD_BLOCK)
			continue;
		if (sent <= 0)
			return sent == 0 ? IO_ERROR_CONNECTION_RESET : (int)sent;
		for (ssize_t i = 0; i < sent; ++i)
			session->ciphertext.pop_front();
	}
	return 0;
}

static int
pump(st_tls_session *session, Deadline& deadline)
{
	uint8_t buffer[16384];
	unsigned events;
	int status = socket_wait(session->socket, SOCKET_WAIT_READABLE,
	    deadline.remaining(), &events);
	ssize_t received;

	if (status < 0)
		return status;
	received = socket_receive(session->socket, buffer, sizeof(buffer),
	    SOCKET_RECEIVE_NONE);
	if (received == IO_ERROR_WOULD_BLOCK)
		return IO_ERROR_TIMED_OUT;
	if (received <= 0)
		return received == 0 ? IO_ERROR_CONNECTION_RESET : (int)received;
	try {
		session->channel->received_data(
		    std::span(buffer, static_cast<size_t>(received)));
	} catch (const std::exception& exception) {
		report(session, IO_ERROR_PROTOCOL, true, exception.what());
		return IO_ERROR_PROTOCOL;
	}
	if (session->fatal)
		return IO_ERROR_PROTOCOL;
	return 0;
}

extern "C" int
st_tls_client_open(st_tls_session_t **result, socket_handle_t socket,
	const struct st_tls_client_options *options)
{
	std::unique_ptr<st_tls_session> session;
	bool psk;
	bool client_certificate;
	int status;

	if (result == nullptr || options == nullptr
	    || !socket_handle_is_valid(socket))
		return IO_ERROR_INVALID_ARGUMENT;
	if (static_cast<unsigned>(options->server_auth) > ST_TLS_SERVER_AUTH_UNTRUSTED)
		return IO_ERROR_INVALID_ARGUMENT;
	*result = nullptr;
	psk = options->psk != nullptr || options->psk_size != 0
	    || options->psk_identity != nullptr;
	if (psk && (options->psk == nullptr || options->psk_size == 0
	    || options->psk_identity == nullptr
	    || options->psk_identity[0] == '\0'
	    || (options->psk_version != ST_TLS_PROTOCOL_1_2
	    && options->psk_version != ST_TLS_PROTOCOL_1_3)))
		return IO_ERROR_INVALID_ARGUMENT;
	client_certificate = options->client_certificate_file != nullptr
	    || options->client_key_file != nullptr;
	if (!psk && client_certificate
	    && (options->client_certificate_file == nullptr
	    || options->client_key_file == nullptr))
		return IO_ERROR_INVALID_ARGUMENT;
	if (!psk && options->server_auth == ST_TLS_SERVER_AUTH_CERTIFICATE
	    && options->trusted_certificate_file == nullptr)
		return IO_ERROR_INVALID_ARGUMENT;
	if (!psk && options->server_auth != ST_TLS_SERVER_AUTH_NONE
	    && (options->server_name == nullptr || options->server_name[0] == '\0'))
		return IO_ERROR_INVALID_ARGUMENT;
	try {
		session = std::make_unique<st_tls_session>();
		session->socket = socket;
		session->diagnostic = options->diagnostic;
		session->diagnostic_context = options->diagnostic_context;
		session->peer_chain = options->peer_chain;
		session->peer_chain_context = options->peer_chain_context;
		session->random = std::make_shared<Botan::AutoSeeded_RNG>();
		session->sessions =
		    std::make_shared<Botan::TLS::Session_Manager_Noop>();
		session->credentials = std::make_shared<ClientCredentials>(*options);
		if (psk)
			session->policy = std::make_shared<PskPolicy>(
			    options->psk_version, options->psk_policy);
		else
			session->policy = std::make_shared<ClientPolicy>(
			    options->minimum_version, options->maximum_version);
		session->callbacks = std::make_shared<Callbacks>(session.get(),
		    !psk && options->server_auth != ST_TLS_SERVER_AUTH_NONE,
		    !psk && options->server_auth == ST_TLS_SERVER_AUTH_CERTIFICATE);
		Botan::TLS::Server_Information server(options->server_name == nullptr
		    ? "" : options->server_name);
		Botan::TLS::Protocol_Version offered =
		    (psk ? options->psk_version : options->maximum_version)
		    == ST_TLS_PROTOCOL_1_2
		    ? Botan::TLS::Protocol_Version::TLS_V12
		    : Botan::TLS::Protocol_Version::TLS_V13;
		session->channel = std::make_unique<Botan::TLS::Client>(
		    session->callbacks, session->sessions, session->credentials,
		    session->policy, session->random, server, offered);
	} catch (const std::bad_alloc&) {
		return IO_ERROR_OUT_OF_MEMORY;
	} catch (const std::exception& exception) {
		if (options->diagnostic != nullptr) {
			const struct st_tls_diagnostic diagnostic = {
				IO_ERROR_PROTOCOL, 0, true, "Botan", exception.what()
			};
			options->diagnostic(&diagnostic, options->diagnostic_context);
		}
		return IO_ERROR_PROTOCOL;
	}
	status = socket_set_blocking(socket, false);
	if (status < 0)
		return status;
	Deadline deadline(options->handshake_timeout_ms == 0
	    ? 30000 : options->handshake_timeout_ms);
	while (!session->channel->is_active()) {
		status = flush_pending(session.get(), deadline);
		if (status == 0)
			status = pump(session.get(), deadline);
		if (status < 0) {
			(void)socket_set_blocking(socket, true);
			return status;
		}
	}
	status = flush_pending(session.get(), deadline);
	if (status < 0 && status != IO_ERROR_TIMED_OUT) {
		(void)socket_set_blocking(socket, true);
		return status;
	}
	if (psk && !session->psk_used) {
		(void)socket_set_blocking(socket, true);
		return IO_ERROR_PROTOCOL;
	}
	session->peer_chain = nullptr;
	session->peer_chain_context = nullptr;
	*result = session.release();
	return 0;
}

extern "C" int
st_tls_send(st_tls_session_t *session, const void *data, size_t size,
	int64_t timeout_ms, size_t *sent)
{
	if (sent != nullptr)
		*sent = 0;
	if (session == nullptr || (data == nullptr && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	std::lock_guard<std::mutex> guard(session->write_mutex);
	if (session->terminating)
		return IO_ERROR_NOT_CONNECTED;
	Deadline deadline(timeout_ms);
	int status = flush_pending(session, deadline);
	if (status < 0)
		return status;
	try {
		std::lock_guard<std::mutex> provider(session->provider_mutex);
		session->channel->send(std::span(
		    static_cast<const uint8_t *>(data), size));
	} catch (const std::exception& exception) {
		report(session, IO_ERROR_PROTOCOL, true, exception.what());
		return IO_ERROR_PROTOCOL;
	}
	if (sent != nullptr)
		*sent = size;
	status = flush_pending(session, deadline);
	return status == IO_ERROR_TIMED_OUT ? 0 : status;
}

extern "C" int
st_tls_receive(st_tls_session_t *session, void *data, size_t size,
	int64_t timeout_ms, size_t *received)
{
	if (received != nullptr)
		*received = 0;
	if (session == nullptr || (data == nullptr && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	std::lock_guard<std::mutex> guard(session->read_mutex);
	Deadline deadline(timeout_ms);
	while (session->plaintext.empty()) {
		if (session->peer_closed)
			return IO_ERROR_NOT_CONNECTED;
		int status = pump(session, deadline);
		if (status < 0)
			return status;
		status = flush_pending(session, deadline);
		if (status < 0 && session->plaintext.empty())
			return status;
	}
	size_t count = std::min(size, session->plaintext.size());
	for (size_t i = 0; i < count; ++i) {
		static_cast<uint8_t *>(data)[i] = session->plaintext.front();
		session->plaintext.pop_front();
	}
	if (received != nullptr)
		*received = count;
	return 0;
}

extern "C" int
st_tls_flush(st_tls_session_t *session, int64_t timeout_ms)
{
	if (session == nullptr)
		return IO_ERROR_INVALID_ARGUMENT;
	std::lock_guard<std::mutex> guard(session->write_mutex);
	Deadline deadline(timeout_ms);
	return flush_pending(session, deadline);
}

extern "C" bool st_tls_has_pending(const st_tls_session_t *session)
{ return session != nullptr && !session->plaintext.empty(); }
extern "C" enum st_tls_protocol_version st_tls_protocol_version(
	const st_tls_session_t *session)
{ return session == nullptr ? ST_TLS_PROTOCOL_DEFAULT : session->version; }
extern "C" const char *st_tls_cipher(const st_tls_session_t *session)
{ return session == nullptr || session->cipher.empty() ? nullptr : session->cipher.c_str(); }
extern "C" const char *st_tls_error(const st_tls_session_t *session)
{ return session == nullptr ? "invalid TLS session" : session->error.c_str(); }
extern "C" size_t st_tls_peer_certificate_count(
	const st_tls_session_t *session)
{ return session == nullptr ? 0 : session->peer_certificates.size(); }

extern "C" int
st_tls_peer_certificate(const st_tls_session_t *session, size_t index,
	const void **data, size_t *size)
{
	if (session == nullptr || data == nullptr || size == nullptr)
		return IO_ERROR_INVALID_ARGUMENT;
	if (index >= session->peer_certificates.size())
		return IO_ERROR_OUT_OF_RANGE;
	*data = session->peer_certificates[index].data();
	*size = session->peer_certificates[index].size();
	return 0;
}

extern "C" int
st_tls_terminate(st_tls_session_t *session)
{
	if (session == nullptr)
		return IO_ERROR_INVALID_ARGUMENT;
	if (!session->terminating.exchange(true)) {
		try {
			std::lock_guard<std::mutex> provider(session->provider_mutex);
			if (!session->channel->is_closed())
				session->channel->close();
		} catch (...) {
		}
		(void)socket_shutdown(session->socket, SOCKET_SHUTDOWN_BOTH);
	}
	return 0;
}

extern "C" void
st_tls_close(st_tls_session_t *session)
{
	if (session == nullptr)
		return;
	if (!session->terminating) {
		try {
			if (!session->channel->is_closed())
				session->channel->close();
			Deadline deadline(0);
			(void)flush_pending(session, deadline);
		} catch (...) {
		}
	}
	(void)socket_close(session->socket);
	delete session;
}
