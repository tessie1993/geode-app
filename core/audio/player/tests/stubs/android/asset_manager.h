#pragma once

// Mixer uses only DSP declarations from the shared API header. No Android asset
// API is exercised by this host target; retain its opaque type for those declarations.
typedef struct AAssetManager AAssetManager;
