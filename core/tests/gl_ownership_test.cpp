// Host-side checks for who deletes a GL name (core/viz/gl/GlHandle.hpp). A type that owns a GL object (it
// generates the name and its release or destructor deletes it) must never be copied: two copies would delete
// the same name twice, or one would delete it while the other still draws with it. A type that is handed
// from one owner to another (ComputeProgram into ComputePass, a FullscreenTriangle) moves the name and
// leaves the source empty, so the source deletes nothing; every other owner can be neither copied nor moved.
// Built and run by CI ("Native host tests" in .github/workflows/native-host-tests.yml) with the system
// compiler, from the repo root.
//
// The seam is each type's public interface. The real Compute.cpp and Quad.cpp run; only the system boundary
// is faked: GL (core/tests/fakegl, modelled below as the live names of each kind and every delete, so a
// delete of a name that is not live reads as a double delete). Link seams stand in for the parts of our own
// code that Compute.cpp names but never reaches here: no staged build is open (ProgramStager::current() is
// null by its contract outside an Attempt) and the probe is never asked, so the rest aborts if ever called.
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <utility>

#include "viz/gl/Compute.hpp"
#include "viz/gl/Framebuffer.hpp"
#include "viz/gl/GlHandle.hpp"
#include "viz/gl/GlProber.hpp"
#include "viz/gl/ProgramStager.hpp"
#include "viz/gl/Program.hpp"
#include "viz/gl/Quad.hpp"
#include "viz/styles/shared/Scene.hpp"
#include "viz/gl/Texture.hpp"
#include "viz/passes/Transition.hpp"
#include "viz/styles/silk/SimPass.hpp"
#include "viz/frame/ExportPasses.hpp"
#include "viz/frame/Pass.hpp"
#include "viz/liquid/LiquidPass.hpp"

namespace viz = geode::viz;

// ---- the gate: no GL owner can be copied ----------------------------------------------------------------
// The polymorphic bases carry the rule for every scene, frame pass and simulation pass derived from them.
template <typename T>
constexpr bool kNotCopyable = !std::is_copy_constructible_v<T> && !std::is_copy_assignable_v<T>;

static_assert(kNotCopyable<viz::GlProgram>);
static_assert(kNotCopyable<viz::GlVertexArray>);
static_assert(kNotCopyable<viz::GlBuffer>);
static_assert(kNotCopyable<viz::GlTexture>);
static_assert(kNotCopyable<viz::GlFramebuffer>);
static_assert(kNotCopyable<viz::ComputeProgram>);
static_assert(kNotCopyable<viz::ComputePass>);
static_assert(kNotCopyable<viz::FullscreenTriangle>);
static_assert(kNotCopyable<viz::TransitionPrograms>);
static_assert(kNotCopyable<viz::liquid::LiquidPass>);
static_assert(kNotCopyable<viz::Scene>);
static_assert(kNotCopyable<viz::frame::Pass>);
static_assert(kNotCopyable<viz::frame::WatermarkPass>);
static_assert(kNotCopyable<viz::frame::SeamPass>);
static_assert(kNotCopyable<viz::sim::SimPass>);

// What is handed over moves, and a move cannot throw half way.
static_assert(std::is_nothrow_move_constructible_v<viz::GlProgram> && std::is_nothrow_move_assignable_v<viz::GlProgram>);
static_assert(std::is_nothrow_move_constructible_v<viz::ComputeProgram> && std::is_nothrow_move_assignable_v<viz::ComputeProgram>);
static_assert(std::is_nothrow_move_constructible_v<viz::ComputePass>);
static_assert(std::is_nothrow_move_constructible_v<viz::FullscreenTriangle> && std::is_nothrow_move_assignable_v<viz::FullscreenTriangle>);
// A handle is one name and nothing else.
static_assert(sizeof(viz::GlProgram) == sizeof(GLuint));
// A program built into a uniform cache is owned with it.
static_assert(kNotCopyable<viz::GlSampler>);
static_assert(kNotCopyable<viz::OwnedProgram>);
static_assert(std::is_nothrow_move_constructible_v<viz::OwnedProgram> && std::is_nothrow_move_assignable_v<viz::OwnedProgram>);
static_assert(kNotCopyable<viz::Framebuffer>);
static_assert(kNotCopyable<viz::Texture>);

// ---- the APK's log, our own code's link seams --------------------------------------------------------------
extern "C" int __android_log_print(int, const char*, const char*, ...) { return 0; }

geode::viz::ProgramStager* geode::viz::ProgramStager::current() { return nullptr; }
GLuint geode::viz::ProgramStager::obtain(const std::vector<ShaderStage>&, std::string*, bool*, bool) { std::abort(); }
void geode::viz::prober::drainErrors() { std::abort(); }
int geode::viz::prober::limit(unsigned int) { std::abort(); }
std::string geode::viz::ComputeProof::sentence() const { std::abort(); }

// ---- GL, as the names of one context ----------------------------------------------------------------------
namespace fake {

// Each kind numbers its own names from 1, as drivers do.
struct Kind {
    GLuint next = 1;
    std::set<GLuint> live;
    int deletes = 0;  // deletes of a live name
    int strays = 0;   // deletes of a name that is not live: the same name deleted twice
    GLuint make() {
        live.insert(next);
        return next++;
    }
    void drop(GLuint name) {
        if (name == 0) return;  // GL ignores 0
        if (live.erase(name) == 1) {
            ++deletes;
        } else {
            ++strays;
        }
    }
};

struct Context {
    Kind shaders, programs, vaos, buffers, textures, framebuffers, samplers;
    GLenum status = GL_FRAMEBUFFER_COMPLETE;  // what glCheckFramebufferStatus answers
    GLint linkedSize[3] = {1, 1, 1};
};

Context gl;

void newContext() { gl = Context{}; }

int strays() {
    return gl.shaders.strays + gl.programs.strays + gl.vaos.strays + gl.buffers.strays + gl.textures.strays + gl.framebuffers.strays +
           gl.samplers.strays;
}

size_t liveNames() {
    return gl.shaders.live.size() + gl.programs.live.size() + gl.vaos.live.size() + gl.buffers.live.size() + gl.textures.live.size() +
           gl.framebuffers.live.size() + gl.samplers.live.size();
}

}  // namespace fake

extern "C" {
GLuint glCreateShader(GLenum) { return fake::gl.shaders.make(); }
void glDeleteShader(GLuint shader) { fake::gl.shaders.drop(shader); }
GLuint glCreateProgram(void) { return fake::gl.programs.make(); }
void glDeleteProgram(GLuint program) { fake::gl.programs.drop(program); }
void glGenVertexArrays(GLsizei n, GLuint* arrays) {
    for (GLsizei i = 0; i < n; ++i) arrays[i] = fake::gl.vaos.make();
}
void glDeleteVertexArrays(GLsizei n, const GLuint* arrays) {
    for (GLsizei i = 0; i < n; ++i) fake::gl.vaos.drop(arrays[i]);
}
void glGenBuffers(GLsizei n, GLuint* buffers) {
    for (GLsizei i = 0; i < n; ++i) buffers[i] = fake::gl.buffers.make();
}
void glDeleteBuffers(GLsizei n, const GLuint* buffers) {
    for (GLsizei i = 0; i < n; ++i) fake::gl.buffers.drop(buffers[i]);
}
void glDeleteTextures(GLsizei n, const GLuint* textures) {
    for (GLsizei i = 0; i < n; ++i) fake::gl.textures.drop(textures[i]);
}
void glDeleteFramebuffers(GLsizei n, const GLuint* framebuffers) {
    for (GLsizei i = 0; i < n; ++i) fake::gl.framebuffers.drop(framebuffers[i]);
}
void glShaderSource(GLuint, GLsizei, const GLchar* const*, const GLint*) {}
void glCompileShader(GLuint) {}
void glGetShaderiv(GLuint, GLenum, GLint* params) { *params = 1; }
void glGetShaderInfoLog(GLuint, GLsizei, GLsizei* length, GLchar*) {
    if (length) *length = 0;
}
void glAttachShader(GLuint, GLuint) {}
void glDetachShader(GLuint, GLuint) {}
void glLinkProgram(GLuint) {}
// Linked, at the local size the test asks for.
void glGetProgramiv(GLuint, GLenum pname, GLint* params) {
    if (pname == GL_COMPUTE_WORK_GROUP_SIZE) {
        for (int i = 0; i < 3; ++i) params[i] = fake::gl.linkedSize[i];
        return;
    }
    *params = 1;
}
void glGetProgramInfoLog(GLuint, GLsizei, GLsizei* length, GLchar*) {
    if (length) *length = 0;
}
GLint glGetUniformLocation(GLuint, const GLchar*) { return -1; }
void glUseProgram(GLuint) {}
GLenum glGetError(void) { return GL_NO_ERROR; }
void glGetIntegeri_v(GLenum, GLuint, GLint* data) { *data = 0; }
void glDispatchCompute(GLuint, GLuint, GLuint) {}
void glMemoryBarrier(GLbitfield) {}
void glBindImageTexture(GLuint, GLuint, GLint, GLboolean, GLint, GLenum, GLenum) {}
void glActiveTexture(GLenum) {}
void glBindTexture(GLenum, GLuint) {}
void glBindVertexArray(GLuint) {}
void glBindBuffer(GLenum, GLuint) {}
void glBufferData(GLenum, GLsizeiptr, const void*, GLenum) {}
void glEnableVertexAttribArray(GLuint) {}
void glVertexAttribPointer(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*) {}
void glDrawArrays(GLenum, GLint, GLsizei) {}
void glDisable(GLenum) {}
void glColorMask(GLboolean, GLboolean, GLboolean, GLboolean) {}
void glDepthMask(GLboolean) {}
void glStencilMask(GLuint) {}
void glBlendEquation(GLenum) {}
void glPixelStorei(GLenum, GLint) {}
void glBindSampler(GLuint, GLuint) {}
void glGenTextures(GLsizei n, GLuint* textures) {
    for (GLsizei i = 0; i < n; ++i) textures[i] = fake::gl.textures.make();
}
void glGenFramebuffers(GLsizei n, GLuint* framebuffers) {
    for (GLsizei i = 0; i < n; ++i) framebuffers[i] = fake::gl.framebuffers.make();
}
void glGenSamplers(GLsizei n, GLuint* samplers) {
    for (GLsizei i = 0; i < n; ++i) samplers[i] = fake::gl.samplers.make();
}
void glDeleteSamplers(GLsizei n, const GLuint* samplers) {
    for (GLsizei i = 0; i < n; ++i) fake::gl.samplers.drop(samplers[i]);
}
void glTexParameteri(GLenum, GLenum, GLint) {}
void glTexImage2D(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) {}
void glTexSubImage2D(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*) {}
void glFramebufferTexture2D(GLenum, GLenum, GLenum, GLuint, GLint) {}
GLenum glCheckFramebufferStatus(GLenum) { return fake::gl.status; }
void glClearColor(GLfloat, GLfloat, GLfloat, GLfloat) {}
void glClear(GLbitfield) {}
void glInvalidateFramebuffer(GLenum, GLsizei, const GLenum*) {}
void glBindFramebuffer(GLenum, GLuint) {}
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

const viz::WorkGroupSize kLocal{8, 8, 1};

// ComputeProgram::build outside a staged build, linked at the local size it asked for.
std::optional<viz::ComputeProgram> buildProgram() {
    fake::gl.linkedSize[0] = kLocal.x;
    fake::gl.linkedSize[1] = kLocal.y;
    fake::gl.linkedSize[2] = kLocal.z;
    std::string error;
    return viz::ComputeProgram::build("ownership", "void main() {}", kLocal, &error);
}

void aHandleDeletesItsNameOnceWhereverItMoved() {
    fake::newContext();
    const GLuint name = glCreateProgram();
    {
        viz::GlProgram first(name);
        viz::GlProgram second(std::move(first));
        check(first.get() == 0 && !first, "a moved-from handle holds no name");
        check(second.get() == name, "the moved-to handle holds the name");
        viz::GlProgram third;
        third = std::move(second);
        check(second.get() == 0 && third.get() == name, "move assignment hands the name over too");
        check(fake::gl.programs.deletes == 0, "nothing is deleted while one handle still holds the name");
    }
    check(fake::gl.programs.deletes == 1, "the name is deleted once, by the handle that held it last");

    const GLuint kept = glCreateProgram();
    const GLuint replaced = glCreateProgram();
    {
        viz::GlProgram holder(replaced);
        viz::GlProgram incoming(kept);
        holder = std::move(incoming);
        check(fake::gl.programs.live.count(replaced) == 0, "a handle assigned over deletes the name it held");
        check(fake::gl.programs.live.count(kept) == 1, "and keeps the one it was given");
        auto& alias = holder;
        holder = std::move(alias);
        check(holder.get() == kept && fake::gl.programs.live.count(kept) == 1, "a move onto itself keeps the name");
        holder.reset();
        check(holder.get() == 0 && fake::gl.programs.live.count(kept) == 0, "reset deletes the name now");
    }

    const GLuint lost = glCreateProgram();
    {
        viz::GlProgram orphan(lost);
        orphan.forget();
        check(orphan.get() == 0, "forget drops the name");
    }
    check(fake::gl.programs.live.count(lost) == 1, "a forgotten name is not deleted: its context is gone");

    {
        viz::GlTexture texture(fake::gl.textures.make());
        viz::GlFramebuffer framebuffer(fake::gl.framebuffers.make());
        viz::GlBuffer buffer(fake::gl.buffers.make());
        viz::GlVertexArray vao(fake::gl.vaos.make());
    }
    check(fake::gl.textures.deletes == 1 && fake::gl.framebuffers.deletes == 1 && fake::gl.buffers.deletes == 1 &&
              fake::gl.vaos.deletes == 1,
          "each kind of handle deletes its own kind of name");
    check(fake::strays() == 0, "no name is deleted twice");
}

void aComputeProgramMovedIntoAPassIsDeletedOnce() {
    fake::newContext();
    {
        // As compute/SimPass.cpp builds one: the optional's program moves into the pass.
        auto program = buildProgram();
        check(program.has_value() && program->program() != 0, "the compute program builds");
        const GLuint name = program ? program->program() : 0;
        std::optional<viz::ComputePass> pass;
        if (program) pass.emplace("ownership", std::move(*program), static_cast<GLbitfield>(viz::ComputeReader::TextureSample));
        check(program && program->program() == 0, "the program left behind holds no name");
        check(pass && pass->programName() == name, "the pass holds the program");
        program.reset();
        check(fake::gl.programs.deletes == 0, "dropping the moved-from program deletes nothing");
        std::optional<viz::ComputePass> moved(std::move(pass));
        pass.reset();
        check(fake::gl.programs.deletes == 0, "a pass moved on deletes nothing behind it");
        moved->release();
        check(fake::gl.programs.deletes == 1 && fake::gl.programs.live.count(name) == 0, "release deletes the program");
        check(moved->programName() == 0, "a released pass holds no name");
        moved->release();
    }
    check(fake::gl.programs.deletes == 1, "a released pass deletes nothing more, released again or destroyed");

    {
        auto program = buildProgram();
        const GLuint name = program ? program->program() : 0;
        if (program) {
            viz::ComputePass pass("ownership", std::move(*program), static_cast<GLbitfield>(viz::ComputeReader::TextureSample));
            pass.forget();
        }
        check(fake::gl.programs.live.count(name) == 1, "a forgotten pass deletes nothing: its context is gone");
    }

    {
        auto program = buildProgram();
        const GLuint name = program ? program->program() : 0;
        if (program) {
            viz::ComputePass pass("ownership", std::move(*program), static_cast<GLbitfield>(viz::ComputeReader::TextureSample));
        }
        check(fake::gl.programs.live.count(name) == 0, "a pass that still holds its program deletes it when it goes");
    }
    check(fake::strays() == 0, "no compute program is deleted twice");
}

void aTriangleMovedOnIsDeletedOnce() {
    fake::newContext();
    {
        viz::FullscreenTriangle first;
        first.draw();  // made on first use
        const GLuint vao = first.vao();
        check(vao != 0 && fake::gl.buffers.live.size() == 1, "drawing makes the triangle's VAO and buffer");
        viz::FullscreenTriangle second(std::move(first));
        check(first.vao() == 0 && second.vao() == vao, "the VAO moves to the new triangle");
        viz::FullscreenTriangle third;
        third = std::move(second);
        check(second.vao() == 0 && third.vao() == vao, "move assignment hands the VAO over too");
        check(fake::gl.vaos.deletes == 0 && fake::gl.buffers.deletes == 0, "nothing is deleted while one triangle holds the names");
        third.release();
        check(fake::gl.vaos.deletes == 1 && fake::gl.buffers.deletes == 1, "release deletes the VAO and the buffer");
    }
    check(fake::gl.vaos.deletes == 1 && fake::gl.buffers.deletes == 1, "no triangle deletes them again");

    {
        viz::FullscreenTriangle held;
        held.create();
    }
    check(fake::gl.vaos.deletes == 2 && fake::gl.buffers.deletes == 2, "a triangle deletes its names when it goes");

    {
        viz::FullscreenTriangle lost;
        lost.create();
        lost.forget();
    }
    check(fake::gl.vaos.deletes == 2 && fake::gl.buffers.deletes == 2, "a forgotten triangle deletes nothing: its context is gone");
    check(fake::strays() == 0, "no triangle name is deleted twice");
}

// The owning uniform cache: the program goes with the cache, and the cache never names a program it gave up.
void anOwnedProgramDeletesItsProgramOnce() {
    fake::newContext();
    const GLuint first = glCreateProgram();
    const GLuint second = glCreateProgram();
    {
        viz::OwnedProgram held(first);
        check(held.program() == first, "an owned program names its program");
        viz::OwnedProgram moved(std::move(held));
        check(held.program() == 0 && moved.program() == first, "a move leaves the source naming no program");
        moved.reset(second);
        check(fake::gl.programs.live.count(first) == 0 && moved.program() == second, "reset deletes the old program and names the new one");
        viz::OwnedProgram other;
        other = std::move(moved);
        check(moved.program() == 0 && other.program() == second && fake::gl.programs.deletes == 1, "move assignment hands it over");
        other.release();
        check(other.program() == 0 && fake::gl.programs.live.count(second) == 0, "release deletes it");
    }
    check(fake::gl.programs.deletes == 2, "nothing is deleted twice when they go");

    const GLuint lost = glCreateProgram();
    {
        viz::OwnedProgram orphan(lost);
        orphan.forget();
        check(orphan.program() == 0, "forget leaves the cache naming no program");
    }
    check(fake::gl.programs.live.count(lost) == 1, "a forgotten program is not deleted");

    const GLuint kept = glCreateProgram();
    {
        viz::OwnedProgram scoped(kept);
    }
    check(fake::gl.programs.live.count(kept) == 0, "an owned program still held is deleted when it goes");

    {
        viz::GlSampler a(viz::gl_gen::sampler());
        viz::GlSampler b(viz::gl_gen::sampler());
        const GLuint aName = a.get();
        const GLuint bName = b.get();
        a.swap(b);
        check(a.get() == bName && b.get() == aName && fake::gl.samplers.deletes == 0, "a swap trades names and deletes none");
    }
    check(fake::gl.samplers.deletes == 2 && fake::gl.samplers.live.empty(), "samplers are deleted as samplers");
    check(fake::strays() == 0, "no program or sampler is deleted twice");
}

// A framebuffer and a texture: every name made is deleted exactly once on release, none after forget, and none
// is left behind when they go.
void framebuffersAndTexturesDeleteEachNameOnce() {
    fake::newContext();
    {
        viz::Framebuffer fb("ownership");
        check(fb.ensure(4, 4) && fb.fbo() != 0 && fb.tex() != 0, "a framebuffer makes its FBO and texture");
        check(fb.ensure(8, 8), "and makes them again at a new size");
        check(fake::gl.framebuffers.deletes == 1 && fake::gl.textures.deletes == 1, "deleting the old pair once");
        viz::Framebuffer other("other");
        other.ensure(2, 2);
        const GLuint mine = fb.fbo();
        const GLuint theirs = other.fbo();
        fb.swap(other);
        check(fb.fbo() == theirs && other.fbo() == mine && fake::gl.framebuffers.deletes == 1, "a swap trades the pairs and deletes none");
        other.release();
        check(other.fbo() == 0 && other.tex() == 0 && fake::gl.framebuffers.live.count(mine) == 0, "release deletes the pair");
        other.release();
    }
    check(fake::liveNames() == 0, "a framebuffer still holding its pair deletes it when it goes");

    fake::gl.status = 0;
    {
        viz::Framebuffer broken("incomplete");
        check(!broken.ensure(4, 4) && broken.fbo() == 0, "an incomplete framebuffer gives its pair back");
    }
    check(fake::liveNames() == 0, "and leaves nothing behind");
    fake::gl.status = GL_FRAMEBUFFER_COMPLETE;

    {
        viz::Framebuffer lost("lost");
        lost.ensure(4, 4);
        lost.forget();
        check(lost.fbo() == 0 && !lost.ok(), "forget drops the pair");
    }
    check(fake::gl.framebuffers.live.size() == 1 && fake::gl.textures.live.size() == 1, "a forgotten pair is not deleted");

    fake::newContext();
    {
        viz::Texture texture;
        texture.createImage(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, 4, GL_LINEAR, GL_CLAMP_TO_EDGE);
        const GLuint first = texture.id();
        texture.createImage(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 8, 8, GL_LINEAR, GL_CLAMP_TO_EDGE);
        check(fake::gl.textures.live.count(first) == 0 && texture.id() != 0, "a texture made again deletes the old one");
        texture.release();
        check(texture.id() == 0 && fake::gl.textures.live.empty(), "release deletes it");
        texture.createImage(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, 4, GL_LINEAR, GL_CLAMP_TO_EDGE);
    }
    check(fake::gl.textures.live.empty(), "a texture still held is deleted when it goes");
    {
        viz::Texture lost;
        lost.createImage(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, 4, GL_LINEAR, GL_CLAMP_TO_EDGE);
        lost.forget();
    }
    check(fake::gl.textures.live.size() == 1, "a forgotten texture is not deleted");
    check(fake::strays() == 0, "no framebuffer or texture name is deleted twice");
}

}  // namespace

int main() {
    aHandleDeletesItsNameOnceWhereverItMoved();
    aComputeProgramMovedIntoAPassIsDeletedOnce();
    aTriangleMovedOnIsDeletedOnce();
    anOwnedProgramDeletesItsProgramOnce();
    framebuffersAndTexturesDeleteEachNameOnce();
    if (failures != 0) {
        std::printf("%d GL ownership check(s) failed\n", failures);
        return 1;
    }
    std::printf("GL ownership: all checks passed\n");
    return 0;
}
