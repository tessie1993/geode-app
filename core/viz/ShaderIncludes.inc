// The shader include allow-list: every `//#include <name>` a shader may use.
//
// The one source of truth. core/viz/ShaderSource.cpp pastes this file into its
// kIncludes array; tools/check_shaders.py and tools/glslcheck/check.mjs
// read it as text. Keep it to `"name",` entries and `//`
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
// The feedback pass's MilkDrop UV transform (core/viz/liquid/FeedbackMotion.hpp); liquid_frag.
"lib_feedback",
// The simulation scenes' shared 2.5D look (parallax relief, light, eight materials); after lib_sound.
"lib_relief",
// A simulation presented in 3D on the camera rig (core/viz/present/FieldPresent); its march comes after the present's colours.
"lib_field3d",
"lib_field3d_march",
// One library per style family (core/viz/Catalogue.cpp): the styles of a family share it.
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
"lib_flame",
"lib_opal",
"lib_hollow",
"lib_prism",
"lib_fluor",
"lib_loom",
"lib_shard",
"lib_petal",
"lib_fold",
// Their second halves, included after the style's own map: the march that calls it (no prototypes).
// The five older families' field (the style's map through one entry point, where Grow wraps it).
"lib_arbor_field",
"lib_bulb_field",
"lib_chrys_field",
"lib_lucid_field",
"lib_flame_march",
"lib_opal_march",
"lib_hollow_march",
"lib_prism_march",
"lib_fluor_march",
"lib_loom_march",
"lib_shard_march",
"lib_petal_march",
// Fold has five marches, one to a kind of fractal; each style includes lib_fold and one of them.
"lib_fold_march_glow",
"lib_fold_march_cone",
"lib_fold_march_slice",
"lib_fold_march_pair",
"lib_fold_march_probe",
// The neon set (five loose styles): normalized shape controls, the palette pull, shared sound drives.
"lib_neon",
// Morph World's space: the World Genome's operator chain and its three render modes (core/viz/world).
"lib_world",
