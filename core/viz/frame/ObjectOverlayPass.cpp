#include "viz/frame/ObjectOverlayPass.hpp"

#include "scene/Scene.hpp"

namespace geode::viz::frame {

bool ObjectOverlayPass::create(const ShaderSource& assets, ProgramBinaryCache* cache, const std::string& fadeVert,
                               std::string* error) {
    release();
    const auto frag = assets.load("scene_objects_frag.glsl", error);
    if (!frag) return false;
    program_ = program::build(fadeVert, *frag, cache, error);
    if (program_ == 0) return false;
    uniforms_ = UniformCache(program_);
    const GLuint block = glGetUniformBlockIndex(program_, "SceneObjects");
    if (block == GL_INVALID_INDEX) {
        if (error != nullptr) *error = "scene_objects_frag has no SceneObjects block";
        release();
        return false;
    }
    glUniformBlockBinding(program_, block, kBinding);
    // The whole block once, so each frame only rewrites the live shapes' bytes (glBufferSubData).
    glGenBuffers(1, &ubo_);
    glBindBuffer(GL_UNIFORM_BUFFER, ubo_);
    glBufferData(GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>(sizeof(frame_.shapes)), nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    // fade_vert makes its triangle from gl_VertexID; a VAO of its own, as EdgeGlowPass explains.
    glGenVertexArrays(1, &vao_);
    return true;
}

void ObjectOverlayPass::setFrame(const ::geode::scene::Scene* scene, const CameraPose* camera, GLuint paletteLut) {
    scene_ = scene;
    camera_ = camera;
    paletteLut_ = paletteLut;
}

bool ObjectOverlayPass::wants(const FrameCtx& ctx) const {
    // Every surface, export included: live and export must draw the same objects (#515).
    return scene_ != nullptr && camera_ != nullptr && scene_->objects().pool().live() > 0 && ctx.width > 0 &&
           ctx.height > 0;
}

void ObjectOverlayPass::prepare(const FrameCtx& /*ctx*/) {
    prepared_ = scene_ != nullptr && scene_->packOverlay(frame_) > 0;
}

void ObjectOverlayPass::draw(const PassInputs& /*inputs*/, const PassTarget& target) {
    // A driver that would not build the program shows the world alone.
    if (program_ == 0 || vao_ == 0 || ubo_ == 0 || !prepared_ || camera_ == nullptr || target.width <= 0 ||
        target.height <= 0) {
        return;
    }
    prepared_ = false;
    const auto& f = frame_;
    const CameraPose& c = *camera_;

    glBindBuffer(GL_UNIFORM_BUFFER, ubo_);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>(f.bytes()), f.data());
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    glViewport(0, 0, target.width, target.height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    // Premultiplied over the world's colour; the target's alpha stays what the world wrote.
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    glUseProgram(program_);
    glBindBufferBase(GL_UNIFORM_BUFFER, kBinding, ubo_);

    glUniform2f(uniforms_.loc("uResolution"), static_cast<float>(target.width), static_cast<float>(target.height));
    const float basis[9] = {c.right[0], c.right[1], c.right[2], c.up[0], c.up[1], c.up[2], c.fwd[0], c.fwd[1], c.fwd[2]};
    glUniform3fv(uniforms_.loc("uCamPos"), 1, c.pos.data());
    glUniformMatrix3fv(uniforms_.loc("uCamBasis"), 1, GL_FALSE, basis);
    glUniform1f(uniforms_.loc("uCamFocal"), c.focal);
    glUniform1i(uniforms_.loc("uObjectCount"), f.count);
    glUniform4f(uniforms_.loc("uObjLook"), f.look.hue, f.look.emission, f.look.gain, f.look.fog);
    glUniform4f(uniforms_.loc("uObjView"), f.look.roll, f.look.lens, 0.0f, 0.0f);

    const auto& pal = f.palette;
    glUniform1f(uniforms_.loc("uPalBase"), pal.base);
    glUniform1f(uniforms_.loc("uPalRange"), pal.range);
    glUniform1f(uniforms_.loc("uPal2Base"), pal.base2);
    glUniform1f(uniforms_.loc("uPal2Range"), pal.range2);
    glUniform1f(uniforms_.loc("uPaletteMix"), pal.mix);
    glUniform1f(uniforms_.loc("uColorShift"), pal.colorShift);
    glUniform1f(uniforms_.loc("uHueRange"), pal.hueRange);
    const bool lut = paletteLut_ != 0 && pal.lutMix > 0.0f;
    glUniform1f(uniforms_.loc("uPalLutMix"), lut ? pal.lutMix : 0.0f);
    glUniform1f(uniforms_.loc("uPalLutRow"), pal.lutRow);
    if (paletteLut_ != 0) {
        glActiveTexture(GL_TEXTURE0 + kLutUnit);
        glBindTexture(GL_TEXTURE_2D, paletteLut_);
        glUniform1i(uniforms_.loc("uPalLut"), static_cast<GLint>(kLutUnit));
        glActiveTexture(GL_TEXTURE0);
    }

    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glBindBufferBase(GL_UNIFORM_BUFFER, kBinding, 0);
    glDisable(GL_BLEND);
}

void ObjectOverlayPass::release() {
    if (program_ != 0) glDeleteProgram(program_);
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
    if (ubo_ != 0) glDeleteBuffers(1, &ubo_);
    program_ = 0;
    vao_ = 0;
    ubo_ = 0;
    uniforms_ = UniformCache(0);
    scene_ = nullptr;
    camera_ = nullptr;
    prepared_ = false;
}

}  // namespace geode::viz::frame
