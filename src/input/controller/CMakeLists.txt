# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(SyncTERMControllers LANGUAGES C)
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCStandard.cmake")
	include(CTest)
endif()
foreach(component base/time base/md5 io/filesystem text/utf8)
	get_filename_component(name "${component}" NAME)
	if(NOT TARGET st_${name} AND NOT TARGET syncterm_${name})
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../${component}" "${CMAKE_CURRENT_BINARY_DIR}/${name}")
	endif()
endforeach()

add_library(st_controller STATIC controller.c controller_mapping.c controller_descriptor.c)
target_include_directories(st_controller PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_controller PRIVATE st_time st_md5 st_utf8 syncterm_path syncterm_filesystem)
set_target_properties(st_controller PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermCheckC17Source.cmake")
syncterm_check_c17_source([=[
#include <sys/types.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <dev/usb/usb_ioctl.h>
#include <fcntl.h>
int main(void) {
	struct usb_device_info info = {0};
	struct usb_gen_descriptor desc = {0};
	return ioctl(-1, USB_GET_DEVICEINFO, &info) + ioctl(-1, USB_GET_REPORT_DESC, &desc)
	    + info.udi_bustypeNo + desc.ugd_actlen + O_CLOEXEC;
}
]=] ST_CONTROLLER_HAVE_UHID)
if(NOT ST_CONTROLLER_HAVE_UHID AND UNIX)
	syncterm_check_c17_source([=[
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/hidraw.h>
#include <linux/input.h>
int main(void) {
	struct hidraw_devinfo info;
	struct hidraw_report_descriptor desc;
	int length;
	return ioctl(-1, HIDIOCGRAWINFO, &info) + ioctl(-1, HIDIOCGRDESCSIZE, &length)
	    + ioctl(-1, HIDIOCGRDESC, &desc) + BUS_USB + O_CLOEXEC;
}
]=] ST_CONTROLLER_HAVE_LINUX_HIDRAW)
endif()
if(ST_CONTROLLER_HAVE_UHID OR ST_CONTROLLER_HAVE_LINUX_HIDRAW)
	target_sources(st_controller PRIVATE controller_raw.c)
elseif(WIN32 OR APPLE)
	include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SynctermDependencies.cmake")
	syncterm_dependency(NAME HIDAPI PACKAGE "hidapi>=0.14" HEADER hidapi.h LIBRARIES hidapi
		DEFAULT STATIC REQUIRED LINK_REQUIRED PROBE
		"#include <hidapi.h>
int main(void) { unsigned char data[32]; return hid_get_report_descriptor((hid_device *)0, data, sizeof data); }")
	target_sources(st_controller PRIVATE controller_hid.c)
	target_link_libraries(st_controller PRIVATE ${ST_DEP_HIDAPI_TARGET})
	if(APPLE)
		set(ST_CONTROLLER_DARWIN_HID ON)
	endif()
else()
	find_package(hidapi 0.14 QUIET CONFIG)
	if(TARGET hidapi::hidraw)
		set(_st_hidapi hidapi::hidraw)
	elseif(TARGET hidapi::netbsd)
		set(_st_hidapi hidapi::netbsd)
	endif()
	find_package(PkgConfig QUIET)
	if(PkgConfig_FOUND AND (NOT CMAKE_CROSSCOMPILING OR DEFINED ENV{PKG_CONFIG_SYSROOT_DIR}))
		if(NOT _st_hidapi)
			pkg_search_module(ST_CONTROLLER_HID QUIET IMPORTED_TARGET hidapi-hidraw>=0.14 hidapi-netbsd>=0.14)
			if(ST_CONTROLLER_HID_FOUND)
				set(_st_hidapi PkgConfig::ST_CONTROLLER_HID)
			endif()
		endif()
		pkg_check_modules(ST_CONTROLLER_USB QUIET IMPORTED_TARGET libusb-1.0)
	endif()
	if(_st_hidapi)
		target_sources(st_controller PRIVATE controller_hid.c)
		target_link_libraries(st_controller PRIVATE ${_st_hidapi})
	elseif(ST_CONTROLLER_USB_FOUND)
		target_sources(st_controller PRIVATE controller_usb.c)
		target_link_libraries(st_controller PRIVATE PkgConfig::ST_CONTROLLER_USB)
	else()
		target_sources(st_controller PRIVATE controller_nohid.c)
	endif()
endif()
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
