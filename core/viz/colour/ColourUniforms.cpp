#include "viz/colour/ColourUniforms.hpp"

namespace geode::viz::colour {

void upload(UniformCache& locs, const ColourFrame* frame) {
    if (frame == nullptr || !frame->active) {
        glUniform1f(locs.loc("uColourOn"), 0.0f);
        return;
    }
    const ColourFrame& f = *frame;
    glUniform1f(locs.loc("uColourOn"), 1.0f);
    glUniform3fv(locs.loc("uColourRamp"), ColourFrame::kRamp, f.ramp[0].data());
    glUniform1f(locs.loc("uColourAway"), f.away);
    glUniform1f(locs.loc("uColourDrift"), f.drift);
    // Row-major on the CPU, so transpose: GLSL's uColourHue * c then reads rows, as the CSS matrices do.
    glUniformMatrix3fv(locs.loc("uColourHue"), 1, GL_TRUE, f.hue.data());
    glUniformMatrix3fv(locs.loc("uColourClash"), 1, GL_TRUE, f.clash.data());
    glUniform1f(locs.loc("uColourClashAmt"), f.clashAmount);
    glUniform4fv(locs.loc("uColourHighlight"), 1, f.highlight.data());
    glUniform3f(locs.loc("uColourGrade"), f.shade, f.contrast, f.saturation);
}

}  // namespace geode::viz::colour
