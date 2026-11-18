#pragma once
#include <GLES3/gl3.h>

namespace geode::viz {

// The framebuffer and viewport the renderer bound for the scene it is drawing now, so a scene (or a pass inside
// it) that binds its own targets restores them without asking the driver. Outside the renderer's scene draws
// nothing is recorded and the driver is asked. GL thread; each thread keeps its own, as each renderer does.
class SceneTarget {
public:
    // Renderer: just before a scene's draw(), with `fbo` bound and the viewport at 0, 0, width, height.
    SceneTarget(GLuint fbo, int width, int height)
        : fbo_(static_cast<GLint>(fbo)), viewport_{0, 0, width, height}, previous_(current()) {
        current() = this;
    }
    ~SceneTarget() { current() = previous_; }
    SceneTarget(const SceneTarget&) = delete;
    SceneTarget& operator=(const SceneTarget&) = delete;

    // The bound draw framebuffer.
    static GLint fbo() {
        if (const SceneTarget* t = current()) return t->fbo_;
        GLint fbo = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
        return fbo;
    }
    // The bound draw framebuffer and the viewport (x, y, width, height).
    static void read(GLint& fbo, GLint* viewport) {
        if (const SceneTarget* t = current()) {
            fbo = t->fbo_;
            for (int i = 0; i < 4; i++) viewport[i] = t->viewport_[i];
            return;
        }
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
        glGetIntegerv(GL_VIEWPORT, viewport);
    }

private:
    static const SceneTarget*& current() {
        static thread_local const SceneTarget* target = nullptr;
        return target;
    }

    GLint fbo_;
    GLint viewport_[4];
    const SceneTarget* previous_;
};

}  // namespace geode::viz
