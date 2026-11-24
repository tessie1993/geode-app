#pragma once
#include <filesystem>
#include <string>
#include <system_error>

namespace geode::test {

// The APK's flat shaders/ folder is merged from core/viz/styles/glsl/<group>/shaders (app/build.gradle.kts).
// The file behind an asset name such as "shaders/lib_sound.glsl", searched through every group under
// `glslRoot` (ending in a slash); a name no group holds comes back as root + name, so the open fails.
inline std::string shaderAssetPath(const std::string& glslRoot, const std::string& asset) {
    namespace fs = std::filesystem;
    std::error_code ec;
    for (const auto& group : fs::directory_iterator(glslRoot, ec)) {
        const fs::path file = group.path() / asset;
        if (fs::is_regular_file(file, ec)) return file.string();
    }
    return glslRoot + asset;
}

}  // namespace geode::test
