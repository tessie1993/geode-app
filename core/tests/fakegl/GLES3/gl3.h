/* Host-test stand-in for <GLES3/gl3.h>: the GL system boundary, faked (core/tests/edge_glow_pass_test.cpp).
 *
 * Only the types, enums and entry points that core/viz/frame/EdgeGlowPass.cpp, core/viz/Program.cpp and
 * core/viz/Transition.cpp (core/tests/transition_programs_test.cpp), core/viz/Quad.cpp and core/viz/GlHandle.hpp
 * (core/tests/gl_ownership_test.cpp, with core/viz/Framebuffer.cpp and core/viz/Texture.cpp) and the headers they include name. Declarations only; the test defines every function as a small
 * model of a GL context: names that are generated, live and deleted, a bound program, VAO and framebuffer,
 * the blend state, and one probe pixel per framebuffer that a draw blends into. Enum values are the real
 * ones from the Khronos header, so a mixed-up enum still reads as the wrong one.
 *
 * Use it with -Icore/tests/fakegl and WITHOUT -Icore/tests/stub (this directory has its own android/). */
#pragma once

#include <cstddef>
#include <cstdint>

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef char GLchar;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;

#define GL_ZERO 0
#define GL_ONE 1
#define GL_TRIANGLES 0x0004
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_FUNC_ADD 0x8006
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_STENCIL_TEST 0x0B90
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_UNIFORM_SIZE 0x8A38
#define GL_FRAMEBUFFER 0x8D40
#define GL_INVALID_INDEX 0xFFFFFFFFu
#define GL_FALSE 0
#define GL_TRUE 1
#define GL_FLOAT 0x1406
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_TEXTURE_2D 0x0DE1
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_SAMPLE_ALPHA_TO_COVERAGE 0x809E
#define GL_TEXTURE0 0x84C0
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_PIXEL_PACK_BUFFER 0x88EB
#define GL_PIXEL_UNPACK_BUFFER 0x88EC
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_UNSIGNED_BYTE 0x1401
#define GL_RGBA 0x1908
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_RGBA8 0x8058
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_COLOR_ATTACHMENT1 0x8CE1

typedef ptrdiff_t GLsizeiptr;

#ifdef __cplusplus
extern "C" {
#endif

/* Names. */
GLuint glCreateShader(GLenum type);
void glDeleteShader(GLuint shader);
GLuint glCreateProgram(void);
void glDeleteProgram(GLuint program);
void glGenVertexArrays(GLsizei n, GLuint* arrays);
void glDeleteVertexArrays(GLsizei n, const GLuint* arrays);
void glGenBuffers(GLsizei n, GLuint* buffers);
void glDeleteBuffers(GLsizei n, const GLuint* buffers);
void glDeleteTextures(GLsizei n, const GLuint* textures);
void glDeleteFramebuffers(GLsizei n, const GLuint* framebuffers);
void glGenTextures(GLsizei n, GLuint* textures);
void glGenFramebuffers(GLsizei n, GLuint* framebuffers);
void glGenSamplers(GLsizei count, GLuint* samplers);
void glDeleteSamplers(GLsizei count, const GLuint* samplers);
void glDetachShader(GLuint program, GLuint shader);
GLenum glGetError(void);

/* Building. */
void glShaderSource(GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length);
void glCompileShader(GLuint shader);
void glGetShaderiv(GLuint shader, GLenum pname, GLint* params);
void glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
void glAttachShader(GLuint program, GLuint shader);
void glLinkProgram(GLuint program);
void glGetProgramiv(GLuint program, GLenum pname, GLint* params);
void glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog);

/* Uniforms. */
GLint glGetUniformLocation(GLuint program, const GLchar* name);
void glGetUniformIndices(GLuint program, GLsizei uniformCount, const GLchar* const* uniformNames, GLuint* uniformIndices);
void glGetActiveUniformsiv(GLuint program, GLsizei uniformCount, const GLuint* uniformIndices, GLenum pname, GLint* params);
void glUseProgram(GLuint program);
void glUniform1f(GLint location, GLfloat v0);
void glUniform1i(GLint location, GLint v0);
void glUniform2f(GLint location, GLfloat v0, GLfloat v1);
void glUniform2i(GLint location, GLint v0, GLint v1);
void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
void glUniform3i(GLint location, GLint v0, GLint v1, GLint v2);
void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);

/* State and drawing. */
void glBindFramebuffer(GLenum target, GLuint framebuffer);
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
void glEnable(GLenum cap);
void glDisable(GLenum cap);
void glBlendEquation(GLenum mode);
void glBlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha);
void glBindVertexArray(GLuint array);
void glDrawArrays(GLenum mode, GLint first, GLsizei count);
void glBindBuffer(GLenum target, GLuint buffer);
void glBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage);
void glEnableVertexAttribArray(GLuint index);
void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer);
void glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
void glDepthMask(GLboolean flag);
void glStencilMask(GLuint mask);
void glPixelStorei(GLenum pname, GLint param);
void glBindSampler(GLuint unit, GLuint sampler);
void glActiveTexture(GLenum texture);
void glBindTexture(GLenum target, GLuint texture);
void glTexParameteri(GLenum target, GLenum pname, GLint param);
void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type,
                  const void* pixels);
void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type,
                     const void* pixels);
void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
GLenum glCheckFramebufferStatus(GLenum target);
void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
void glClear(GLbitfield mask);
void glInvalidateFramebuffer(GLenum target, GLsizei numAttachments, const GLenum* attachments);

#ifdef __cplusplus
}
#endif
