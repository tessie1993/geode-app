#include "viz/compute/SimPass.hpp"

#include <algorithm>
#include <array>

#include "util/Log.hpp"

namespace geode::viz::sim {

namespace {

constexpr const char* kTag = "SimPass";

class BaseSimPass : public SimPass {
public:
    BaseSimPass(std::string label, StateEncoding encoding, std::unique_ptr<SimField> state)
        : label_(std::move(label)), encoding_(encoding), state_(std::move(state)) {}

    const std::string& label() const override { return label_; }
    int width() const override { return state_->width(); }
    int height() const override { return state_->height(); }
    GLuint stateTexture() const override { return state_->readTexture(); }
    void resize(int width, int height) override {
        requestedWidth_ = width;
        requestedHeight_ = height;
    }
    void clear() override { state_->clear(); }
    std::string displayShader(const std::string& body) const override { return glsl::displayShader(encoding_, body); }
    void bindStateFor(UniformCache& display, int unit) override {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_2D, state_->readTexture());
        glUniform1i(display.loc(glsl::kUniformState), unit);
        glUniform2i(display.loc(glsl::kUniformSize), state_->width(), state_->height());
    }
    void release() override { state_->release(); }
    void forget() override { state_->forget(); }

protected:
    bool ensureStorage() { return state_->ensure(requestedWidth_, requestedHeight_); }

    std::string label_;
    StateEncoding encoding_;
    std::unique_ptr<SimField> state_;

private:
    int requestedWidth_ = 0;
    int requestedHeight_ = 0;
};

class ComputeSimPass : public BaseSimPass {
public:
    ComputeSimPass(std::string label, StateEncoding encoding, std::unique_ptr<SimField> state, ComputePass pass, WorkGroupCount maxGroups)
        : BaseSimPass(std::move(label), encoding, std::move(state)),
          pass_(std::move(pass)),
          maxGroups_(maxGroups),
          uniforms_(pass_.programName(), [this](int unit, GLuint texture) { pass_.texture(unit, texture); }) {}

    const char* pathLabel() const override { return "compute"; }

    bool step(const SimUniformBinder& binder) override {
        if (!ensureStorage() || !ensureGroups()) return false;
        pass_.begin();
        pass_.texture(glsl::kStateTextureUnit, state_->readTexture());
        pass_.image(glsl::kStateImageUnit, state_->writeTexture(), encoding_.format, ImageAccess::Write);
        uniforms_.i1(glsl::kUniformState, glsl::kStateTextureUnit);
        uniforms_.i2(glsl::kUniformSize, state_->width(), state_->height());
        binder(uniforms_);
        pass_.dispatch(groups_);
        pass_.end();
        state_->swap();
        return true;
    }

    void release() override {
        pass_.release();
        BaseSimPass::release();
    }

    void forget() override {
        pass_.forget();
        BaseSimPass::forget();
    }

private:
    bool ensureGroups() {
        if (state_->width() == groupsWidth_ && state_->height() == groupsHeight_) return groups_.x > 0;
        groups_ = pass_.localSize().groupsFor(state_->width(), state_->height());
        groupsWidth_ = state_->width();
        groupsHeight_ = state_->height();
        if (groups_.x > maxGroups_.x || groups_.y > maxGroups_.y || groups_.z > maxGroups_.z) {
            GEODE_LOGW(kTag, "%s: %dx%d needs %dx%d work groups, over this device's %dx%d limit", label_.c_str(), state_->width(),
                       state_->height(), groups_.x, groups_.y, maxGroups_.x, maxGroups_.y);
            groups_ = {0, 0, 0};
            return false;
        }
        return true;
    }

    ComputePass pass_;
    WorkGroupCount maxGroups_;
    SimUniforms uniforms_;
    WorkGroupCount groups_{0, 0, 0};
    int groupsWidth_ = -1;
    int groupsHeight_ = -1;
};

class FragmentSimPass : public BaseSimPass {
public:
    FragmentSimPass(std::string label, StateEncoding encoding, std::unique_ptr<SimField> state, GLuint program)
        : BaseSimPass(std::move(label), encoding, std::move(state)),
          program_(program),
          uniforms_(program, [this](int unit, GLuint texture) {
              glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
              glBindTexture(GL_TEXTURE_2D, texture);
              if (unit < kTrackedUnits) sceneTextureUnits_ |= (1u << unit);
          }) {}

    const char* pathLabel() const override { return "fragment"; }

    bool step(const SimUniformBinder& binder) override {
        if (!ensureStorage()) return false;
        if (vao_ == 0) glGenVertexArrays(1, &vao_);
        GLint previousFramebuffer = 0;
        GLint previousViewport[4] = {0, 0, 0, 0};
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousFramebuffer);
        glGetIntegerv(GL_VIEWPORT, previousViewport);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, state_->writeFramebuffer());
        glViewport(0, 0, state_->width(), state_->height());
        const GLenum attachments[1] = {GL_COLOR_ATTACHMENT0};
        glInvalidateFramebuffer(GL_DRAW_FRAMEBUFFER, 1, attachments);
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glUseProgram(program_);
        glActiveTexture(GL_TEXTURE0 + glsl::kStateTextureUnit);
        glBindTexture(GL_TEXTURE_2D, state_->readTexture());
        uniforms_.i1(glsl::kUniformState, glsl::kStateTextureUnit);
        uniforms_.i2(glsl::kUniformSize, state_->width(), state_->height());
        binder(uniforms_);
        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        unbindTextures();
        glUseProgram(0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
        glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
        state_->swap();
        return true;
    }

    void release() override {
        if (program_ != 0) glDeleteProgram(program_);
        if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
        program_ = 0;
        vao_ = 0;
        BaseSimPass::release();
    }

    void forget() override {
        program_ = 0;
        vao_ = 0;
        BaseSimPass::forget();
    }

private:
    static constexpr int kTrackedUnits = 32;

    void unbindTextures() {
        glActiveTexture(GL_TEXTURE0 + glsl::kStateTextureUnit);
        glBindTexture(GL_TEXTURE_2D, 0);
        for (int unit = 0; unit < kTrackedUnits; ++unit) {
            if ((sceneTextureUnits_ & (1u << unit)) == 0) continue;
            glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        sceneTextureUnits_ = 0;
        glActiveTexture(GL_TEXTURE0);
    }

    GLuint program_;
    SimUniforms uniforms_;
    uint32_t sceneTextureUnits_ = 0;
    GLuint vao_ = 0;
};

std::optional<ComputePass> computePass(const SimSpec& spec, const StateEncoding& encoding, const ComputeSupport& support,
                                       const std::function<void(const std::string&)>& onDiagnostic) {
    const WorkGroupSize localSize = workGroupSizeForGrid(support.limits, spec.preferredInvocations);
    const std::string source = glsl::computeStep(encoding, localSize, spec.stepBody);
    std::string error;
    auto program = ComputeProgram::build(spec.label, source, localSize, &error);
    if (!program) {
        onDiagnostic(spec.label + ": compute step did not build, using fragment ping-pong — " + error);
        return std::nullopt;
    }
    GEODE_LOGI(kTag, "%s: compute step at local size %s", spec.label.c_str(), localSize.toString().c_str());
    // The ping-pong makes each texture an image store target and a sampled source in turn, so both barrier edges are needed.
    return ComputePass(spec.label, std::move(*program), spec.resultReadBy | static_cast<GLbitfield>(ComputeReader::ImageLoadStore));
}

}  // namespace

void SimUniforms::sampler(const char* name, GLuint texture) {
    auto it = units_.find(name);
    if (it == units_.end()) it = units_.emplace(name, glsl::kFirstSceneTextureUnit + static_cast<int>(units_.size())).first;
    bindTexture_(it->second, texture);
    glUniform1i(cache_.loc(name), it->second);
}

void SimUniforms::f4Array(const char* name, const float* values, int count, int declared) {
    glUniform4fv(cache_.loc(name), std::min(count, cache_.arrayCount(name, declared)), values);
}

SimPass::Build SimPass::build(const SimSpec& spec, const GlProfile& gl, ProgramBinaryCache* cache,
                              const std::function<void(const std::string&)>& onDiagnostic) {
    const ResolvedFormat& resolved = spec.sampling == SimSampling::WholeTexels ? gl.formats.simulationState : gl.formats.advectedField;
    const auto format = imageFormatOf(resolved.format);
    if (!format) {
        return Failed{spec.label + ": the format policy resolved state to " + probedFormatName(resolved.format) +
                      ", which has no GLSL descriptor here (" + resolved.because + ")"};
    }
    StateEncoding encoding;
    encoding.format = *format;
    encoding.packed = resolved.encoding == TexelEncoding::FloatBitsInUint;
    // An integer texture can never be filtered; the packed encoding interpolates by hand instead.
    encoding.filterable = resolved.filterable && !imageFormatInfo(*format).integerTexels;
    encoding.stateScale = resolved.encoding == TexelEncoding::PreScaled ? spec.stateScale : 1.0f;
    auto state = std::make_unique<SimField>(spec.label, *format, encoding.filterable);

    const ComputeSupport support = ComputeSupport::query(gl);
    if (!support.available) {
        onDiagnostic(spec.label + ": fragment ping-pong — " + support.because);
    } else if (auto pass = computePass(spec, encoding, support, onDiagnostic)) {
        onDiagnostic(spec.label + ": compute dispatch — " + support.because);
        return std::unique_ptr<SimPass>(new ComputeSimPass(spec.label, encoding, std::move(state), std::move(*pass), support.limits.maxGroupCount));
    }
    std::string error;
    const GLuint program = program::build(glsl::fullscreenVertex(), glsl::fragmentStep(encoding, spec.stepBody), cache, &error);
    if (program == 0) {
        state->release();
        return Failed{spec.label + ": fragment step did not build — " + error};
    }
    return std::unique_ptr<SimPass>(new FragmentSimPass(spec.label, encoding, std::move(state), program));
}

}  // namespace geode::viz::sim
