#pragma once
#include <GLES3/gl3.h>

namespace geode::viz {

class Texture {
public:
    Texture() = default;
    ~Texture() { release(); }
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    GLuint id() const { return id_; }
    int width() const { return width_; }
    int height() const { return height_; }
    bool ok() const { return id_ != 0; }

    // Immutable storage via glTexStorage2D with the given filter and clamp-to-edge wrap.
    void createStorage(GLenum internalFormat, int width, int height, GLint filter);
    // Mutable storage via glTexImage2D, for the RGBA8/R32F uploads the Kotlin scenes make.
    void createImage(GLenum internalFormat, GLenum format, GLenum type, int width, int height, GLint filter, GLint wrap);
    void bind(int unit) const;
    void upload(GLenum format, GLenum type, const void* pixels, int width, int height, int x = 0, int y = 0) const;
    void release();
    void forget();

private:
    void allocate(int width, int height, GLint filter, GLint wrap);

    GLuint id_ = 0;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace geode::viz
