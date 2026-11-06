/* Host-test stand-in for <GLES3/gl3.h>: the GL system boundary, faked (core/tests/edge_glow_pass_test.cpp).
 *
 * Only the types, enums and entry points that core/viz/frame/EdgeGlowPass.cpp and core/viz/Program.cpp
 * (and the headers they include) name. Declarations only; the test defines every function as a small
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

#ifdef __cplusplus
}
#endif
