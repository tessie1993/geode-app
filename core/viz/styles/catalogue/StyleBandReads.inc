// Shader and Life styles that read no bass or no treble envelope of the sound frame. Bass gain and Treble gain
// are shown only where the style reads that band (catalogue::reaches BassBand, TrebleBand, BandGains), so a
// control on screen moves something. They once reached every one of these through the motion field's
// zoom and turbulence terms, which are gone. style_reads_test holds every style not listed here to reading the
// band; a style that gains a read of one is deleted from its list, and a listed one is simply hidden.
// Included by Catalogue.cpp with an X(id) macro per list.
#define GEODE_NO_BASS_STYLES(X) \
    X("arbor_bonsai") X("arbor_canopy") X("arbor_coral") X("arbor_frost") X("arbor_rosette") X("arbor_sakura") \
    X("arbor_synapse") X("bead_vortex") X("bulb_contour") X("bulb_glow") X("bulb_hybrid") X("bulb_ink") \
    X("bulb_julia") X("bulb_lotus") X("chrys_fall") X("chrys_glass") X("chrys_ink") X("chrys_kaleido") \
    X("chrys_phyllo") X("chrys_tunnel") X("crystal_canyon") X("dive_cycle") X("dive_helix") X("dive_jewel") \
    X("dive_lattice") X("dive_mandala") X("dive_mushroom") X("dive_neon") X("dive_orbit") X("fractal_dive") \
    X("fractal_tunnel") X("glow_vortex") X("grid") X("gyroid_cavern") X("hex_hive") X("hexgrid") \
    X("lucid_disc") X("lucid_hive") X("lucid_tunnel") X("lucid_vortex") X("mandel") X("metaballs") \
    X("nebula_cluster") X("nebula_spiral") X("nebula_veil") X("particle_tunnel") X("ring") X("scope") \
    X("terra_terraces") X("terra_wire") X("tunnel") X("voronoi") X("warp")
#define GEODE_NO_TREBLE_STYLES(X) \
    X("arbor_bolt") X("arbor_bonsai") X("arbor_canopy") X("arbor_coral") X("arbor_grove") X("arbor_roots") \
    X("arbor_rosette") X("arbor_spire") X("arbor_synapse") X("arbor_willow") X("aurora_ocean") \
    X("bead_vortex") X("black_hole") X("bulb_cavern") X("bulb_chrome") X("bulb_classic") X("bulb_contour") \
    X("bulb_glow") X("bulb_hybrid") X("bulb_ink") X("bulb_shell") X("bulb_slice") X("chrome_nebula") \
    X("chrys_bloom") X("chrys_corolla") X("chrys_fall") X("chrys_glass") X("chrys_ink") X("chrys_kaleido") \
    X("chrys_lotus") X("chrys_meadow") X("chrys_neon") X("chrys_phyllo") X("chrys_tunnel") X("crystal_canyon") \
    X("dive_accordion") X("dive_cycle") X("dive_gyro") X("dive_helix") X("dive_jewel") X("dive_lattice") \
    X("dive_mandala") X("dive_mushroom") X("dive_neon") X("dive_orbit") X("dive_serpent") X("dive_show") \
    X("flame_aurora") X("flame_helix") X("flame_knot") X("flame_lotus") X("flame_nova") X("flame_orb") \
    X("flame_spiral") X("flame_star") X("flame_veil") X("flame_web") X("fold_lace") X("fold_nerve") \
    X("fold_onion") X("fold_quartz") X("fold_scaffold") X("fold_spire") X("fold_strata") X("fold_thorn") \
    X("fold_twin") X("fold_vault") X("fractal_dive") X("fractal_tunnel") X("glass_bubbles") X("glass_caustic") \
    X("glass_cubes") X("glass_drops") X("glass_gem") X("glass_ice") X("glass_knot") X("glass_lens") \
    X("glass_marble") X("glass_orb") X("glass_pool") X("glass_prism") X("glow_vortex") X("grid") X("hex_hive") \
    X("hollow_bone") X("hollow_cave") X("hollow_coral") X("hollow_hive") X("hollow_neuron") X("hollow_ribs") \
    X("hollow_roots") X("hollow_shell") X("hollow_sinew") X("hollow_throat") X("jelly_abyss") X("kaleido") \
    X("life_bug") X("life_coral") X("life_gyre") X("life_helix") X("life_hydro") X("life_labyrinth") \
    X("life_mitosis") X("life_orbium") X("life_pulsar") X("life_worms") X("liss") X("loom_basket") \
    X("loom_braid") X("loom_cloth") X("loom_cocoon") X("loom_curtain") X("loom_net") X("loom_rope") \
    X("loom_sail") X("loom_spokes") X("loom_spool") X("lucid_bulge") X("lucid_hive") X("lucid_ink") \
    X("lucid_lattice") X("lucid_mandala") X("lucid_petal") X("lucid_tunnel") X("lucid_vortex") \
    X("lucid_weave") X("mandel") X("mandelbox_cathedral") X("menger_core") X("metaballs") X("nebula_cluster") \
    X("nebula_dust") X("nebula_flight") X("nebula_horizon") X("nebula_nova") X("nebula_pillars") \
    X("nebula_pulsar") X("nebula_ring") X("nebula_spiral") X("nebula_storm") X("nebula_web") \
    X("neon_chrome_sea") X("opal_bubbles") X("opal_cells") X("opal_domes") X("opal_drop") X("opal_lava") \
    X("opal_melt") X("opal_nautilus") X("opal_pearls") X("opal_ribbon") X("opal_rings") X("outrun_drive") \
    X("particle_tunnel") X("petal_bells") X("petal_buds") X("petal_drift") X("petal_fern") X("petal_garland") \
    X("petal_gates") X("petal_rose") X("petal_scales") X("petal_seed") X("petal_stair") X("plasma") \
    X("ribbon_storm") X("ring") X("ringed_planet") X("ripples") X("starfield") X("terra_canyon") \
    X("terra_dunes") X("terra_glacier") X("terra_mesa") X("terra_moon") X("terra_ridge") X("terra_terraces") \
    X("terra_topo") X("terra_volcano") X("terra_wire") X("truchet_arcs") X("truchet_flow") X("truchet_hex") \
    X("truchet_knot") X("truchet_maze") X("truchet_neon") X("truchet_quad") X("truchet_relief") \
    X("truchet_tri") X("truchet_tunnel") X("voronoi") X("warp")
