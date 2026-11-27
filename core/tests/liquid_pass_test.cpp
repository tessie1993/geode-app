// Host-side checks for liquid::LiquidPass (core/viz/liquid/LiquidPass.hpp) over a faked GL context: which
// targets a frame writes and which texture it hands the composite. Built and run by CI from the repo root.
//
// What is pinned: a shader style's Trails keep one attachment per side (the liquid), discard that one
// attachment before the draw, and hand the composite the liquid just written; Liquid mode keeps the RGBA8
// display beside it and hands the composite the display. Trading one for the other at the same size keeps the
// liquid textures (the pour carries on) and frees the display it drops; a driver that will not attach half
// float beside RGBA8 falls back to RGBA8 sides. A held clock shows the last frame again. The transform and
// pass flags a frame uploads are motion::derive's and liquid::passFlags'. Nothing is left behind.
//
// The real ShaderSource and program::build run on the shipped liquid_frag.glsl; only GL and the APK's asset
// store are faked. What the fake cannot show: the pixels (feedback_motion_test pins the transform's C++
// mirror, tools/check_shaders.py compiles the shader) and anything about a real driver.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "viz/gl/ProgramStager.hpp"
#include "viz/liquid/LiquidPass.hpp"

namespace liquid = geode::viz::liquid;

// ---- the APK's asset store, over the repo's assets folder ----------------------------------------------
struct AAssetManager {
    std::string root;
};
struct AAsset {
    std::string bytes;
};

extern "C" AAsset* AAssetManager_open(AAssetManager* manager, const char* filename, int /*mode*/) {
    if (manager == nullptr || filename == nullptr) return nullptr;
    std::ifstream in(manager->root + filename, std::ios::binary);
    if (!in) return nullptr;
    std::ostringstream bytes;
    bytes << in.rdbuf();
    return new AAsset{bytes.str()};
}
extern "C" off_t AAsset_getLength(AAsset* asset) { return static_cast<off_t>(asset->bytes.size()); }
extern "C" int AAsset_read(AAsset* asset, void* buf, size_t count) {
    const size_t n = std::min(count, asset->bytes.size());
    std::memcpy(buf, asset->bytes.data(), n);
    return static_cast<int>(n);
}
extern "C" void AAsset_close(AAsset* asset) { delete asset; }
extern "C" int __android_log_print(int, const char*, const char*, ...) { return 0; }

// ---- our own code's link seams (as in edge_glow_pass_test.cpp) ------------------------------------------
geode::viz::ProgramStager* geode::viz::ProgramStager::current() { return nullptr; }
GLuint geode::viz::ProgramStager::obtain(const std::vector<ShaderStage>&, std::string*, bool*, bool) { std::abort(); }
std::optional<geode::viz::ProgramKey> geode::viz::ProgramBinaryCache::keyFor(const std::string&, const std::string&) {
    std::abort();
}
GLuint geode::viz::ProgramBinaryCache::load(const ProgramKey&) { std::abort(); }
void geode::viz::ProgramBinaryCache::markRetrievable(GLuint) { std::abort(); }
void geode::viz::ProgramBinaryCache::store(const ProgramKey&, GLuint) { std::abort(); }

// ---- GL, as a small context model -----------------------------------------------------------------------
namespace fake {

struct Fbo {
    std::map<GLenum, GLuint> attached;  // attachment -> texture
    int drawBuffers = 1;
};

struct Context {
    GLuint next = 1;
    std::set<GLuint> shaders, programs, textures, fbos;
    std::map<GLuint, GLint> format;  // texture -> internal format
    std::map<GLuint, Fbo> fbo;
    std::map<GLuint, std::string> shaderSource, programSource;
    std::map<GLuint, std::vector<std::string>> locations;
    std::map<GLuint, std::map<std::string, std::vector<float>>> uniforms;
    GLuint program = 0, boundFbo = 0, boundTex = 0;
    // The last draw: its framebuffer, how many attachments were discarded on it before, and the uniforms.
    GLuint drawnFbo = 0;
    int discarded = 0;
    int drawnDiscarded = -1;
    std::map<std::string, std::vector<float>> drawnUniforms;
    // A driver that will not complete a framebuffer with half float beside RGBA8.
    bool refuseHalfBeside8 = false;
};

Context gl;

void setUniform(GLint location, std::vector<float> values) {
    if (gl.program == 0 || location < 0) return;
    const auto& names = gl.locations[gl.program];
    if (static_cast<size_t>(location) >= names.size()) return;
    gl.uniforms[gl.program][names[static_cast<size_t>(location)]] = std::move(values);
}

float uniform(const char* name, size_t i = 0) {
    const auto it = gl.drawnUniforms.find(name);
    return it == gl.drawnUniforms.end() || it->second.size() <= i ? -999.0f : it->second[i];
}

}  // namespace fake

extern "C" {
GLuint glCreateShader(GLenum) {
    const GLuint name = fake::gl.next++;
    fake::gl.shaders.insert(name);
    return name;
}
void glDeleteShader(GLuint shader) { fake::gl.shaders.erase(shader); }
GLuint glCreateProgram(void) {
    const GLuint name = fake::gl.next++;
    fake::gl.programs.insert(name);
    return name;
}
void glDeleteProgram(GLuint program) {
    fake::gl.programs.erase(program);
    if (fake::gl.program == program) fake::gl.program = 0;
}
void glShaderSource(GLuint shader, GLsizei count, const GLchar* const* string, const GLint*) {
    std::string src;
    for (GLsizei i = 0; i < count; ++i) src += string[i];
    fake::gl.shaderSource[shader] = src;
}
void glCompileShader(GLuint) {}
void glGetShaderiv(GLuint, GLenum, GLint* params) { *params = 1; }
void glGetShaderInfoLog(GLuint, GLsizei, GLsizei* length, GLchar*) {
    if (length) *length = 0;
}
void glAttachShader(GLuint program, GLuint shader) { fake::gl.programSource[program] += fake::gl.shaderSource[shader]; }
void glLinkProgram(GLuint) {}
void glGetProgramiv(GLuint, GLenum, GLint* params) { *params = 1; }
void glGetProgramInfoLog(GLuint, GLsizei, GLsizei* length, GLchar*) {
    if (length) *length = 0;
}
GLint glGetUniformLocation(GLuint program, const GLchar* name) {
    if (fake::gl.programs.count(program) == 0) return -1;
    if (fake::gl.programSource[program].find(std::string(" ") + name + ";") == std::string::npos) return -1;
    auto& names = fake::gl.locations[program];
    const auto it = std::find(names.begin(), names.end(), name);
    if (it != names.end()) return static_cast<GLint>(it - names.begin());
    names.emplace_back(name);
    return static_cast<GLint>(names.size() - 1);
}
void glGetUniformIndices(GLuint, GLsizei count, const GLchar* const*, GLuint* indices) {
    for (GLsizei i = 0; i < count; ++i) indices[i] = GL_INVALID_INDEX;
}
void glGetActiveUniformsiv(GLuint, GLsizei, const GLuint*, GLenum, GLint* params) { *params = 1; }
void glUseProgram(GLuint program) { fake::gl.program = fake::gl.programs.count(program) ? program : 0; }
void glUniform1f(GLint location, GLfloat v0) { fake::setUniform(location, {v0}); }
void glUniform1i(GLint location, GLint v0) { fake::setUniform(location, {static_cast<float>(v0)}); }
void glUniform2f(GLint location, GLfloat v0, GLfloat v1) { fake::setUniform(location, {v0, v1}); }
void glUniform4fv(GLint location, GLsizei count, const GLfloat* value) {
    fake::setUniform(location, std::vector<float>(value, value + 4 * count));
}

void glGenTextures(GLsizei n, GLuint* textures) {
    for (GLsizei i = 0; i < n; ++i) {
        textures[i] = fake::gl.next++;
        fake::gl.textures.insert(textures[i]);
    }
}
void glDeleteTextures(GLsizei n, const GLuint* textures) {
    for (GLsizei i = 0; i < n; ++i) {
        fake::gl.textures.erase(textures[i]);
        fake::gl.format.erase(textures[i]);
    }
}
void glGenFramebuffers(GLsizei n, GLuint* framebuffers) {
    for (GLsizei i = 0; i < n; ++i) {
        framebuffers[i] = fake::gl.next++;
        fake::gl.fbos.insert(framebuffers[i]);
        fake::gl.fbo[framebuffers[i]] = {};
    }
}
void glDeleteFramebuffers(GLsizei n, const GLuint* framebuffers) {
    for (GLsizei i = 0; i < n; ++i) {
        fake::gl.fbos.erase(framebuffers[i]);
        fake::gl.fbo.erase(framebuffers[i]);
        if (fake::gl.boundFbo == framebuffers[i]) fake::gl.boundFbo = 0;
    }
}
void glBindFramebuffer(GLenum, GLuint framebuffer) {
    fake::gl.boundFbo = framebuffer;
    fake::gl.discarded = 0;
}
void glActiveTexture(GLenum) {}
void glBindTexture(GLenum, GLuint texture) { fake::gl.boundTex = texture; }
void glTexParameteri(GLenum, GLenum, GLint) {}
void glTexImage2D(GLenum, GLint, GLint internalformat, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) {
    fake::gl.format[fake::gl.boundTex] = internalformat;
}
void glFramebufferTexture2D(GLenum, GLenum attachment, GLenum, GLuint texture, GLint) {
    auto& f = fake::gl.fbo[fake::gl.boundFbo];
    if (texture == 0) {
        f.attached.erase(attachment);
    } else {
        f.attached[attachment] = texture;
    }
}
void glDrawBuffers(GLsizei n, const GLenum*) { fake::gl.fbo[fake::gl.boundFbo].drawBuffers = n; }
GLenum glCheckFramebufferStatus(GLenum) {
    const auto& f = fake::gl.fbo[fake::gl.boundFbo];
    const auto liquid = f.attached.find(GL_COLOR_ATTACHMENT0);
    if (liquid == f.attached.end()) return 0;
    if (fake::gl.refuseHalfBeside8 && f.attached.count(GL_COLOR_ATTACHMENT1) && fake::gl.format[liquid->second] == GL_RGBA16F) {
        return 0;
    }
    return GL_FRAMEBUFFER_COMPLETE;
}
void glClearColor(GLfloat, GLfloat, GLfloat, GLfloat) {}
void glClear(GLbitfield) {}
void glInvalidateFramebuffer(GLenum, GLsizei numAttachments, const GLenum*) { fake::gl.discarded = numAttachments; }
void glViewport(GLint, GLint, GLsizei, GLsizei) {}
void glDisable(GLenum) {}
void glBindVertexArray(GLuint) {}
void glDrawArrays(GLenum, GLint, GLsizei) {
    fake::gl.drawnFbo = fake::gl.boundFbo;
    fake::gl.drawnDiscarded = fake::gl.discarded;
    fake::gl.drawnUniforms = fake::gl.uniforms[fake::gl.program];
}
void glDeleteBuffers(GLsizei, const GLuint*) {}
void glDeleteSamplers(GLsizei, const GLuint*) {}
void glDeleteVertexArrays(GLsizei, const GLuint*) {}
void glGenBuffers(GLsizei, GLuint*) {}
void glGenSamplers(GLsizei, GLuint*) {}
void glGenVertexArrays(GLsizei, GLuint*) {}
}  // extern "C"

// ---- the checks -----------------------------------------------------------------------------------------
namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

AAssetManager repoAssets{"app/src/main/assets/"};
constexpr int kW = 108;
constexpr int kH = 240;
constexpr GLuint kStyleTex = 9000;
constexpr GLuint kZeroTex = 9001;

std::string fadeVert() {
    std::string error;
    const auto src = geode::viz::ShaderSource(&repoAssets).load("fade_vert.glsl", &error);
    return src ? *src : std::string();
}

bool build(liquid::LiquidPass& pass, bool halfFloat) {
    fake::gl = fake::Context{};
    std::string error;
    const bool ok = pass.create(geode::viz::ShaderSource(&repoAssets), nullptr, fadeVert(), halfFloat, &error);
    if (!ok) std::printf("build failed: %s\n", error.c_str());
    return ok;
}

// What Renderer::drawTrails asks at the default trail zoom and warp.
liquid::LiquidPass::Frame trailsFrame(float dt = 1.0f / 60.0f) {
    liquid::LiquidPass::Frame f;
    f.styleTex = kStyleTex;
    f.bornTex = kStyleTex;
    f.velocityTex = kZeroTex;
    f.styleWidth = kW;
    f.styleHeight = kH;
    f.dt = dt;
    f.inject = 0.1f;
    f.sharpen = 0.35f;
    f.time = 12.5f;
    return f;
}

// The liquid texture a side's framebuffer draws into.
GLuint liquidOf(GLuint fbo) {
    const auto& a = fake::gl.fbo[fbo].attached;
    const auto it = a.find(GL_COLOR_ATTACHMENT0);
    return it == a.end() ? 0 : it->second;
}

std::set<GLuint> liquids() {
    std::set<GLuint> out;
    for (const auto& [name, f] : fake::gl.fbo) out.insert(liquidOf(name));
    return out;
}

void trailsWriteOneTargetAndShowIt() {
    liquid::LiquidPass pass;
    check(build(pass, true), "the pass builds from the shipped shader");
    check(pass.ensure(kW, kH, false), "Trails' targets are made");
    check(fake::gl.fbos.size() == 2, "two sides");
    for (const auto& [name, f] : fake::gl.fbo) {
        check(f.attached.size() == 1 && f.attached.count(GL_COLOR_ATTACHMENT0) == 1, "a Trails side has the liquid alone");
        check(f.drawBuffers == 1, "and draws one buffer");
        check(fake::gl.format[f.attached.at(GL_COLOR_ATTACHMENT0)] == GL_RGBA16F, "the liquid is half float");
    }
    check(fake::gl.textures.size() == 2, "no display texture is made for Trails");
    const GLuint shown = pass.run(trailsFrame());
    check(shown == liquidOf(fake::gl.drawnFbo) && shown != 0, "the composite reads the liquid just written");
    check(fake::gl.drawnDiscarded == 1, "one attachment discarded before the draw");
    check(fake::uniform("uMotionFlags") == 0.0f, "the identity transform is not run");
    check(fake::uniform("uPassFlags") == 0.0f, "no flow, no born disc");
    check(fake::uniform("uRotCS", 0) == 1.0f && fake::uniform("uRotCS", 1) == 0.0f, "the rotation's cos and sin come from the CPU");
    const GLuint next = pass.run(trailsFrame());
    check(next != shown && next == liquidOf(fake::gl.drawnFbo), "the next frame writes and shows the other side");
    check(pass.run(trailsFrame(0.0f)) == next, "a held clock shows the last frame again");
    pass.release();
    check(fake::gl.textures.empty() && fake::gl.fbos.empty() && fake::gl.programs.empty(), "release leaves nothing behind");
}

void liquidModeKeepsItsDisplayAndTradingKeepsTheLiquid() {
    liquid::LiquidPass pass;
    check(build(pass, true), "the pass builds");
    check(pass.ensure(kW, kH, false), "Trails first");
    pass.run(trailsFrame());
    const std::set<GLuint> poured = liquids();
    check(pass.ensure(kW, kH, true), "then Liquid mode at the same size");
    check(liquids() == poured, "the liquid carries over");
    GLuint display = 0;
    for (const auto& [name, f] : fake::gl.fbo) {
        check(f.attached.count(GL_COLOR_ATTACHMENT1) == 1 && f.drawBuffers == 2, "a Liquid side has the display too");
        if (display == 0) display = f.attached.at(GL_COLOR_ATTACHMENT1);
        check(f.attached.at(GL_COLOR_ATTACHMENT1) == display, "one display shared by both sides");
    }
    check(fake::gl.format[display] == GL_RGBA8, "the display is RGBA8");
    liquid::LiquidPass::Frame f = trailsFrame();
    f.flowing = true;
    f.bornGain = 0.5f;
    f.velScaleX = f.velScaleY = 2.0f;
    check(pass.run(f) == display, "the composite reads the display in Liquid mode");
    check(fake::gl.drawnDiscarded == 2, "both attachments discarded before the draw");
    check(fake::uniform("uPassFlags") == static_cast<float>(liquid::kPassFlow | liquid::kPassBorn), "flow and born run");
    check(pass.ensure(kW, kH, false), "back to Trails");
    check(liquids() == poured, "the liquid still carries over");
    check(fake::gl.textures.count(display) == 0, "the display is freed");
    for (const auto& [name, side] : fake::gl.fbo) {
        check(side.attached.size() == 1 && side.drawBuffers == 1, "a side has the liquid alone again");
    }
    check(pass.run(trailsFrame()) == liquidOf(fake::gl.drawnFbo), "and Trails show the liquid");
    check(pass.ensure(kW / 2, kH / 2, false) && liquids() != poured, "a new size starts the liquid over");
    pass.release();
    check(fake::gl.textures.empty() && fake::gl.fbos.empty(), "release leaves nothing behind");
}

void aDriverThatWillNotMixFormatsFallsBackToRgba8() {
    liquid::LiquidPass pass;
    check(build(pass, true), "the pass builds");
    fake::gl.refuseHalfBeside8 = true;
    check(pass.ensure(kW, kH, false), "Trails need no RGBA8 beside the half float");
    for (const auto& [name, f] : fake::gl.fbo) check(fake::gl.format[liquidOf(name)] == GL_RGBA16F, "so they keep half float");
    check(pass.ensure(kW, kH, true), "Liquid mode still gets its targets");
    for (const auto& [name, f] : fake::gl.fbo) {
        check(fake::gl.format[liquidOf(name)] == GL_RGBA8 && f.drawBuffers == 2, "on RGBA8 sides with the display");
    }
    check(fake::gl.textures.size() == 3, "two liquids and one display, nothing leaked by the failed attach");
    pass.release();
    check(fake::gl.textures.empty() && fake::gl.fbos.empty(), "release leaves nothing behind");
}

}  // namespace

int main() {
    trailsWriteOneTargetAndShowIt();
    liquidModeKeepsItsDisplayAndTradingKeepsTheLiquid();
    aDriverThatWillNotMixFormatsFallsBackToRgba8();
    if (failures > 0) {
        std::printf("%d liquid pass check(s) failed\n", failures);
        return 1;
    }
    std::printf("liquid pass: all checks passed\n");
    return 0;
}
