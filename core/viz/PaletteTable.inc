// The built-in palettes (viz/Palette library()), in library order. Define the two row macros, include, undef.
//
// GEODE_CLASSIC(id, name, base, range): rows 0..20, the old cosine hue ramps in the `palette` index order
// looks were saved by; viz/Params' SceneParams::palettes() reads these rows too, so there is one table.
// The library samples each to kMaxStops stops: a ramp round the whole wheel (range 1) Loops, the others run
// once and Mirror, like a new palette. Rows 12..20 (Royal .. Yellow) are retired (SceneParams::kListedPalettes):
// kept, never moved, so a saved index still draws, but no picker lists them and no roll deals them.
GEODE_CLASSIC("spectrum", "Spectrum", 0.0f, 1.0f)
GEODE_CLASSIC("neon", "Neon", 0.5f, 0.45f)
GEODE_CLASSIC("fire", "Fire", 0.0f, 0.14f)
GEODE_CLASSIC("ocean", "Ocean", 0.5f, 0.2f)
GEODE_CLASSIC("mono", "Mono", 0.6f, 0.02f)
GEODE_CLASSIC("candy", "Candy", 0.85f, 0.5f)
GEODE_CLASSIC("forest", "Forest", 0.33f, 0.18f)
GEODE_CLASSIC("aurora", "Aurora", 0.45f, 0.7f)
GEODE_CLASSIC("sunset", "Sunset", 0.05f, 0.3f)
GEODE_CLASSIC("ice", "Ice", 0.55f, 0.15f)
GEODE_CLASSIC("vapor", "Vapor", 0.78f, 0.35f)
GEODE_CLASSIC("toxic", "Toxic", 0.25f, 0.25f)
// Retired from here to row 20.
GEODE_CLASSIC("royal", "Royal", 0.7f, 0.25f)
GEODE_CLASSIC("blush", "Blush", 0.93f, 0.12f)
GEODE_CLASSIC("copper", "Copper", 0.07f, 0.1f)
GEODE_CLASSIC("mint", "Mint", 0.4f, 0.12f)
GEODE_CLASSIC("galaxy", "Galaxy", 0.65f, 0.5f)
GEODE_CLASSIC("cherry", "Cherry", 0.97f, 0.08f)
GEODE_CLASSIC("cyan", "Cyan", 0.5f, 0.08f)
GEODE_CLASSIC("magenta", "Magenta", 0.833f, 0.08f)
GEODE_CLASSIC("yellow", "Yellow", 0.167f, 0.08f)
// GEODE_COLOURS(id, name, Source, Wrap, Space, colours...): rows 21.. as listed colours, evenly spread.
// Rows 21..25 are the old colour maps (`paletteLut` 0..4, Crameri's cyclic maps): entries 0, 16, .. 240 of
// each 256-entry row of shaders/cyclic_palettes.bin. They are cyclic, so they Loop.
GEODE_COLOURS("bamo", "bamO", ColourMap, Loop, Rgb, 0x4F3043, 0x6D3961, 0x8F4F80, 0xAA689A, 0xC285B2, 0xD4A7C6, 0xDAC0CC, 0xD7CBC7,
              0xCECDBB, 0xB6C19C, 0x94A376, 0x7B875D, 0x676E4D, 0x575642, 0x4D453C, 0x49383A)
GEODE_COLOURS("broco", "brocO", ColourMap, Loop, Rgb, 0x372F38, 0x36354E, 0x3A476C, 0x48628B, 0x617FA5, 0x809BB9, 0xA1B5C9, 0xBFCBD0,
              0xCFD3C5, 0xC8C9A6, 0xB1B082, 0x939260, 0x757445, 0x5B5932, 0x474229, 0x3B342B)
GEODE_COLOURS("corko", "corkO", ColourMap, Loop, Rgb, 0x3F3E3A, 0x3E3D4B, 0x3F4864, 0x495E84, 0x5F7A9F, 0x7B96B4, 0x97AFC3, 0xACC3C8,
              0xAFCBBC, 0x9EC3A3, 0x84B183, 0x699962, 0x547D44, 0x486431, 0x43512C, 0x414531)
GEODE_COLOURS("romao", "romaO", ColourMap, Loop, Rgb, 0x733957, 0x7E3942, 0x8B4433, 0x99582C, 0xAA752F, 0xBD9842, 0xCFBC66, 0xD6D790,
              0xCBE1B3, 0xAFDCC8, 0x8BCBCF, 0x68B1CB, 0x5393BF, 0x4F75AC, 0x595891, 0x674372)
GEODE_COLOURS("viko", "vikO", ColourMap, Loop, Rgb, 0x4F1A3D, 0x432653, 0x383B6E, 0x345689, 0x4575A1, 0x6895B6, 0x93B1C5, 0xBCC1C6,
              0xD5BEB3, 0xD9AB92, 0xD08F6D, 0xBD6F49, 0xA14B2C, 0x822C1F, 0x6B1A21, 0x5B152C)
// Rows 26..: palettes of picked colours, blended in OKLab and mirrored like a new palette of your own.
GEODE_COLOURS("ember", "Ember", BuiltIn, Mirror, Oklab, 0x2B0F0E, 0x8C1C13, 0xE2531B, 0xF7A23B, 0xFFE29A)
GEODE_COLOURS("lagoon", "Lagoon", BuiltIn, Mirror, Oklab, 0x022C43, 0x115173, 0x1F8A9E, 0x7FD8BE, 0xE8F7EE)
GEODE_COLOURS("pastel", "Pastel", BuiltIn, Mirror, Oklab, 0xFFB5C2, 0xFFD8A8, 0xFFF3B0, 0xB9F3C9, 0xA8D8FF, 0xD5B8FF)
GEODE_COLOURS("synthwave", "Synthwave", BuiltIn, Mirror, Oklab, 0x2D00F7, 0x6A00F4, 0xB100E8, 0xF20089, 0xFF6D00, 0xFFD500)
GEODE_COLOURS("noir", "Noir", BuiltIn, Mirror, Oklab, 0x0B0B0D, 0x3A3A40, 0x8A8A92, 0xE6E6EA)
GEODE_COLOURS("moss", "Moss", BuiltIn, Mirror, Oklab, 0x1E2D24, 0x3C5A3A, 0x7A9A54, 0xC5D86D, 0xF4F1BB)
GEODE_COLOURS("reef", "Reef", BuiltIn, Mirror, Oklab, 0x003049, 0xD62828, 0xF77F00, 0xFCBF49, 0xEAE2B7)
GEODE_COLOURS("lavender", "Lavender", BuiltIn, Mirror, Oklab, 0x2E1A47, 0x5B3E96, 0x9C7AD6, 0xD7C6F5)
