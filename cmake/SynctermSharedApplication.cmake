# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause

# Only libraries reachable from the shared application boundary need PIC and
# hidden implementation symbols. Imported dependencies keep their own policy.
function(syncterm_shared_dependencies target)
	if(NOT TARGET "${target}")
		return()
	endif()
	get_target_property(_alias "${target}" ALIASED_TARGET)
	if(_alias)
		set(target "${_alias}")
	endif()
	get_target_property(_imported "${target}" IMPORTED)
	get_target_property(_visited "${target}" SYNCTERM_SHARED_DEPENDENCY)
	if(_imported OR _visited)
		return()
	endif()
	set_property(TARGET "${target}" PROPERTY SYNCTERM_SHARED_DEPENDENCY TRUE)
	get_target_property(_type "${target}" TYPE)
	if(_type STREQUAL "STATIC_LIBRARY" OR _type STREQUAL "OBJECT_LIBRARY")
		set_target_properties("${target}" PROPERTIES
			POSITION_INDEPENDENT_CODE YES
			C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden)
	endif()
	get_target_property(_links "${target}" LINK_LIBRARIES)
	get_target_property(_interface "${target}" INTERFACE_LINK_LIBRARIES)
	foreach(_dependency IN LISTS _links _interface)
		if(_dependency MATCHES "^\\$<LINK_ONLY:(.+)>$")
			set(_dependency "${CMAKE_MATCH_1}")
		endif()
		syncterm_shared_dependencies("${_dependency}")
	endforeach()
endfunction()
