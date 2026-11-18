#include "viz/Texture.hpp"

namespace geode::viz {

void Texture::allocate(int width, int height, GLint filter, GLint wrap) {
    release();
    id_.reset(gl_gen::texture());
    glBindTexture(GL_TEXTURE_2D, id_.get());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    width_ = width;
    height_ = height;
}

void Texture::createImage(GLenum internalFormat, GLenum format, GLenum type, int width, int height, GLint filter, GLint wrap) {
    allocate(width, height, filter, wrap);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internalFormat), width, height, 0, format, type, nullptr);
}

void Texture::bind(int unit) const {
    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
    glBindTexture(GL_TEXTURE_2D, id_.get());
}

void Texture::upload(GLenum format, GLenum type, const void* pixels, int width, int height, int x, int y) const {
    glBindTexture(GL_TEXTURE_2D, id_.get());
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, format, type, pixels);
}

void Texture::release() {
    id_.reset();
    width_ = 0;
    height_ = 0;
}

void Texture::forget() {
    id_.forget();
    width_ = 0;
    height_ = 0;
}

}  // namespace geode::viz
