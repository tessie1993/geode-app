#include "viz/Program.hpp"

#include <algorithm>
#include <vector>

namespace geode::viz {

GLint UniformCache::loc(const std::string& name) {
    const auto it = locations_.find(name);
    if (it != locations_.end()) return it->second;
    const GLint location = glGetUniformLocation(program_, name.c_str());
    locations_.emplace(name, location);
    return location;
}

GLsizei UniformCache::arrayCount(const std::string& name, GLsizei declared) {
    const auto it = arraySizes_.find(name);
    if (it != arraySizes_.end()) return std::min(it->second, declared);
    const std::string first = name + "[0]";
    const char* names[1] = {first.c_str()};
    GLuint index = GL_INVALID_INDEX;
    glGetUniformIndices(program_, 1, names, &index);
    if (index == GL_INVALID_INDEX) {
        names[0] = name.c_str();
        glGetUniformIndices(program_, 1, names, &index);
    }
    GLsizei size = declared;
    if (index != GL_INVALID_INDEX) {
        GLint reported = 0;
        glGetActiveUniformsiv(program_, 1, &index, GL_UNIFORM_SIZE, &reported);
        size = std::max(reported, 1);
    }
    arraySizes_.emplace(name, size);
    return std::min(size, declared);
}

namespace program {

namespace {

std::string infoLog(GLuint object, bool isProgram) {
    GLint length = 0;
    if (isProgram) glGetProgramiv(object, GL_INFO_LOG_LENGTH, &length); else glGetShaderiv(object, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<size_t>(std::max(length, 1)), '\0');
    GLsizei written = 0;
    if (isProgram) glGetProgramInfoLog(object, length, &written, log.data()); else glGetShaderInfoLog(object, length, &written, log.data());
    log.resize(static_cast<size_t>(std::max(written, 0)));
    return log;
}

}  // namespace

GLuint compile(GLenum type, const std::string& source, std::string* error) {
    const GLuint shader = glCreateShader(type);
    const char* text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == 0) {
        if (error) *error = infoLog(shader, false);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint build(const std::string& vertexSrc, const std::string& fragmentSrc, ProgramBinaryCache* cache, std::string* error) {
    const auto key = cache ? cache->keyFor(vertexSrc, fragmentSrc) : std::nullopt;
    if (key) {
        const GLuint cached = cache->load(*key);
        if (cached != 0) return cached;
    }
    const GLuint vs = compile(GL_VERTEX_SHADER, vertexSrc, error);
    if (vs == 0) return 0;
    const GLuint fs = compile(GL_FRAGMENT_SHADER, fragmentSrc, error);
    if (fs == 0) {
        glDeleteShader(vs);
        return 0;
    }
    const GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    if (key) cache->markRetrievable(prog);
    glLinkProgram(prog);
    GLint status = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &status);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (status == 0) {
        if (error) *error = "Link failed: " + infoLog(prog, true);
        glDeleteProgram(prog);
        return 0;
    }
    if (key) cache->store(*key, prog);
    return prog;
}

}  // namespace program

}  // namespace geode::viz
