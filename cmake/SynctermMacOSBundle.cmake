# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

include_guard(GLOBAL)

function(syncterm_macos_bundle target)
	if(NOT APPLE)
		return()
	endif()
	set(_resources "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../resources/macos")
	set_target_properties(${target} PROPERTIES
		MACOSX_BUNDLE TRUE
		OUTPUT_NAME SyncTERM
		MACOSX_BUNDLE_INFO_PLIST "${_resources}/Info.plist.in"
		MACOSX_BUNDLE_BUNDLE_NAME SyncTERM
		MACOSX_BUNDLE_GUI_IDENTIFIER net.bbsdev.syncterm
		MACOSX_BUNDLE_ICON_FILE SyncTERM.icns
		# Retain the legacy full/numeric version split, using the application's
		# Unicode prerelease spelling for the short version.
		MACOSX_BUNDLE_SHORT_VERSION_STRING "2.0α"
		MACOSX_BUNDLE_BUNDLE_VERSION 2.0
		MACOSX_BUNDLE_LONG_VERSION_STRING "2.0α"
		MACOSX_BUNDLE_INFO_STRING "SyncTERM 2.0α — an ANSI-BBS terminal supporting telnet, rlogin, and SSH"
		MACOSX_BUNDLE_COPYRIGHT "Copyright 2026 Stephen Hurd")
	target_sources(${target} PRIVATE "${_resources}/SyncTERM.icns")
	set_source_files_properties("${_resources}/SyncTERM.icns" PROPERTIES
		MACOSX_PACKAGE_LOCATION Resources)
endfunction()
