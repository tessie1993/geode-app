#pragma once

// The Pass interface of ADR 0006's one frame graph (README.md beside this file). Written to the
// exact shape #439 (the edge glow pass) introduces, so the two branches reconcile without a change:
// whichever lands second keeps this file as it is.

#include "sound/SoundFrame.h"

namespace geode::viz::frame {
enum class Surface : int { Live, Wallpaper, Dream, Export };
struct FrameCtx { int width, height; double seconds; float dt; Surface surface; const GeodeSoundFrame* sound; };
struct PassInputs { unsigned sceneTex = 0, compositeTex = 0; };
struct PassTarget { unsigned fbo = 0; int width = 0, height = 0; };
class Pass { public:
  virtual ~Pass() = default;
  virtual bool wants(const FrameCtx&) const = 0;
  virtual void prepare(const FrameCtx&) = 0;   // CPU only
  virtual void draw(const PassInputs&, const PassTarget&) = 0;  // GL thread
  virtual void release() = 0;  // drop GL names before a new context gens
};}
