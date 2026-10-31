# Truchet and Glass redo: idea board

The user's brief: the twelve Glass and twelve Truchet styles are too basic. We are rebuilding them
on upgraded `lib_glass` and `lib_truchet`. This board lists the techniques we borrow, what makes
each one striking, and where it comes from.

**Licence rule.** Most Shadertoy code is CC BY-NC-SA, and preset packs carry their own licences.
MuzViz is a paid app, so we take **ideas and published techniques only** and write original code.
No shader code is copied. The sources below are credited for inspiration.

## Sources

- Christopher Carlson, *Multi-Scale Truchet Patterns*, Bridges 2018: [archive.bridgesmathart.org](https://archive.bridgesmathart.org/2018/bridges2018-39.html). Winged tiles that subdivide by halves and swap colours at each level.
- Ned Batchelder, *Truchet images*: [nedbatchelder.com](https://nedbatchelder.com/blog/202208/truchet_images.html). A readable walk through Carlson's tiles and their seams.
- Cameron Browne, *Truchet curves and surfaces*: [cambolbro.com](https://cambolbro.com/graphics/truchet-figures/). 3D Truchet: tiles in a cube that join into tubes and surfaces.
- Shane's Truchet series on Shadertoy (simplex weave, extruded tubes, the oldschool tube), credited by [shadcn.io's simplex Truchet weave](https://www.shadcn.io/shaders/simplex-truchet-weave-shader). Over-and-under crossings on a triangle grid, tubes lit as 3D.
- Inigo Quilez, articles: [iquilezles.org/articles](https://iquilezles.org/articles/). SDF primitives and smooth min, tetrahedral normals, palettes, soft shadows.
- Evan Wallace, *WebGL Water*: [madebyevan.com/webgl-water](https://madebyevan.com/webgl-water/). Caustics from how refracted light compresses area.
- Shadertoy glass refraction example `wt2GDW`: [shadertoy.com/view/wt2GDW](https://www.shadertoy.com/view/wt2GDW).
- MilkDrop presets: NestDrop's *Cream of the Crop* pack, about 9,800 presets hand-picked from about 52,000 ([NestDrop pack](https://www.patreon.com/posts/pack-nestdrop-1-91682111), and recommended by [projectM Android](https://f-droid.org/packages/nl.neerdael.projectm/)). Its best presets use feedback zoom, a warp field that follows the bass, and kaleidoscope folds.
- Subreddits to mine for favourites: r/shaders, r/generative, r/creativecoding, r/visualizers, r/milkdrop.

## Shared library upgrades

### lib_glass

| Technique | What makes it striking | Source |
|---|---|---|
| Two internal bounces, budget-gated | Gems and cubes get their "fire": light that hits total internal reflection bounces again instead of stopping | textbook optics; Shadertoy gem shaders |
| Dispersion along a spectrum (3 to 6 taps, set by Detail) | Rainbow edges on every facet, not just R/G/B fringes | Shadertoy dispersion work |
| Thin-film colour on a Fresnel rim | Oil-slick and soap colour that shifts with view angle | glassFilm (two-beam interference) |
| Caustic net from the area Jacobian | A moving pool-floor web of light with no texture | Evan Wallace, WebGL Water |
| Frosted and roughened glass (normal noise scaled by roughness) | Ice and sea glass, blurred reflections | iq, noise articles |
| Volume glow inside the body (Beer absorption plus emission) | The inside of the glass glows with the music | MilkDrop-style emissive cores |

### lib_truchet

| Technique | What makes it striking | Source |
|---|---|---|
| Multi-scale (quadtree) Truchet | Big sweeping paths next to fine filigree, colours flipping at each level | Carlson, Bridges 2018 |
| Layered Truchet (two or three planes, parallax) | Depth without a march | Shane |
| Over-and-under weave | Paths read as woven cord, with shadow at each crossing | Shane, simplex weave |
| 3D Truchet tiles in a cube, marched | Tubes that loop through space; fly-through tunnels | Browne, *Truchet curves and surfaces* |
| Tubes extruded from the 2D tiling (height and round profile) | Plumbing and neon tubing with real lighting | Shane |
| Animated rewiring on onsets | The maze reconnects on the beat | existing truchetRoll |

## Per-style directions (first pass)

Every style moves several things to different parts of the music at once. For example: the bass
drives scale or IOR, onsets rewire tiles or spawn drops, the highs drive sparkle or dispersion,
the key drives the hue, and surges drive the camera. Colour comes from `lib_palette`, the camera
from `lib_camera`, and style ids stay the same.

### Glass

| Style | Direction |
|---|---|
| glass_orb | A crystal ball with a spectral rim, a caustic on the floor, and a thin-film swirl on its surface |
| glass_pool | A pool seen from above: caustic net, dispersed refraction of tiles, ripples from onsets |
| glass_prism | A prism that splits a beam into a true spectrum fan, with the fan's angle set by pitch |
| glass_lens | A lens array: a magnifying sweep over the music's spectrum |
| glass_drops | Rain on a window: drops that slide and merge, each one refracting a blurred scene |
| glass_bubbles | Soap bubbles with thin-film colour that pop on onsets |
| glass_marble | A marble with a coloured ribbon core, two bounces, and the core swirling with the mids |
| glass_caustic | A full-screen caustic light show, dispersed, under a rippling surface |
| glass_cubes | Ice cubes with internal bounces, tumbling on the kick |
| glass_gem | A brilliant-cut gem with two-bounce fire and spectral sparkle on the highs |
| glass_ice | Frosted, cracked ice with light glowing through the cracks |
| glass_knot | A glass torus knot with an emissive core and a dispersed rim |

### Truchet

| Style | Direction |
|---|---|
| truchet_arcs | Multi-scale Carlson arcs with level colours flipping on onsets |
| truchet_hex | A hex Truchet extruded into lit tubes |
| truchet_weave | An over-and-under weave of cords with crossing shadows |
| truchet_maze | A diagonal maze with glowing walls, the maze rewiring on the beat |
| truchet_tri | Triangle Truchet with parallax layers |
| truchet_quad | A quadtree Truchet that subdivides with the energy |
| truchet_tubes | 3D cube-Truchet tubes, marched, seen from outside |
| truchet_neon | Neon tubing with bloom, current flowing along the paths |
| truchet_flow | Particles flowing along the Truchet paths |
| truchet_knot | A Celtic-knot weave with ribbons |
| truchet_tunnel | A fly-through tunnel of 3D Truchet tubes |
| truchet_relief | A Truchet relief carved in stone or metal (lib_relief) |

These are first-pass directions. The user picks among them before we build.
