#pragma once
#include <GLES3/gl3.h>

#include <algorithm>
#include <array>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "viz/gl/GlHandle.hpp"
#include "viz/gl/ProgramBinaryCache.hpp"
#include "viz/gl/UniformId.hpp"
#include "viz/gl/UniformMemo.hpp"

namespace geode::viz {

// Hashes a name without building a std::string: a lookup by const char* or string_view finds its entry in place.
struct UniformNameHash {
    using is_transparent = void;
    size_t operator()(std::string_view name) const noexcept { return std::hash<std::string_view>{}(name); }
};

class UniformCache {
public:
    explicit UniformCache(GLuint program = 0) : program_(program) {
        slots_.fill(kUnresolved);
        arraySlots_.fill(kUnresolved);
    }
    GLuint program() const { return program_; }
    GLint loc(std::string_view name);
    GLsizei arrayCount(std::string_view name, GLsizei declared);
    // The per-frame path: an interned name (viz/gl/UniformId.hpp) is looked up in this program once, then read from
    // a slot. A rebuilt program is a new UniformCache, so its slots start empty.
    GLint loc(Uniform id) {
        GLint& slot = slots_[static_cast<size_t>(id)];
        if (slot == kUnresolved) slot = loc(std::string_view(kUniformNames[static_cast<size_t>(id)]));
        return slot;
    }
    GLsizei arrayCount(Uniform id, GLsizei declared) {
        GLsizei& slot = arraySlots_[static_cast<size_t>(id)];
        if (slot == kUnresolved) slot = arraySize(kUniformNames[static_cast<size_t>(id)], declared);
        return std::min(slot, declared);
    }

    // Uploads that skip the driver call when the value is the one this program already holds (UniformMemo). A
    // location of -1 (the program never reads the name) is skipped too. A uniform written through these must not
    // also be written with a bare glUniform*, or the memo would not see that write.
    void set1f(Uniform id, float v) {
        const GLint at = loc(id);
        if (at >= 0 && memo_.changed(index(id), &v, sizeof v)) glUniform1f(at, v);
    }
    void set2f(Uniform id, float a, float b) {
        const GLint at = loc(id);
        const float v[2] = {a, b};
        if (at >= 0 && memo_.changed(index(id), v, sizeof v)) glUniform2f(at, a, b);
    }
    void set3f(Uniform id, float a, float b, float c) {
        const GLint at = loc(id);
        const float v[3] = {a, b, c};
        if (at >= 0 && memo_.changed(index(id), v, sizeof v)) glUniform3f(at, a, b, c);
    }
    void set4f(Uniform id, float a, float b, float c, float d) {
        const GLint at = loc(id);
        const float v[4] = {a, b, c, d};
        if (at >= 0 && memo_.changed(index(id), v, sizeof v)) glUniform4f(at, a, b, c, d);
    }
    void set1i(Uniform id, GLint v) {
        const GLint at = loc(id);
        if (at >= 0 && memo_.changed(index(id), &v, sizeof v)) glUniform1i(at, v);
    }
    // `count` vectors of `components` floats (glUniform{1..4}fv).
    void setFloats(Uniform id, int components, GLsizei count, const float* data) {
        const GLint at = loc(id);
        if (at < 0 || count <= 0) return;
        const size_t bytes = sizeof(float) * static_cast<size_t>(components) * static_cast<size_t>(count);
        if (!memo_.changed(index(id), data, bytes)) return;
        switch (components) {
            case 1: glUniform1fv(at, count, data); break;
            case 2: glUniform2fv(at, count, data); break;
            case 3: glUniform3fv(at, count, data); break;
            default: glUniform4fv(at, count, data); break;
        }
    }
    // Every remembered value is unknown again (the program's uniforms were written some other way).
    void forgetValues() { memo_.clear(); }

private:
    static size_t index(Uniform id) { return static_cast<size_t>(id); }
    static constexpr GLint kUnresolved = -2;
    // The live length of an array uniform, or `declared` when the program does not report one.
    GLsizei arraySize(std::string_view name, GLsizei declared);

    GLuint program_;
    UniformMemo memo_{static_cast<size_t>(Uniform::kCount)};
    std::array<GLint, static_cast<size_t>(Uniform::kCount)> slots_;
    std::array<GLsizei, static_cast<size_t>(Uniform::kCount)> arraySlots_;
    std::unordered_map<std::string, GLint, UniformNameHash, std::equal_to<>> locations_;
    std::unordered_map<std::string, GLsizei, UniformNameHash, std::equal_to<>> arraySizes_;
};

// A program its holder built and deletes, with the uniform locations read from it (a UniformCache that owns
// its program). Moves, never copies; the moved-from one holds 0. reset() deletes the program held and takes
// another, release() deletes it, forget() drops it without a GL call after context loss (viz/gl/GlHandle.hpp).
class OwnedProgram : public UniformCache {
public:
    OwnedProgram() = default;
    explicit OwnedProgram(GLuint program) : UniformCache(program), owner_(program) {}
    OwnedProgram(const OwnedProgram&) = delete;
    OwnedProgram& operator=(const OwnedProgram&) = delete;
    OwnedProgram(OwnedProgram&& other) noexcept : UniformCache(std::move(other)), owner_(std::move(other.owner_)) {
        other.UniformCache::operator=(UniformCache());
    }
    OwnedProgram& operator=(OwnedProgram&& other) noexcept {
        if (this != &other) {
            owner_ = std::move(other.owner_);
            UniformCache::operator=(std::move(other));
            other.UniformCache::operator=(UniformCache());
        }
        return *this;
    }
    ~OwnedProgram() = default;

    void reset(GLuint program = 0) {
        owner_.reset(program);
        UniformCache::operator=(UniformCache(program));
    }
    void release() { reset(); }
    void forget() {
        owner_.forget();
        UniformCache::operator=(UniformCache());
    }

private:
    GlProgram owner_;
};

namespace program {

// A shader's (isProgram = false) or program's info log.
std::string infoLog(GLuint object, bool isProgram);

// Compiles one stage; 0 on failure with the driver log in `error`.
GLuint compile(GLenum type, const std::string& source, std::string* error);

// Links, restoring from the binary cache first; 0 on failure with the reason in `error`. A null `cache`
// neither reads nor stores a binary, staged or not.
GLuint build(const std::string& vertexSrc, const std::string& fragmentSrc, ProgramBinaryCache* cache, std::string* error);

}  // namespace program

}  // namespace geode::viz
