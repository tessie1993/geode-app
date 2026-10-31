#pragma once
#include <GLES3/gl3.h>

#include <array>

#include "api/geode_api.h"
#include "viz/Program.hpp"
#include "viz/Reactions.hpp"
#include "viz/Texture.hpp"

namespace geode::viz {

// The sound engine's frame as the uniforms lib_sound.glsl declares: uSound (every scalar), uSoundTex
// (spectrum, waveform and EQ rows), uSoundDrive, and the held events and counts (uSoundEvents,
// uSoundCounts).
//
// It lived inside ShaderScene until the simulation scenes (Silk, Fluid, Life, Acid, Cymatics) wanted
// the same frame in their own passes, for lib_relief's tilt and light. One owner of the upload keeps
// every scene reading the frame the same way, so a style and a simulation pass that both say
// SND_KICK_ENV see the same number on the same frame.
//
// Each scene owns its own instance, like its MotionField: the ReactSignals stepped here are a pure
// function of the frames step() receives, so live and export (which steps the same frame sequence
// offscreen) hold the same events without a channel back to the renderer's own copy.
class SoundUniforms {
public:
    // uSoundTex: row 0 spectrum, row 1 waveform, row 2 EQ, each resampled to this width (lib_sound).
    static constexpr int kTexWidth = 128;
    static constexpr int kTexRows = 3;
    // GEODE_SOUND_SCALARS rounded up to whole vec4s: uSound[kVec4s].
    static constexpr int kVec4s = (GEODE_SOUND_SCALARS + 3) / 4;

    // GL thread: creates uSoundTex (R32F, kTexWidth x kTexRows), linear where the driver can filter floats.
    void init();
    void release();
    // Keeps the frame for upload() and steps the held events from it. Call once per update, not per draw.
    void step(const GeodeSoundFrame& frame, float dt);
    // Uploads the kept frame to the program `locs` belongs to, which must be bound (glUseProgram).
    // `drive` is the style's Audio drive slider (SceneParams.audioDrive). The texture goes on `unit`;
    // the active texture unit is left at 0. Names the program never reads cost one dropped call each.
    void upload(UniformCache& locs, float drive, int unit = 3);

    const GeodeSoundFrame& frame() const { return frame_; }
    const ReactSignals& signals() const { return signals_; }

private:
    GeodeSoundFrame frame_{};
    ReactSignals signals_;
    Texture tex_;
    std::array<float, kVec4s * 4> scalars_{};
    std::array<float, kTexWidth * kTexRows> texData_{};
};

}  // namespace geode::viz
