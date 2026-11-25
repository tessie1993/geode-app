#include "viz/glsl/SoundUniforms.hpp"

#include <algorithm>
#include <cstring>

namespace geode::viz {

namespace {

// Linear resample of `src` (n points) across `width` texels, so every row of uSoundTex reads 0..1 in x
// whatever the engine's point count.
void resampleRow(const float* src, int n, float* dst, int width) {
    for (int i = 0; i < width; ++i) {
        const float x = width > 1 ? static_cast<float>(i) * static_cast<float>(n - 1) / static_cast<float>(width - 1) : 0.0f;
        const int a = std::min(static_cast<int>(x), n - 1);
        const int b = std::min(a + 1, n - 1);
        const float t = x - static_cast<float>(a);
        dst[i] = src[a] + (src[b] - src[a]) * t;
    }
}

}  // namespace

void SoundUniforms::init() {
    const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    const bool floatLinear = extensions && std::strstr(extensions, "OES_texture_float_linear") != nullptr;
    tex_.createImage(GL_R32F, GL_RED, GL_FLOAT, kTexWidth, kTexRows, floatLinear ? GL_LINEAR : GL_NEAREST, GL_CLAMP_TO_EDGE);
}

void SoundUniforms::release() { tex_.release(); }

void SoundUniforms::step(const GeodeSoundFrame& frame, float dt) {
    frame_ = frame;
    signals_.step(frame_, dt);
}

// The whole GeodeSoundFrame, raw: scalars as uSound (lib_sound names them), the arrays as uSoundTex.
// No drive, clamp or slew here; the engine's values are already dt-scaled and bounded, and styles
// shape them themselves.
void SoundUniforms::upload(UniformCache& locs, float drive, int unit) {
    // A name the program never reads has location -1: its work (the copy, the resample, the texture upload) is skipped.
    const GLint soundLoc = locs.loc(Uniform::uSound);
    if (soundLoc >= 0) {
        const float* scalars = reinterpret_cast<const float*>(&frame_);
        std::copy(scalars, scalars + GEODE_SOUND_SCALARS, scalars_.begin());
        glUniform4fv(soundLoc, kVec4s, scalars_.data());
    }
    const GLint texLoc = locs.loc(Uniform::uSoundTex);
    if (texLoc >= 0) {
        resampleRow(frame_.spectrum, GEODE_SOUND_SPECTRUM_POINTS, texData_.data(), kTexWidth);
        resampleRow(frame_.waveform, GEODE_SOUND_WAVEFORM_POINTS, texData_.data() + kTexWidth, kTexWidth);
        resampleRow(frame_.eq, GEODE_SOUND_EQ_BANDS, texData_.data() + 2 * kTexWidth, kTexWidth);
        for (int i = 0; i < kTexWidth; ++i) texData_[3 * kTexWidth + i] = frame_.chroma[i * GEODE_SOUND_CHROMA / kTexWidth];
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, tex_.id());
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kTexWidth, kTexRows, GL_RED, GL_FLOAT, texData_.data());
        locs.set1i(Uniform::uSoundTex, unit);
        glActiveTexture(GL_TEXTURE0);
    }
    locs.set1f(Uniform::uSoundDrive, std::max(drive, 0.0f));
    const ReactSignals& r = signals_;
    locs.set4f(Uniform::uSoundEvents, r.value(ReactSource::HardCut), r.value(ReactSource::Surge), r.value(ReactSource::Returned),
               r.value(ReactSource::NoteChange));
    locs.set2f(Uniform::uSoundCounts, r.onsetCount(), r.cutCount());
}

}  // namespace geode::viz
