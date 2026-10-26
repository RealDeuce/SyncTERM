/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "filesystem.h"

#include "io_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		    __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static void
make_path(char *result, size_t size, const char *directory, const char *name)
{
	int written = snprintf(result, size, "%s/%s", directory, name);
	CHECK(written >= 0 && (size_t)written < size);
}

static void
write_text(const char *path, const char *text)
{
	FILE *stream = NULL;
	CHECK(fs_open(&stream, path, "wb") == 0);
	if (stream == NULL)
		return;
	CHECK(fwrite(text, 1, strlen(text), stream) == strlen(text));
	CHECK(fclose(stream) == 0);
}

int
main(void)
{
	char directory[256];
#ifdef _WIN32
	long process_id = (long)_getpid();
#else
	long process_id = (long)getpid();
#endif
	int written = snprintf(directory, sizeof(directory),
	    "filesystem-test-%ld", process_id);
	CHECK(written >= 0 && (size_t)written < sizeof(directory));
	if (written < 0 || (size_t)written >= sizeof(directory))
		return EXIT_FAILURE;
	(void)fs_remove_directory(directory);
	CHECK(fs_create_directory(directory) == 0);

	char source[512], destination[512], replacement[512];
	char nested[512], child[512];
	make_path(source, sizeof(source), directory, "source.txt");
	make_path(destination, sizeof(destination), directory, "destination.txt");
	make_path(replacement, sizeof(replacement), directory, "replacement.txt");
	make_path(nested, sizeof(nested), directory, "one/two/three");
	make_path(child, sizeof(child), nested, "child.txt");

	struct fs_info untouched = {FS_ENTRY_OTHER, 42, 43};
	CHECK(fs_get_info(&untouched, "path-that-does-not-exist") ==
	    IO_ERROR_NOT_FOUND);
	CHECK(untouched.kind == FS_ENTRY_OTHER && untouched.size == 42 &&
	    untouched.modified_time == 43);

	write_text(source, "source contents");
	FILE *exclusive = NULL;
	CHECK(fs_open(&exclusive, source, "wbx") == IO_ERROR_ALREADY_EXISTS);
	CHECK(exclusive == NULL);
	struct fs_info info;
	CHECK(fs_get_info(&info, source) == 0);
	CHECK(info.kind == FS_ENTRY_FILE);
	CHECK(info.size == strlen("source contents"));

	FILE *stream = NULL;
	CHECK(fs_open(&stream, source, "r+b") == 0);
	if (stream != NULL) {
		CHECK(fseek(stream, 3, SEEK_SET) == 0);
		CHECK(fs_stream_size(stream) == (int64_t)strlen("source contents"));
		CHECK(ftell(stream) == 3);
		CHECK(fs_stream_truncate(stream, UINT64_MAX) == IO_ERROR_OUT_OF_RANGE);
		CHECK(fs_stream_size(stream) == (int64_t)strlen("source contents"));
		CHECK(fs_stream_truncate(stream, UINT64_C(5) * 1024 * 1024 * 1024 + 7) == 0);
		CHECK(fs_stream_size(stream) == INT64_C(5368709127));
		CHECK(ftell(stream) == 3);
		CHECK(fs_stream_truncate(stream, strlen("source contents")) == 0);
		CHECK(fclose(stream) == 0);
	}

	CHECK(fs_set_modified_time(source, INT64_C(1700000000)) == 0);
	CHECK(fs_get_info(&info, source) == 0);
	CHECK(info.modified_time == INT64_C(1700000000));

#ifndef _WIN32
	CHECK(chmod(source, 0751) == 0);
#endif
	CHECK(fs_copy_file(source, destination) == 0);
	CHECK(fs_get_info(&info, destination) == 0);
	CHECK(info.size == strlen("source contents"));
	CHECK(info.modified_time == INT64_C(1700000000));
#ifndef _WIN32
	struct stat native;
	CHECK(stat(destination, &native) == 0);
	CHECK((native.st_mode & 0777) == 0751);
#endif
	CHECK(fs_copy_file(source, source) == IO_ERROR_INVALID_ARGUMENT);
#ifndef _WIN32
	char hardlink[512];
	make_path(hardlink, sizeof(hardlink), directory, "source-hardlink");
	CHECK(link(source, hardlink) == 0);
	CHECK(fs_copy_file(source, hardlink) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(fs_get_info(&info, source) == 0);
	CHECK(info.size == strlen("source contents"));
	CHECK(fs_remove_file(hardlink) == 0);
#endif

	write_text(replacement, "replacement");
	CHECK(fs_rename(replacement, destination) == 0);
	CHECK(fs_get_info(&info, replacement) == IO_ERROR_NOT_FOUND);
	CHECK(fs_get_info(&info, destination) == 0);
	CHECK(info.size == strlen("replacement"));
	CHECK(fs_replace_file(destination, "atomic contents", 15) == 0);
	CHECK(fs_get_info(&info, destination) == 0);
	CHECK(info.size == 15);
	CHECK(fs_replace_file(destination, NULL, 0) == 0);
	CHECK(fs_get_info(&info, destination) == 0);
	CHECK(info.size == 0);
	CHECK(fs_replace_file(NULL, "x", 1) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(fs_replace_file(destination, NULL, 1) ==
	    IO_ERROR_INVALID_ARGUMENT);
	CHECK(fs_open(&stream, destination, "r+b") == 0);
	CHECK(fs_stream_position(stream) == 0);
	CHECK(fs_stream_seek(stream, 0) == 0);
	CHECK(fs_stream_seek(stream, -1) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(fclose(stream) == 0);

	CHECK(fs_create_directories(nested) == 0);
	{
		char parent_file[512];

		make_path(parent_file, sizeof(parent_file), directory,
		    "parents/child/file.txt");
		CHECK(fs_create_parent_directories(parent_file) == 0);
		CHECK(fs_get_info(&info, parent_file) == IO_ERROR_NOT_FOUND);
	}
	CHECK(fs_create_directories(nested) == 0);
	CHECK(fs_create_directory(nested) == IO_ERROR_ALREADY_EXISTS);
	CHECK(fs_get_info(&info, nested) == 0);
	CHECK(info.kind == FS_ENTRY_DIRECTORY);
	write_text(child, "child");
	CHECK(fs_remove_directory(nested) == IO_ERROR_DIRECTORY_NOT_EMPTY);
	CHECK(fs_remove_file(nested) == IO_ERROR_IS_DIRECTORY);
	CHECK(fs_remove_directory(child) == IO_ERROR_NOT_DIRECTORY);
	CHECK(fs_remove_file(child) == 0);
	CHECK(fs_remove_directory(nested) == 0);
	char two[512], one[512];
	make_path(two, sizeof(two), directory, "one/two");
	make_path(one, sizeof(one), directory, "one");
	CHECK(fs_remove_directory(two) == 0);
	CHECK(fs_remove_directory(one) == 0);

#ifndef _WIN32
	char fifo[512], link[512], dangling[512];
	make_path(fifo, sizeof(fifo), directory, "fifo");
	CHECK(mkfifo(fifo, 0600) == 0);
	CHECK(fs_get_info(&info, fifo) == 0);
	CHECK(info.kind == FS_ENTRY_OTHER);
	CHECK(fs_remove_file(fifo) == 0);
	make_path(link, sizeof(link), directory, "source-link");
	CHECK(symlink("source.txt", link) == 0);
	CHECK(fs_get_link_info(&info, link) == 0);
	CHECK(info.kind == FS_ENTRY_LINK);
	CHECK(fs_get_info(&info, link) == 0);
	CHECK(info.kind == FS_ENTRY_FILE);
	CHECK(fs_remove_file(link) == 0);
	make_path(dangling, sizeof(dangling), directory, "dangling-link");
	CHECK(symlink("missing", dangling) == 0);
	CHECK(fs_get_info(&info, dangling) == IO_ERROR_NOT_FOUND);
	CHECK(fs_get_link_info(&info, dangling) == 0);
	CHECK(info.kind == FS_ENTRY_LINK);
	CHECK(fs_remove_file(dangling) == 0);
#else
	char exact[512];
	char link[512], dangling[512];
	make_path(link, sizeof(link), directory, "source-link");
	DWORD link_flags = 0;
#ifdef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
	link_flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
#endif
	BOOL made_link = CreateSymbolicLinkA(link, "source.txt", link_flags);
	if (!made_link && link_flags != 0 &&
	    GetLastError() == ERROR_INVALID_PARAMETER) {
		link_flags = 0;
		made_link = CreateSymbolicLinkA(link, "source.txt", 0);
	}
	if (made_link) {
		CHECK(fs_get_link_info(&info, link) == 0);
		CHECK(info.kind == FS_ENTRY_LINK);
		CHECK(fs_get_info(&info, link) == 0);
		CHECK(info.kind == FS_ENTRY_FILE);
		CHECK(fs_remove_file(link) == 0);
		make_path(dangling, sizeof(dangling), directory,
		    "dangling-link");
		CHECK(CreateSymbolicLinkA(dangling, "missing", link_flags));
		CHECK(fs_get_info(&info, dangling) == IO_ERROR_NOT_FOUND);
		CHECK(fs_get_link_info(&info, dangling) == 0);
		CHECK(info.kind == FS_ENTRY_LINK);
		CHECK(fs_remove_file(dangling) == 0);
	}
	else
		fprintf(stderr, "Windows link tests skipped (error %lu)\n",
		    (unsigned long)GetLastError());
	make_path(exact, sizeof(exact), directory, "NUL");
	write_text(exact, "exact");
	CHECK(fs_get_info(&info, exact) == 0);
	CHECK(info.kind == FS_ENTRY_FILE);
	CHECK(fs_remove_file(exact) == 0);
	make_path(exact, sizeof(exact), directory, "trailing. ");
	write_text(exact, "exact");
	CHECK(fs_get_info(&info, exact) == 0);
	CHECK(fs_remove_file(exact) == 0);
	char long_directory[2048];
	snprintf(long_directory, sizeof(long_directory),
	    "%s/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	    "/bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
	    "/cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"
	    "/dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"
	    "/eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee",
	    directory);
	CHECK(fs_create_directories(long_directory) == 0);
	char unicode[2300];
	make_path(unicode, sizeof(unicode), long_directory,
	    "unicode-\xf0\x9f\x98\x80.txt");
	write_text(unicode, "utf8");
	CHECK(fs_get_info(&info, unicode) == 0);
	CHECK(fs_remove_file(unicode) == 0);
	/* Remove the long directory chain from the leaf upward. */
	for (;;) {
		CHECK(fs_remove_directory(long_directory) == 0);
		char *slash = strrchr(long_directory, '/');
		if (slash == NULL || slash <= long_directory + strlen(directory))
			break;
		*slash = '\0';
	}
	const char invalid[] = "\xc0\xaf";
	CHECK(fs_get_info(&info, invalid) == IO_ERROR_INVALID_ENCODING);
#endif

	CHECK(fs_remove_file(destination) == 0);
	CHECK(fs_remove_file(source) == 0);
	make_path(source, sizeof(source), directory, "parents/child");
	CHECK(fs_remove_directory(source) == 0);
	make_path(source, sizeof(source), directory, "parents");
	CHECK(fs_remove_directory(source) == 0);
	CHECK(fs_remove_directory(directory) == 0);
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
