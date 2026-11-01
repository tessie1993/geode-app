#pragma once
#include <GLES3/gl3.h>

#include <string>

#include "viz/NoteResponse.hpp"
#include "viz/Program.hpp"
#include "viz/ShaderSource.hpp"

namespace geode::viz {

// The note blooms (NoteResponse's four looks, note_bloom_frag + lib_bloom) over the finished frame.
//
// Drawn by Renderer::composite after the composite pass, last, straight into the
// target framebuffer: so it lays over every family (shader styles, MilkDrop, the fluid and simulation
// scenes, layers and transitions) the same way, and the offscreen export, which runs the same render(),
// draws the same flowers. It never enters the post chain: a style's grade or a transition does not bend
// it.
//
// Cost: nothing at all while there is no live bloom (the layer off, silence, drums with no melody). With
// blooms it is one fullscreen-triangle draw scissored to the blooms' bounding box, in which each pixel
// tests every bloom's disc before any work; see note_bloom_frag.glsl for the per-pixel figure.
//
// Blend: screen (ONE, ONE_MINUS_SRC_COLOR) on colour, destination alpha kept. Screen only lightens and
// never passes white, so the blooms cannot clip a bright style, and with the shader's 0.8 cap and
// NoteResponse's soft 160 ms rise and three-a-second budget they stay inside VisualSafety's flash rules.
class NoteBloomPass {
public:
    // The blooms' brightness against the screen blend: the flowers stay glass, never paint.
    static constexpr float kGain = 0.9f;

    ~NoteBloomPass() { release(); }
    bool create(const ShaderSource& assets, ProgramBinaryCache* cache, const std::string& fadeVert, std::string* error);
    void release();
    // After CompositePass::draw, into the bound target of width × height pixels.
    void draw(const NoteUniforms& notes, int width, int height, GLuint quadVao);

private:
    GLuint program_ = 0;
    UniformCache uniforms_;
};

}  // namespace geode::viz
