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
#include "viz/ShaderSource.hpp"
#include "viz/TransitionLibrary.hpp"
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
class TransitionPrograms {
public:
    TransitionPrograms(const ShaderSource& assets, ProgramBinaryCache* cache) : catalog_(assets), assets_(assets), cache_(cache) {}
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
    UniformCache& programFor(const std::string& id);
    void warm(const std::string& id);
    void uploadParamsIfNeeded(UniformCache& program, const TransitionCatalog::Def* def);

private:
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
    const UniformCache* uploadedFor_ = nullptr;
    const TransitionCatalog::Def* uploadedDef_ = nullptr;
};

}  // namespace geode::viz
