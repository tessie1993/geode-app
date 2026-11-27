#pragma once
#include <GLES3/gl3.h>

#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "viz/gl/Program.hpp"
#include "viz/gl/ProgramStager.hpp"
#include "viz/glsl/ShaderSource.hpp"
#include "viz/passes/TransitionLibrary.hpp"
#include "viz/modulation/VisualSafety.hpp"

namespace geode::viz {

// The built-in styles plus the GL Transitions library asset (parsed by TransitionLibrary).
class TransitionCatalog {
public:
    static constexpr int kStyleLibrary = 5;

    using Param = TransitionParam;
    using Def = TransitionDef;

    explicit TransitionCatalog(const ShaderSource& assets) : assets_(assets) {}

    const std::vector<Def>& library();
    std::vector<std::string> allIds();
    // The built-in styles' ids, in TransitionStyle order: allIds() without the library.
    static std::vector<std::string> builtInIds();
    const Def* definition(const std::string& id);
    static std::optional<TransitionStyle> builtIn(const std::string& id);
    static std::string spliceInto(const std::string& base, const Def& def);
    static void uploadParams(GLuint program, const Def& def);

private:
    const ShaderSource& assets_;
    bool loaded_ = false;
    std::vector<Def> library_;
    std::map<std::string, size_t> byName_;
};

// composite_frag.glsl's texture units. Each program here is told them once, when it is built (a sampler keeps its
// unit until the program is linked again); CompositePass binds its textures to them every frame.
namespace composite_unit {
inline constexpr int kTexA = 0;
inline constexpr int kTexB = 1;
inline constexpr int kFlow = 2;
inline constexpr int kNoise = 4;
inline constexpr int kUnderlay = 5;
inline constexpr int kTexL = 6;
}  // namespace composite_unit

// The base composite program plus an LRU of four spliced variants.
//
// Live, a spliced variant is compiled by its own ProgramStager in budgeted steps (step()), never by the frame
// that asks for it: a library transition's compile is a whole shader build, which on a slow driver is a dropped
// frame or more. Until it is built the base program answers, and draws the plain crossfade. An export builds
// inline (setStaged(false)), so its frames are the same however long the driver takes.
class TransitionPrograms {
public:
    TransitionPrograms(const ShaderSource& assets, ProgramBinaryCache* cache)
        : catalog_(assets), assets_(assets), cache_(cache), stager_(cache) {}
    ~TransitionPrograms() { release(); }
    // Owns the programs it builds: neither copied nor moved (viz/gl/GlHandle.hpp).
    TransitionPrograms(const TransitionPrograms&) = delete;
    TransitionPrograms& operator=(const TransitionPrograms&) = delete;

    // Compiles the base composite program; returns false with the driver log in `error`.
    bool create(const std::string& fadeVert, std::string* error);
    void release();
    // The context is gone: drops every program without a GL call.
    void forget();
    TransitionCatalog& catalog() { return catalog_; }
    const TransitionCatalog::Def* definition(const std::string& id) { return catalog_.definition(id); }
    // The program to draw `id` with: its built variant, else the base program (asking the stager for it when live).
    UniformCache& programFor(const std::string& id);
    // A transition about to start: its own program is built (a built-in style always is). False: crossfade instead.
    bool ready(const std::string& id);
    bool isBase(const UniformCache& program) const { return &program == &base_; }
    void warm(const std::string& id);
    // Live (true, the default) or an export (false: programFor builds inline).
    void setStaged(bool staged) { staged_ = staged; }
    // The new context's GL_KHR_parallel_shader_compile, after create().
    void onContext(bool parallelCompile) { stager_.onContext(parallelCompile); }
    // Once per frame while staging(): a budgeted slice of the queued compiles.
    void step(double nowS);
    bool staging() const { return !stager_.idle(); }
    void uploadParamsIfNeeded(UniformCache& program, const TransitionCatalog::Def* def);

private:
    // `id`'s spliced program: built inline, or from the stager (0 and `placeholder` while it compiles).
    GLuint build(const std::string& id, const TransitionCatalog::Def& def, std::string* error, bool* placeholder);
    static constexpr size_t kMaxPrograms = 4;

    TransitionCatalog catalog_;
    const ShaderSource& assets_;
    ProgramBinaryCache* cache_;
    OwnedProgram base_;
    std::string source_;
    std::string fadeVert_;
    std::list<std::pair<std::string, std::unique_ptr<OwnedProgram>>> lru_;
    // Ids whose spliced program failed to build in this context: not tried again until release().
    std::set<std::string> failed_;
    ProgramStager stager_;
    bool staged_ = true;
    // Ids handed to the stager and not taken back yet.
    std::set<std::string> pending_;
    const UniformCache* uploadedFor_ = nullptr;
    const TransitionCatalog::Def* uploadedDef_ = nullptr;
};

}  // namespace geode::viz
