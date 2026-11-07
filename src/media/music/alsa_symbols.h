/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#define ST_ALSA_SYMBOLS(X) \
	X(int, snd_seq_open, (snd_seq_t **, const char *, int, int)) \
	X(int, snd_seq_close, (snd_seq_t *)) \
	X(int, snd_seq_set_client_name, (snd_seq_t *, const char *)) \
	X(int, snd_seq_create_simple_port, (snd_seq_t *, const char *, unsigned, unsigned)) \
	X(int, snd_seq_connect_to, (snd_seq_t *, int, int, int)) \
	X(int, snd_seq_parse_address, (snd_seq_t *, snd_seq_addr_t *, const char *)) \
	X(int, snd_seq_event_output_direct, (snd_seq_t *, snd_seq_event_t *)) \
	X(int, snd_midi_event_new, (size_t, snd_midi_event_t **)) \
	X(void, snd_midi_event_free, (snd_midi_event_t *)) \
	X(long, snd_midi_event_encode, (snd_midi_event_t *, const unsigned char *, long, snd_seq_event_t *))
