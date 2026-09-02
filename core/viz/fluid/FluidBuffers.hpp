#pragma once
#include <GLES3/gl3.h>

#include <utility>

namespace geode::viz::fluid {

struct TexFormat {
    GLenum internal;
    GLenum format;
    GLenum type;
};

constexpr TexFormat kRgba8{GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};

// Port of FluidBuffers.Formats: the half-float roles this GPU proved renderable, with fallbacks.
struct Formats {
    TexFormat r;
    TexFormat rg;
    TexFormat rgba;
    TexFormat rgba32;
    bool hasRgba32 = false;
    bool ok = false;
};

Formats probeFormats();
std::pair<int, int> resolution(int res, int width, int height);

// Port of FluidBuffers.Fbo: one texture behind one framebuffer, cleared to opaque black on creation.
class Fbo {
public:
    Fbo() = default;
    Fbo(int width, int height, TexFormat fmt, bool linear) : width_(width), height_(height), fmt_(fmt), linear_(linear) {}
    ~Fbo() { release(); }
    Fbo(const Fbo&) = delete;
    Fbo& operator=(const Fbo&) = delete;
    Fbo(Fbo&& o) noexcept { *this = std::move(o); }
    Fbo& operator=(Fbo&& o) noexcept;

    int width() const { return width_; }
    int height() const { return height_; }
    GLuint fbo() const { return fbo_; }
    GLuint tex() const { return tex_; }
    bool ok() const { return fbo_ != 0 && tex_ != 0; }

    void create();
    void discardContents() const;
    void release();

private:
    int width_ = 0;
    int height_ = 0;
    TexFormat fmt_ = kRgba8;
    bool linear_ = false;
    GLuint fbo_ = 0;
    GLuint tex_ = 0;
};

// Port of FluidBuffers.DoubleFbo: a read/write pair swapped after every step.
class DoubleFbo {
public:
    DoubleFbo() = default;
    DoubleFbo(int width, int height, TexFormat fmt, bool linear) : read_(width, height, fmt, linear), write_(width, height, fmt, linear) {}

    Fbo& read() { return *readPtr(); }
    Fbo& write() { return *writePtr(); }
    const Fbo& read() const { return *readPtr(); }
    int width() const { return read_.width(); }
    int height() const { return read_.height(); }
    bool ok() const { return read_.ok() && write_.ok(); }

    void create() {
        read_.create();
        write_.create();
    }
    void swap() { flipped_ = !flipped_; }
    void release() {
        read_.release();
        write_.release();
    }

private:
    Fbo* readPtr() { return flipped_ ? &write_ : &read_; }
    Fbo* writePtr() { return flipped_ ? &read_ : &write_; }
    const Fbo* readPtr() const { return flipped_ ? &write_ : &read_; }

    Fbo read_;
    Fbo write_;
    bool flipped_ = false;
};

// Port of FluidBuffers.DoubleMrt: two colour attachments per side, for particle position + metadata.
class DoubleMrt {
public:
    class Side {
    public:
        Side() = default;
        Side(int width, int height, TexFormat fmtA, TexFormat fmtB) : width_(width), height_(height), fmtA_(fmtA), fmtB_(fmtB) {}
        ~Side() { release(); }
        Side(const Side&) = delete;
        Side& operator=(const Side&) = delete;

        GLuint fbo() const { return fbo_; }
        GLuint texA() const { return texA_; }
        GLuint texB() const { return texB_; }
        bool ok() const { return fbo_ != 0 && texA_ != 0 && texB_ != 0; }

        void create();
        void discardContents() const;
        void release();

    private:
        static GLuint makeTex(int w, int h, TexFormat fmt);

        int width_ = 0;
        int height_ = 0;
        TexFormat fmtA_ = kRgba8;
        TexFormat fmtB_ = kRgba8;
        GLuint fbo_ = 0;
        GLuint texA_ = 0;
        GLuint texB_ = 0;
    };

    DoubleMrt() = default;
    DoubleMrt(int width, int height, TexFormat fmtA, TexFormat fmtB)
        : width_(width), height_(height), read_(width, height, fmtA, fmtB), write_(width, height, fmtA, fmtB) {}

    int width() const { return width_; }
    int height() const { return height_; }
    Side& read() { return flipped_ ? write_ : read_; }
    Side& write() { return flipped_ ? read_ : write_; }
    bool ok() const { return read_.ok() && write_.ok(); }

    void create() {
        read_.create();
        write_.create();
    }
    void swap() { flipped_ = !flipped_; }
    void release() {
        read_.release();
        write_.release();
    }

private:
    int width_ = 0;
    int height_ = 0;
    Side read_;
    Side write_;
    bool flipped_ = false;
};

}  // namespace geode::viz::fluid
