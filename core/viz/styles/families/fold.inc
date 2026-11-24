// Fractals made by folding space, each drawn by its own march: glowing threads, cone-traced crusts, 4D
// sections, alternating hybrids and sign-probed surfaces. Its library is lib_fold; each style also includes
// one of five lib_fold_march_* (glow, cone, slice, pair, probe).
GEODE_FAMILY_BEGIN(fold, "fold_", "lib_fold", Fractals)
GEODE_STYLE(fold, "fold_lace", March | Form | Formula | Escape)
GEODE_STYLE(fold, "fold_nerve", March | Form | Formula | Escape)
GEODE_STYLE(fold, "fold_spire", March | Form | Formula | Shadow | Escape)
GEODE_STYLE(fold, "fold_vault", March | Form | Formula | Shadow | Escape)
GEODE_STYLE(fold, "fold_onion", March | Form | Formula | Escape)
GEODE_STYLE(fold, "fold_strata", March | Form | Formula | Escape)
GEODE_STYLE(fold, "fold_twin", March | Form | Formula | Shadow | Escape)
GEODE_STYLE(fold, "fold_scaffold", March | Form | Formula | Shadow | Escape)
GEODE_STYLE(fold, "fold_thorn", March | Form | Formula | Shadow | Escape)
GEODE_STYLE(fold, "fold_quartz", March | Form | Formula | Shadow | Escape)
GEODE_FAMILY_END(fold)
