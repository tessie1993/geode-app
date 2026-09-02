#pragma once
#include <GLES3/gl3.h>

#include "viz/ShaderSource.hpp"

namespace geode::viz::blue_noise {

constexpr int kSize = 64;
constexpr float kDitherAmount = 1.0f / 255.0f;

// Port of BlueNoise.createTexture: the 64x64 R8 mask from shaders/blue_noise_64.bin; 0 when absent.
GLuint createTexture(const ShaderSource& assets);

}  // namespace geode::viz::blue_noise
