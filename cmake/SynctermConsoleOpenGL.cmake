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
	if(ST_CONSOLE_HAVE_GDI OR ST_CONSOLE_HAVE_QUARTZ)
		find_package(OpenGL QUIET)
	endif()
	cmake_push_check_state(RESET)
	if(ST_CONSOLE_HAVE_X11)
		st_console_dependency(GLX gl GL/glx.h GL RUNTIME INSTALLED_ONLY
			SYMBOLS glXChooseFBConfig glXCreateNewContext glXGetProcAddressARB glXMakeCurrent
			glXQueryVersion glXSwapBuffers glXDestroyContext)
		if(ST_DEP_GLX_TARGET)
			set(ST_CONSOLE_HAVE_GLX 1)
		endif()
	endif()
	if(ST_CONSOLE_HAVE_WAYLAND)
		st_console_dependency(EGL egl EGL/egl.h EGL RUNTIME INSTALLED_ONLY HEADERS EGL/eglext.h
			SYMBOLS eglGetPlatformDisplay eglCreatePlatformWindowSurface eglGetProcAddress eglChooseConfig
			eglInitialize eglBindAPI eglCreateContext eglMakeCurrent eglSwapBuffers eglSwapInterval
			eglDestroyContext eglDestroySurface eglTerminate)
		st_console_dependency(WAYLAND_EGL wayland-egl wayland-egl-core.h wayland-egl RUNTIME INSTALLED_ONLY
			SYMBOLS wl_egl_window_create wl_egl_window_resize wl_egl_window_destroy)
		if(ST_DEP_EGL_TARGET AND ST_DEP_WAYLAND_EGL_TARGET)
			set(ST_CONSOLE_HAVE_WAYLAND_EGL 1)
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
