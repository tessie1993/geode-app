#pragma once
#include <GLES3/gl3.h>

#include <list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "viz/Program.hpp"
#include "viz/ShaderSource.hpp"
#include "viz/VisualSafety.hpp"

namespace geode::viz {

// Port of TransitionCatalog.kt: the built-in styles plus the GL Transitions library asset.
class TransitionCatalog {
public:
    static constexpr int kStyleLibrary = 5;

    struct Param {
        std::string name;
        std::string type;
        std::vector<float> values;
    };

    struct Def {
        std::string name;
        std::string author;
        std::string license;
        std::vector<Param> params;
        std::string glsl;
    };

    explicit TransitionCatalog(const ShaderSource& assets) : assets_(assets) {}

    const std::vector<Def>& library();
    std::vector<std::string> allIds();
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

// Port of TransitionPrograms.kt: the base composite program plus an LRU of four spliced variants.
class TransitionPrograms {
public:
    TransitionPrograms(const ShaderSource& assets, ProgramBinaryCache* cache) : catalog_(assets), assets_(assets), cache_(cache) {}
    ~TransitionPrograms() { release(); }

    // Compiles the base composite program; returns false with the driver log in `error`.
    bool create(const std::string& fadeVert, std::string* error);
    void release();
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
    UniformCache base_;
    std::string source_;
    std::string fadeVert_;
    std::list<std::pair<std::string, std::unique_ptr<UniformCache>>> lru_;
    const UniformCache* uploadedFor_ = nullptr;
    const TransitionCatalog::Def* uploadedDef_ = nullptr;
};

}  // namespace geode::viz
