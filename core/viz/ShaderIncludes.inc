// The shader include allow-list: every `//#include <name>` a shader may use.
//
// The one source of truth. core/viz/ShaderSource.cpp pastes this file into its
// kIncludes array; tools/check_shaders.py, tools/glslcheck/check.mjs and
// StyleFamiliesTest read it as text. Keep it to `"name",` entries and `//`
// line comments: the readers take every double-quoted name outside a comment.
// A new lib_*.glsl in app/src/main/assets/shaders must be added here, or it
// fails on the phone and in both checks as an unknown include.

"lib_palette",
"lib_scene_uniforms",
"lib_scene_grade",
"lib_sdf3",
"lib_touch",
"lib_psrdnoise2",
"lib_particle_common",
"lib_particle_shade",
"lib_scene_motion",
"lib_dmt",
"lib_camera",
// The sound engine's frame (core/sound): uSound, uSoundTex and the SND_* names.
"lib_sound",
// Endless zoom's two octave layers (core/viz/EndlessZoom.hpp); pasted by lib_scene_uniforms.
"lib_zoom",
// The feedback pass's MilkDrop UV transform (core/viz/liquid/FeedbackMotion.hpp); liquid_frag.
"lib_feedback",
// The simulation scenes' shared 2.5D look (parallax relief, light, eight materials); after lib_sound.
"lib_relief",
// One library per style family (StyleFamilies.kt): the twelve styles of a family share it.
"lib_arbor",
"lib_bulb",
"lib_chrys",
"lib_lucid",
"lib_dive",
"lib_glass",
"lib_nebula",
"lib_terra",
"lib_truchet",
"lib_foam",
// The neon set (five loose styles): normalized shape controls, the palette pull, shared sound drives.
"lib_neon",
// Morph World's space: the World Genome's operator chain and its three render modes (core/viz/world).
"lib_world",
