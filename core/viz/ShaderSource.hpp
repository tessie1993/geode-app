#pragma once
#include <android/asset_manager.h>

#include <optional>
#include <string>

namespace geode::viz {

class ShaderSource {
public:
    explicit ShaderSource(AAssetManager* assets) : assets_(assets) {}

    // Reads shaders/<name> from the APK and resolves its //#include lines; nullopt when the asset is missing.
    std::optional<std::string> load(const std::string& name, std::string* error) const;
    std::optional<std::string> readAsset(const std::string& path) const;
    std::optional<std::string> resolveIncludes(const std::string& source, std::string* error) const;

private:
    static bool isIncludeName(const std::string& name);

    AAssetManager* assets_;
};

}  // namespace geode::viz
