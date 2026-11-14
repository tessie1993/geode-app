#pragma once
#include <GLES3/gl3.h>

#include <array>
#include <memory>
#include <optional>
#include <string>

#include "viz/Program.hpp"
#include "viz/Scene.hpp"
#include "viz/fluid/FluidBuffers.hpp"
#include "viz/present/FieldGeometry.hpp"
#include "viz/scenes/ProgramLoader.hpp"
#include "viz/scenes/ShaderScene.hpp"

namespace geode::viz::present {

// What a state holds where. field_prep_frag reads three numbers per texel as weights over the state's
// RGBA: the height (shaped by a smoothstep between `low` and `high` into 0..1), a tint (a palette
// position the kind's present reads) and an age (old matter takes the second material). Any texture a
// kind steps can be the state: a solver's field, a dye, a field-space trail.
struct Channels {
    std::array<float, 4> height{1.0f, 0.0f, 0.0f, 0.0f};
    float low = 0.0f;
    float high = 1.0f;
    std::array<float, 4> tint{};
    std::array<float, 4> age{};
};

// One material for lib_field3d's shading, as numbers rather than a branch, so young and old matter blend
// by age instead of switching. a: diffuse, specular, shininess (a power, 1 and up), rim. b: reflection of
// the sky, emission of dense matter, contour lines, the film's sheen. Every term is lit in the palette's
// colours; none adds white or takes colour out.
struct Material {
    std::array<float, 4> a{};
    std::array<float, 4> b{};
};

// A simulation presented as a 3D surface on the camera rig (lib_field3d). Each frame the kind's state is
// written into a field texture (field_prep_frag: the height, tint and age, the long side along x, the
// drift remainder taken off, mipmapped so the march reads it at its footprint), then the kind's present
// shader draws it. The present is a ShaderScene, so it gets the whole shared block every style gets (the
// params, the palette, the knobs, the sound, the camera, uSteps from Detail) and
// reads it through the same libraries the newest families do; the field and its geometry come beside it
// (ShaderScene::Field). The simulation itself is the owner's: its step, its touch, its clock.
class FieldPresent {
public:
    FieldPresent() = default;
    ~FieldPresent() { release(); }
    FieldPresent(const FieldPresent&) = delete;
    FieldPresent& operator=(const FieldPresent&) = delete;

    // On the GL thread, in the owner's init(): the prep program now, the present's program in prepare()
    // or its first draw. False when the prep pass did not build; the host has the reason.
    bool init(const std::string& id, const ProgramLoader& loader, const SceneHost& host, const std::string& presentFrag);
    void prepare();
    bool buildFailed() const;
    void release();

    void setParams(const SceneParams& params);
    void setCamera(const CameraPose* pose);
    void setPaletteLut(GLuint texture);
    void resize(int width, int height);
    void update(const GeodeSoundFrame& sound, float dt);
    bool drawsCamera() const;

    // The style's look, held until changed.
    void setLook(Surface surface, float depth, const Material& young, const Material& old, float tone, float span);

    // Writes the field from `state` (stateW x stateH, with fracX, fracY texels of drift not yet in it) and
    // draws the present into the framebuffer and viewport bound on entry, which it leaves bound.
    void draw(GLuint state, int stateW, int stateH, float fracX, float fracY, const Channels& channels, fluid::TexFormat format);
    // Draws the present again from the field written last: a held frame whose eye moved.
    void redraw();

    // Where a finger at y-up NDC (x, y) lands on the state, as the state's own y-up NDC; nullopt when it
    // misses the surface. The same ray the present casts, with the shape controls at rest (FieldGeometry).
    std::optional<std::array<float, 2>> touchToState(float x, float y, int stateW, int stateH, float fracX, float fracY) const;

private:
    Geometry geometry(int stateW, int stateH) const;
    bool ensureField(int longSide, int shortSide, fluid::TexFormat format);

    std::unique_ptr<ShaderScene> present_;
    ShaderScene::Field field_;
    GLuint prepProgram_ = 0;
    UniformCache prepLocs_{0};
    GLuint vao_ = 0;
    std::optional<fluid::Fbo> fieldFbo_;
    GLenum fieldFormat_ = 0;
    const CameraPose* camera_ = nullptr;
    GLuint paletteLut_ = 0;
    int width_ = 1;
    int height_ = 1;
    Surface surface_ = Surface::Terrain;
    float depth_ = 1.0f;
};

}  // namespace geode::viz::present
