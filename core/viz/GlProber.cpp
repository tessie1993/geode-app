#include "viz/GlProber.hpp"

#include <GLES3/gl31.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <sstream>
#include <vector>

#include "util/Log.hpp"
#include "viz/Half.hpp"

namespace geode::viz {

namespace {

constexpr const char* kTag = "GlProber";
constexpr int kProbeSize = 4;
constexpr int kFilterWidth = 2;
constexpr int kFilterHeight = 1;
constexpr const char* kTimerQueryExtension = "GL_EXT_disjoint_timer_query";
constexpr GLenum kTimeElapsedExt = 0x88BF;
constexpr GLenum kGpuDisjointExt = 0x8FBB;
constexpr uint64_t kTimerCeilingNs = 500000000ULL;
constexpr int kQueryPollLimit = 256;
constexpr int kErrorDrainLimit = 32;
constexpr float kFilterWindowLow = 0.30f;
constexpr float kFilterWindowHigh = 0.70f;
constexpr std::array<float, 4> kRenderInput = {0.25f, 0.5f, 0.75f, 1.0f};
constexpr std::array<float, 4> kBlendInput = {0.25f, 0.25f, 0.25f, 0.25f};
constexpr std::array<float, 4> kBlendExpected = {0.5f, 0.5f, 0.5f, 0.5f};
constexpr std::array<float, 4> kPackInput = {1.5f, -2.25f, 3.75f, 0.5f};
constexpr std::array<float, 4> kVertexFetchTexel = {64.0f / 255.0f, 128.0f / 255.0f, 192.0f / 255.0f, 1.0f};

constexpr const char* kFullscreenVert = R"(#version 300 es
precision highp float;
precision highp int;
void main() {
    vec2 corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
)";
constexpr const char* kFloatFrag = R"(#version 300 es
precision highp float;
precision highp int;
uniform vec4 uValue;
out vec4 fragColor;
void main() {
    fragColor = uValue;
}
)";
constexpr const char* kUintFrag = R"(#version 300 es
precision highp float;
precision highp int;
uniform vec4 uValue;
out uvec4 fragColor;
void main() {
    fragColor = floatBitsToUint(uValue);
}
)";
constexpr const char* kSampleFrag = R"(#version 300 es
precision highp float;
precision highp int;
uniform sampler2D uSrc;
out vec4 fragColor;
void main() {
    fragColor = vec4(texture(uSrc, vec2(0.5, 0.5)).r, 0.0, 0.0, 1.0);
}
)";
constexpr const char* kVtfVert = R"(#version 300 es
precision highp float;
precision highp int;
uniform sampler2D uSrc;
out vec4 vFetched;
void main() {
    vFetched = texelFetch(uSrc, ivec2(0, 0), 0);
    vec2 corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
)";
constexpr const char* kVtfFrag = R"(#version 300 es
precision highp float;
precision highp int;
in vec4 vFetched;
out vec4 fragColor;
void main() {
    fragColor = vFetched;
}
)";

struct FormatSpec {
    GLenum internalFormat;
    GLenum uploadFormat;
    GLenum uploadType;
    int components;
    float tolerance;
    bool integer;
};

const FormatSpec& specOf(ProbedFormat format) {
    static const std::array<FormatSpec, kProbedFormatCount> kSpecs = {{
        {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, 2.0f / 255.0f, false},
        {GL_R16F, GL_RED, GL_HALF_FLOAT, 1, 1e-3f, false},
        {GL_RG16F, GL_RG, GL_HALF_FLOAT, 2, 1e-3f, false},
        {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, 4, 1e-3f, false},
        {GL_R32F, GL_RED, GL_FLOAT, 1, 1e-6f, false},
        {GL_RGBA32UI, GL_RGBA_INTEGER, GL_UNSIGNED_INT, 4, 0.0f, true},
    }};
    return kSpecs[static_cast<int>(format)];
}

struct ProbeTarget {
    GLuint texture = 0;
    GLuint framebuffer = 0;
    bool complete = false;
};

int componentsOf(GLenum format) {
    switch (format) {
        case GL_RED: case GL_RED_INTEGER: return 1;
        case GL_RG: case GL_RG_INTEGER: return 2;
        case GL_RGB: case GL_RGB_INTEGER: return 3;
        case GL_RGBA: case GL_RGBA_INTEGER: return 4;
        default: return 0;
    }
}

int bytesOf(GLenum type) {
    switch (type) {
        case GL_UNSIGNED_BYTE: case GL_BYTE: return 1;
        case GL_HALF_FLOAT: case GL_UNSIGNED_SHORT: case GL_SHORT: return 2;
        case GL_FLOAT: case GL_UNSIGNED_INT: case GL_INT: return 4;
        default: return 0;
    }
}

std::string glString(GLenum name) {
    const GLubyte* s = glGetString(name);
    return s ? reinterpret_cast<const char*>(s) : "";
}

// Owns every GL object the probe creates so teardown has one place to go.
class GlArena {
public:
    ~GlArena() { releaseAll(); }

    GLuint vertexArray() {
        GLuint id = 0;
        glGenVertexArrays(1, &id);
        glBindVertexArray(id);
        glBindVertexArray(0);
        vertexArrays_.push_back(id);
        return id;
    }

    GLuint query() {
        GLuint id = 0;
        glGenQueries(1, &id);
        queries_.push_back(id);
        return id;
    }

    ProbeTarget target(const FormatSpec& spec) {
        ProbeTarget t;
        t.texture = storage(spec, kProbeSize, kProbeSize, GL_NEAREST);
        glGenFramebuffers(1, &t.framebuffer);
        framebuffers_.push_back(t.framebuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, t.framebuffer);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.texture, 0);
        const GLenum status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
        prober::drainErrors();
        t.complete = status == GL_FRAMEBUFFER_COMPLETE;
        return t;
    }

    GLuint filterSource(const FormatSpec& spec) {
        const GLuint texture = storage(spec, kFilterWidth, kFilterHeight, GL_LINEAR);
        std::vector<float> texels(static_cast<size_t>(kFilterWidth * spec.components), 0.0f);
        texels[0] = 0.0f;
        texels[spec.components] = 1.0f;
        upload(spec, kFilterWidth, kFilterHeight, texels);
        return texture;
    }

    GLuint vertexFetchSource() {
        const FormatSpec& spec = specOf(ProbedFormat::RGBA8);
        const GLuint texture = storage(spec, 1, 1, GL_NEAREST);
        upload(spec, 1, 1, std::vector<float>(kVertexFetchTexel.begin(), kVertexFetchTexel.end()));
        return texture;
    }

    GLuint program(const char* what, const char* vertexSource, const char* fragmentSource) {
        const GLuint vertex = compile(GL_VERTEX_SHADER, vertexSource);
        const GLuint fragment = compile(GL_FRAGMENT_SHADER, fragmentSource);
        if (vertex == 0 || fragment == 0) {
            if (vertex) glDeleteShader(vertex);
            if (fragment) glDeleteShader(fragment);
            GEODE_LOGW(kTag, "probe shader for '%s' did not compile; that probe reports unsupported", what);
            return 0;
        }
        const GLuint program = glCreateProgram();
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        glLinkProgram(program);
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        GLint status = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &status);
        if (status == 0) {
            GEODE_LOGW(kTag, "probe program '%s' did not link", what);
            glDeleteProgram(program);
            return 0;
        }
        programs_.push_back(program);
        return program;
    }

    void releaseAll() {
        if (!textures_.empty()) glDeleteTextures(static_cast<GLsizei>(textures_.size()), textures_.data());
        if (!framebuffers_.empty()) glDeleteFramebuffers(static_cast<GLsizei>(framebuffers_.size()), framebuffers_.data());
        if (!vertexArrays_.empty()) glDeleteVertexArrays(static_cast<GLsizei>(vertexArrays_.size()), vertexArrays_.data());
        if (!queries_.empty()) glDeleteQueries(static_cast<GLsizei>(queries_.size()), queries_.data());
        for (const GLuint p : programs_) glDeleteProgram(p);
        textures_.clear();
        framebuffers_.clear();
        vertexArrays_.clear();
        queries_.clear();
        programs_.clear();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

private:
    GLuint storage(const FormatSpec& spec, int width, int height, GLint filter) {
        GLuint id = 0;
        glGenTextures(1, &id);
        textures_.push_back(id);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexStorage2D(GL_TEXTURE_2D, 1, spec.internalFormat, width, height);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        return id;
    }

    void upload(const FormatSpec& spec, int width, int height, const std::vector<float>& texels) {
        const int bytes = bytesOf(spec.uploadType);
        std::vector<unsigned char> buffer(texels.size() * static_cast<size_t>(bytes));
        size_t at = 0;
        for (const float value : texels) {
            switch (spec.uploadType) {
                case GL_UNSIGNED_BYTE: {
                    const int v = std::clamp(static_cast<int>(value * 255.0f), 0, 255);
                    buffer[at] = static_cast<unsigned char>(v);
                    break;
                }
                case GL_HALF_FLOAT: {
                    const uint16_t h = half::fromFloat(value);
                    std::memcpy(buffer.data() + at, &h, sizeof h);
                    break;
                }
                case GL_FLOAT:
                    std::memcpy(buffer.data() + at, &value, sizeof value);
                    break;
                default:
                    break;
            }
            at += static_cast<size_t>(bytes);
        }
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, spec.uploadFormat, spec.uploadType, buffer.data());
        prober::drainErrors();
    }

    static GLuint compile(GLenum type, const char* source) {
        const GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint status = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
        if (status == 0) {
            GEODE_LOGW(kTag, "probe shader did not compile");
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    std::vector<GLuint> textures_;
    std::vector<GLuint> framebuffers_;
    std::vector<GLuint> vertexArrays_;
    std::vector<GLuint> queries_;
    std::vector<GLuint> programs_;
};

// Captures the context state the probe writes, neutralises it, and restores it on destruction.
class GlStateGuard {
public:
    GlStateGuard() {
        drawFramebuffer_ = getInt(GL_DRAW_FRAMEBUFFER_BINDING);
        readFramebuffer_ = getInt(GL_READ_FRAMEBUFFER_BINDING);
        program_ = getInt(GL_CURRENT_PROGRAM);
        vertexArray_ = getInt(GL_VERTEX_ARRAY_BINDING);
        packBuffer_ = getInt(GL_PIXEL_PACK_BUFFER_BINDING);
        unpackBuffer_ = getInt(GL_PIXEL_UNPACK_BUFFER_BINDING);
        activeTexture_ = getInt(GL_ACTIVE_TEXTURE);
        glGetIntegerv(GL_VIEWPORT, viewport_.data());
        glGetBooleanv(GL_COLOR_WRITEMASK, colorMask_.data());
        blend_ = glIsEnabled(GL_BLEND);
        scissor_ = glIsEnabled(GL_SCISSOR_TEST);
        depth_ = glIsEnabled(GL_DEPTH_TEST);
        stencil_ = glIsEnabled(GL_STENCIL_TEST);
        cull_ = glIsEnabled(GL_CULL_FACE);
        dither_ = glIsEnabled(GL_DITHER);
        blendSrcRgb_ = getInt(GL_BLEND_SRC_RGB);
        blendDstRgb_ = getInt(GL_BLEND_DST_RGB);
        blendSrcAlpha_ = getInt(GL_BLEND_SRC_ALPHA);
        blendDstAlpha_ = getInt(GL_BLEND_DST_ALPHA);
        blendEquationRgb_ = getInt(GL_BLEND_EQUATION_RGB);
        blendEquationAlpha_ = getInt(GL_BLEND_EQUATION_ALPHA);
        packAlignment_ = getInt(GL_PACK_ALIGNMENT);
        unpackAlignment_ = getInt(GL_UNPACK_ALIGNMENT);
        packRowLength_ = getInt(GL_PACK_ROW_LENGTH);
        packSkipPixels_ = getInt(GL_PACK_SKIP_PIXELS);
        packSkipRows_ = getInt(GL_PACK_SKIP_ROWS);
        unpackRowLength_ = getInt(GL_UNPACK_ROW_LENGTH);
        unpackSkipPixels_ = getInt(GL_UNPACK_SKIP_PIXELS);
        unpackSkipRows_ = getInt(GL_UNPACK_SKIP_ROWS);
        glActiveTexture(GL_TEXTURE0);
        unitTexture_ = getInt(GL_TEXTURE_BINDING_2D);
        unitSampler_ = getInt(GL_SAMPLER_BINDING);
        neutralise();
    }

    ~GlStateGuard() {
        glActiveTexture(GL_TEXTURE0);
        glBindSampler(0, static_cast<GLuint>(unitSampler_));
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(unitTexture_));
        glActiveTexture(static_cast<GLenum>(activeTexture_));
        glUseProgram(static_cast<GLuint>(program_));
        glBindVertexArray(static_cast<GLuint>(vertexArray_));
        glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(packBuffer_));
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(unpackBuffer_));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFramebuffer_));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(readFramebuffer_));
        glViewport(viewport_[0], viewport_[1], viewport_[2], viewport_[3]);
        glColorMask(colorMask_[0], colorMask_[1], colorMask_[2], colorMask_[3]);
        glBlendFuncSeparate(blendSrcRgb_, blendDstRgb_, blendSrcAlpha_, blendDstAlpha_);
        glBlendEquationSeparate(blendEquationRgb_, blendEquationAlpha_);
        setEnabled(GL_BLEND, blend_);
        setEnabled(GL_SCISSOR_TEST, scissor_);
        setEnabled(GL_DEPTH_TEST, depth_);
        setEnabled(GL_STENCIL_TEST, stencil_);
        setEnabled(GL_CULL_FACE, cull_);
        setEnabled(GL_DITHER, dither_);
        glPixelStorei(GL_PACK_ALIGNMENT, packAlignment_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, unpackAlignment_);
        glPixelStorei(GL_PACK_ROW_LENGTH, packRowLength_);
        glPixelStorei(GL_PACK_SKIP_PIXELS, packSkipPixels_);
        glPixelStorei(GL_PACK_SKIP_ROWS, packSkipRows_);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, unpackRowLength_);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, unpackSkipPixels_);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, unpackSkipRows_);
    }

private:
    static GLint getInt(GLenum pname) {
        GLint out = 0;
        glGetIntegerv(pname, &out);
        return out;
    }
    static void setEnabled(GLenum capability, bool enabled) {
        if (enabled) glEnable(capability); else glDisable(capability);
    }
    void neutralise() {
        glBindSampler(0, 0);
        glDisable(GL_DITHER);
        glDisable(GL_BLEND);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glBlendEquation(GL_FUNC_ADD);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_PACK_ROW_LENGTH, 0);
        glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
        glPixelStorei(GL_PACK_SKIP_ROWS, 0);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        prober::drainErrors();
    }

    GLint drawFramebuffer_ = 0, readFramebuffer_ = 0, program_ = 0, vertexArray_ = 0, packBuffer_ = 0, unpackBuffer_ = 0;
    GLint activeTexture_ = 0, unitTexture_ = 0, unitSampler_ = 0;
    std::array<GLint, 4> viewport_{};
    std::array<GLboolean, 4> colorMask_{};
    bool blend_ = false, scissor_ = false, depth_ = false, stencil_ = false, cull_ = false, dither_ = false;
    GLint blendSrcRgb_ = 0, blendDstRgb_ = 0, blendSrcAlpha_ = 0, blendDstAlpha_ = 0, blendEquationRgb_ = 0, blendEquationAlpha_ = 0;
    GLint packAlignment_ = 0, unpackAlignment_ = 0, packRowLength_ = 0, packSkipPixels_ = 0, packSkipRows_ = 0;
    GLint unpackRowLength_ = 0, unpackSkipPixels_ = 0, unpackSkipRows_ = 0;
};

void bindDraw(GLuint framebuffer) {
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, kProbeSize, kProbeSize);
}

void clearFloat() {
    const GLfloat zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    glClearBufferfv(GL_COLOR, 0, zero);
}

void drawFullscreen(GLuint vao) {
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void uniform4f(GLuint program, const char* name, const std::array<float, 4>& values) {
    const GLint location = glGetUniformLocation(program, name);
    if (location >= 0) glUniform4fv(location, 1, values.data());
}

bool readFloats(GLuint framebuffer, const FormatSpec& spec, int texel, std::array<float, 4>& out) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
    prober::drainErrors();
    GLenum format;
    GLenum type;
    if (spec.internalFormat == GL_RGBA8) {
        format = GL_RGBA;
        type = GL_UNSIGNED_BYTE;
    } else {
        format = static_cast<GLenum>(prober::limit(GL_IMPLEMENTATION_COLOR_READ_FORMAT));
        type = static_cast<GLenum>(prober::limit(GL_IMPLEMENTATION_COLOR_READ_TYPE));
    }
    const int components = componentsOf(format);
    const int bytes = bytesOf(type);
    if (components == 0 || bytes == 0) return false;
    const int stride = components * bytes;
    std::vector<unsigned char> buffer(static_cast<size_t>(kProbeSize * kProbeSize * stride));
    glReadPixels(0, 0, kProbeSize, kProbeSize, format, type, buffer.data());
    if (glGetError() != GL_NO_ERROR) return false;
    out = {0.0f, 0.0f, 0.0f, 1.0f};
    const int base = texel * stride;
    for (int channel = 0; channel < std::min(components, 4); channel++) {
        const unsigned char* at = buffer.data() + base + channel * bytes;
        switch (type) {
            case GL_UNSIGNED_BYTE: out[channel] = *at / 255.0f; break;
            case GL_HALF_FLOAT: {
                uint16_t h = 0;
                std::memcpy(&h, at, sizeof h);
                out[channel] = half::toFloat(h);
                break;
            }
            case GL_FLOAT: std::memcpy(&out[channel], at, sizeof(float)); break;
            default: return false;
        }
    }
    return true;
}

bool readUints(GLuint framebuffer, int texel, std::array<uint32_t, 4>& out) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
    prober::drainErrors();
    std::vector<uint32_t> buffer(static_cast<size_t>(kProbeSize * kProbeSize * 4));
    glReadPixels(0, 0, kProbeSize, kProbeSize, GL_RGBA_INTEGER, GL_UNSIGNED_INT, buffer.data());
    if (glGetError() != GL_NO_ERROR) return false;
    for (int i = 0; i < 4; i++) out[i] = buffer[static_cast<size_t>(texel * 4 + i)];
    return true;
}

bool matches(const std::array<float, 4>& read, const std::array<float, 4>& expected, const FormatSpec& spec) {
    for (int i = 0; i < spec.components; i++) {
        if (std::fabs(read[i] - expected[i]) > spec.tolerance) return false;
    }
    return true;
}

bool renderExactly(GLuint vao, GLuint program, const ProbeTarget& target, const FormatSpec& spec) {
    if (program == 0) return false;
    bindDraw(target.framebuffer);
    clearFloat();
    glUseProgram(program);
    uniform4f(program, "uValue", kRenderInput);
    drawFullscreen(vao);
    std::array<float, 4> first{};
    std::array<float, 4> last{};
    if (!readFloats(target.framebuffer, spec, 0, first)) return false;
    if (!readFloats(target.framebuffer, spec, kProbeSize * kProbeSize - 1, last)) return false;
    return matches(first, kRenderInput, spec) && matches(last, kRenderInput, spec);
}

bool renderPackedExactly(GLuint vao, GLuint program, const ProbeTarget& target) {
    if (program == 0) return false;
    bindDraw(target.framebuffer);
    const GLuint zero[4] = {0, 0, 0, 0};
    glClearBufferuiv(GL_COLOR, 0, zero);
    glUseProgram(program);
    uniform4f(program, "uValue", kPackInput);
    drawFullscreen(vao);
    std::array<uint32_t, 4> expected{};
    for (int i = 0; i < 4; i++) std::memcpy(&expected[i], &kPackInput[i], sizeof(float));
    std::array<uint32_t, 4> first{};
    std::array<uint32_t, 4> last{};
    if (!readUints(target.framebuffer, 0, first)) return false;
    if (!readUints(target.framebuffer, kProbeSize * kProbeSize - 1, last)) return false;
    return first == expected && last == expected;
}

bool blendsAdditively(GLuint vao, GLuint program, const ProbeTarget& target, const FormatSpec& spec) {
    if (program == 0) return false;
    bindDraw(target.framebuffer);
    clearFloat();
    glUseProgram(program);
    uniform4f(program, "uValue", kBlendInput);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE);
    drawFullscreen(vao);
    drawFullscreen(vao);
    glDisable(GL_BLEND);
    std::array<float, 4> read{};
    if (!readFloats(target.framebuffer, spec, 0, read)) return false;
    return matches(read, kBlendExpected, spec);
}

bool filtersLinearly(GlArena& arena, GLuint vao, GLuint program, const FormatSpec& spec, const ProbeTarget& scratch) {
    if (program == 0 || !scratch.complete) return false;
    const GLuint source = arena.filterSource(spec);
    bindDraw(scratch.framebuffer);
    clearFloat();
    glUseProgram(program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, source);
    glUniform1i(glGetUniformLocation(program, "uSrc"), 0);
    drawFullscreen(vao);
    std::array<float, 4> read{};
    if (!readFloats(scratch.framebuffer, specOf(ProbedFormat::RGBA8), 0, read)) return false;
    return read[0] > kFilterWindowLow && read[0] < kFilterWindowHigh;
}

FormatProbe probeFormat(GlArena& arena, GLuint vao, ProbedFormat format, GLuint floatProgram, GLuint uintProgram,
                        GLuint sampleProgram, const ProbeTarget& scratch) {
    const FormatSpec& spec = specOf(format);
    const ProbeTarget target = arena.target(spec);
    FormatProbe probe;
    probe.attachable = target.complete;
    probe.rendersExactly = target.complete && (spec.integer ? renderPackedExactly(vao, uintProgram, target)
                                                            : renderExactly(vao, floatProgram, target, spec));
    probe.blendsAdditively = target.complete && !spec.integer && blendsAdditively(vao, floatProgram, target, spec);
    probe.filtersLinearly = !spec.integer && filtersLinearly(arena, vao, sampleProgram, spec, scratch);
    return probe;
}

bool probeVertexTextureFetch(GlArena& arena, GLuint vao, const ProbeTarget& scratch) {
    if (!scratch.complete) return false;
    const GLuint program = arena.program("vertex texture fetch", kVtfVert, kVtfFrag);
    if (program == 0) return false;
    const GLuint source = arena.vertexFetchSource();
    bindDraw(scratch.framebuffer);
    clearFloat();
    glUseProgram(program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, source);
    glUniform1i(glGetUniformLocation(program, "uSrc"), 0);
    drawFullscreen(vao);
    std::array<float, 4> read{};
    if (!readFloats(scratch.framebuffer, specOf(ProbedFormat::RGBA8), 0, read)) return false;
    return matches(read, kVertexFetchTexel, specOf(ProbedFormat::RGBA8));
}

bool probeTimerQuery(GlArena& arena, GLuint vao, GLuint program, const ProbeTarget& scratch) {
    if (program == 0 || !scratch.complete) return false;
    const GLuint query = arena.query();
    prober::drainErrors();
    glBeginQuery(kTimeElapsedExt, query);
    if (glGetError() != GL_NO_ERROR) return false;
    bindDraw(scratch.framebuffer);
    clearFloat();
    glUseProgram(program);
    uniform4f(program, "uValue", kRenderInput);
    drawFullscreen(vao);
    glEndQuery(kTimeElapsedExt);
    if (glGetError() != GL_NO_ERROR) return false;

    GLuint out = 0;
    bool ready = false;
    for (int spin = 0; spin < kQueryPollLimit; spin++) {
        glGetQueryObjectuiv(query, GL_QUERY_RESULT_AVAILABLE, &out);
        if (out != 0) {
            ready = true;
            break;
        }
        if (spin == 0) glFinish();
    }
    if (!ready) return false;
    glGetQueryObjectuiv(query, GL_QUERY_RESULT, &out);
    const uint64_t elapsedNs = out;
    const bool disjoint = prober::limit(kGpuDisjointExt) != 0;
    return !disjoint && elapsedNs > 0 && elapsedNs < kTimerCeilingNs;
}

std::set<std::string> extensions() {
    std::set<std::string> out;
    const int count = prober::limit(GL_NUM_EXTENSIONS);
    for (int index = 0; index < count; index++) {
        const GLubyte* s = glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(index));
        if (s && *s) out.insert(reinterpret_cast<const char*>(s));
    }
    std::istringstream in(glString(GL_EXTENSIONS));
    std::string e;
    while (in >> e) out.insert(e);
    prober::drainErrors();
    return out;
}

}  // namespace

namespace prober {

GlIdentity identity() {
    return GlIdentity{glString(GL_VENDOR), glString(GL_RENDERER), glString(GL_VERSION)};
}

int limit(unsigned int pname) {
    drainErrors();
    GLint out = 0;
    glGetIntegerv(pname, &out);
    return glGetError() != GL_NO_ERROR ? 0 : out;
}

void drainErrors() {
    int drained = 0;
    while (glGetError() != GL_NO_ERROR && drained < kErrorDrainLimit) drained++;
}

GlProbeReport probe() {
    const GlIdentity id = identity();
    GlStateGuard guard;
    GlArena arena;

    GlProbeReport report;
    report.vendor = id.vendor;
    report.renderer = id.renderer;
    report.versionString = id.versionString;
    report.extensions = extensions();
    const auto version = GlVersion::parse(id.versionString);
    const bool es31 = version && *version >= GlVersion{3, 1};

    const GLuint vao = arena.vertexArray();
    const ProbeTarget scratch = arena.target(specOf(ProbedFormat::RGBA8));
    const GLuint floatProgram = arena.program("float fill", kFullscreenVert, kFloatFrag);
    const GLuint uintProgram = arena.program("packed uint fill", kFullscreenVert, kUintFrag);
    const GLuint sampleProgram = arena.program("texture sample", kFullscreenVert, kSampleFrag);

    for (int i = 0; i < kProbedFormatCount; i++) {
        const auto format = static_cast<ProbedFormat>(i);
        report.formats[format] = probeFormat(arena, vao, format, floatProgram, uintProgram, sampleProgram, scratch);
    }

    report.vertexTextureFetchProven = probeVertexTextureFetch(arena, vao, scratch);
    report.timerQueryPresent = report.extensions.count(kTimerQueryExtension) > 0;
    report.timerQueryProven = report.timerQueryPresent && probeTimerQuery(arena, vao, floatProgram, scratch);

    report.maxTextureSize = limit(GL_MAX_TEXTURE_SIZE);
    report.maxColorAttachments = limit(GL_MAX_COLOR_ATTACHMENTS);
    report.maxVertexTextureImageUnits = limit(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS);
    report.maxComputeWorkGroupInvocations = es31 ? limit(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS) : 0;
    report.maxComputeStorageBlocks = es31 ? limit(GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS) : 0;
    report.maxFragmentStorageBlocks = es31 ? limit(GL_MAX_FRAGMENT_SHADER_STORAGE_BLOCKS) : 0;
    report.maxComputeImageUniforms = es31 ? limit(GL_MAX_COMPUTE_IMAGE_UNIFORMS) : 0;
    report.programBinaryFormats = limit(GL_NUM_PROGRAM_BINARY_FORMATS);

    arena.releaseAll();
    drainErrors();
    return report;
}

}  // namespace prober

}  // namespace geode::viz
