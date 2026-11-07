/* Independent note and Standard MIDI File expectations. No device output. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_music.h"
#include "midi_file.h"
#include "io_error.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); exit(1); } } while (0)
static const uint8_t song[] = {
	'M','T','h','d',0,0,0,6,0,0,0,1,0,96,
	'M','T','r','k',0,0,0,12,
	0,0x90,60,100,96,0x80,60,0,0,0xff,0x2f,0,
};
static FILE *stream(const void *data, size_t size)
{
	FILE *f = tmpfile(); CHECK(f != NULL); CHECK(fwrite(data, 1, size, f) == size); CHECK(fflush(f) == 0); return f;
}
static void ansi(void)
{
	const char *names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	for (int pitch = 0; pitch < 84; ++pitch) {
		char text[64]; struct st_ansi_music_state state; struct st_music_note *notes; size_t count; bool fore;
		st_ansi_music_init(&state);
		CHECK(snprintf(text, sizeof(text), "MB O%d %s N%d", pitch / 12, names[pitch % 12], pitch) > 0);
		CHECK(st_ansi_music_parse(text, true, &state, &notes, &count, &fore) == 0);
		CHECK(count == 2 && notes[0].pitch == pitch && notes[1].pitch == pitch && !fore);
		CHECK(notes[0].sound_frames == 19315 && notes[0].pause_frames == 2734);
		free(notes);
	}
	CHECK(fabs(st_music_note_frequency(0) - 65.406391325) < 0.000001);
	CHECK(st_music_note_frequency(33) == 440);
	CHECK(fabs(st_music_note_frequency(83) - 7902.132820098) < 0.000001);
	struct st_ansi_music_state state; struct st_music_note *notes; size_t count; bool fore;
	st_ansi_music_init(&state);
	CHECK(st_ansi_music_parse("MB O0 C- O6 B+ N84 N2147483647+ P4", true, &state, &notes, &count, &fore) == 0);
	CHECK(count == 5); for (size_t i = 0; i < count; ++i) CHECK(notes[i].pitch == -1); free(notes);
	CHECK(st_ansi_music_parse("T200?", true, &state, &notes, &count, &fore) == IO_ERROR_INVALID_FORMAT);
	CHECK(state.tempo == 120);
	CHECK(st_ansi_music_parse("T00000000000000000120 L18446744073709551616 O4294967296", true, &state, &notes, &count, &fore) == 0);
	CHECK(state.tempo == 120 && state.length == 64 && state.octave == 6); free(notes);
	CHECK(st_ansi_music_parse("MF T120 L4 MN C. MS D ML E", true, &state, &notes, &count, &fore) == 0);
	CHECK(fore && count == 3 && notes[0].sound_frames + notes[0].pause_frames == 33074);
	CHECK(notes[1].sound_frames == 16537 && notes[1].pause_frames == 5512);
	CHECK(notes[2].sound_frames == 22050 && notes[2].pause_frames == 0); free(notes);
}
static void midi(void)
{
	struct st_midi_file *file; struct st_midi_event event;
	FILE *f = stream(song, sizeof(song));
	CHECK(st_midi_file_open(&file, f, NULL, NULL) == 0);
	CHECK(st_midi_file_next(file, &event) == 0 && event.time_ns == 0 && event.size == 3 && event.bytes[0] == 0x90);
	CHECK(st_midi_file_next(file, &event) == 0 && event.time_ns == 500000000 && event.bytes[0] == 0x80);
	CHECK(st_midi_file_next(file, &event) == 1 && event.time_ns == 500000000);
	st_midi_file_close(file); fclose(f);
	for (size_t size = 0; size < sizeof(song); ++size) {
		f = stream(song, size); CHECK(st_midi_file_open(&file, f, NULL, NULL) < 0); fclose(f);
	}
	uint8_t changed[sizeof(song)]; memcpy(changed, song, sizeof(song)); changed[9] = 2;
	f = stream(changed, sizeof(changed)); CHECK(st_midi_file_open(&file, f, NULL, NULL) == IO_ERROR_NOT_SUPPORTED); fclose(f);
	memcpy(changed, song, sizeof(song)); changed[28] = 0xff;
	f = stream(changed, sizeof(changed)); CHECK(st_midi_file_open(&file, f, NULL, NULL) < 0); fclose(f);
	uint8_t rmid[20 + sizeof(song)]; memset(rmid, 0, sizeof(rmid));
	memcpy(rmid, "RIFF", 4); rmid[4] = sizeof(rmid) - 8; memcpy(rmid + 8, "RMIDdata", 8); rmid[16] = sizeof(song); memcpy(rmid + 20, song, sizeof(song));
	f = stream(rmid, sizeof(rmid)); CHECK(st_midi_file_open(&file, f, NULL, NULL) == 0); st_midi_file_close(file); fclose(f);
	static const uint8_t multi[] = {
		'M','T','h','d',0,0,0,6,0,1,0,2,0,96,
		'M','T','r','k',0,0,0,11, 0,0xff,0x51,3,15,66,64,96,0xff,0x2f,0,
		'M','T','r','k',0,0,0,12, 0,0x90,64,100,96,0x80,64,0,0,0xff,0x2f,0,
	};
	f = stream(multi, sizeof(multi)); CHECK(st_midi_file_open(&file, f, NULL, NULL) == 0);
	CHECK(st_midi_file_next(file, &event) == 0 && event.time_ns == 0);
	CHECK(st_midi_file_next(file, &event) == 0 && event.time_ns == 1000000000);
	st_midi_file_close(file); fclose(f);
}
static void track(const uint8_t *data, size_t size, uint16_t division, int expected, uint64_t last)
{
	uint8_t header[] = {'M','T','h','d',0,0,0,6,0,0,0,1,0,0,'M','T','r','k',0,0,0,0};
	header[12] = (uint8_t)(division >> 8); header[13] = (uint8_t)division;
	for (int i = 0; i < 4; ++i) header[18 + i] = (uint8_t)(size >> ((3 - i) * 8));
	FILE *f = tmpfile(); CHECK(f != NULL);
	CHECK(fwrite(header, 1, sizeof(header), f) == sizeof(header));
	CHECK(fwrite(data, 1, size, f) == size && fflush(f) == 0);
	struct st_midi_file *parsed; int status = st_midi_file_open(&parsed, f, NULL, NULL);
	if (expected < 0) CHECK(status < 0);
	else {
		CHECK(status == 0); struct st_midi_event e; int count = 0;
		while ((status = st_midi_file_next(parsed, &e)) == 0) ++count;
		CHECK(status == 1 && count == expected && e.time_ns == last);
		st_midi_file_close(parsed);
	}
	fclose(f);
}
static void boundaries(void)
{
	/* Running status, zero-velocity Note On, and tempo at the same tick. */
	static const uint8_t running[] = {0,0x90,60,100,96,60,0,0,0xff,0x2f,0};
	track(running, sizeof(running), 96, 2, 500000000);
	/* 100 ticks at 25 frames/second * 40 ticks/frame = 100ms. */
	static const uint8_t smpte[] = {0,0x90,60,100,100,0x80,60,0,0,0xff,0x2f,0};
	track(smpte, sizeof(smpte), 0xe728, 2, 100000000);
	/* -29 denotes 30000/1001, not an approximation to 29fps. */
	track(smpte, sizeof(smpte), 0xe328, 2, 83416666);
	track(smpte, sizeof(smpte), 0xe628, -1, 0);
	track(smpte, sizeof(smpte), 0xe700, -1, 0);
	static const uint8_t sysex[] = {0,0xf0,3,0x7e,0x7f,9,0,0xf7,2,1,0xf7,
	    0,0xf7,4,0x90,64,0xf8,100,0,0xff,0x2f,0};
	track(sysex, sizeof(sysex), 96, 3, 0);
	static const uint8_t unfinished[] = {0,0xf0,2,0x7e,0x7f,0,0xff,0x2f,0};
	track(unfinished, sizeof(unfinished), 96, -1, 0);
	static const uint8_t partial[] = {0,0xf7,2,0x90,64,0,0xff,0x2f,0};
	track(partial, sizeof(partial), 96, -1, 0);
	static const uint8_t missing_status[] = {0,60,100,0,0xff,0x2f,0};
	track(missing_status, sizeof(missing_status), 96, -1, 0);
	static const uint8_t bad_vlq[] = {0xff,0xff,0xff,0xff,0,0x90,60,1};
	track(bad_vlq, sizeof(bad_vlq), 96, -1, 0);
	static const uint8_t zero_tempo[] = {0,0xff,0x51,3,0,0,0,0,0xff,0x2f,0};
	track(zero_tempo, sizeof(zero_tempo), 96, -1, 0);
	/* Sparse payload crosses the arena limit without allocating or reading
	 * tens of megabytes in the test or validation path. */
	static const uint8_t oversized[] = {'M','T','h','d',0,0,0,6,0,0,0,1,0,96,
	    'M','T','r','k',2,0,0,10,0,0xf0,0x90,0x80,0x80,0};
	FILE *f = stream(oversized, sizeof(oversized));
	CHECK(fseek(f, (long)sizeof(oversized) + 33554432L - 1, SEEK_SET) == 0);
	CHECK(fputc(0xf7, f) == 0xf7 && fwrite("\0\xff\x2f\0", 1, 4, f) == 4 && fflush(f) == 0);
	struct st_midi_file *parsed;
	CHECK(st_midi_file_open(&parsed, f, NULL, NULL) == IO_ERROR_MESSAGE_TOO_LARGE);
	fclose(f);
}
int main(void) { ansi(); midi(); boundaries(); return 0; }
