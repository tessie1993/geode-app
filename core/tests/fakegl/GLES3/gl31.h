/* Host-test stand-in for <GLES3/gl31.h>: the ES 3.1 additions core/viz/Compute.cpp and core/viz/Compute.hpp name
 * (core/tests/gl_ownership_test.cpp), over the ES 3.0 fake beside it. Declarations only, real enum values;
 * the test that links Compute.cpp defines every function. Use it with -Icore/tests/fakegl. */
#pragma once

#include <GLES3/gl3.h>

#define GL_NO_ERROR 0
#define GL_RGBA8 0x8058
#define GL_R32F 0x822E
#define GL_MAX_COMPUTE_SHARED_MEMORY_SIZE 0x8262
#define GL_COMPUTE_WORK_GROUP_SIZE 0x8267
#define GL_RGBA16F 0x881A
#define GL_READ_ONLY 0x88B8
#define GL_WRITE_ONLY 0x88B9
#define GL_READ_WRITE 0x88BA
#define GL_RGBA32UI 0x8D70
#define GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS 0x90EB
#define GL_COMPUTE_SHADER 0x91B9
#define GL_MAX_COMPUTE_TEXTURE_IMAGE_UNITS 0x91BC
#define GL_MAX_COMPUTE_WORK_GROUP_COUNT 0x91BE
#define GL_MAX_COMPUTE_WORK_GROUP_SIZE 0x91BF

#define GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT 0x00000001
#define GL_ELEMENT_ARRAY_BARRIER_BIT 0x00000002
#define GL_UNIFORM_BARRIER_BIT 0x00000004
#define GL_TEXTURE_FETCH_BARRIER_BIT 0x00000008
#define GL_SHADER_IMAGE_ACCESS_BARRIER_BIT 0x00000020
#define GL_COMMAND_BARRIER_BIT 0x00000040
#define GL_PIXEL_BUFFER_BARRIER_BIT 0x00000080
#define GL_TEXTURE_UPDATE_BARRIER_BIT 0x00000100
#define GL_BUFFER_UPDATE_BARRIER_BIT 0x00000200
#define GL_FRAMEBUFFER_BARRIER_BIT 0x00000400
#define GL_TRANSFORM_FEEDBACK_BARRIER_BIT 0x00000800
#define GL_ATOMIC_COUNTER_BARRIER_BIT 0x00001000
#define GL_SHADER_STORAGE_BARRIER_BIT 0x00002000

#ifdef __cplusplus
extern "C" {
#endif

void glDispatchCompute(GLuint num_groups_x, GLuint num_groups_y, GLuint num_groups_z);
void glMemoryBarrier(GLbitfield barriers);
void glBindImageTexture(GLuint unit, GLuint texture, GLint level, GLboolean layered, GLint layer, GLenum access, GLenum format);
void glGetIntegeri_v(GLenum target, GLuint index, GLint* data);

#ifdef __cplusplus
}
#endif
