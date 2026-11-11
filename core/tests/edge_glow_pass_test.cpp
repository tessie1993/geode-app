// Host-side checks for frame::EdgeGlowPass (core/viz/frame/EdgeGlowPass.hpp, #439, tests from #503): the
// lifetime of a frame Pass and what one wallpaper frame of it leaves in the target. Built and run by CI
// ("Native host tests" in .github/workflows/android.yml) with the system compiler, from the repo root.
//
// The seam is the pass's public interface (create, setOptions, wants, prepare, draw, release, level). The
// real ShaderSource and program::build run; only the system boundaries are faked: GL
// (core/tests/fakegl/GLES3/gl3.h, modelled below as names, bindings, blend state and one probe pixel per
// framebuffer) and the APK's asset store (served from the repo's real app/src/main/assets, so the pass
// builds from the shader that ships). Link seams stand in for two of our own classes that program::build
// names but never reaches here: ProgramStager::current() returns null (its contract outside a staged scene
// build) and the pass is built without a ProgramBinaryCache, so their other members abort if ever called.
//
// What the fake cannot show: the shader's per-pixel mask (edge_glow_test.cpp pins its C++ mirror, and
// tools/check_shaders.py compiles it), the viewport, and anything about a real driver.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "viz/ProgramStager.hpp"
#include "viz/frame/EdgeGlowPass.hpp"

namespace frame = geode::viz::frame;
namespace eg = geode::viz::frame::edge_glow;

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

// ---- our own code's link seams --------------------------------------------------------------------------
// program::build (Program.cpp, linked for real) names the stager and the binary cache. Outside a staged scene
// build ProgramStager::current() is null by its contract, and the pass is built with no cache, so the rest
// is unreachable here: it aborts, which would show at once if that ever changed.
geode::viz::ProgramStager* geode::viz::ProgramStager::current() { return nullptr; }
GLuint geode::viz::ProgramStager::obtain(const std::vector<ShaderStage>&, std::string*, bool*) { std::abort(); }
std::optional<geode::viz::ProgramKey> geode::viz::ProgramBinaryCache::keyFor(const std::string&, const std::string&) {
    std::abort();
}
GLuint geode::viz::ProgramBinaryCache::load(const ProgramKey&) { std::abort(); }
void geode::viz::ProgramBinaryCache::markRetrievable(GLuint) { std::abort(); }
void geode::viz::ProgramBinaryCache::store(const ProgramKey&, GLuint) { std::abort(); }

// ---- GL, as a small context model -----------------------------------------------------------------------
namespace fake {

struct Pixel {
    float r, g, b, a;
};
// What the composite left in every framebuffer's probe pixel before the glow: an opaque-ish warm colour.
constexpr Pixel kUnder{0.8f, 0.6f, 0.4f, 0.7f};
// The fragment shader's black weight at the probe pixel. The shader does not run on the host; any value
// strictly between 0 and 1 shows how the draw blends.
constexpr float kShaderAlpha = 0.5f;

struct Context {
    // Each kind numbers its own names from 1, as drivers do, so a new context hands out the same numbers.
    GLuint nextShader = 1, nextProgram = 1, nextVao = 1;
    std::set<GLuint> shaders, programs, vaos;
    std::map<GLuint, std::string> shaderSource;
    std::map<GLuint, std::string> programSource;  // every attached stage's source, for uniform lookups
    std::map<GLuint, std::vector<std::string>> locations;
    std::map<GLuint, std::map<std::string, std::vector<float>>> uniforms;
    GLuint program = 0, vao = 0, fbo = 0;
    bool blend = false;
    GLenum srcRgb = GL_ONE, dstRgb = GL_ZERO, srcAlpha = GL_ONE, dstAlpha = GL_ZERO;
    std::map<GLuint, Pixel> pixels;
    std::map<GLuint, std::map<std::string, std::vector<float>>> drawnUniforms;
    bool linkFails = false;
};

Context gl;

// A new EGL context: every name of the old one is gone, and numbering starts over.
void newContext() { gl = Context{}; }

Pixel at(GLuint fbo) {
    const auto it = gl.pixels.find(fbo);
    return it == gl.pixels.end() ? kUnder : it->second;
}

size_t liveNames() { return gl.shaders.size() + gl.programs.size() + gl.vaos.size(); }

float factor(GLenum f, float srcA) {
    switch (f) {
        case GL_ZERO: return 0.0f;
        case GL_ONE: return 1.0f;
        case GL_SRC_ALPHA: return srcA;
        case GL_ONE_MINUS_SRC_ALPHA: return 1.0f - srcA;
        default: return -100.0f;  // a factor this model does not know reads as garbage
    }
}

// glUniform* on the bound program; ignored for location -1, as GL ignores it.
void setUniform(GLint location, std::vector<float> values) {
    const GLuint p = gl.program;
    if (p == 0 || location < 0) return;
    const auto& names = gl.locations[p];
    if (static_cast<size_t>(location) >= names.size()) return;
    gl.uniforms[p][names[static_cast<size_t>(location)]] = std::move(values);
}

}  // namespace fake

extern "C" {
GLuint glCreateShader(GLenum) {
    const GLuint name = fake::gl.nextShader++;
    fake::gl.shaders.insert(name);
    return name;
}
void glDeleteShader(GLuint shader) { fake::gl.shaders.erase(shader); }
GLuint glCreateProgram(void) {
    const GLuint name = fake::gl.nextProgram++;
    fake::gl.programs.insert(name);
    return name;
}
void glDeleteProgram(GLuint program) {
    fake::gl.programs.erase(program);
    if (fake::gl.program == program) fake::gl.program = 0;
}
void glGenVertexArrays(GLsizei n, GLuint* arrays) {
    for (GLsizei i = 0; i < n; ++i) {
        arrays[i] = fake::gl.nextVao++;
        fake::gl.vaos.insert(arrays[i]);
    }
}
void glDeleteVertexArrays(GLsizei n, const GLuint* arrays) {
    for (GLsizei i = 0; i < n; ++i) {
        fake::gl.vaos.erase(arrays[i]);
        if (fake::gl.vao == arrays[i]) fake::gl.vao = 0;
    }
}
void glShaderSource(GLuint shader, GLsizei count, const GLchar* const* string, const GLint*) {
    std::string src;
    for (GLsizei i = 0; i < count; ++i) src += string[i];
    fake::gl.shaderSource[shader] = src;
}
void glCompileShader(GLuint) {}
void glGetShaderiv(GLuint, GLenum, GLint* params) { *params = 1; }  // compiled; a one-byte (empty) log
void glGetShaderInfoLog(GLuint, GLsizei, GLsizei* length, GLchar*) {
    if (length) *length = 0;
}
void glAttachShader(GLuint program, GLuint shader) { fake::gl.programSource[program] += fake::gl.shaderSource[shader]; }
void glLinkProgram(GLuint) {}
void glGetProgramiv(GLuint, GLenum pname, GLint* params) {
    *params = pname == GL_LINK_STATUS ? (fake::gl.linkFails ? 0 : 1) : 1;
}
void glGetProgramInfoLog(GLuint, GLsizei, GLsizei* length, GLchar*) {
    if (length) *length = 0;
}
// A uniform the linked shaders do not declare has no location (-1), as on a driver.
GLint glGetUniformLocation(GLuint program, const GLchar* name) {
    if (fake::gl.programs.count(program) == 0) return -1;
    const std::string& src = fake::gl.programSource[program];
    if (src.find("uniform") == std::string::npos || src.find(std::string(" ") + name + ";") == std::string::npos) return -1;
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
void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) {
    fake::setUniform(location, {v0, v1, v2, v3});
}
void glBindFramebuffer(GLenum, GLuint framebuffer) { fake::gl.fbo = framebuffer; }
void glViewport(GLint, GLint, GLsizei, GLsizei) {}
void glEnable(GLenum cap) {
    if (cap == GL_BLEND) fake::gl.blend = true;
}
void glDisable(GLenum cap) {
    if (cap == GL_BLEND) fake::gl.blend = false;
}
void glBlendEquation(GLenum) {}
void glBlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha) {
    fake::gl.srcRgb = sfactorRGB;
    fake::gl.dstRgb = dfactorRGB;
    fake::gl.srcAlpha = sfactorAlpha;
    fake::gl.dstAlpha = dfactorAlpha;
}
void glBindVertexArray(GLuint array) { fake::gl.vao = fake::gl.vaos.count(array) ? array : 0; }
// A triangle drawn with a live program and a live vertex array of its own lays the shader's black over the
// bound framebuffer's probe pixel, through the blend state; anything else draws nothing.
void glDrawArrays(GLenum mode, GLint, GLsizei count) {
    if (mode != GL_TRIANGLES || count < 3 || fake::gl.program == 0 || fake::gl.vao == 0) return;
    const fake::Pixel dst = fake::at(fake::gl.fbo);
    const fake::Pixel src{0.0f, 0.0f, 0.0f, fake::kShaderAlpha};
    fake::Pixel out = src;
    if (fake::gl.blend) {
        const float sc = fake::factor(fake::gl.srcRgb, src.a), dc = fake::factor(fake::gl.dstRgb, src.a);
        const float sa = fake::factor(fake::gl.srcAlpha, src.a), da = fake::factor(fake::gl.dstAlpha, src.a);
        out = {src.r * sc + dst.r * dc, src.g * sc + dst.g * dc, src.b * sc + dst.b * dc, src.a * sa + dst.a * da};
    }
    fake::gl.pixels[fake::gl.fbo] = out;
    fake::gl.drawnUniforms[fake::gl.fbo] = fake::gl.uniforms[fake::gl.program];
}
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

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

bool samePixel(const fake::Pixel& p, const fake::Pixel& q) {
    return near(p.r, q.r) && near(p.g, q.g) && near(p.b, q.b) && near(p.a, q.a);
}

AAssetManager repoAssets{"app/src/main/assets/"};
AAssetManager noAssets{"/nonexistent-geode-assets/"};

constexpr GLuint kTargetFbo = 7;
constexpr int kW = 1080;
constexpr int kH = 2400;
constexpr float kDt = 1.0f / 60.0f;

std::string fadeVert() {
    std::string error;
    const auto src = geode::viz::ShaderSource(&repoAssets).load("fade_vert.glsl", &error);
    return src ? *src : std::string();
}

bool build(frame::EdgeGlowPass& pass, AAssetManager& assets = repoAssets) {
    std::string error;
    return pass.create(geode::viz::ShaderSource(&assets), nullptr, fadeVert(), &error);
}

eg::Options glowOn(float left = 0.0f, float top = 0.0f, float width = 0.0f, float height = 0.0f) {
    eg::Options o;
    o.enabled = true;
    o.left = left;
    o.top = top;
    o.width = width;
    o.height = height;
    return o;
}

frame::FrameCtx wallpaperFrame(const GeodeSoundFrame* sound, int w = kW, int h = kH) {
    return frame::FrameCtx{w, h, 0.0, kDt, frame::Surface::Wallpaper, sound};
}

void drawFrame(frame::EdgeGlowPass& pass, const frame::FrameCtx& ctx) {
    pass.prepare(ctx);
    pass.draw(frame::PassInputs{}, frame::PassTarget{kTargetFbo, ctx.width, ctx.height});
}

// Kills: blending left off, a plain glBlendFunc that also blends alpha, the draw going to framebuffer 0,
// a create() that asks for the wrong asset. Hand-worked: half black over (0.8, 0.6, 0.4) halves the
// colour to (0.4, 0.3, 0.2), and the composite's alpha 0.7 stays 0.7.
void theGlowDarkensTheTargetsColourAndKeepsItsAlpha() {
    fake::newContext();
    frame::EdgeGlowPass pass;
    check(build(pass), "the pass builds from the shipped edge_glow_frag.glsl (run from the repo root)");
    pass.setOptions(glowOn());
    drawFrame(pass, wallpaperFrame(nullptr));
    check(samePixel(fake::at(kTargetFbo), fake::Pixel{0.4f, 0.3f, 0.2f, 0.7f}), "the glow darkens the target's colour and keeps its alpha");
    check(samePixel(fake::at(0), fake::kUnder), "the glow draws into the target framebuffer, not the window's");
}

// Kills removing `prepared_ = false` in draw(): a second draw of the same frame would lay the black twice
// and darken the rim to a quarter (0.2) instead of a half (0.4). The next prepared frame draws again.
void aPreparedFrameIsLaidOnlyOnce() {
    fake::newContext();
    frame::EdgeGlowPass pass;
    build(pass);
    pass.setOptions(glowOn());
    drawFrame(pass, wallpaperFrame(nullptr));
    pass.draw(frame::PassInputs{}, frame::PassTarget{kTargetFbo, kW, kH});
    check(near(fake::at(kTargetFbo).r, 0.4f), "drawing twice without a new frame darkens only once");
    drawFrame(pass, wallpaperFrame(nullptr));
    check(near(fake::at(kTargetFbo).r, 0.2f), "the next prepared frame is laid again");
}

// Kills removing the `!prepared_` guard: a draw before any frame was prepared leaves the target as it was.
void aDrawBeforeAnyFrameIsPreparedLeavesTheTarget() {
    fake::newContext();
    frame::EdgeGlowPass pass;
    build(pass);
    pass.setOptions(glowOn());
    pass.draw(frame::PassInputs{}, frame::PassTarget{kTargetFbo, kW, kH});
    check(samePixel(fake::at(kTargetFbo), fake::kUnder), "a draw before any prepared frame leaves the target");
}

// Kills an empty release(): once released, a pass draws nothing, however it is driven.
void aReleasedPassDrawsNothing() {
    fake::newContext();
    frame::EdgeGlowPass pass;
    build(pass);
    pass.setOptions(glowOn());
    pass.release();
    drawFrame(pass, wallpaperFrame(nullptr));
    check(samePixel(fake::at(kTargetFbo), fake::kUnder), "a released pass draws nothing");
    check(fake::liveNames() == 0, "a released pass holds no GL names");
}

// Kills a release() that deletes but does not forget its names (Pass.hpp's reason for release): after a
// new context hands the same numbers to another object, releasing again, or destroying the pass, must not
// delete that object's program or vertex array.
void releasingAgainNeverDeletesAnotherObjectsNames() {
    fake::newContext();
    GLuint otherProgram = 0, otherVao = 0;
    {
        frame::EdgeGlowPass pass;
        build(pass);
        fake::newContext();  // the surface was recreated: the old names died with the old context
        pass.release();      // what Renderer::onSurfaceCreated does before anything generates names
        otherProgram = glCreateProgram();
        glGenVertexArrays(1, &otherVao);
        pass.release();
    }
    check(fake::gl.programs.count(otherProgram) == 1, "releasing twice leaves another object's program alive");
    check(fake::gl.vaos.count(otherVao) == 1, "releasing twice leaves another object's vertex array alive");
}

// Kills a destructor that does not release: a destroyed pass leaves no names in the context.
void aDestroyedPassLeavesNoNamesBehind() {
    fake::newContext();
    {
        frame::EdgeGlowPass pass;
        build(pass);
        check(fake::liveNames() > 0, "a built pass holds its names");
    }
    check(fake::liveNames() == 0, "a destroyed pass leaves no GL names behind");
}

// Kills removing the release() at the top of create(): building again replaces the old names, so after
// one release nothing is left.
void rebuildingAPassLeavesNoNamesBehind() {
    fake::newContext();
    frame::EdgeGlowPass pass;
    build(pass);
    build(pass);
    pass.release();
    check(fake::liveNames() == 0, "building a pass twice and releasing it leaves no GL names behind");
}

// Kills a create() that reports success, or generates names, without its shader: a missing asset leaves the
// wallpaper showing the full visual (the Kotlin pass's behaviour) and nothing allocated.
void aMissingShaderLeavesTheFullVisual() {
    fake::newContext();
    frame::EdgeGlowPass pass;
    check(!build(pass, noAssets), "a missing edge_glow_frag.glsl fails the build");
    pass.setOptions(glowOn());
    drawFrame(pass, wallpaperFrame(nullptr));
    check(samePixel(fake::at(kTargetFbo), fake::kUnder), "without its shader the glow draws nothing");
    check(fake::liveNames() == 0, "a failed build holds no GL names");
}

// Kills removing `if (program_ == 0) return false`: a driver that will not link the program leaves the
// full visual and nothing allocated.
void aDriverThatRejectsTheProgramLeavesTheFullVisual() {
    fake::newContext();
    fake::gl.linkFails = true;
    frame::EdgeGlowPass pass;
    check(!build(pass), "a link failure fails the build");
    pass.setOptions(glowOn());
    drawFrame(pass, wallpaperFrame(nullptr));
    check(samePixel(fake::at(kTargetFbo), fake::kUnder), "with no program the glow draws nothing");
    check(fake::liveNames() == 0, "a rejected program holds no GL names");
}

// Kills `surface != Export` -> `==`, `&&` -> `||` and `> 0` -> `>= 0` on the size, and dropping the enabled
// test: the glow belongs to a turned-on wallpaper frame of real size, never to an export.
void theGlowWantsOnlyTurnedOnWallpaperFramesOfRealSize() {
    frame::EdgeGlowPass pass;
    check(!pass.wants(wallpaperFrame(nullptr)), "a pass the shell has not turned on wants no frame");
    pass.setOptions(glowOn());
    check(pass.wants(wallpaperFrame(nullptr)), "a turned-on pass wants a wallpaper frame");
    frame::FrameCtx exportFrame = wallpaperFrame(nullptr);
    exportFrame.surface = frame::Surface::Export;
    check(!pass.wants(exportFrame), "an export frame never draws the edge glow");
    check(!pass.wants(wallpaperFrame(nullptr, 0, kH)), "a frame of zero width wants no glow");
    check(!pass.wants(wallpaperFrame(nullptr, kW, 0)), "a frame of zero height wants no glow");
}

// Kills removing the reset in setOptions(), and resetting on every call: turning the glow off and on again
// starts the band from rest, while the shell moving the window (the launcher panning) keeps it breathing.
void turningTheGlowBackOnStartsTheBandFromRest() {
    frame::EdgeGlowPass pass;
    pass.setOptions(glowOn());
    GeodeSoundFrame loud{};
    for (int i = 0; i < 30; ++i) {
        loud.rms = 0.5f + 0.001f * static_cast<float>(i);
        pass.prepare(wallpaperFrame(&loud));
    }
    const float open = pass.level();
    check(open > 0.9f, "half a second of loud music opens the band");
    pass.setOptions(glowOn(540.0f, 0.0f, 1080.0f, 2400.0f));
    check(pass.level() == open, "moving the window while on keeps the band where it was");
    pass.setOptions(eg::Options{});
    pass.setOptions(glowOn());
    check(pass.level() == 0.0f, "turning the glow off and on again starts the band from rest");
}

// Kills a prepare() that ignores the sound frame or the frame's size, or passes the surface's width and
// height swapped: the band drawn widens with the music, and on a panned 2160 x 2400 wallpaper the window
// reaches the shader in GL's y-up frame. Hand-worked: top 100, height 2000 on a 2400 tall surface puts the
// window's bottom at 2400 - 100 - 2000 = 300.
void theDrawnBandBreathesWithTheMusicOnTheScreensWindow() {
    fake::newContext();
    frame::EdgeGlowPass pass;
    build(pass);
    pass.setOptions(glowOn(540.0f, 100.0f, 1080.0f, 2000.0f));
    drawFrame(pass, wallpaperFrame(nullptr, 2160, 2400));
    auto drawn = fake::gl.drawnUniforms[kTargetFbo];
    const std::vector<float> window{540.0f, 300.0f, 1080.0f, 2000.0f};
    check(drawn["uWindow"] == window, "the shader gets the screen's window in GL's y-up frame");
    check(drawn["uWidth"].size() == 1 && drawn["uRadius"].size() == 1, "the shader gets the band and the corner radius");
    const float rest = drawn["uWidth"].empty() ? 0.0f : drawn["uWidth"][0];
    GeodeSoundFrame loud{};
    for (int i = 0; i < 30; ++i) {
        loud.rms = 0.5f + 0.001f * static_cast<float>(i);
        drawFrame(pass, wallpaperFrame(&loud, 2160, 2400));
    }
    drawn = fake::gl.drawnUniforms[kTargetFbo];
    const float wide = drawn["uWidth"].empty() ? 0.0f : drawn["uWidth"][0];
    check(rest > 0.0f && wide > rest, "loud music widens the band the shader draws");
}

}  // namespace

int main() {
    theGlowDarkensTheTargetsColourAndKeepsItsAlpha();
    aPreparedFrameIsLaidOnlyOnce();
    aDrawBeforeAnyFrameIsPreparedLeavesTheTarget();
    aReleasedPassDrawsNothing();
    releasingAgainNeverDeletesAnotherObjectsNames();
    aDestroyedPassLeavesNoNamesBehind();
    rebuildingAPassLeavesNoNamesBehind();
    aMissingShaderLeavesTheFullVisual();
    aDriverThatRejectsTheProgramLeavesTheFullVisual();
    theGlowWantsOnlyTurnedOnWallpaperFramesOfRealSize();
    turningTheGlowBackOnStartsTheBandFromRest();
    theDrawnBandBreathesWithTheMusicOnTheScreensWindow();
    if (failures != 0) {
        std::printf("%d edge glow pass check(s) failed\n", failures);
        return 1;
    }
    std::printf("edge glow pass: all checks passed\n");
    return 0;
}
