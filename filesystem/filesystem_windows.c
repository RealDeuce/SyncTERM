/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "filesystem.h"

#include "io_error.h"
#include "path.h"
#include "path_windows.h"
#include "st_windows_utf.h"

#include <windows.h>

#include <errno.h>
#include <io.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static int
fs_windows_error(DWORD error)
{
	return io_error_from_windows(error);
}

static int
fs_errno_error(int error)
{
	return io_error_from_errno(error);
}

static int
fs_utf8_to_wide(const char *source, wchar_t **result)
{
	return st_windows_utf8_to_utf16(result, source);
}

static int
fs_windows_path(const char *path, wchar_t **result)
{
	return path_windows_extended(result, path);
}

static int64_t
fs_filetime_seconds(FILETIME value)
{
	const uint64_t epoch = UINT64_C(116444736000000000);
	const uint64_t ticks_per_second = UINT64_C(10000000);
	ULARGE_INTEGER ticks;

	ticks.LowPart = value.dwLowDateTime;
	ticks.HighPart = value.dwHighDateTime;
	if (ticks.QuadPart >= epoch)
		return (int64_t)((ticks.QuadPart - epoch) / ticks_per_second);
	uint64_t difference = epoch - ticks.QuadPart;
	return -(int64_t)((difference + ticks_per_second - 1) /
	    ticks_per_second);
}

static int
fs_seconds_filetime(int64_t value, FILETIME *result)
{
	const uint64_t epoch = UINT64_C(116444736000000000);
	const uint64_t ticks_per_second = UINT64_C(10000000);
	uint64_t ticks;

	if (value >= 0) {
		uint64_t positive = (uint64_t)value;
		if (positive > (UINT64_MAX - epoch) / ticks_per_second)
			return IO_ERROR_OUT_OF_RANGE;
		ticks = epoch + positive * ticks_per_second;
	}
	else {
		uint64_t magnitude = (uint64_t)(-(value + 1)) + 1;
		if (magnitude > epoch / ticks_per_second)
			return IO_ERROR_OUT_OF_RANGE;
		ticks = epoch - magnitude * ticks_per_second;
	}
	ULARGE_INTEGER converted;
	converted.QuadPart = ticks;
	result->dwLowDateTime = converted.LowPart;
	result->dwHighDateTime = converted.HighPart;
	return 0;
}

static void
fs_windows_info(struct fs_info *result, DWORD attributes, DWORD size_high,
    DWORD size_low, FILETIME modified_time, DWORD file_type)
{
	if (file_type != FILE_TYPE_DISK)
		result->kind = FS_ENTRY_OTHER;
	else if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
		result->kind = FS_ENTRY_DIRECTORY;
	else
		result->kind = FS_ENTRY_FILE;
	result->size = result->kind == FS_ENTRY_FILE ?
	    ((uint64_t)size_high << 32) | size_low : 0;
	result->modified_time = fs_filetime_seconds(modified_time);
}

int
fs_get_info(struct fs_info *result, const char *path)
{
	wchar_t *wide;
	BY_HANDLE_FILE_INFORMATION native;
	struct fs_info info;
	int error;

	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	error = fs_windows_path(path, &wide);
	if (error < 0)
		return error;
	HANDLE handle = CreateFileW(wide, FILE_READ_ATTRIBUTES,
	    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
	    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	free(wide);
	if (handle == INVALID_HANDLE_VALUE)
		return fs_windows_error(GetLastError());
	if (!GetFileInformationByHandle(handle, &native)) {
		error = fs_windows_error(GetLastError());
		CloseHandle(handle);
		return error;
	}
	SetLastError(NO_ERROR);
	DWORD type = GetFileType(handle);
	if (type == FILE_TYPE_UNKNOWN && GetLastError() != NO_ERROR) {
		error = fs_windows_error(GetLastError());
		CloseHandle(handle);
		return error;
	}
	fs_windows_info(&info, native.dwFileAttributes, native.nFileSizeHigh,
	    native.nFileSizeLow, native.ftLastWriteTime, type);
	CloseHandle(handle);
	*result = info;
	return 0;
}

int
fs_get_link_info(struct fs_info *result, const char *path)
{
	wchar_t *wide;
	WIN32_FILE_ATTRIBUTE_DATA native;
	struct fs_info info;
	int error;

	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	error = fs_windows_path(path, &wide);
	if (error < 0)
		return error;
	if (!GetFileAttributesExW(wide, GetFileExInfoStandard, &native)) {
		error = fs_windows_error(GetLastError());
		free(wide);
		return error;
	}
	free(wide);
	if ((native.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
		info.kind = FS_ENTRY_LINK;
		info.size = 0;
		info.modified_time = fs_filetime_seconds(native.ftLastWriteTime);
	}
	else
		fs_windows_info(&info, native.dwFileAttributes,
		    native.nFileSizeHigh, native.nFileSizeLow,
		    native.ftLastWriteTime, FILE_TYPE_DISK);
	*result = info;
	return 0;
}

int
fs_open(FILE **result, const char *path, const char *mode)
{
	wchar_t *wide_path;
	wchar_t *wide_mode;
	FILE *stream;
	int error;

	if (result == NULL || mode == NULL || *mode == '\0')
		return IO_ERROR_INVALID_ARGUMENT;
	error = fs_windows_path(path, &wide_path);
	if (error < 0)
		return error;
	error = fs_utf8_to_wide(mode, &wide_mode);
	if (error < 0) {
		free(wide_path);
		return error;
	}
	stream = _wfopen(wide_path, wide_mode);
	free(wide_mode);
	free(wide_path);
	if (stream == NULL)
		return fs_errno_error(errno);
	*result = stream;
	return 0;
}

int64_t
fs_stream_size(FILE *stream)
{
	if (stream == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	int descriptor = _fileno(stream);
	if (descriptor < 0)
		return fs_errno_error(errno);
	__int64 size = _filelengthi64(descriptor);
	if (size < 0)
		return fs_errno_error(errno);
	return (int64_t)size;
}

int
fs_stream_truncate(FILE *stream, uint64_t size)
{
	if (stream == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (size > INT64_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	if (fflush(stream) != 0)
		return fs_errno_error(errno);
	int descriptor = _fileno(stream);
	if (descriptor < 0)
		return fs_errno_error(errno);
	errno_t error = _chsize_s(descriptor, (__int64)size);
	return error == 0 ? 0 : fs_errno_error(error);
}

int
fs_stream_sync(FILE *stream)
{
	if (stream == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (fflush(stream) != 0)
		return fs_errno_error(errno);
	int descriptor = _fileno(stream);
	if (descriptor < 0)
		return fs_errno_error(errno);
	if (_commit(descriptor) != 0)
		return fs_errno_error(errno);
	return 0;
}

int
fs_set_modified_time(const char *path, int64_t modified_time)
{
	wchar_t *wide;
	FILETIME native_time;
	int error = fs_seconds_filetime(modified_time, &native_time);
	if (error < 0)
		return error;
	error = fs_windows_path(path, &wide);
	if (error < 0)
		return error;
	HANDLE handle = CreateFileW(wide, FILE_WRITE_ATTRIBUTES,
	    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
	    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	free(wide);
	if (handle == INVALID_HANDLE_VALUE)
		return fs_windows_error(GetLastError());
	if (!SetFileTime(handle, NULL, NULL, &native_time))
		error = fs_windows_error(GetLastError());
	else
		error = 0;
	CloseHandle(handle);
	return error;
}

int
fs_remove_file(const char *path)
{
	wchar_t *wide;
	int error = fs_windows_path(path, &wide);
	if (error < 0)
		return error;
	DWORD attributes = GetFileAttributesW(wide);
	if (attributes == INVALID_FILE_ATTRIBUTES) {
		error = fs_windows_error(GetLastError());
		free(wide);
		return error;
	}
	if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
		free(wide);
		return IO_ERROR_IS_DIRECTORY;
	}
	if (!DeleteFileW(wide))
		error = fs_windows_error(GetLastError());
	else
		error = 0;
	free(wide);
	return error;
}

int
fs_remove_directory(const char *path)
{
	wchar_t *wide;
	int error = fs_windows_path(path, &wide);
	if (error < 0)
		return error;
	DWORD attributes = GetFileAttributesW(wide);
	if (attributes == INVALID_FILE_ATTRIBUTES) {
		error = fs_windows_error(GetLastError());
		free(wide);
		return error;
	}
	if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
		free(wide);
		return IO_ERROR_NOT_DIRECTORY;
	}
	if (!RemoveDirectoryW(wide))
		error = fs_windows_error(GetLastError());
	else
		error = 0;
	free(wide);
	return error;
}

int
fs_rename(const char *source, const char *destination)
{
	wchar_t *wide_source;
	wchar_t *wide_destination;
	int error = fs_windows_path(source, &wide_source);
	if (error < 0)
		return error;
	error = fs_windows_path(destination, &wide_destination);
	if (error < 0) {
		free(wide_source);
		return error;
	}
	if (!MoveFileExW(wide_source, wide_destination,
	    MOVEFILE_REPLACE_EXISTING))
		error = fs_windows_error(GetLastError());
	else
		error = 0;
	free(wide_destination);
	free(wide_source);
	return error;
}

int
fs_copy_file(const char *source, const char *destination)
{
	wchar_t *wide_source;
	wchar_t *wide_destination;
	int error = fs_windows_path(source, &wide_source);
	if (error < 0)
		return error;
	error = fs_windows_path(destination, &wide_destination);
	if (error < 0) {
		free(wide_source);
		return error;
	}
	HANDLE source_handle = CreateFileW(wide_source, FILE_READ_ATTRIBUTES,
	    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
	    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (source_handle == INVALID_HANDLE_VALUE) {
		error = fs_windows_error(GetLastError());
		free(wide_destination);
		free(wide_source);
		return error;
	}
	HANDLE destination_handle = CreateFileW(wide_destination,
	    FILE_READ_ATTRIBUTES,
	    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
	    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (destination_handle != INVALID_HANDLE_VALUE) {
		BY_HANDLE_FILE_INFORMATION source_info;
		BY_HANDLE_FILE_INFORMATION destination_info;
		if (!GetFileInformationByHandle(source_handle, &source_info) ||
		    !GetFileInformationByHandle(destination_handle,
		    &destination_info))
			error = fs_windows_error(GetLastError());
		else if (source_info.dwVolumeSerialNumber ==
		    destination_info.dwVolumeSerialNumber &&
		    source_info.nFileIndexHigh == destination_info.nFileIndexHigh &&
		    source_info.nFileIndexLow == destination_info.nFileIndexLow)
			error = IO_ERROR_INVALID_ARGUMENT;
		CloseHandle(destination_handle);
	}
	else {
		DWORD native_error = GetLastError();
		if (native_error != ERROR_FILE_NOT_FOUND &&
		    native_error != ERROR_PATH_NOT_FOUND)
			error = fs_windows_error(native_error);
	}
	CloseHandle(source_handle);
	if (error < 0) {
		free(wide_destination);
		free(wide_source);
		return error;
	}
	if (!CopyFileW(wide_source, wide_destination, FALSE))
		error = fs_windows_error(GetLastError());
	else
		error = 0;
	free(wide_destination);
	free(wide_source);
	return error;
}

int
fs_create_directory(const char *path)
{
	wchar_t *wide;
	int error = fs_windows_path(path, &wide);
	if (error < 0)
		return error;
	if (!CreateDirectoryW(wide, NULL))
		error = fs_windows_error(GetLastError());
	else
		error = 0;
	free(wide);
	return error;
}

static size_t
fs_windows_root_length(const wchar_t *path)
{
	if (wcsncmp(path, L"\\\\?\\UNC\\", 8) == 0) {
		size_t position = 8;
		while (path[position] != L'\0' && path[position] != L'\\')
			position++;
		if (path[position] == L'\0')
			return 0;
		position++;
		while (path[position] != L'\0' && path[position] != L'\\')
			position++;
		return path[position] == L'\\' ? position + 1 : position;
	}
	if (wcsncmp(path, L"\\\\?\\", 4) == 0 && path[5] == L':' &&
	    path[6] == L'\\')
		return 7;
	return 0;
}

static int
fs_create_or_confirm_directory(wchar_t *path)
{
	if (CreateDirectoryW(path, NULL))
		return 0;
	DWORD error = GetLastError();
	if (error != ERROR_ALREADY_EXISTS)
		return fs_windows_error(error);
	DWORD attributes = GetFileAttributesW(path);
	if (attributes == INVALID_FILE_ATTRIBUTES)
		return fs_windows_error(GetLastError());
	return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ? 0 :
	    IO_ERROR_NOT_DIRECTORY;
}

int
fs_create_directories(const char *path)
{
	wchar_t *wide;
	int error = fs_windows_path(path, &wide);
	if (error < 0)
		return error;
	size_t root = fs_windows_root_length(wide);
	if (root == 0) {
		free(wide);
		return IO_ERROR_INVALID_ARGUMENT;
	}
	size_t length = wcslen(wide);
	while (length > root && wide[length - 1] == L'\\')
		wide[--length] = L'\0';
	for (wchar_t *current = wide + root; *current != L'\0'; current++) {
		if (*current != L'\\')
			continue;
		*current = L'\0';
		error = fs_create_or_confirm_directory(wide);
		*current = L'\\';
		if (error < 0)
			break;
	}
	if (error == 0 && length > root)
		error = fs_create_or_confirm_directory(wide);
	free(wide);
	return error;
}
