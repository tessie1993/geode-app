#include "stage/StageLifecycle.hpp"

namespace geode::stage {

// A surface is wanted for a window with a size, while the host is not paused; a surface stage also lets it go
// while its rule says Stopped. The context, once made, stays until quit or a loss.
bool Lifecycle::wantsSurface() const {
    if (!window_ || width_ <= 0 || height_ <= 0 || paused_) return false;
    return !(kind_ == Kind::Surface && mode_ == RunMode::Stopped);
}

// Drawing is wanted while the surface is and the rule asks for any picture (Dark keeps the GL, only black).
bool Lifecycle::wantsDrawing() const { return wantsSurface() && mode_ != RunMode::Stopped; }

bool Lifecycle::drawable() const { return !quit_ && current_ && clientCreated_ && !lost_ && wantsSurface(); }

// From what is wanted, not what exists: a surface replaced or a context rebuilt within one batch of events keeps
// the vsync (and the pacer's measure of the panel) running, since frames only draw once canDraw holds. The actions
// that build come before StartVsync in next(), so a vsync never starts with nothing to draw on.
bool Lifecycle::wantsVsync() const {
    if (quit_ || !wantsSurface() || refused_ || surfaceBad_) return false;
    return mode_ == RunMode::Animated || (mode_ == RunMode::Still && settling_);
}

bool Lifecycle::canDraw() const { return drawable() && toldWidth_ == width_ && toldHeight_ == height_; }

// Leaving a drawing state sheds the spare scenes while the context is still current, before the surface goes
// (#854): the system's trim only arrives once no frame is left to read it. Coming back before the trim ran
// cancels it.
void Lifecycle::noteDrawing(bool before) {
    const bool after = wantsDrawing();
    if (after) {
        trimDue_ = false;
    } else if (before && clientCreated_ && glCurrent()) {
        trimDue_ = true;
    }
}

void Lifecycle::markLost() {
    if (!context_ || lost_) return;
    lost_ = true;
    ++lostInARow_;
}

void Lifecycle::windowSet(int width, int height) {
    if (quit_) return;
    const bool before = wantsDrawing();
    // A surface made on the window this one replaces must go before one is made here: a window takes one
    // producer at a time.
    if (surface_) stale_ = true;
    window_ = true;
    width_ = width;
    height_ = height;
    // A new window is a fresh start: a bad surface or a refused context is tried once more.
    surfaceBad_ = false;
    refused_ = false;
    lostInARow_ = 0;
    noteDrawing(before);
}

void Lifecycle::windowChanged(int width, int height) {
    if (quit_ || !window_) return;
    const bool before = wantsDrawing();
    width_ = width;
    height_ = height;
    noteDrawing(before);
}

void Lifecycle::windowGone() {
    if (quit_ || !window_) return;
    const bool before = wantsDrawing();
    window_ = false;
    surfaceBad_ = false;
    noteDrawing(before);
}

void Lifecycle::setMode(RunMode mode, int64_t nowMs) {
    if (quit_) return;
    const bool before = wantsDrawing();
    const RunMode previous = mode_;
    mode_ = mode;
    if (mode == RunMode::Still) {
        // On the way into a still frame the surface paces for a moment, so a scene still being built finishes
        // on screen; restated while still, it draws one more frame for the restated look.
        const int64_t settle = viz::surface::settleMs(previous, mode);
        if (settle > 0) {
            settling_ = true;
            settleUntilMs_ = nowMs + settle;
        } else if (!settling_) {
            drawOnce_ = true;
        }
    } else {
        settling_ = false;
    }
    noteDrawing(before);
}

void Lifecycle::tick(int64_t nowMs) {
    if (settling_ && nowMs >= settleUntilMs_) settling_ = false;
}

void Lifecycle::pause() {
    if (quit_) return;
    const bool before = wantsDrawing();
    paused_ = true;
    noteDrawing(before);
}

void Lifecycle::resume() {
    if (quit_) return;
    const bool before = wantsDrawing();
    paused_ = false;
    noteDrawing(before);
}

void Lifecycle::redrawNeeded() {
    if (quit_ || vsync_) return;
    drawOnce_ = true;
}

void Lifecycle::contextLost() {
    if (quit_) return;
    markLost();
}

void Lifecycle::surfaceBad() {
    if (quit_ || !surface_) return;
    surfaceBad_ = true;
}

void Lifecycle::frameDrawn() { lostInARow_ = 0; }

void Lifecycle::surfacelessSupported(bool supported) { surfacelessOk_ = supported; }

void Lifecycle::quit() { quit_ = true; }

uint32_t Lifecycle::stateWord() const {
    uint32_t w = static_cast<uint32_t>(mode_) << kStateModeShift;
    if (window_) w |= kStateWindow;
    if (context_) w |= kStateContext;
    if (surface_) w |= kStateSurface;
    if (canDraw()) w |= kStateDrawable;
    if (vsync_) w |= kStateVsync;
    if (paused_) w |= kStatePaused;
    if (refused_) w |= kStateRefused;
    if (surfaceBad_) w |= kStateSurfaceBad;
    if (released_) w |= kStateReleased;
    return w;
}

Action Lifecycle::next() const {
    if (quit_) {
        if (vsync_) return {Act::StopVsync};
        if (!released_) {
            // The client frees its GL names with the context current wherever one can be: on the window surface
            // while it is usable, else with no surface where the driver allows it, else without a context.
            if (context_ && !lost_ && !glCurrent()) {
                if (surfaceUsable()) return {Act::MakeCurrent};
                if (surfacelessOk_) return {Act::MakeCurrentSurfaceless};
            }
            return {Act::ClientRelease, 0, 0, glCurrent() && !lost_};
        }
        if (surface_) return {Act::DestroySurface};
        if (surfaceless_) return {Act::ReleaseCurrent};
        if (context_) return {Act::DestroyContext};
        const uint32_t state = stateWord();
        if (state != reported_) return {Act::ReportState, 0, 0, false, state};
        return {};
    }
    if (vsync_ && !wantsVsync()) return {Act::StopVsync};
    if (lost_) {
        // Every name in the context is gone: the client forgets them, then the surface and context go and are
        // made again below.
        if (clientCreated_) return {Act::ClientContextLost};
        if (surface_) return {Act::DestroySurface};
        if (surfaceless_) return {Act::ReleaseCurrent};
        if (context_) return {Act::DestroyContext};
    }
    if (trimDue_) {
        if (glCurrent()) return {Act::ClientTrimSpares};
        if (surfaceUsable()) return {Act::MakeCurrent};
    }
    if (surface_ && (!wantsSurface() || stale_ || surfaceBad_)) return {Act::DestroySurface};
    if (!context_ && wantsSurface() && !refused_) return {Act::CreateContext};
    if (context_ && !surface_ && wantsSurface() && !surfaceBad_) return {Act::CreateSurface};
    if (surface_ && !current_) return {Act::MakeCurrent};
    if (current_ && !clientCreated_) return {Act::ClientSurfaceCreated};
    if (current_ && (toldWidth_ != width_ || toldHeight_ != height_)) return {Act::ClientSurfaceChanged, width_, height_};
    if (!vsync_ && wantsVsync()) return {Act::StartVsync};
    if (drawOnce_ && !vsync_ && canDraw()) return {Act::DrawOnce};
    const uint32_t state = stateWord();
    if (state != reported_) return {Act::ReportState, 0, 0, false, state};
    return {};
}

void Lifecycle::done(const Action& action, Outcome outcome) {
    const bool ok = outcome == Outcome::Done;
    switch (action.act) {
        case Act::None: break;
        case Act::CreateContext:
            if (ok) {
                context_ = true;
                clientCreated_ = false;
            } else {
                refused_ = true;
            }
            break;
        case Act::CreateSurface:
            if (ok) {
                surface_ = true;
                stale_ = false;
                current_ = false;
                toldWidth_ = 0;
                toldHeight_ = 0;
            } else {
                surfaceBad_ = true;
            }
            break;
        case Act::MakeCurrent:
            if (ok) {
                current_ = true;
                surfaceless_ = false;
            } else if (outcome == Outcome::ContextLost) {
                markLost();
            } else {
                surfaceBad_ = true;
            }
            break;
        case Act::MakeCurrentSurfaceless:
            if (ok) {
                surfaceless_ = true;
            } else if (outcome == Outcome::ContextLost) {
                markLost();
            } else {
                surfacelessOk_ = false;
            }
            break;
        case Act::ClientSurfaceCreated: clientCreated_ = true; break;
        case Act::ClientSurfaceChanged:
            toldWidth_ = action.width;
            toldHeight_ = action.height;
            break;
        case Act::StartVsync:
            vsync_ = true;
            drawOnce_ = false;  // the next vsync draws anyway
            break;
        case Act::StopVsync: vsync_ = false; break;
        case Act::DrawOnce: drawOnce_ = false; break;
        case Act::ClientTrimSpares: trimDue_ = false; break;
        case Act::ClientContextLost:
            clientCreated_ = false;
            trimDue_ = false;
            break;
        case Act::ReleaseCurrent:
            surfaceless_ = false;
            current_ = false;
            trimDue_ = false;
            break;
        case Act::DestroySurface:
            surface_ = false;
            stale_ = false;
            current_ = false;
            toldWidth_ = 0;
            toldHeight_ = 0;
            trimDue_ = false;
            break;
        case Act::DestroyContext:
            context_ = false;
            clientCreated_ = false;
            current_ = false;
            surfaceless_ = false;
            lost_ = false;
            trimDue_ = false;
            if (lostInARow_ >= kMaxLostInARow) refused_ = true;
            break;
        case Act::ClientRelease: released_ = true; break;
        case Act::ReportState: reported_ = action.state; break;
    }
}

}  // namespace geode::stage
