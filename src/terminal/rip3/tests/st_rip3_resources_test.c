/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_resources.h"
#include "filesystem.h"
#include "path.h"
#include "io_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
		return 1; \
	} \
} while (0)

static int
put(const char *directory, const char *name, const char *text)
{
	char *path = NULL;
	int status = path_join_alloc(&path, directory, name);
	if (status == 0)
		status = fs_replace_file(path, text, strlen(text));
	free(path);
	return status;
}

static int
remove_file(const char *directory, const char *name)
{
	char *path = NULL;
	int status = path_join_alloc(&path, directory, name);
	if (status == 0)
		status = fs_remove_file(path);
	free(path);
	return status;
}

static int
expect(const struct st_rip3_resource_paths *paths,
    enum st_rip3_resource_kind kind, const char *name, const char *directory,
    const char *actual_name, const char *content)
{
	struct st_rip3_resource resource = {0};
	char bytes[64], *path = NULL;
	size_t length;

	CHECK(st_rip3_resource_open(&resource, paths, kind, name) == 0);
	CHECK(path_join_alloc(&path, directory, actual_name) == 0);
	CHECK(strcmp(resource.path, path) == 0);
	CHECK(fs_stream_position(resource.stream) == 0);
	length = fread(bytes, 1, sizeof(bytes), resource.stream);
	CHECK(!ferror(resource.stream) && length == strlen(content));
	CHECK(memcmp(bytes, content, length) == 0);
	free(path);
	st_rip3_resource_dispose(&resource);
	CHECK(resource.stream == NULL && resource.path == NULL);
	st_rip3_resource_dispose(&resource);
	return 0;
}

static int
failure(const struct st_rip3_resource_paths *paths,
    enum st_rip3_resource_kind kind, const char *name, int status)
{
	char marker[] = "unchanged";
	struct st_rip3_resource resource = {stdout, marker};
	CHECK(st_rip3_resource_open(&resource, paths, kind, name) == status);
	CHECK(resource.stream == stdout && resource.path == marker);
	return 0;
}

int
main(int argc, char **argv)
{
	char *directories[7] = {0}, *subdirectory = NULL;
	const char *names[] = {"host", "shared", "search-one", "search-two",
	    "fonts", "empty", "missing"};
	const char *search[5];
	struct st_rip3_resource_paths paths;
	static const char *media[] = {"STAMP.BMP", "HOT.BMH", "MASK.BMM",
	    "OLD.ICN", "OLD.MSK", "OLD.HOT", "PHOTO.JPG", "SOUND.WAV",
	    "SCENE.RIP", "OTHER.DAT"};

	CHECK(argc == 2);
	CHECK(fs_create_directories(argv[1]) == 0);
	for (size_t i = 0; i < 7; i++) {
		CHECK(path_join_alloc(&directories[i], argv[1], names[i]) == 0);
		if (i < 6)
			CHECK(fs_create_directories(directories[i]) == 0);
	}
	search[0] = directories[6]; /* Missing entries do not stop lookup. */
	search[1] = NULL;
	search[2] = "";
	search[3] = directories[2];
	search[4] = directories[3];
	paths = (struct st_rip3_resource_paths){.host=directories[0],.shared=directories[1],
	    .fonts=directories[4],.search=search,.search_count=5};
	for (size_t i = 0; i < 4; i++) {
		char content[] = {(char)('0' + i), 0};
		CHECK(put(directories[i], "Menu.BmP", content) == 0);
	}
	for (size_t i = 0; i < 4; i++) {
		char content[] = {(char)('0' + i), 0};
		CHECK(expect(&paths, ST_RIP3_RESOURCE_MEDIA, "mENU.bMP",
		    directories[i], "Menu.BmP", content) == 0);
		CHECK(remove_file(directories[i], "Menu.BmP") == 0);
	}
	CHECK(failure(&paths, ST_RIP3_RESOURCE_MEDIA, "menu.bmp", IO_ERROR_NOT_FOUND) == 0);
	/* One directory family for every media type, without extension dispatch. */
	for (size_t i = 0; i < sizeof(media) / sizeof(media[0]); i++) {
		CHECK(put(directories[1], media[i], "media") == 0);
		CHECK(expect(&paths, ST_RIP3_RESOURCE_MEDIA, media[i],
		    directories[1], media[i], "media") == 0);
		CHECK(remove_file(directories[1], media[i]) == 0);
	}
	for (size_t i = 0; i < 4; i++)
		CHECK(put(directories[i], "FONT.CHR", "media namespace") == 0);
	CHECK(put(directories[4], "Font.Chr", "global") == 0);
	CHECK(expect(&paths, ST_RIP3_RESOURCE_FONT, "font.chr",
	    directories[4], "Font.Chr", "global") == 0);
	CHECK(remove_file(directories[4], "Font.Chr") == 0);
	CHECK(failure(&paths, ST_RIP3_RESOURCE_FONT, "FONT.CHR", IO_ERROR_NOT_FOUND) == 0);
	for (size_t i = 0; i < 4; i++)
		CHECK(remove_file(directories[i], "FONT.CHR") == 0);
	CHECK(put(directories[4], "GLOBAL.BMP", "font store") == 0);
	CHECK(failure(&paths, ST_RIP3_RESOURCE_MEDIA, "GLOBAL.BMP", IO_ERROR_NOT_FOUND) == 0);
	CHECK(remove_file(directories[4], "GLOBAL.BMP") == 0);
	/* A directory with the requested name is not a resource file. */
	CHECK(path_join_alloc(&subdirectory, directories[0], "DIRECTORY.BMP") == 0);
	CHECK(fs_create_directory(subdirectory) == 0);
	CHECK(put(directories[1], "directory.bmp", "file") == 0);
	CHECK(expect(&paths, ST_RIP3_RESOURCE_MEDIA, "DIRECTORY.BMP",
	    directories[1], "directory.bmp", "file") == 0);
	CHECK(remove_file(directories[1], "directory.bmp") == 0);
	CHECK(fs_remove_directory(subdirectory) == 0);
	free(subdirectory);
	/* Existing Unicode comparison is client policy beyond DOS ASCII names. */
	CHECK(put(directories[2], "\xce\xa9mega.BMP", "utf8") == 0);
	CHECK(expect(&paths, ST_RIP3_RESOURCE_MEDIA, "\xcf\x89MEGA.bmp",
	    directories[2], "\xce\xa9mega.BMP", "utf8") == 0);
	CHECK(remove_file(directories[2], "\xce\xa9mega.BMP") == 0);
	/* Probe the directory's actual case behavior, rather than assuming it
	 * from the operating system. Ambiguous directories cannot exist on some
	 * volumes; where supported, both spelling and enumeration order are ignored. */
	CHECK(put(directories[0], "Twin.BMP", "one") == 0);
	CHECK(put(directories[0], "twin.bmp", "two") == 0);
	char *twin = NULL;
	FILE *stream = NULL;
	char content[3];
	CHECK(path_join_alloc(&twin, directories[0], "Twin.BMP") == 0);
	CHECK(fs_open(&stream, twin, "rb") == 0);
	CHECK(fread(content, 1, sizeof(content), stream) == sizeof(content));
	CHECK(fclose(stream) == 0);
	free(twin);
	if (memcmp(content, "one", 3) == 0) {
		CHECK(failure(&paths, ST_RIP3_RESOURCE_MEDIA, "TWIN.BMP", IO_ERROR_CONFLICT) == 0);
		CHECK(failure(&paths, ST_RIP3_RESOURCE_MEDIA, "Twin.BMP", IO_ERROR_CONFLICT) == 0);
		/* A rejected tier does not prevent a later usable match. */
		CHECK(put(directories[3], "TWIN.BMP", "later") == 0);
		CHECK(expect(&paths, ST_RIP3_RESOURCE_MEDIA, "twin.bmp",
		    directories[3], "TWIN.BMP", "later") == 0);
		CHECK(remove_file(directories[3], "TWIN.BMP") == 0);
		CHECK(remove_file(directories[0], "twin.bmp") == 0);
	}
	else
		CHECK(memcmp(content, "two", 3) == 0);
	CHECK(remove_file(directories[0], "Twin.BMP") == 0);
	/* A misconfigured directory pointing at a file is a missing tier, and
	 * lookup never opens that file as if it were the requested resource. */
	CHECK(put(directories[0], "NOT-A-DIRECTORY", "unrelated") == 0);
	char *not_directory = NULL;
	CHECK(path_join_alloc(&not_directory, directories[0], "NOT-A-DIRECTORY") == 0);
	paths.host = not_directory;
	CHECK(put(directories[1], "FALLBACK.BMP", "fallback") == 0);
	CHECK(expect(&paths, ST_RIP3_RESOURCE_MEDIA, "fallback.bmp",
	    directories[1], "FALLBACK.BMP", "fallback") == 0);
	CHECK(remove_file(directories[1], "FALLBACK.BMP") == 0);
	CHECK(failure(&paths, ST_RIP3_RESOURCE_MEDIA, "fallback.bmp", IO_ERROR_NOT_FOUND) == 0);
	paths.host = directories[0];
	free(not_directory);
	CHECK(remove_file(directories[0], "NOT-A-DIRECTORY") == 0);
	static const char *invalid[] = {"", ".", "..", "../menu.bmp", "a/b",
	    "a\\b", "/menu.bmp", "C:menu.bmp", "C:\\menu.bmp", "*.BMP",
	    "?.BMP", "name:stream", "a|b", "a<b", "a>b", "a\"b", "a\nb",
	    "name.", "name "};
	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
		CHECK(failure(&paths, ST_RIP3_RESOURCE_MEDIA, invalid[i], IO_ERROR_INVALID_ARGUMENT) == 0);
	CHECK(failure(&paths, ST_RIP3_RESOURCE_MEDIA, "\xff.bmp", IO_ERROR_INVALID_ENCODING) == 0);
	CHECK(failure(&paths, ST_RIP3_RESOURCE_MEDIA, NULL, IO_ERROR_INVALID_ARGUMENT) == 0);
	CHECK(failure(NULL, ST_RIP3_RESOURCE_MEDIA, "menu.bmp", IO_ERROR_INVALID_ARGUMENT) == 0);
	CHECK(st_rip3_resource_open(NULL, &paths, ST_RIP3_RESOURCE_MEDIA, "menu.bmp") == IO_ERROR_INVALID_ARGUMENT);
	CHECK(failure(&paths, (enum st_rip3_resource_kind)100, "menu.bmp", IO_ERROR_INVALID_ARGUMENT) == 0);
	paths.search = NULL;
	CHECK(failure(&paths, ST_RIP3_RESOURCE_MEDIA, "menu.bmp", IO_ERROR_INVALID_ARGUMENT) == 0);
	paths.search_count = 0;
	paths.host = NULL;
	paths.shared = "";
	paths.fonts = NULL;
	CHECK(failure(&paths, ST_RIP3_RESOURCE_MEDIA, "menu.bmp", IO_ERROR_NOT_FOUND) == 0);
	CHECK(failure(&paths, ST_RIP3_RESOURCE_FONT, "font.chr", IO_ERROR_NOT_FOUND) == 0);
	st_rip3_resource_dispose(NULL);
	for (size_t i = 0; i < 7; i++) {
		if (i < 6)
			CHECK(fs_remove_directory(directories[i]) == 0);
		free(directories[i]);
	}
	CHECK(fs_remove_directory(argv[1]) == 0);
	return 0;
}
