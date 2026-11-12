#include "viz/android/AssetStarterSource.hpp"

namespace geode::viz::milk {

std::vector<std::string> AssetStarterSource::names() {
    std::vector<std::string> out;
    if (!assets_) return out;
    AAssetDir* dir = AAssetManager_openDir(assets_, kStarterAssetDir);
    if (!dir) return out;
    while (const char* name = AAssetDir_getNextFileName(dir)) out.emplace_back(name);
    AAssetDir_close(dir);
    return out;
}

bool AssetStarterSource::read(const std::string& name, std::string& bytes) {
    if (!assets_) return false;
    const std::string path = std::string(kStarterAssetDir) + "/" + name;
    AAsset* asset = AAssetManager_open(assets_, path.c_str(), AASSET_MODE_STREAMING);
    if (!asset) return false;
    bytes.clear();
    char buffer[16384];
    bool ok = true;
    for (;;) {
        const int n = AAsset_read(asset, buffer, sizeof buffer);
        if (n < 0) ok = false;
        if (n <= 0) break;
        bytes.append(buffer, static_cast<size_t>(n));
    }
    AAsset_close(asset);
    return ok;
}

}  // namespace geode::viz::milk
