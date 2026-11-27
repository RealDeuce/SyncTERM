/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include "ddofx_config.h"
#include "filesystem.h"
#include "path.h"
#include "st_ddofx.h"
#include "st_threads.h"
#include "st_time.h"

#if ST_DDO_MEMORY_WIN32
 #include <windows.h>
#elif ST_DDO_MEMORY_MACH
 #include <mach/mach.h>
#elif ST_DDO_MEMORY_HAIKU
 #include <OS.h>
#elif ST_DDO_MEMORY_SYSCONF || ST_DDO_MEMORY_SYSCTL
 #include <unistd.h>
 #if ST_DDO_MEMORY_SYSCTL
  #include <sys/types.h>
  #include <sys/sysctl.h>
 #endif
#endif

/* Script variables, comparisons and memory requests use signed DOS longs. */
static int32_t
capacity(uint64_t units, uint64_t bytes)
{
	return bytes && units > INT32_MAX / bytes ? INT32_MAX : (int32_t)(units * bytes);
}

int32_t
st_ddofx_available_memory(void)
{
#if ST_DDO_MEMORY_WIN32
	MEMORYSTATUSEX info = {0};
	info.dwLength       = sizeof(info);
	if (GlobalMemoryStatusEx(&info))
		return capacity(1,
		    info.ullAvailVirtual < info.ullAvailPageFile ? info.ullAvailVirtual : info.ullAvailPageFile);
#elif ST_DDO_MEMORY_MACH
	vm_statistics64_data_t pages;
	mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
	vm_size_t              bytes;
	mach_port_t            host = mach_host_self();
	bool ok = host_page_size(host, &bytes) == KERN_SUCCESS
	          && host_statistics64(host, HOST_VM_INFO64, (host_info64_t)&pages, &count) == KERN_SUCCESS;
	mach_port_deallocate(mach_task_self(), host);
	if (ok)
		return capacity((uint64_t)pages.free_count + pages.inactive_count, bytes);
#elif ST_DDO_MEMORY_HAIKU
	system_info info;
	if (get_system_info(&info) == B_OK && info.max_pages >= info.used_pages)
		return capacity(info.max_pages - info.used_pages, B_PAGE_SIZE);
#elif ST_DDO_MEMORY_SYSCONF || ST_DDO_MEMORY_SYSCTL
	long bytes = sysconf(_SC_PAGESIZE);
	if (bytes <= 0)
		return -1;
 #if ST_DDO_MEMORY_SYSCTL
	unsigned free_pages, inactive;
	size_t   size = sizeof(free_pages);
	if (!sysctlbyname("vm.stats.vm.v_free_count", &free_pages, &size, NULL, 0) && size == sizeof(free_pages)) {
		size = sizeof(inactive);
		if (sysctlbyname("vm.stats.vm.v_inactive_count", &inactive, &size, NULL, 0)
		    || size != sizeof(inactive))
			inactive = 0;
		return capacity((uint64_t)free_pages + inactive, (uint64_t)bytes);
	}
 #endif
 #if ST_DDO_MEMORY_SYSCONF
	long pages = sysconf(_SC_AVPHYS_PAGES);
	if (pages >= 0)
		return capacity((uint64_t)pages, (uint64_t)bytes);
 #endif
#endif
	return -1;
}

int32_t
st_ddofx_available_drive(const char *directory)
{
	if (!directory || !*directory)
		return -1;
	uint64_t bytes;
	int      status = fs_available_bytes(&bytes, directory);
	if (status == IO_ERROR_NOT_FOUND) {
		/* A new connection may not have received its first DDO asset yet. */
		size_t size   = strlen(directory) + 1;
		char  *parent = malloc(size);
		if (!parent)
			return -1;
		if (path_parent(parent, size, directory) >= 0)
			status = fs_available_bytes(&bytes, parent);
		free(parent);
	}
	return status == 0 ? capacity(1, bytes) : -1;
}

static st_once_flag_t speed_once = ST_ONCE_FLAG_INIT;
static int32_t        speed_index;

static void
measure_speed(void)
{
	/* The SDK describes the index but leaves its units unspecified.
	 * MPORT.EXE counts string edits plus integer and real arithmetic over
	 * two one-second trials, then divides the total by five. Sample those
	 * operations briefly and normalize to the same duration. */
	static const char label[] = "TESTING THE RAM";
	uint32_t          seed = 1, iterations = 0;
	volatile char     text[sizeof(label)];
	volatile double   real;
	uint64_t          start, now;
	if (st_time_monotonic_ns(&start))
		return;
	do {
		for (unsigned i = 0; i < 256; ++i) {
			for (size_t j = 0; j < sizeof(label); ++j)
				text[j] = label[j];
			for (size_t j = 1; j < sizeof(label); ++j)
				text[j - 1] = text[j];
			seed       = seed * UINT32_C(1664525) + 1;
			uint32_t n = 5 + ((seed >> 8) % 10000);
			uint32_t a = n * 15, b = n * 6 + a;
			uint32_t product = (2 * a / 5 + b) * b;
			real             = (double)(product / (a + 1) * n) / 6.789;
			real             = real * 3.145;
		}
		iterations += 256;
		if (st_time_monotonic_ns(&now) || now < start)
			return;
	} while (now - start < UINT64_C(10000000) && iterations < (1u << 20));
	if (now > start) {
		uint64_t index = (uint64_t)iterations * UINT64_C(400000000) / (now - start);
		speed_index    = index <= INT32_MAX ? (int32_t)index : -1;
	}
}

int32_t
st_ddofx_speed_index(void)
{
	st_call_once(&speed_once, measure_speed);
	return speed_index;
}
