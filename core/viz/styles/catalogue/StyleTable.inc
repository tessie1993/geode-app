// The style table: one row per style, the single place a style id, its look and its per-style facts
// are written. core/viz/styles/catalogue/Catalogue.cpp includes this file several times with different macro bodies and
// derives from it everything the catalogue answers: the picker order, the families' id lists, the
// marched, Form, Formula, Shapes, Shadow and Escape sets, the spin rates and the looks. No other file
// lists an id for these; catalogue_test checks the result against core/tests/catalogue.txt.
//
// Rows, in picker order: the loose fragment styles first, then the families in family order, each
// family's opener first.
//
//   GEODE_LOOSE(id, Look, flags, ...)      a loose style (ShaderScene compiles shaders/<id>_frag.glsl)
//   GEODE_FAMILY_BEGIN(name, prefix, library, Look)   opens a family; its styles take its look
//   GEODE_STYLE(name, id, flags, ...)      one style of the family opened last
//   GEODE_FAMILY_END(name)
//
// flags is None or the flags below joined with |:
//   March    spends the Detail budget (a raymarch)         OffRig   marches from a view of its own, not the camera rig
//   Form     draws Spawn, Fractal, Front and Back layer    Formula  keeps alternative distance formulas (uFormula)
//   Shapes   melts the shape graph in (uShapeMix)          Shadow   its lit surface casts soft shadows (uShadow)
//   Escape   its fractal folds colour by escape            Spin(r)  turns at rate r (signed; the Spin control scales it)
// A style that marches flies the camera rig unless it is OffRig. style_reads_test holds each flag to
// the shader's own reads.
//
// Escape hatch: the trailing `...` of a row is ignored by Catalogue.cpp. A family with parameters of
// its own writes them there and reads them by including this file with a GEODE_STYLE of its own, so
// family-specific fields stay in that family's rows and nothing shared has to know them. Each family's
// BEGIN..END block is its own file in core/viz/styles/families, #included below in family order.
//
// The dedicated renderers' styles (silk, life, mycelium, acid, fluid, cymatics) are not
// rows: their tables live in core/viz/styles/catalogue/StyleCatalog.

// Loose styles, in picker order.
GEODE_LOOSE("julia", Fractals, March | Form)
GEODE_LOOSE("tunnel", FlyThrough, March | Form | Spin(-0.5f))
GEODE_LOOSE("mandel", Fractals, March | Form)
GEODE_LOOSE("kaleido", Patterns, March | Form | Spin(1.0f))
GEODE_LOOSE("plasma", Flowing, March | Form)
GEODE_LOOSE("bars", Classic, March | Form)
GEODE_LOOSE("ring", Classic, March | Form)
GEODE_LOOSE("scope", Classic, March | Form)
GEODE_LOOSE("liss", Classic, March | Form | Shapes)
GEODE_LOOSE("warp", Flowing, March | Form)
GEODE_LOOSE("grid", Patterns, March | Form)
GEODE_LOOSE("voronoi", Patterns, March | Form)
GEODE_LOOSE("metaballs", Flowing, March | Form)
GEODE_LOOSE("ripples", Flowing, March | Form)
GEODE_LOOSE("starfield", Space, March | Form)
GEODE_LOOSE("waves", Classic, March | Form)
GEODE_LOOSE("hexgrid", Patterns, March | Form)
GEODE_LOOSE("spiral", Patterns, March | Form | Spin(1.2f))
GEODE_LOOSE("aurora", Space, March | Form)
GEODE_LOOSE("solar", Space, March | Form | Shapes)
GEODE_LOOSE("winter", Psychedelic, March | Form)
GEODE_LOOSE("lava", Flowing, March | Form)
GEODE_LOOSE("vanishing", FlyThrough, March | OffRig | Form)
GEODE_LOOSE("morphogen", Flowing, March | OffRig | Form)
GEODE_LOOSE("nebula", Space, March | Form)
GEODE_LOOSE("noneuclid", FlyThrough, March | OffRig | Form)
GEODE_LOOSE("kifs", Fractals, March | Form | Spin(0.8f))
GEODE_LOOSE("orb_lattice", Glass, March | Form | Spin(0.7f))
GEODE_LOOSE("rod_tunnel", FlyThrough, March | Form | Spin(0.6f))
GEODE_LOOSE("neon_tiles", Glass, March | Form)
GEODE_LOOSE("chroma_orb", Glass, March | Form | Spin(-0.6f))
GEODE_LOOSE("mandala_dome", Patterns, March | Form | Spin(0.45f))
GEODE_LOOSE("bead_vortex", Glass, March | Form | Spin(1.4f))
GEODE_LOOSE("merkaba_grid", Glass, March | Form | Spin(0.4f))
GEODE_LOOSE("spiral_eye", Patterns, March | Form | Spin(-0.9f))
GEODE_LOOSE("blacklight_bloom", Glass, March | Form)
GEODE_LOOSE("fractal_temple", Fractals, March | Form)
GEODE_LOOSE("curl_bloom", Flowing, March | OffRig | Form)
GEODE_LOOSE("nectar_flow", Flowing, March | OffRig | Form)
GEODE_LOOSE("crystal_canyon", FlyThrough, March | Form)
GEODE_LOOSE("neon_city", Glass, March | Form)
GEODE_LOOSE("particle_tunnel", FlyThrough, March | Form | Spin(-0.8f))
GEODE_LOOSE("chrome_nebula", Space, March | Form | Shapes)
GEODE_LOOSE("fractal_dive", FlyThrough, March | Form)
GEODE_LOOSE("aurora_ocean", Space, March | Form)
GEODE_LOOSE("spectrum_terrain", FlyThrough, March | Form)
GEODE_LOOSE("outrun_drive", FlyThrough, March)
GEODE_LOOSE("menger_core", Fractals, March | Form)
GEODE_LOOSE("gyroid_cavern", FlyThrough, March | Form)
GEODE_LOOSE("ringed_planet", Space, March)
GEODE_LOOSE("jelly_abyss", Flowing, March | Form)
GEODE_LOOSE("ribbon_storm", Flowing, March)
GEODE_LOOSE("prism_rain", Glass, March | Form)
GEODE_LOOSE("supershape", Fractals, March | Form)
GEODE_LOOSE("fractal_tunnel", FlyThrough, March | Form)
GEODE_LOOSE("glow_vortex", Glass, March | Form | Spin(-1.1f))
GEODE_LOOSE("neon_chrome_sea", Glass, March | Form)
GEODE_LOOSE("black_hole", Space, March)
GEODE_LOOSE("hex_hive", Patterns, March | Form | Spin(0.3f))
GEODE_LOOSE("mandelbox_cathedral", Fractals, March | Form)
GEODE_LOOSE("morph_world", FlyThrough, March)
GEODE_LOOSE("pool", Flowing, None)

// Each family lists its own styles, so it carries its own count; the opener is first.
#include "viz/styles/families/arbor.inc"

#include "viz/styles/families/bulb.inc"

#include "viz/styles/families/chrysanthemum.inc"

#include "viz/styles/families/lucid.inc"

#include "viz/styles/families/dive.inc"

#include "viz/styles/families/glass.inc"

#include "viz/styles/families/nebula.inc"

#include "viz/styles/families/terrain.inc"

#include "viz/styles/families/truchet.inc"

#include "viz/styles/families/foam.inc"

#include "viz/styles/families/flame.inc"

#include "viz/styles/families/opal.inc"

#include "viz/styles/families/hollow.inc"

#include "viz/styles/families/prism.inc"

#include "viz/styles/families/fluor.inc"

#include "viz/styles/families/loom.inc"

#include "viz/styles/families/shard.inc"

#include "viz/styles/families/petal.inc"

#include "viz/styles/families/fold.inc"
