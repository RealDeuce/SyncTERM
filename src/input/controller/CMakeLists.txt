# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(SyncTERMControllers LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCStandard.cmake")
	include(CTest)
endif()
foreach(component base/time base/md5 base/threads base/dynamic_library io/filesystem text/utf8)
	get_filename_component(name "${component}" NAME)
	if(NOT TARGET st_${name} AND NOT TARGET syncterm_${name})
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../${component}" "${CMAKE_CURRENT_BINARY_DIR}/${name}")
	endif()
endforeach()

add_library(st_controller STATIC controller.c controller_mapping.c controller_descriptor.c controller_native.c controller_xusb.c)
target_include_directories(st_controller PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_controller PRIVATE syncterm_threads syncterm_dynamic_library st_time st_md5 st_utf8 syncterm_path syncterm_filesystem)
set_target_properties(st_controller PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermDependencies.cmake")
set(_st_hid_package hidapi)
set(_st_hid_libraries hidapi)
set(_st_hid_options)
if(APPLE)
	set(ST_CONTROLLER_DARWIN_HID 1)
	set(_st_hid_extra_header "#include <hidapi_darwin.h>")
	set(_st_hid_extra_call "hid_darwin_set_open_exclusive(0);")
elseif(NOT WIN32)
	# The hidraw and NetBSD backends allow descriptor access without claiming
	# USB interfaces. Their public SDK is also usable without a backend DSO.
	set(_st_hid_package hidapi-hidraw)
	set(_st_hid_libraries hidapi-hidraw hidapi-netbsd)
	set(_st_hid_options ALTERNATIVES INSTALLED_ONLY)
	find_package(hidapi 0.14 QUIET CONFIG)
	find_package(PkgConfig QUIET)
	if(PkgConfig_FOUND AND (NOT CMAKE_CROSSCOMPILING OR DEFINED ENV{PKG_CONFIG_SYSROOT_DIR}))
		pkg_search_module(ST_CONTROLLER_HID_NATIVE QUIET hidapi-hidraw>=0.14 hidapi-netbsd>=0.14)
	endif()
	if(TARGET hidapi::netbsd OR ST_CONTROLLER_HID_NATIVE_MODULE_NAME STREQUAL "hidapi-netbsd")
		set(_st_hid_package hidapi-netbsd)
		set(_st_hid_libraries hidapi-netbsd hidapi-hidraw)
	endif()
endif()
syncterm_dependency(NAME HIDAPI PACKAGE "${_st_hid_package}>=0.14" HEADER hidapi.h
	LIBRARIES ${_st_hid_libraries} DEFAULT RUNTIME FEATURE ST_CONTROLLER_USE_HIDAPI ${_st_hid_options}
	PROBE "
#include <hidapi.h>
${_st_hid_extra_header}
int main(void) {
	${_st_hid_extra_call}
	int (*descriptor)(hid_device *, unsigned char *, size_t) = hid_get_report_descriptor;
	return descriptor(NULL, NULL, HID_API_MAX_REPORT_DESCRIPTOR_SIZE);
}")
if(ST_DEP_HIDAPI_TARGET)
	set(ST_CONTROLLER_HIDAPI ON)
	string(JOIN "\", \"" ST_CONTROLLER_HID_NAMES ${_st_hid_libraries})
	target_link_libraries(st_controller PRIVATE ${ST_DEP_HIDAPI_TARGET})
	target_sources(st_controller PRIVATE controller_hid.c)
endif()
set(ST_CONTROLLER_EVDEV OFF)
if(WIN32)
	include(CheckCSourceCompiles)
	include(CMakePushCheckState)
	cmake_push_check_state(RESET)
	set(CMAKE_REQUIRED_LIBRARIES xinput9_1_0)
	check_c_source_compiles("#include <windows.h>
#include <xinput.h>
int main(void) { XINPUT_STATE s; return (int)XInputGetState(0, &s); }" ST_CONTROLLER_XINPUT)
	cmake_pop_check_state()
	if(ST_CONTROLLER_XINPUT)
		target_sources(st_controller PRIVATE controller_xinput.c)
		target_link_libraries(st_controller PRIVATE xinput9_1_0)
	endif()
else()
	set(ST_CONTROLLER_USB_LIBRARY usb-1.0)
	set(ST_CONTROLLER_USB_MAJOR 0)
	syncterm_dependency(NAME LIBUSB PACKAGE libusb-1.0 HEADER libusb.h LIBRARIES ${ST_CONTROLLER_USB_LIBRARY}
		DEFAULT RUNTIME FEATURE ST_CONTROLLER_USE_LIBUSB PROBE
		"#include <libusb.h>
int main(void) { libusb_context *ctx; return libusb_init(&ctx); }")
	if(ST_DEP_LIBUSB_TARGET)
		set(ST_CONTROLLER_LIBUSB ON)
		if(ST_DEP_LIBUSB_MODE STREQUAL "RUNTIME")
			get_property(ST_CONTROLLER_USB_LIBRARY TARGET ${ST_DEP_LIBUSB_TARGET} PROPERTY ST_DEP_RUNTIME_NAMES)
			if(ST_CONTROLLER_USB_LIBRARY STREQUAL "usb")
				set(ST_CONTROLLER_USB_MAJOR 3)
			endif()
		endif()
		target_sources(st_controller PRIVATE controller_usb.c)
		target_link_libraries(st_controller PRIVATE ${ST_DEP_LIBUSB_TARGET})
	endif()
endif()

if(NOT WIN32 AND NOT APPLE)
	syncterm_dependency(NAME UDEV PACKAGE libudev HEADER libudev.h LIBRARIES udev DEFAULT RUNTIME INSTALLED_ONLY FEATURE ST_CONTROLLER_USE_EVDEV
		PROBE [=[
#include <libudev.h>
int main(void) { return udev_enumerate_add_match_sysname(udev_enumerate_new(udev_new()), "event*"); }
]=])
	find_path(_st_event_include linux/input.h HINTS "${ST_DEP_EVDEV_ROOT}" PATH_SUFFIXES include)
	set(_st_event_includes)
	if(_st_event_include)
		set(_st_event_includes "${_st_event_include}")
	endif()
	syncterm_dependency(NAME EVDEV INCLUDES ${_st_event_includes} PACKAGE libevdev HEADER libevdev/libevdev.h LIBRARIES evdev DEFAULT RUNTIME INSTALLED_ONLY FEATURE ST_CONTROLLER_USE_EVDEV
		PROBE [=[
#define _POSIX_C_SOURCE 200809L
#include <sys/types.h>
#include <fcntl.h>
#include <libevdev/libevdev.h>
int main(void) { struct libevdev *d; return libevdev_new_from_fd(open("/dev/null", O_NONBLOCK | O_CLOEXEC), &d) + BTN_DPAD_RIGHT; }
]=])
	if(ST_DEP_EVDEV_TARGET AND ST_DEP_UDEV_TARGET)
		set(ST_CONTROLLER_EVDEV ON)
		target_sources(st_controller PRIVATE controller_evdev.c)
		target_link_libraries(st_controller PRIVATE ${ST_DEP_EVDEV_TARGET} ${ST_DEP_UDEV_TARGET})
	endif()
endif()
foreach(dep HIDAPI LIBUSB UDEV EVDEV)
	if(ST_DEP_${dep}_TARGET AND NOT ST_DEP_${dep}_MODE STREQUAL "RUNTIME")
		set(ST_DEP_${dep}_LINKED 1)
	endif()
endforeach()
syncterm_export_configuration(st_controller ST_CONTROLLER_HIDAPI ST_CONTROLLER_LIBUSB ST_CONTROLLER_EVDEV ST_CONTROLLER_XINPUT)
configure_file(controller_config.h.in st_controller_config.h)
target_include_directories(st_controller PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
if(MSVC)
	target_compile_options(st_controller PRIVATE /W4)
	target_compile_definitions(st_controller PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
	target_compile_options(st_controller PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(BUILD_TESTING)
	add_executable(st_controller_test tests/controller_test.c)
	target_link_libraries(st_controller_test PRIVATE st_controller)
	set_target_properties(st_controller_test PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	add_test(NAME controller_mapping COMMAND st_controller_test)
endif()
