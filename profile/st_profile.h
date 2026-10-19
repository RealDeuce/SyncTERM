/* SyncTERM connection profile model. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_PROFILE_H_
#define ST_PROFILE_H_

#include "st_codepage.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define ST_PROFILE_NAME_SCALAR_MAX 30
#define ST_PROFILE_NAME_MAX (ST_PROFILE_NAME_SCALAR_MAX * 2)
#define ST_PROFILE_ADDRESS_MAX 64
#define ST_PROFILE_USER_MAX 30
#define ST_PROFILE_PASSWORD_MAX 128
#define ST_PROFILE_SYSTEM_PASSWORD_MAX 128
#define ST_PROFILE_FONT_MAX 79
#define ST_PROFILE_GHOST_PROGRAM_MAX 8
#define ST_PROFILE_COMMENT_MAX 1023
#define ST_PROFILE_WREN_SCRIPTS_MAX 1023
#define ST_PROFILE_TLS_PSK_IDENTITY_MAX 128
#define ST_PROFILE_TLS_PSK_MAX 256
#define ST_PROFILE_TERMINAL_NAME_MAX 31

enum st_profile_source {
	ST_PROFILE_SOURCE_USER,
	ST_PROFILE_SOURCE_SYSTEM,
};

enum st_connection_type {
	ST_CONNECTION_UNKNOWN,
	ST_CONNECTION_RLOGIN,
	ST_CONNECTION_RLOGIN_REVERSED,
	ST_CONNECTION_TELNET,
	ST_CONNECTION_RAW,
	ST_CONNECTION_SSH,
	ST_CONNECTION_SSH_NO_AUTH,
	ST_CONNECTION_MODEM,
	ST_CONNECTION_SERIAL,
	ST_CONNECTION_SERIAL_3WIRE,
	ST_CONNECTION_SHELL,
	ST_CONNECTION_MBBS_GHOST,
	ST_CONNECTION_TELNET_TLS,
	ST_CONNECTION_MQTT,
};

enum st_screen_mode {
	ST_SCREEN_MODE_CURRENT,
	ST_SCREEN_MODE_80X25,
	ST_SCREEN_MODE_LCD_80X25,
	ST_SCREEN_MODE_80X28,
	ST_SCREEN_MODE_80X30,
	ST_SCREEN_MODE_80X43,
	ST_SCREEN_MODE_80X50,
	ST_SCREEN_MODE_80X60,
	ST_SCREEN_MODE_132X37,
	ST_SCREEN_MODE_132X52,
	ST_SCREEN_MODE_132X25,
	ST_SCREEN_MODE_132X28,
	ST_SCREEN_MODE_132X30,
	ST_SCREEN_MODE_132X34,
	ST_SCREEN_MODE_132X43,
	ST_SCREEN_MODE_132X50,
	ST_SCREEN_MODE_132X60,
	ST_SCREEN_MODE_C64,
	ST_SCREEN_MODE_C128_40,
	ST_SCREEN_MODE_C128_80,
	ST_SCREEN_MODE_ATARI,
	ST_SCREEN_MODE_ATARI_XEP80,
	ST_SCREEN_MODE_CUSTOM,
	ST_SCREEN_MODE_EGA_80X25,
	ST_SCREEN_MODE_VGA_80X25,
	ST_SCREEN_MODE_PRESTEL,
	ST_SCREEN_MODE_BEEB,
	ST_SCREEN_MODE_ATARIST_40X25,
	ST_SCREEN_MODE_ATARIST_80X25,
	ST_SCREEN_MODE_ATARIST_80X25_MONO,
};

enum st_address_family {
	ST_ADDRESS_FAMILY_UNSPECIFIED,
	ST_ADDRESS_FAMILY_IPV4,
	ST_ADDRESS_FAMILY_IPV6,
};

enum st_rip_version {
	ST_RIP_VERSION_NONE,
	ST_RIP_VERSION_1,
	ST_RIP_VERSION_3,
};

enum st_profile_parity {
	ST_PROFILE_PARITY_NONE,
	ST_PROFILE_PARITY_EVEN,
	ST_PROFILE_PARITY_ODD,
};

enum st_profile_flow_control {
	ST_PROFILE_FLOW_RTS_CTS,
	ST_PROFILE_FLOW_XON_XOFF,
	ST_PROFILE_FLOW_RTS_CTS_XON_XOFF,
	ST_PROFILE_FLOW_NONE,
};

enum st_profile_log_level {
	ST_PROFILE_LOG_EMERGENCY,
	ST_PROFILE_LOG_ALERT,
	ST_PROFILE_LOG_CRITICAL,
	ST_PROFILE_LOG_ERROR,
	ST_PROFILE_LOG_WARNING,
	ST_PROFILE_LOG_NOTICE,
	ST_PROFILE_LOG_INFO,
	ST_PROFILE_LOG_DEBUG,
};

enum st_profile_music {
	ST_PROFILE_MUSIC_SYNCTERM,
	ST_PROFILE_MUSIC_BANSI,
	ST_PROFILE_MUSIC_ALL,
};

/* Other encoding values are canonical enum st_codepage values. */
enum { ST_PROFILE_ENCODING_UTF8 = 255 };

bool st_profile_encoding_valid(int encoding);
int st_profile_encoding_canonical(int encoding);
const char *st_profile_encoding_name(int encoding);
bool st_profile_encoding_parse(const char *name, int *encoding);

enum st_tls_version {
	ST_TLS_VERSION_UNKNOWN,
	ST_TLS_VERSION_1_2,
	ST_TLS_VERSION_1_3,
};

struct st_profile {
	/* These path fields are owned; NULL represents an absent path. */
	char *download_directory;
	char *upload_directory;
	char *log_path;
	char *tls_trusted_certificate;
	char *tls_client_certificate;
	char *tls_client_key;
	time_t added;
	time_t connected;
	uint32_t palette[16];
	uint32_t calls;
	uint32_t bps_rate;
	int32_t sort_order;
	int connection_type;
	int source;
	int identifier;
	int screen_mode;
	int encoding;
	int transfer_log_level;
	int protocol_log_level;
	int music;
	int address_family;
	int rip_version;
	int flow_control;
	int tls_psk_version;
	int tls_version_floor;
	int parity;
	unsigned palette_size;
	uint16_t port;
	uint16_t stop_bits;
	uint16_t data_bits;
	char name[ST_PROFILE_NAME_MAX + 1];
	char address[ST_PROFILE_ADDRESS_MAX + 1];
	char username[ST_PROFILE_USER_MAX + 1];
	char password[ST_PROFILE_PASSWORD_MAX + 1];
	char system_password[ST_PROFILE_SYSTEM_PASSWORD_MAX + 1];
	char font[ST_PROFILE_FONT_MAX + 1];
	char ghost_program[ST_PROFILE_GHOST_PROGRAM_MAX + 1];
	char comment[ST_PROFILE_COMMENT_MAX + 1];
	char wren_scripts[ST_PROFILE_WREN_SCRIPTS_MAX + 1];
	char tls_psk_identity[ST_PROFILE_TLS_PSK_IDENTITY_MAX + 1];
	char tls_psk[ST_PROFILE_TLS_PSK_MAX + 1];
	char terminal_name[ST_PROFILE_TERMINAL_NAME_MAX + 1];
	uint8_t ssh_fingerprint[32];
	uint8_t ssh_fingerprint_size;
	bool hide_status;
	bool append_log;
	bool hide_popups;
	bool force_lcf;
	bool yellow_is_yellow;
	bool sftp_public_key;
	bool ssh_allow_aes128_cbc;
	bool ssh_accept_early_data;
	bool telnet_no_binary;
	bool defer_telnet_negotiation;
	bool tls_trust_web_pki;
	bool expand_lf;
};

/* Returns zero for connection types without an inherent network port. */
uint16_t st_connection_default_port(enum st_connection_type type);
void st_profile_init(struct st_profile *profile);
void st_profile_dispose(struct st_profile *profile);
/* Result must previously have been initialized; it is replaced on success. */
bool st_profile_copy(struct st_profile *result,
	const struct st_profile *source);

const char *st_profile_ghost_program(const struct st_profile *profile);
bool st_profile_script_name_valid(const char *name, size_t length);
/* Append one name to a canonical comma list, retaining first occurrence order.
 * Failure leaves the list unchanged. */
bool st_profile_scripts_append(char *scripts, const char *name, size_t length);
/* ASCII whitespace is trimmed per item. Invalid input leaves result unchanged. */
bool st_profile_scripts_parse(char *result, const char *input);

#endif
