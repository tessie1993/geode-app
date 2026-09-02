#pragma once
#include <string>

#include "viz/Compute.hpp"

namespace geode::viz::sim {

// Port of SimStateEncoding: how the state texels are stored and read back.
struct StateEncoding {
    GlImageFormat format = GlImageFormat::RGBA16F;
    bool packed = false;
    bool filterable = true;
    float stateScale = 1.0f;
};

// Port of SimGlsl.kt: the shared preamble and the three program shapes built around a step body.
namespace glsl {
constexpr int kStateTextureUnit = 0;
constexpr int kStateImageUnit = 0;
constexpr int kFirstSceneTextureUnit = 1;
constexpr const char* kUniformState = "uSimState";
constexpr const char* kUniformSize = "uSimSize";

const char* fullscreenVertex();
std::string fragmentStep(const StateEncoding& encoding, const std::string& body);
std::string computeStep(const StateEncoding& encoding, const WorkGroupSize& localSize, const std::string& body);
std::string displayShader(const StateEncoding& encoding, const std::string& body);
}  // namespace glsl

}  // namespace geode::viz::sim
