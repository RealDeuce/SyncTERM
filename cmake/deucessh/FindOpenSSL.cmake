# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
# Scoped to DeuceSSH: SyncTERM has already validated the OpenSSL 3 API and
# resolved its complete link contract. Do not discover a second installation.
add_library(OpenSSL::Crypto INTERFACE IMPORTED)
set_property(TARGET OpenSSL::Crypto PROPERTY
	INTERFACE_LINK_LIBRARIES "${_st_crypto_dependency}")
get_property(OPENSSL_INCLUDE_DIR TARGET "${_st_crypto_dependency}"
	PROPERTY INTERFACE_INCLUDE_DIRECTORIES)
set(OpenSSL_FOUND TRUE)
