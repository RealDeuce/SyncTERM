# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
include(CMakePushCheckState)
foreach(feature OPENGL SLANG)
	set(ST_CONSOLE_${feature} AUTO CACHE STRING "${feature} scaler support: AUTO, ON, OFF")
	set_property(CACHE ST_CONSOLE_${feature} PROPERTY STRINGS AUTO ON OFF)
	if(NOT ST_CONSOLE_${feature} MATCHES "^(AUTO|ON|OFF)$")
		message(FATAL_ERROR "Invalid ST_CONSOLE_${feature}")
	endif()
endforeach()

if(NOT ST_CONSOLE_OPENGL STREQUAL "OFF")
	find_package(OpenGL QUIET)
	cmake_push_check_state(RESET)
	if(ST_CONSOLE_HAVE_X11 AND TARGET OpenGL::GL)
		set(CMAKE_REQUIRED_LIBRARIES OpenGL::GL)
		syncterm_check_c17_source("#include <GL/glx.h>
			int main(void) { int n; GLXFBConfig *c = glXChooseFBConfig(0,0,0,&n);
			return !glXCreateNewContext(0,*c,GLX_RGBA_TYPE,0,1) || !glXGetProcAddressARB((const GLubyte*)\"glViewport\"); }"
			ST_GLX_API)
		if(ST_GLX_API)
			set(ST_CONSOLE_HAVE_GLX 1)
			list(APPEND _st_console_dependencies OpenGL::GL)
		endif()
	endif()
	if(ST_CONSOLE_HAVE_WAYLAND AND PkgConfig_FOUND)
		pkg_check_modules(ST_GL_EGL QUIET IMPORTED_TARGET egl wayland-egl)
		if(TARGET PkgConfig::ST_GL_EGL)
			set(CMAKE_REQUIRED_LIBRARIES PkgConfig::ST_GL_EGL)
			syncterm_check_c17_source("#include <EGL/egl.h>
				#include <EGL/eglext.h>
				#include <wayland-egl-core.h>
				int main(void) { return !eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR,0,0)
				|| !eglCreatePlatformWindowSurface(0,0,wl_egl_window_create(0,1,1),0); }" ST_WAYLAND_EGL_API)
			if(ST_WAYLAND_EGL_API)
				set(ST_CONSOLE_HAVE_WAYLAND_EGL 1)
				list(APPEND _st_console_dependencies PkgConfig::ST_GL_EGL)
			endif()
		endif()
	endif()
	if(ST_CONSOLE_HAVE_GDI AND TARGET OpenGL::GL)
		set(CMAKE_REQUIRED_LIBRARIES OpenGL::GL gdi32)
		syncterm_check_c17_source("#include <windows.h>
			#include <GL/gl.h>
			int main(void) { HDC dc = GetDC(0); HGLRC ctx = wglCreateContext(dc);
			return !wglMakeCurrent(dc,ctx) || !wglGetProcAddress(\"glViewport\") || !SwapBuffers(dc); }" ST_WGL_API)
		if(ST_WGL_API)
			set(ST_CONSOLE_HAVE_WGL 1)
			list(APPEND _st_console_dependencies OpenGL::GL)
		endif()
	endif()
	if(ST_CONSOLE_HAVE_QUARTZ AND TARGET OpenGL::GL)
		include(CheckOBJCSourceCompiles)
		set(CMAKE_REQUIRED_LIBRARIES OpenGL::GL ${ST_CONSOLE_APPKIT_FRAMEWORK})
		check_objc_source_compiles("#define GL_SILENCE_DEPRECATION
			#import <AppKit/AppKit.h>
			int main(void) { NSOpenGLPixelFormatAttribute a[] = {NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion4_1Core, 0};
			NSOpenGLPixelFormat *f = [[NSOpenGLPixelFormat alloc] initWithAttributes:a];
			NSOpenGLContext *c = [[NSOpenGLContext alloc] initWithFormat:f shareContext:nil];
			[c makeCurrentContext]; [c flushBuffer]; return 0; }" ST_NSOPENGL_API)
		if(ST_NSOPENGL_API)
			set(ST_CONSOLE_HAVE_NSOPENGL 1)
			list(APPEND _st_console_dependencies OpenGL::GL)
		endif()
	endif()
	if(ST_CONSOLE_HAVE_SDL)
		if(ST_CONSOLE_SDL_INCLUDE_DIRS)
			set(CMAKE_REQUIRED_INCLUDES ${ST_CONSOLE_SDL_INCLUDE_DIRS})
			set(CMAKE_REQUIRED_LIBRARIES)
			syncterm_check_c17_source("#define SDL_MAIN_HANDLED
				#include <SDL.h>
				int main(void) { return !sizeof(&SDL_GL_CreateContext) || !sizeof(&SDL_GL_GetDrawableSize)
				|| !sizeof(&SDL_GL_GetCurrentWindow) || !sizeof(&SDL_GL_GetProcAddress); }" ST_SDL_GL_API)
			set(ST_CONSOLE_HAVE_SDL_GL ${ST_SDL_GL_API})
		else()
			# The selected dependency supplies the SDL 2 API when it is built.
			set(ST_CONSOLE_HAVE_SDL_GL 1)
		endif()
	endif()
	cmake_pop_check_state()
endif()

if(ST_CONSOLE_HAVE_GLX OR ST_CONSOLE_HAVE_WAYLAND_EGL OR ST_CONSOLE_HAVE_WGL OR ST_CONSOLE_HAVE_NSOPENGL OR ST_CONSOLE_HAVE_SDL_GL)
	set(ST_CONSOLE_HAVE_OPENGL 1)
elseif(ST_CONSOLE_OPENGL STREQUAL "ON" OR ST_CONSOLE_SLANG STREQUAL "ON")
	message(FATAL_ERROR "OpenGL scaling requires a GL-capable window backend")
endif()

if(ST_CONSOLE_HAVE_OPENGL AND NOT ST_CONSOLE_SLANG STREQUAL "OFF")
	syncterm_dependency_options(LIBRASHADER RUNTIME)
	set(required)
	if(ST_CONSOLE_SLANG STREQUAL "ON" OR NOT ST_DEP_LIBRASHADER_LINKAGE STREQUAL "AUTO" OR NOT ST_DEP_LIBRASHADER_SOURCE STREQUAL "AUTO")
		set(required REQUIRED)
	endif()
	set(probe "#define LIBRA_RUNTIME_OPENGL\n#include <librashader.h>\n
		_Static_assert(LIBRASHADER_CURRENT_ABI == 2, \"librashader ABI 2 required\");\n
		int main(void) { libra_preset_opt_t options = {.version = 2, .original_aspect_uniforms = true, .frametime_uniforms = true}; (void)options;\n")
	foreach(symbol instance_abi_version instance_api_version error_write error_free error_free_string
		preset_create_with_options preset_free gl_filter_chain_create gl_filter_chain_frame gl_filter_chain_free)
		string(APPEND probe "PFN_libra_${symbol} volatile p_${symbol} = libra_${symbol}; (void)p_${symbol};\n")
	endforeach()
	string(APPEND probe "return 0; }")
	syncterm_dependency(NAME LIBRASHADER PACKAGE librashader HEADER librashader.h LIBRARIES rashader
		DEFAULT RUNTIME ${required} PROBE "${probe}")
	if(ST_DEP_LIBRASHADER_TARGET)
		set(ST_CONSOLE_HAVE_SLANG 1)
		list(APPEND _st_console_dependencies ${ST_DEP_LIBRASHADER_TARGET})
		if(NOT ST_DEP_LIBRASHADER_MODE STREQUAL "RUNTIME")
			set(ST_DEP_LIBRASHADER_LINKED 1)
		endif()
	endif()
endif()
foreach(shader gl pointy)
	set(shader_file "console_${shader}.frag")
	set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${shader_file}")
	file(READ "${CMAKE_CURRENT_SOURCE_DIR}/${shader_file}" source)
	string(REPLACE "\n" "\\n\",\n\"" ST_${shader}_SOURCE "${source}")
endforeach()
configure_file(console_gl_shader.h.in console_gl_shader.h @ONLY)
