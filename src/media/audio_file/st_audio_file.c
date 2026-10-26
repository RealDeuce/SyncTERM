/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_audio_file.h"
#include "st_audio_file_config.h"
#include "io_error.h"

#include <stdlib.h>
#include <string.h>

bool
st_audio_file_supported(void)
{
	return ST_AUDIO_FILE_HAVE_SNDFILE != 0;
}

#if ST_AUDIO_FILE_HAVE_SNDFILE
#include "dynamic_library.h"
#include "filesystem.h"
#include "st_threads.h"

#include <sndfile.h>

struct sndfile_api {
	SNDFILE *(*open_virtual)(SF_VIRTUAL_IO *, int, SF_INFO *, void *);
	sf_count_t (*read_frames)(SNDFILE *, short *, sf_count_t);
	int (*close_file)(SNDFILE *);
	int (*format_check)(const SF_INFO *);
};
static struct sndfile_api api;
static dynamic_library_t *library;
static st_once_flag_t load_once = ST_ONCE_FLAG_INIT;

static void
load_sndfile(void)
{
	const char *names[] = {"sndfile", "libsndfile"};

	if (dynamic_library_open(&library, names, 2, 1, DYNAMIC_LIBRARY_LOCAL) != 0)
		return;
	api.open_virtual = (SNDFILE *(*)(SF_VIRTUAL_IO *, int, SF_INFO *, void *))
	    dynamic_library_symbol(library, "sf_open_virtual");
	api.read_frames = (sf_count_t (*)(SNDFILE *, short *, sf_count_t))
	    dynamic_library_symbol(library, "sf_readf_short");
	api.close_file = (int (*)(SNDFILE *))dynamic_library_symbol(library, "sf_close");
	api.format_check = (int (*)(const SF_INFO *))
	    dynamic_library_symbol(library, "sf_format_check");
	if (api.open_virtual == NULL || api.read_frames == NULL
	    || api.close_file == NULL || api.format_check == NULL) {
		(void)dynamic_library_close(library);
		library = NULL;
		memset(&api, 0, sizeof(api));
	}
}

bool
st_audio_file_available(void)
{
	st_call_once(&load_once, load_sndfile);
	return library != NULL;
}

bool
st_audio_file_format_available(uint32_t major, uint32_t subtype)
{
	SF_INFO info = {.samplerate = 44100, .channels = 2};

	if (!st_audio_file_available() || major == 0 || major > 0x0fff
	    || subtype == 0 || subtype > 0xffff)
		return false;
	info.format = (int)((major << 16) | subtype);
	return api.format_check(&info) != 0;
}

struct audio_source {
	const uint8_t *memory;
	FILE *stream;
	sf_count_t length;
	sf_count_t position;
};

static sf_count_t
source_length(void *context)
{
	return ((struct audio_source *)context)->length;
}

static sf_count_t
source_seek(sf_count_t offset, int whence, void *context)
{
	struct audio_source *source = context;
	sf_count_t base, position;

	if (whence == SEEK_SET) base = 0;
	else if (whence == SEEK_CUR) base = source->position;
	else if (whence == SEEK_END) base = source->length;
	else return -1;
	if (offset < -base || offset > source->length - base)
		return -1;
	position = base + offset;
	if (source->stream != NULL && fs_stream_seek(source->stream, position) != 0)
		return -1;
	source->position = position;
	return position;
}

static sf_count_t
source_read(void *destination, sf_count_t count, void *context)
{
	struct audio_source *source = context;
	size_t size;

	if (count <= 0)
		return 0;
	if (count > source->length - source->position)
		count = source->length - source->position;
	size = (uint64_t)count > SIZE_MAX ? SIZE_MAX : (size_t)count;
	if (source->stream != NULL)
		size = fread(destination, 1, size, source->stream);
	else if (size != 0)
		memcpy(destination, source->memory + (size_t)source->position, size);
	source->position += (sf_count_t)size;
	return (sf_count_t)size;
}

static sf_count_t
source_write(const void *data, sf_count_t count, void *context)
{
	(void)data; (void)count; (void)context;
	return 0;
}

static sf_count_t
source_position(void *context)
{
	return ((struct audio_source *)context)->position;
}

static int16_t
interpolate(int16_t a, int16_t b, uint32_t fraction)
{
	int64_t product = (int64_t)((int32_t)b - a) * fraction;
	/* The reference uses arithmetic right shift. Specify its floor
	 * rounding explicitly instead of depending on signed >> semantics. */
	int64_t delta = product >= 0 ? product / INT64_C(4294967296)
	    : -((-product + INT64_C(4294967295)) / INT64_C(4294967296));

	return (int16_t)(a + delta);
}

static int
convert(int16_t **result, size_t *result_count, const int16_t *raw,
	size_t count, unsigned channels, unsigned rate)
{
	uint64_t scaled, output_count;
	int16_t *output;

	if (count > (UINT64_MAX - rate / 2) / 44100)
		return IO_ERROR_OUT_OF_RANGE;
	scaled = (uint64_t)count * 44100 + rate / 2;
	output_count = scaled / rate;
	if (output_count == 0) output_count = 1;
	if (output_count > SIZE_MAX / (2 * sizeof(int16_t))
	    || output_count > UINT64_MAX / rate)
		return IO_ERROR_OUT_OF_RANGE;
	output = malloc((size_t)output_count * 2 * sizeof(int16_t));
	if (output == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	for (size_t j = 0; j < (size_t)output_count; ++j) {
		uint64_t position = (uint64_t)j * rate;
		size_t index = (size_t)(position / 44100);
		uint32_t fraction = (uint32_t)(((position % 44100) << 32) / 44100);
		size_t next, left, right;

		if (index >= count) { index = count - 1; fraction = 0; }
		next = index + 1 < count ? index + 1 : index;
		left = index * channels;
		right = left + (channels >= 2 ? 1 : 0);
		output[j * 2] = interpolate(raw[left], raw[next * channels], fraction);
		output[j * 2 + 1] = interpolate(raw[right],
		    raw[next * channels + (channels >= 2 ? 1 : 0)], fraction);
	}
	*result = output;
	*result_count = (size_t)output_count;
	return 0;
}

static int
decode(int16_t **frames, size_t *frame_count, struct audio_source *source)
{
	SF_VIRTUAL_IO io = {
		.get_filelen = source_length, .seek = source_seek,
		.read = source_read, .write = source_write, .tell = source_position,
	};
	SF_INFO info = {0};
	SNDFILE *file;
	int16_t *raw;
	sf_count_t count;
	int status;

	if (!st_audio_file_available())
		return IO_ERROR_NOT_SUPPORTED;
	file = api.open_virtual(&io, SFM_READ, &info, source);
	if (file == NULL)
		return IO_ERROR_INVALID_FORMAT;
	if (info.frames <= 0 || info.channels <= 0 || info.samplerate <= 0
	    || (uint64_t)info.frames > SIZE_MAX / sizeof(int16_t) / (unsigned)info.channels) {
		(void)api.close_file(file);
		return IO_ERROR_INVALID_FORMAT;
	}
	raw = malloc((size_t)info.frames * (unsigned)info.channels * sizeof(int16_t));
	if (raw == NULL) {
		(void)api.close_file(file);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	count = api.read_frames(file, raw, info.frames);
	(void)api.close_file(file);
	status = count > 0 && count <= info.frames
	    ? convert(frames, frame_count, raw, (size_t)count,
	        (unsigned)info.channels, (unsigned)info.samplerate)
	    : IO_ERROR_INVALID_FORMAT;
	free(raw);
	return status;
}

int
st_audio_file_decode_memory(int16_t **frames, size_t *frame_count,
	const void *data, size_t size)
{
	struct audio_source source = {.memory = data, .length = (sf_count_t)size};

	if (frames == NULL || frame_count == NULL || data == NULL || size > INT64_MAX)
		return IO_ERROR_INVALID_ARGUMENT;
	return decode(frames, frame_count, &source);
}

int
st_audio_file_decode_stream(int16_t **frames, size_t *frame_count, FILE *stream)
{
	struct audio_source source = {.stream = stream};
	int64_t size;
	int status;

	if (frames == NULL || frame_count == NULL || stream == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (!st_audio_file_available())
		return IO_ERROR_NOT_SUPPORTED;
	status = fs_stream_seek(stream, 0);
	if (status != 0) return status;
	size = fs_stream_size(stream);
	if (size < 0) return (int)size;
	source.length = size;
	return decode(frames, frame_count, &source);
}
#else
bool st_audio_file_available(void) { return false; }
bool st_audio_file_format_available(uint32_t major, uint32_t subtype)
{
	(void)major; (void)subtype;
	return false;
}
int st_audio_file_decode_memory(int16_t **frames, size_t *frame_count,
	const void *data, size_t size)
{
	(void)size;
	return frames == NULL || frame_count == NULL || data == NULL
	    ? IO_ERROR_INVALID_ARGUMENT : IO_ERROR_NOT_SUPPORTED;
}
int st_audio_file_decode_stream(int16_t **frames, size_t *frame_count, FILE *stream)
{
	return frames == NULL || frame_count == NULL || stream == NULL
	    ? IO_ERROR_INVALID_ARGUMENT : IO_ERROR_NOT_SUPPORTED;
}
#endif
