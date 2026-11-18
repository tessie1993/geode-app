#pragma once
#include <GLES3/gl3.h>

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "viz/GlHandle.hpp"
#include "viz/ProgramBinaryCache.hpp"

namespace geode::viz {

// Hashes a name without building a std::string: a lookup by const char* or string_view finds its entry in place.
struct UniformNameHash {
    using is_transparent = void;
    size_t operator()(std::string_view name) const noexcept { return std::hash<std::string_view>{}(name); }
};

class UniformCache {
public:
    explicit UniformCache(GLuint program = 0) : program_(program) {}
    GLuint program() const { return program_; }
    GLint loc(std::string_view name);
    GLsizei arrayCount(std::string_view name, GLsizei declared);

private:
    GLuint program_;
    std::unordered_map<std::string, GLint, UniformNameHash, std::equal_to<>> locations_;
    std::unordered_map<std::string, GLsizei, UniformNameHash, std::equal_to<>> arraySizes_;
};

// A program its holder built and deletes, with the uniform locations read from it (a UniformCache that owns
// its program). Moves, never copies; the moved-from one holds 0. reset() deletes the program held and takes
// another, release() deletes it, forget() drops it without a GL call after context loss (viz/GlHandle.hpp).
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

// Links, restoring from the binary cache first; 0 on failure with the reason in `error`.
GLuint build(const std::string& vertexSrc, const std::string& fragmentSrc, ProgramBinaryCache* cache, std::string* error);

}  // namespace program

}  // namespace geode::viz
