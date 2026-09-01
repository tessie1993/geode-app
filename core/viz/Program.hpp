#pragma once
#include <GLES3/gl3.h>

#include <string>
#include <unordered_map>

#include "viz/ProgramBinaryCache.hpp"

namespace geode::viz {

class UniformCache {
public:
    explicit UniformCache(GLuint program = 0) : program_(program) {}
    GLuint program() const { return program_; }
    GLint loc(const std::string& name);
    GLsizei arrayCount(const std::string& name, GLsizei declared);

private:
    GLuint program_;
    std::unordered_map<std::string, GLint> locations_;
    std::unordered_map<std::string, GLsizei> arraySizes_;
};

namespace program {

// Compiles one stage; 0 on failure with the driver log in `error`.
GLuint compile(GLenum type, const std::string& source, std::string* error);

// Links, restoring from the binary cache first; 0 on failure with the reason in `error`.
GLuint build(const std::string& vertexSrc, const std::string& fragmentSrc, ProgramBinaryCache* cache, std::string* error);

}  // namespace program

}  // namespace geode::viz
