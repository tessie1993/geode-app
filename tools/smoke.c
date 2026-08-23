/* Headless projectM smoke test: EGL surfaceless (Mesa llvmpipe) + GLES3 FBO.
 * Mirrors the app's integration: create -> resize -> feed PCM -> render;
 * then load real .milk presets with a hard cut and verify pixels change. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <projectM-4/projectM.h>

#define W 256
#define H 144

static char g_err[512] = {0};
static void on_fail(const char *f, const char *m, void *u) {
    (void) u;
    snprintf(g_err, sizeof(g_err), "%s: %s", f ? f : "?", m ? m : "?");
}

static unsigned long checksum(unsigned char *px, int n, int *nonblack) {
    unsigned long sum = 0;
    *nonblack = 0;
    for (int i = 0; i < n; i += 4) {
        sum += px[i] + px[i + 1] + px[i + 2];
        if (px[i] > 8 || px[i + 1] > 8 || px[i + 2] > 8) (*nonblack)++;
    }
    return sum;
}

static void feed(projectm_handle pm, int frame) {
    float buf[735]; /* 44100/60 */
    for (int i = 0; i < 735; i++) {
        double t = (frame * 735 + i) / 44100.0;
        float s = 0.4f * sinf(2 * 3.14159f * 110 * t) + 0.2f * sinf(2 * 3.14159f * 880 * t);
        if (frame % 30 < 2) s += 0.5f * sinf(2 * 3.14159f * 60 * t); /* beat thump */
        buf[i] = s;
    }
    projectm_pcm_add_float(pm, buf, 735, PROJECTM_MONO);
}

int main(int argc, char **argv) {
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, NULL, NULL)) { printf("FATAL egl init\n"); return 1; }
    eglBindAPI(EGL_OPENGL_ES_API);
    EGLint cfgAttr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, 0, EGL_NONE};
    EGLConfig cfg; EGLint n;
    if (!eglChooseConfig(dpy, cfgAttr, &cfg, 1, &n) || n < 1) { printf("FATAL egl config\n"); return 1; }
    EGLint ctxAttr[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
    if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
        printf("FATAL egl context/current\n"); return 1;
    }
    printf("GL_VERSION: %s\n", glGetString(GL_VERSION));

    GLuint fbo, tex, rbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, W, H);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { printf("FATAL fbo\n"); return 1; }

    projectm_handle pm = projectm_create();
    if (!pm) { printf("FATAL projectm_create\n"); return 1; }
    projectm_set_preset_switch_failed_event_callback(pm, on_fail, NULL);
    projectm_set_fps(pm, 60);
    projectm_set_mesh_size(pm, 48, 32);
    projectm_set_preset_duration(pm, 999999.0);
    projectm_set_preset_locked(pm, true);
    projectm_set_window_size(pm, W, H);

    unsigned char *px = malloc(W * H * 4);
    int nonblack; unsigned long sum;

    for (int f = 0; f < 40; f++) { feed(pm, f); projectm_opengl_render_frame_fbo(pm, fbo); }
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
    sum = checksum(px, W * H * 4, &nonblack);
    printf("IDLE: sum=%lu nonblack=%d/%d err='%s'\n", sum, nonblack, W * H, g_err);

    for (int p = 1; p < argc; p++) {
        g_err[0] = 0;
        projectm_load_preset_file(pm, argv[p], false);
        for (int f = 0; f < 90; f++) { feed(pm, 100 + f); projectm_opengl_render_frame_fbo(pm, fbo); }
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
        unsigned long s2 = checksum(px, W * H * 4, &nonblack);
        printf("PRESET %s: sum=%lu nonblack=%d/%d changed=%s err='%s'\n",
               argv[p], s2, nonblack, W * H, (s2 != sum) ? "yes" : "NO", g_err);
    }
    projectm_destroy(pm);
    printf("DONE\n");
    return 0;
}
