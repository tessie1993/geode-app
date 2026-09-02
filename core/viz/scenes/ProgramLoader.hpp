#pragma once
#include <GLES3/gl3.h>

#include <string>

#include "viz/Program.hpp"
#include "viz/ProgramBinaryCache.hpp"
#include "viz/ShaderSource.hpp"

namespace geode::viz {

// What a scene needs to turn asset names into linked programs: the APK reader and the binary cache.
struct ProgramLoader {
    const ShaderSource& assets;
    ProgramBinaryCache* cache;

    std::string source(const std::string& name, std::string* error) const {
        const auto text = assets.load(name, error);
        return text ? *text : std::string();
    }

    // 0 on failure with the reason in `error`; either name may be empty when its source failed to load.
    GLuint build(const std::string& vertName, const std::string& fragName, std::string* error) const {
        const std::string vert = source(vertName, error);
        if (vert.empty()) return 0;
        const std::string frag = source(fragName, error);
        if (frag.empty()) return 0;
        return program::build(vert, frag, cache, error);
    }

    GLuint buildSource(const std::string& vertSrc, const std::string& fragSrc, std::string* error) const {
        if (vertSrc.empty() || fragSrc.empty()) return 0;
        return program::build(vertSrc, fragSrc, cache, error);
    }
};

}  // namespace geode::viz
