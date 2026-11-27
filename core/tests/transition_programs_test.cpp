// Host-side checks for TransitionPrograms (core/viz/passes/Transition.hpp): the base composite program and its
// LRU of spliced GL Transitions variants. Built and run by CI ("Native host tests") with the system
// compiler, from any directory.
//
// The seam is the class's public interface (create, programFor, uploadParamsIfNeeded, release). The real
// ShaderSource, TransitionCatalog and program::build run; only the system boundaries are faked: GL
// (core/tests/fakegl/GLES3/gl3.h, modelled below as names, compile counts and uniforms per program), the
// APK's asset store (a small in-memory library) and logcat. ProgramStager and ProgramBinaryCache are link
// seams: no stager is current and no cache is passed, so they abort if reached.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "viz/gl/ProgramStager.hpp"
#include "viz/passes/Transition.hpp"

using namespace geode::viz;

// ---- the APK's asset store, in memory -------------------------------------------------------------------
struct AAssetManager {
    std::map<std::string, std::string> files;
};
struct AAsset {
    std::string bytes;
};

extern "C" AAsset* AAssetManager_open(AAssetManager* manager, const char* filename, int /*mode*/) {
    if (manager == nullptr || filename == nullptr) return nullptr;
    const auto it = manager->files.find(filename);
    return it == manager->files.end() ? nullptr : new AAsset{it->second};
}
extern "C" off_t AAsset_getLength(AAsset* asset) { return static_cast<off_t>(asset->bytes.size()); }
extern "C" int AAsset_read(AAsset* asset, void* buf, size_t count) {
    const size_t n = std::min(count, asset->bytes.size());
    std::memcpy(buf, asset->bytes.data(), n);
    return static_cast<int>(n);
}
extern "C" void AAsset_close(AAsset* asset) { delete asset; }

extern "C" int __android_log_print(int, const char*, const char*, ...) { return 0; }

// ---- our own code's link seams --------------------------------------------------------------------------
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

// A shader whose source holds this text does not compile.
constexpr const char* kBroken = "MV_BROKEN";

struct Context {
    GLuint nextShader = 1, nextProgram = 1;
    std::set<GLuint> shaders, programs;
    std::map<GLuint, std::string> shaderSource;
    std::map<GLuint, std::vector<std::string>> locations;
    std::map<GLuint, std::map<std::string, std::vector<float>>> uniforms;
    GLuint program = 0;
    int compiles = 0;
    int locationQueries = 0;
};

Context gl;

void newContext() { gl = Context{}; }

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
void glShaderSource(GLuint shader, GLsizei count, const GLchar* const* string, const GLint*) {
    std::string src;
    for (GLsizei i = 0; i < count; ++i) src += string[i];
    fake::gl.shaderSource[shader] = src;
}
void glCompileShader(GLuint) { ++fake::gl.compiles; }
void glGetShaderiv(GLuint shader, GLenum pname, GLint* params) {
    const bool broken = fake::gl.shaderSource[shader].find(fake::kBroken) != std::string::npos;
    *params = pname == GL_COMPILE_STATUS ? (broken ? 0 : 1) : 1;
}
void glGetShaderInfoLog(GLuint, GLsizei, GLsizei* length, GLchar*) {
    if (length) *length = 0;
}
void glAttachShader(GLuint, GLuint) {}
void glLinkProgram(GLuint) {}
void glGetProgramiv(GLuint, GLenum, GLint* params) { *params = 1; }
void glGetProgramInfoLog(GLuint, GLsizei, GLsizei* length, GLchar*) {
    if (length) *length = 0;
}
// Every name has a location on a live program: the model does not parse the shader.
GLint glGetUniformLocation(GLuint program, const GLchar* name) {
    ++fake::gl.locationQueries;
    if (fake::gl.programs.count(program) == 0) return -1;
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
void glUniform2i(GLint location, GLint v0, GLint v1) {
    fake::setUniform(location, {static_cast<float>(v0), static_cast<float>(v1)});
}
void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) { fake::setUniform(location, {v0, v1, v2}); }
void glUniform3i(GLint location, GLint v0, GLint v1, GLint v2) {
    fake::setUniform(location, {static_cast<float>(v0), static_cast<float>(v1), static_cast<float>(v2)});
}
void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) {
    fake::setUniform(location, {v0, v1, v2, v3});
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

std::string entry(const char* name, const char* glsl) {
    return std::string("{\"name\":\"") + name + "\",\"glsl\":\"" + glsl +
           "\",\"paramsTypes\":{\"amount\":\"float\"},\"defaultParams\":{\"amount\":0.75}}";
}

// Five good transitions (one more than the LRU holds) and one whose GLSL does not compile.
AAssetManager assets{{
    {"shaders/composite_frag.glsl", "#version 300 es\n// __GL_TRANSITION_SOURCE__\nvoid main() {}\n"},
    {"gl_transitions.json", "[" + entry("a", "// a") + "," + entry("b", "// b") + "," + entry("c", "// c") + "," +
                                entry("d", "// d") + "," + entry("e", "// e") + "," + entry("broken", "MV_BROKEN") + "]"},
}};

constexpr const char* kVert = "#version 300 es\nvoid main() {}\n";

// Kills rebuilding a transition the driver already refused: CompositePass::draw asks for the program every
// frame of the transition, so a failed id must cost one build, fall back to the base program, and be tried
// again only after release() (a new context).
void aTransitionThatFailsToBuildIsTriedOncePerContext() {
    fake::newContext();
    const ShaderSource source(&assets);
    TransitionPrograms programs(source, nullptr);
    std::string error;
    check(programs.create(kVert, &error), "the base composite program builds");
    const GLuint base = programs.programFor("fade").program();
    check(base != 0, "a built-in style draws with the base program");

    const int before = fake::gl.compiles;
    check(programs.programFor("broken").program() == base, "a transition that fails to build draws with the base program");
    const int afterFirst = fake::gl.compiles;
    check(afterFirst > before, "the first frame tries to build it");
    for (int frame = 0; frame < 30; ++frame) {
        check(programs.programFor("broken").program() == base, "later frames still draw with the base program");
    }
    check(fake::gl.compiles == afterFirst, "later frames of the transition do not compile it again");
    check(programs.programFor("a").program() != base, "a good transition still builds beside a failed one");

    programs.release();
    check(programs.create(kVert, &error), "the base program builds again after release");
    const int rebuilt = fake::gl.compiles;
    programs.programFor("broken");
    check(fake::gl.compiles > rebuilt, "release forgets the failure, so a new context tries once more");
}

// One frame of CompositePass::draw for a library transition: bind its program, upload its params.
GLuint drawWith(TransitionPrograms& programs, const std::string& id) {
    UniformCache& program = programs.programFor(id);
    glUseProgram(program.program());
    programs.uploadParamsIfNeeded(program, programs.definition(id));
    return program.program();
}

bool hasAmount(GLuint program) {
    const auto& uniforms = fake::gl.uniforms[program];
    const auto it = uniforms.find("amount");
    return it != uniforms.end() && it->second == std::vector<float>{0.75f};
}

// Kills a stale "params already uploaded" note after an eviction: a rebuilt program is a new GL program
// with zeroed uniforms, even when its UniformCache lands on the evicted one's address. "a" is drawn, then
// evicted by "e" (which takes its freed block), then "e" is aged to the front and evicted by "a" again.
void aRebuiltTransitionGetsItsParamsAgain() {
    fake::newContext();
    const ShaderSource source(&assets);
    TransitionPrograms programs(source, nullptr);
    std::string error;
    programs.create(kVert, &error);
    const GLuint first = drawWith(programs, "a");
    check(hasAmount(first), "a library transition's params are uploaded when it is first drawn");
    programs.programFor("b");
    programs.programFor("c");
    programs.programFor("d");
    programs.programFor("e");
    check(fake::gl.programs.count(first) == 0, "a fifth transition evicts the oldest program");
    programs.programFor("b");
    programs.programFor("c");
    programs.programFor("d");
    const GLuint rebuilt = drawWith(programs, "a");
    check(rebuilt != first && fake::gl.programs.count(rebuilt) == 1, "the evicted transition is built again");
    check(hasAmount(rebuilt), "the rebuilt program gets its params again");
}

// Kills a per-frame name lookup: an interned uniform is asked of the driver once per program, then read from
// a slot; a rebuilt program (a new UniformCache) asks again, so it never reads the old program's locations.
void anInternedUniformIsResolvedOncePerProgram() {
    fake::newContext();
    const ShaderSource source(&assets);
    TransitionPrograms programs(source, nullptr);
    std::string error;
    programs.create(kVert, &error);
    UniformCache& base = programs.programFor("fade");
    const GLint first = base.loc(Uniform::uTime);
    const int asked = fake::gl.locationQueries;
    for (int frame = 0; frame < 100; ++frame) check(base.loc(Uniform::uTime) == first, "a slot returns the location it resolved");
    check(fake::gl.locationQueries == asked, "later frames do not ask the driver again");
    check(base.loc(std::string_view("uTime")) == first, "the interned and the named lookups agree");
    check(base.arrayCount(Uniform::uKnob, 6) == base.arrayCount("uKnob", 6), "the interned and the named array counts agree");
    check(base.loc(Uniform::uProgress) != first, "two names get two locations");

    UniformCache elsewhere(base.program() + 100);
    const int before = fake::gl.locationQueries;
    check(elsewhere.loc(Uniform::uTime) == -1, "a program the driver does not have reports no location");
    check(fake::gl.locationQueries == before + 1, "a new UniformCache resolves again instead of reusing another program's slot");
}

bool toldItsUnits(GLuint program) {
    const auto& uniforms = fake::gl.uniforms[program];
    auto is = [&](const char* name, int unit) {
        const auto it = uniforms.find(name);
        return it != uniforms.end() && it->second == std::vector<float>{static_cast<float>(unit)};
    };
    return is("uTexA", composite_unit::kTexA) && is("uTexB", composite_unit::kTexB) && is("uFlow", composite_unit::kFlow) &&
           is("uNoise", composite_unit::kNoise) && is("uUnderlay", composite_unit::kUnderlay) && is("uTexL", composite_unit::kTexL);
}

// CompositePass binds its textures to composite_unit's units and no longer sets the samplers each frame, so every
// program the composite can draw with is told them when it is built: the base, a library transition, and one
// built again after its eviction (a new program, its uniforms at zero).
void everyBuiltProgramIsToldItsSamplerUnits() {
    fake::newContext();
    const ShaderSource source(&assets);
    TransitionPrograms programs(source, nullptr);
    std::string error;
    programs.create(kVert, &error);
    check(toldItsUnits(programs.programFor("fade").program()), "the base program is told its sampler units when built");
    const GLuint first = programs.programFor("a").program();
    check(toldItsUnits(first), "a library transition's program is told them when built");
    for (const char* id : {"b", "c", "d", "e"}) programs.programFor(id);
    check(fake::gl.programs.count(first) == 0, "a fifth transition evicts the oldest program");
    const GLuint rebuilt = programs.programFor("a").program();
    check(rebuilt != first && toldItsUnits(rebuilt), "a program built again is told them again");
}

}  // namespace

int main() {
    aTransitionThatFailsToBuildIsTriedOncePerContext();
    aRebuiltTransitionGetsItsParamsAgain();
    anInternedUniformIsResolvedOncePerProgram();
    everyBuiltProgramIsToldItsSamplerUnits();
    if (failures != 0) {
        std::printf("%d transition programs check(s) failed\n", failures);
        return 1;
    }
    std::printf("transition programs: all checks passed\n");
    return 0;
}
