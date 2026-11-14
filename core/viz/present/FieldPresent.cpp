#include "viz/present/FieldPresent.hpp"

#include <algorithm>
#include <cmath>

namespace geode::viz::present {

namespace {

void put4(float* out, const std::array<float, 4>& v) { std::copy(v.begin(), v.end(), out); }

}  // namespace

bool FieldPresent::init(const std::string& id, const ProgramLoader& loader, const SceneHost& host, const std::string& presentFrag) {
    release();
    std::string error;
    prepProgram_ = loader.build("quad_vert.glsl", "field_prep_frag.glsl", &error);
    if (prepProgram_ == 0) {
        if (host.onShaderError) host.onShaderError("The 3D present is unavailable on this GPU: " + error);
        return false;
    }
    prepLocs_ = UniformCache(prepProgram_);
    glGenVertexArrays(1, &vao_);
    const std::string vert = loader.source("quad_vert.glsl", &error);
    const std::string frag = loader.source(presentFrag, &error);
    // An empty source fails to compile in prepare(), which reports it; buildFailed() then says so.
    present_ = std::make_unique<ShaderScene>(id, vert, frag, loader.cache, host);
    present_->setCamera(camera_);
    present_->setPaletteLut(paletteLut_);
    present_->init();
    present_->resize(width_, height_);
    present_->setField(&field_);
    return true;
}

void FieldPresent::prepare() {
    if (present_) present_->prepare();
}

bool FieldPresent::buildFailed() const { return prepProgram_ == 0 || !present_ || present_->buildFailed(); }

void FieldPresent::release() {
    if (present_) present_->release();
    present_.reset();
    if (prepProgram_ != 0) glDeleteProgram(prepProgram_);
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
    if (fieldFbo_) fieldFbo_->release();
    fieldFbo_.reset();
    prepProgram_ = 0;
    prepLocs_ = UniformCache(0);
    vao_ = 0;
    fieldFormat_ = 0;
    field_.texture = 0;
}

void FieldPresent::setParams(const SceneParams& params) {
    if (present_) present_->setParams(params);
}

void FieldPresent::setCamera(const CameraPose* pose) {
    camera_ = pose;
    if (present_) present_->setCamera(pose);
}

void FieldPresent::setPaletteLut(GLuint texture) {
    paletteLut_ = texture;
    if (present_) present_->setPaletteLut(texture);
}

void FieldPresent::resize(int width, int height) {
    width_ = std::max(width, 1);
    height_ = std::max(height, 1);
    if (present_) present_->resize(width_, height_);
}

void FieldPresent::update(const GeodeSoundFrame& sound, float dt) {
    if (present_) present_->update(sound, dt);
}

bool FieldPresent::drawsCamera() const { return present_ && present_->drawsCamera(); }

void FieldPresent::setLook(Surface surface, float depth, const Material& young, const Material& old, float tone, float span) {
    surface_ = surface;
    depth_ = depth;
    float* look = field_.look.data();
    put4(look, young.a);
    put4(look + 4, young.b);
    put4(look + 8, old.a);
    put4(look + 12, old.b);
    look[16] = std::isfinite(tone) ? tone : 0.0f;
    look[17] = std::isfinite(span) ? span : 1.0f;
}

Geometry FieldPresent::geometry(int stateW, int stateH) const {
    return geometryFor(surface_, depth_, std::max(stateW, stateH), std::min(stateW, stateH),
                       static_cast<float>(width_) / static_cast<float>(height_));
}

bool FieldPresent::ensureField(int longSide, int shortSide, fluid::TexFormat format) {
    if (fieldFbo_ && fieldFbo_->ok() && fieldFbo_->width() == longSide && fieldFbo_->height() == shortSide &&
        fieldFormat_ == format.internal) {
        return true;
    }
    if (fieldFbo_) fieldFbo_->release();
    fieldFbo_.emplace(longSide, shortSide, format, true);
    fieldFbo_->create();
    if (!fieldFbo_->ok()) {
        fieldFbo_.reset();
        field_.texture = 0;
        return false;
    }
    fieldFormat_ = format.internal;
    // Wrapped both ways like every state here, and mipmapped: the march reads it at its footprint.
    glBindTexture(GL_TEXTURE_2D, fieldFbo_->tex());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void FieldPresent::draw(GLuint state, int stateW, int stateH, float fracX, float fracY, const Channels& channels,
                        fluid::TexFormat format) {
    if (prepProgram_ == 0 || !present_) return;
    const int longSide = std::max(std::max(stateW, stateH), 1);
    const int shortSide = std::max(std::min(stateW, stateH), 1);
    GLint prevFbo = 0;
    GLint prevViewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    if (!ensureField(longSide, shortSide, format)) {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
        return;
    }

    // The prep pass: the state into the field, once per frame at the state's own size.
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, fieldFbo_->fbo());
    glViewport(0, 0, longSide, shortSide);
    glUseProgram(prepProgram_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, state);
    glUniform1i(prepLocs_.loc("uState"), 0);
    glUniform1f(prepLocs_.loc("uSwap"), stateH > stateW ? 1.0f : 0.0f);
    glUniform2f(prepLocs_.loc("uFrac"), std::isfinite(fracX) ? fracX / static_cast<float>(std::max(stateW, 1)) : 0.0f,
                std::isfinite(fracY) ? fracY / static_cast<float>(std::max(stateH, 1)) : 0.0f);
    glUniform4fv(prepLocs_.loc("uHeightMask"), 1, channels.height.data());
    glUniform2f(prepLocs_.loc("uHeightRange"), channels.low, std::max(channels.high, channels.low + 1e-3f));
    glUniform4fv(prepLocs_.loc("uTintMask"), 1, channels.tint.data());
    glUniform4fv(prepLocs_.loc("uAgeMask"), 1, channels.age.data());
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, fieldFbo_->tex());
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);

    const Geometry g = geometry(stateW, stateH);
    field_.texture = fieldFbo_->tex();
    field_.size = {static_cast<float>(longSide), static_cast<float>(shortSide)};
    field_.geom = {static_cast<float>(static_cast<int>(g.surface)), g.eye, g.radius, g.tube};
    field_.shape = {g.tileS, g.tileT, g.relief, g.top};

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    present_->draw(0.0f);
}

void FieldPresent::redraw() {
    if (!present_ || field_.texture == 0) return;
    present_->draw(0.0f);
}

std::optional<std::array<float, 2>> FieldPresent::touchToState(float x, float y, int stateW, int stateH, float fracX,
                                                               float fracY) const {
    if (!camera_) return std::nullopt;
    const Geometry g = geometry(stateW, stateH);
    const float aspect = static_cast<float>(width_) / static_cast<float>(height_);
    // view() spans 1 up and the aspect across, the touch field 1 up and 1 across.
    const auto hit = fieldPoint(g, eyeRay(g, *camera_, x * aspect, y));
    if (!hit) return std::nullopt;
    const auto uv = stateUv((*hit)[0], (*hit)[1], stateW, stateH, fracX, fracY);
    return std::array<float, 2>{uv[0] * 2.0f - 1.0f, uv[1] * 2.0f - 1.0f};
}

}  // namespace geode::viz::present
