#pragma once
#include <GLES3/gl3.h>

#include <utility>

namespace geode::viz {

// One GL object name and the duty to delete it, held by exactly one owner.
//
// A GL name is a number, so a struct that keeps one in a GLuint and deletes it in release() or its destructor
// gives every copy of itself the same duty: two copies delete the name twice, and once one has deleted it the
// other draws with a name GL may already have handed to something new. A handle cannot be copied; a move
// hands the name over and leaves the source holding 0, and 0 is never deleted.
//
// GL thread only, with the context that made the name current: reset() and the destructor call GL. When that
// context is gone, forget() drops the name without a GL call (a new context numbers its names from 1 again,
// so deleting an old number there could delete a new object).
//
// A type that owns several names and is never handed on (a scene, a pass) need not be built from handles,
// but it deletes its copy constructor and copy assignment so it can be neither copied nor moved.
// core/tests/gl_ownership_test.cpp checks both rules.
template <void (*Delete)(GLuint)>
class GlHandle {
public:
    GlHandle() = default;
    explicit GlHandle(GLuint name) : name_(name) {}
    ~GlHandle() { reset(); }

    GlHandle(const GlHandle&) = delete;
    GlHandle& operator=(const GlHandle&) = delete;
    GlHandle(GlHandle&& other) noexcept : name_(std::exchange(other.name_, 0)) {}
    GlHandle& operator=(GlHandle&& other) noexcept {
        if (this != &other) {
            reset();
            name_ = std::exchange(other.name_, 0);
        }
        return *this;
    }

    GLuint get() const { return name_; }
    explicit operator bool() const { return name_ != 0; }

    // Deletes the name held (if any) and holds `name` instead.
    void reset(GLuint name = 0) {
        if (name_ != 0) Delete(name_);
        name_ = name;
    }
    // The context that made the name is gone: drop it without a GL call.
    void forget() { name_ = 0; }

private:
    GLuint name_ = 0;
};

namespace gl_delete {
inline void program(GLuint name) { glDeleteProgram(name); }
inline void vertexArray(GLuint name) { glDeleteVertexArrays(1, &name); }
inline void buffer(GLuint name) { glDeleteBuffers(1, &name); }
inline void texture(GLuint name) { glDeleteTextures(1, &name); }
inline void framebuffer(GLuint name) { glDeleteFramebuffers(1, &name); }
}  // namespace gl_delete

using GlProgram = GlHandle<gl_delete::program>;
using GlVertexArray = GlHandle<gl_delete::vertexArray>;
using GlBuffer = GlHandle<gl_delete::buffer>;
using GlTexture = GlHandle<gl_delete::texture>;
using GlFramebuffer = GlHandle<gl_delete::framebuffer>;

}  // namespace geode::viz
