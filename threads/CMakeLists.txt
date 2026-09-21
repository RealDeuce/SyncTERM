# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(SyncTERMThreads LANGUAGES C)
	include(CTest)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/SynctermCheckC17Source.cmake")

set(_st_threads_saved_c_standard ${CMAKE_C_STANDARD})
set(_st_threads_saved_c_standard_required ${CMAKE_C_STANDARD_REQUIRED})
set(_st_threads_saved_c_extensions ${CMAKE_C_EXTENSIONS})
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(_st_threads_saved_required_definitions ${CMAKE_REQUIRED_DEFINITIONS})
set(_st_threads_saved_required_libraries ${CMAKE_REQUIRED_LIBRARIES})
set(ST_THREADS_COMPILE_DEFINITIONS)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	list(APPEND ST_THREADS_COMPILE_DEFINITIONS _GNU_SOURCE)
	list(APPEND CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
endif()

set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads QUIET)
if(Threads_FOUND)
	set(CMAKE_REQUIRED_LIBRARIES Threads::Threads)
	syncterm_check_c17_source([=[
#include <pthread.h>
#include <sched.h>
#include <time.h>

static void initialize(void) { }
static void destroy(void *value) { (void)value; }
static void *start(void *value) { return value; }

int main(void)
{
	_Thread_local static int local_value;
	pthread_t thread;
	pthread_mutex_t mutex;
	pthread_mutexattr_t attributes;
	pthread_cond_t condition;
	pthread_once_t once = PTHREAD_ONCE_INIT;
	pthread_key_t key;
	struct timespec duration = {0, 0};
	void *result;

	(void)pthread_mutexattr_init(&attributes);
	(void)pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_NORMAL);
	(void)pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE);
	(void)pthread_mutex_init(&mutex, &attributes);
	(void)pthread_mutex_lock(&mutex);
	(void)pthread_mutex_trylock(&mutex);
	(void)pthread_mutex_unlock(&mutex);
	(void)pthread_mutex_destroy(&mutex);
	(void)pthread_mutexattr_destroy(&attributes);
	(void)pthread_cond_init(&condition, NULL);
	(void)pthread_cond_signal(&condition);
	(void)pthread_cond_broadcast(&condition);
	(void)pthread_cond_wait(&condition, &mutex);
	(void)pthread_cond_timedwait(&condition, &mutex, &duration);
	(void)pthread_cond_destroy(&condition);
	(void)pthread_once(&once, initialize);
	(void)pthread_key_create(&key, destroy);
	(void)pthread_setspecific(key, NULL);
	(void)pthread_getspecific(key);
	(void)pthread_key_delete(key);
	(void)pthread_create(&thread, NULL, start, NULL);
	(void)pthread_self();
	(void)pthread_equal(thread, thread);
	(void)pthread_detach(thread);
	(void)pthread_join(thread, &result);
	(void)nanosleep(&duration, NULL);
	(void)sched_yield();
	return local_value;
}
]=] ST_THREADS_HAVE_PTHREAD_BACKEND)
endif()

if(ST_THREADS_HAVE_PTHREAD_BACKEND)
	set(ST_THREADS_BACKEND_PTHREAD TRUE)
	set(ST_THREADS_BACKEND_WINDOWS FALSE)
	set(ST_THREADS_SOURCES st_threads_pthread.c st_rwlock_pthread.c)
	syncterm_check_c17_source([=[
#include <pthread.h>
int main(void)
{
	pthread_rwlock_t lock;
	(void)pthread_rwlock_init(&lock, NULL);
	(void)pthread_rwlock_rdlock(&lock);
	(void)pthread_rwlock_tryrdlock(&lock);
	(void)pthread_rwlock_wrlock(&lock);
	(void)pthread_rwlock_trywrlock(&lock);
	(void)pthread_rwlock_unlock(&lock);
	(void)pthread_rwlock_destroy(&lock);
	return 0;
}
]=] ST_THREADS_HAVE_PTHREAD_RWLOCK)
	syncterm_check_c17_source([=[
#include <pthread.h>
#include <time.h>
int main(void)
{
	pthread_mutex_t mutex;
	struct timespec deadline = {0, 0};
	return pthread_mutex_timedlock(&mutex, &deadline);
}
]=] ST_THREADS_HAVE_PTHREAD_MUTEX_TIMEDLOCK)
elseif(WIN32)
	set(CMAKE_REQUIRED_LIBRARIES)
	syncterm_check_c17_source([=[
#include <process.h>
#include <stdint.h>
#include <time.h>
#include <windows.h>

static unsigned __stdcall start(void *value)
{
	return value != NULL;
}
static BOOL CALLBACK initialize(PINIT_ONCE once, PVOID value, PVOID *context)
{
	(void)once;
	(void)value;
	(void)context;
	return TRUE;
}
int main(void)
{
	_Thread_local static int local_value;
	SRWLOCK lock = SRWLOCK_INIT;
	CONDITION_VARIABLE condition = CONDITION_VARIABLE_INIT;
	CRITICAL_SECTION recursive;
	INIT_ONCE once = INIT_ONCE_STATIC_INIT;
	DWORD key = TlsAlloc();
	DWORD code;
	struct timespec now;
	unsigned id;
	uintptr_t thread;

	InitializeSRWLock(&lock);
	AcquireSRWLockExclusive(&lock);
	(void)TryAcquireSRWLockExclusive(&lock);
	ReleaseSRWLockExclusive(&lock);
	InitializeCriticalSection(&recursive);
	EnterCriticalSection(&recursive);
	(void)TryEnterCriticalSection(&recursive);
	LeaveCriticalSection(&recursive);
	DeleteCriticalSection(&recursive);
	InitializeConditionVariable(&condition);
	WakeConditionVariable(&condition);
	WakeAllConditionVariable(&condition);
	(void)SleepConditionVariableSRW(&condition, &lock, 0, 0);
	(void)SleepConditionVariableCS(&condition, &recursive, 0);
	(void)InitOnceExecuteOnce(&once, initialize, NULL, NULL);
	(void)TlsSetValue(key, NULL);
	(void)TlsGetValue(key);
	(void)TlsFree(key);
	thread = _beginthreadex(NULL, 0, start, NULL, 0, &id);
	if (thread != 0) {
		(void)WaitForSingleObject((HANDLE)thread, INFINITE);
		(void)GetExitCodeThread((HANDLE)thread, &code);
		(void)CloseHandle((HANDLE)thread);
	}
	(void)timespec_get(&now, TIME_UTC);
	(void)SwitchToThread();
	Sleep(0);
	return local_value;
}
]=] ST_THREADS_HAVE_WINDOWS_BACKEND)
	if(NOT ST_THREADS_HAVE_WINDOWS_BACKEND)
		message(FATAL_ERROR
			"The target does not provide either the required pthread or Win32 threading APIs")
	endif()
	set(ST_THREADS_BACKEND_PTHREAD FALSE)
	set(ST_THREADS_BACKEND_WINDOWS TRUE)
	set(ST_THREADS_HAVE_PTHREAD_MUTEX_TIMEDLOCK FALSE)
	set(ST_THREADS_HAVE_PTHREAD_RWLOCK FALSE)
	set(ST_THREADS_SOURCES st_threads_windows.c st_rwlock_windows.c)
else()
	message(FATAL_ERROR
		"The target does not provide the required pthread threading APIs")
endif()

set(CMAKE_REQUIRED_DEFINITIONS ${_st_threads_saved_required_definitions})
set(CMAKE_REQUIRED_LIBRARIES ${_st_threads_saved_required_libraries})
set(CMAKE_C_STANDARD ${_st_threads_saved_c_standard})
set(CMAKE_C_STANDARD_REQUIRED ${_st_threads_saved_c_standard_required})
set(CMAKE_C_EXTENSIONS ${_st_threads_saved_c_extensions})
unset(_st_threads_saved_c_standard)
unset(_st_threads_saved_c_standard_required)
unset(_st_threads_saved_c_extensions)
unset(_st_threads_saved_required_definitions)
unset(_st_threads_saved_required_libraries)

configure_file(st_threads_config.h.in st_threads_config.h @ONLY)

add_library(syncterm_threads STATIC ${ST_THREADS_SOURCES})
target_include_directories(syncterm_threads PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
if(ST_THREADS_BACKEND_PTHREAD)
	target_link_libraries(syncterm_threads PUBLIC Threads::Threads)
endif()
if(ST_THREADS_COMPILE_DEFINITIONS)
	target_compile_definitions(syncterm_threads PUBLIC
		${ST_THREADS_COMPILE_DEFINITIONS})
endif()
set_target_properties(syncterm_threads PROPERTIES
	C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

function(st_threads_warnings target)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 /WX)
	else()
		target_compile_options(${target} PRIVATE
			-Wall -Wextra -Wpedantic -Werror)
	endif()
endfunction()
st_threads_warnings(syncterm_threads)

if(BUILD_TESTING)
	add_executable(st_threads_test tests/st_threads_test.c)
	target_link_libraries(st_threads_test PRIVATE syncterm_threads)
	set_target_properties(st_threads_test PROPERTIES
		C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	st_threads_warnings(st_threads_test)
	add_test(NAME st_threads COMMAND st_threads_test)
	set_tests_properties(st_threads PROPERTIES TIMEOUT 20)
endif()
