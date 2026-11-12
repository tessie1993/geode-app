#pragma once

#include <android/asset_manager.h>

#include <string>
#include <vector>

#include "viz/MilkStarterPack.hpp"

namespace geode::viz::milk {

// The starter pack as the APK bundles it, under kStarterAssetDir. Only reads; every rule is MilkStarterPack's.
class AssetStarterSource final : public StarterSource {
public:
    explicit AssetStarterSource(AAssetManager* assets) : assets_(assets) {}
    std::vector<std::string> names() override;
    bool read(const std::string& name, std::string& bytes) override;

private:
    AAssetManager* assets_;
};

}  // namespace geode::viz::milk
