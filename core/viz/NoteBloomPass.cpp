#include "viz/NoteBloomPass.hpp"

#include <algorithm>
#include <cmath>

namespace geode::viz {

bool NoteBloomPass::create(const ShaderSource& assets, ProgramBinaryCache* cache, const std::string& fadeVert, std::string* error) {
    release();
    const auto frag = assets.load("note_bloom_frag.glsl", error);
    if (!frag) return false;
    program_ = program::build(fadeVert, *frag, cache, error);
    if (program_ == 0) return false;
    uniforms_ = UniformCache(program_);
    return true;
}

void NoteBloomPass::release() {
    if (program_ != 0) glDeleteProgram(program_);
    program_ = 0;
    uniforms_ = UniformCache(0);
}

void NoteBloomPass::draw(const NoteUniforms& notes, int width, int height, GLuint quadVao, float gain) {
    if (program_ == 0 || notes.bloomCount <= 0 || notes.amount <= 0.0f || width <= 0 || height <= 0) return;
    const int count = std::min(notes.bloomCount, NoteUniforms::kBlooms);
    // The blooms' bounding box, in window pixels (y up, as gl_FragCoord and the bloom positions are).
    const float shortSide = static_cast<float>(std::min(width, height));
    float x0 = static_cast<float>(width);
    float y0 = static_cast<float>(height);
    float x1 = 0.0f;
    float y1 = 0.0f;
    for (int i = 0; i < count; ++i) {
        const auto& a = notes.bloomA[i];
        // 1.25: a disc tilted in 3D (lib_bloom bloomUntilt) reaches at most that far past its radius.
        const float reach = 1.25f * a[2] * shortSide * std::max(notes.bloomC[i][1], 1.0f) + 2.0f;
        const float cx = a[0] * static_cast<float>(width);
        const float cy = a[1] * static_cast<float>(height);
        x0 = std::min(x0, cx - reach);
        y0 = std::min(y0, cy - reach);
        x1 = std::max(x1, cx + reach);
        y1 = std::max(y1, cy + reach);
    }
    const int sx = std::clamp(static_cast<int>(std::floor(x0)), 0, width);
    const int sy = std::clamp(static_cast<int>(std::floor(y0)), 0, height);
    const int ex = std::clamp(static_cast<int>(std::ceil(x1)), 0, width);
    const int ey = std::clamp(static_cast<int>(std::ceil(y1)), 0, height);
    if (ex <= sx || ey <= sy) return;

    glUseProgram(program_);
    glUniform2f(uniforms_.loc("uResolution"), static_cast<float>(width), static_cast<float>(height));
    glUniform1i(uniforms_.loc("uBloomCount"), count);
    glUniform4fv(uniforms_.loc("uBloomA"), uniforms_.arrayCount("uBloomA", NoteUniforms::kBlooms), notes.bloomA[0].data());
    glUniform4fv(uniforms_.loc("uBloomB"), uniforms_.arrayCount("uBloomB", NoteUniforms::kBlooms), notes.bloomB[0].data());
    glUniform4fv(uniforms_.loc("uBloomC"), uniforms_.arrayCount("uBloomC", NoteUniforms::kBlooms), notes.bloomC[0].data());
    glUniform4fv(uniforms_.loc("uBloomD"), uniforms_.arrayCount("uBloomD", NoteUniforms::kBlooms), notes.bloomD[0].data());
    glUniform1i(uniforms_.loc("uBloomLook"), notes.look);
    glUniform1fv(uniforms_.loc("uNoteHues"), uniforms_.arrayCount("uNoteHues", NoteUniforms::kHues), notes.hues.data());
    glUniform1f(uniforms_.loc("uBloomGain"), gain);

    glEnable(GL_SCISSOR_TEST);
    glScissor(sx, sy, ex - sx, ey - sy);
    glEnable(GL_BLEND);
    // Screen on colour; the destination's alpha stays as the composite left it.
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_COLOR, GL_ZERO, GL_ONE);
    glBindVertexArray(quadVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
}

}  // namespace geode::viz
