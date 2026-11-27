/* OpenGL 3.3 declarations used by the window renderer.
 * Extracted from the Khronos OpenGL API registry, glcorearb.h.
 * Copyright 2013-2020 The Khronos Group Inc. SPDX-License-Identifier: MIT */
#ifndef ST_CONSOLE_GL_API_H_
#define ST_CONSOLE_GL_API_H_
#include "console_config.h"
#if ST_CONSOLE_HAVE_GDI
 #include <windows.h>
 #define ST_GL_CALL APIENTRY
#else
 #define ST_GL_CALL
#endif
typedef unsigned int GLenum, GLuint, GLbitfield;
typedef int          GLint, GLsizei;
typedef float        GLfloat;
typedef char         GLchar;
#define GL_MAJOR_VERSION            0x821B
#define GL_MINOR_VERSION            0x821C
#define GL_TEXTURE_2D               0x0DE1
#define GL_TEXTURE0                 0x84C0
#define GL_TEXTURE_MAX_LEVEL        0x813D
#define GL_TEXTURE_MIN_FILTER       0x2801
#define GL_TEXTURE_MAG_FILTER       0x2800
#define GL_NEAREST                  0x2600
#define GL_UNPACK_ROW_LENGTH        0x0CF2
#define GL_UNPACK_ALIGNMENT         0x0CF5
#define GL_BGRA                     0x80E1
#define GL_UNSIGNED_INT_8_8_8_8_REV 0x8367
#define GL_RGBA8                    0x8058
#define GL_RGBA                     0x1908
#define GL_UNSIGNED_BYTE            0x1401
#define GL_VERTEX_SHADER            0x8B31
#define GL_FRAGMENT_SHADER          0x8B30
#define GL_COMPILE_STATUS           0x8B81
#define GL_LINK_STATUS              0x8B82
#define GL_FRAMEBUFFER              0x8D40
#define GL_COLOR_BUFFER_BIT         0x00004000
#define GL_BLEND                    0x0BE2
#define GL_DEPTH_TEST               0x0B71
#define GL_DITHER                   0x0BD0
#define GL_SCISSOR_TEST             0x0C11
#define GL_FRAMEBUFFER_SRGB         0x8DB9
#define GL_TRIANGLES                0x0004
#define GL_NO_ERROR                 0
#define GL_TEXTURE_SWIZZLE_A        0x8E45
#define GL_ONE                      1

#define GL_COLOR_ATTACHMENT0    0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5

struct st_gl_api {
	void(ST_GL_CALL *GenFramebuffers)(GLsizei count, GLuint *buffers);
	void(ST_GL_CALL *DeleteFramebuffers)(GLsizei count, const GLuint *buffers);
	void(ST_GL_CALL *FramebufferTexture2D)(GLenum target, GLenum attachment, GLenum type, GLuint texture,
	    GLint level);
	GLenum(ST_GL_CALL *CheckFramebufferStatus)(GLenum target);

	void(ST_GL_CALL *GetIntegerv)(GLenum pname, GLint *data);
	GLenum(ST_GL_CALL *GetError)(void);
	void(ST_GL_CALL *GenTextures)(GLsizei n, GLuint *textures);
	void(ST_GL_CALL *DeleteTextures)(GLsizei n, const GLuint *textures);
	void(ST_GL_CALL *BindTexture)(GLenum target, GLuint texture);
	void(ST_GL_CALL *ActiveTexture)(GLenum texture);
	void(ST_GL_CALL *TexImage2D)(GLenum target, GLint level, GLint internalformat, GLsizei width,
	    GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels);
	void(ST_GL_CALL *TexSubImage2D)(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width,
	    GLsizei height, GLenum format, GLenum type, const void *pixels);
	void(ST_GL_CALL *TexParameteri)(GLenum target, GLenum pname, GLint param);
	void(ST_GL_CALL *PixelStorei)(GLenum pname, GLint param);
	void(ST_GL_CALL *GenVertexArrays)(GLsizei n, GLuint *arrays);
	void(ST_GL_CALL *BindVertexArray)(GLuint array);
	void(ST_GL_CALL *DeleteVertexArrays)(GLsizei n, const GLuint *arrays);
	GLuint(ST_GL_CALL *CreateShader)(GLenum type);
	void(ST_GL_CALL *ShaderSource)(GLuint shader, GLsizei count, const GLchar *const *string,
	    const GLint *length);
	void(ST_GL_CALL *CompileShader)(GLuint shader);
	void(ST_GL_CALL *GetShaderiv)(GLuint shader, GLenum pname, GLint *params);
	void(ST_GL_CALL *GetShaderInfoLog)(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
	void(ST_GL_CALL *DeleteShader)(GLuint shader);
	GLuint(ST_GL_CALL *CreateProgram)(void);
	void(ST_GL_CALL *AttachShader)(GLuint program, GLuint shader);
	void(ST_GL_CALL *LinkProgram)(GLuint program);
	void(ST_GL_CALL *GetProgramiv)(GLuint program, GLenum pname, GLint *params);
	void(ST_GL_CALL *GetProgramInfoLog)(GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
	void(ST_GL_CALL *DeleteProgram)(GLuint program);
	void(ST_GL_CALL *UseProgram)(GLuint program);
	GLint(ST_GL_CALL *GetUniformLocation)(GLuint program, const GLchar *name);
	void(ST_GL_CALL *Uniform1i)(GLint location, GLint v0);
	void(ST_GL_CALL *Uniform2i)(GLint location, GLint v0, GLint v1);
	void(ST_GL_CALL *Viewport)(GLint x, GLint y, GLsizei width, GLsizei height);
	void(ST_GL_CALL *ClearColor)(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
	void(ST_GL_CALL *Clear)(GLbitfield mask);
	void(ST_GL_CALL *Disable)(GLenum cap);
	void(ST_GL_CALL *DrawArrays)(GLenum mode, GLint first, GLsizei count);
	void(ST_GL_CALL *BindFramebuffer)(GLenum target, GLuint framebuffer);
	void(ST_GL_CALL *BindSampler)(GLuint unit, GLuint sampler);
};
#endif
