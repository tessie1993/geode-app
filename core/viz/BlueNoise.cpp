#include "viz/BlueNoise.hpp"

namespace geode::viz::blue_noise {

GLuint createTexture(const ShaderSource& assets) {
    const auto bytes = assets.readAsset("shaders/blue_noise_64.bin");
    if (!bytes || bytes->size() < static_cast<size_t>(kSize * kSize)) return 0;
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kSize, kSize, 0, GL_RED, GL_UNSIGNED_BYTE, bytes->data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    return tex;
}

}  // namespace geode::viz::blue_noise
