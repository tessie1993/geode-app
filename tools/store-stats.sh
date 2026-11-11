#!/usr/bin/env bash
# Counts the numbers the Play listing quotes, from the code and the catalogue, so a claim can be
# rechecked before each release:   tools/store-stats.sh
# docs/store/stats.md is this script's output with the reading of each line.
set -euo pipefail
cd "$(dirname "$0")/.."
cat=core/tests/catalogue.txt   # the native catalogue's text, pinned by core/tests/catalogue_test.cpp

styles=$(grep -c '^style|' "$cat")
shader=$(grep -c '^style|[^|]*|SHADER|' "$cat")
families=$(grep -c '^family|' "$cat")
family_styles=$(grep '^family|' "$cat" | cut -d'|' -f5 | tr ',' '\n' | grep -c .)
neon_pro=$(grep -A2 'val neonProIds' app/src/main/java/dev/geode/pro/FreeTier.kt | grep -o 'SceneIds\.[A-Z_]*' | wc -l)
free=$((shader - (family_styles - families) - neon_pro))
loose=$((shader - family_styles))
generic=$(awk '/genericLooks\(\) \{/,/return kLooks/' core/viz/CataloguePresets.cpp | grep -c '^        {"')
variants=$(awk '/variants\(\) \{/,/return kVariants/' core/viz/CataloguePresets.cpp | grep -c '^        {"')
milk=$(find app/src/main/assets/milk -name '*.milk' | wc -l)
val() { grep -E "^\s*$1\s*=" "$2" | head -1 | sed -E 's/.*=\s*//'; }

echo "styles                      $styles"
echo "  by kind                   $(grep '^style|' "$cat" | cut -d'|' -f3 | sort | uniq -c | sort -rn | awk '{printf "%s %s, ", $2, $1}' | sed 's/, $//')"
echo "style families              $families ($family_styles styles, $((family_styles / families)) each)"
echo "free styles                 $free (shader $shader - family non-openers $((family_styles - families)) - neon Pro $neon_pro)"
echo "Pro-only styles             $((styles - free))"
echo "bundled looks (presets)     $((loose * generic + variants)) ($generic generic x $loose loose shader styles + $variants hand-tuned)"
echo "bundled MilkDrop presets    $milk"
echo "Customize categories        $(awk '/^enum class CustomizeCategory/,/;/' app/src/main/java/dev/geode/ui/*.kt | grep -cE '^\s+[A-Z_]+[(,]' || true)"
echo "export resolutions          $(awk '/^enum class ExportQuality/,/^}/' app/src/main/java/dev/geode/export/VideoExporter.kt | grep -oE '^\s+[A-Z0-9]+\(' | tr -d ' (' | paste -sd' ')"
echo "export codecs               $(awk '/^enum class ExportCodec/,/;/' app/src/main/java/dev/geode/export/VideoExporter.kt | grep -oE '^\s+[A-Z0-9]+\(' | tr -d ' (' | paste -sd' ')"
echo "export frame rates          $(grep -o 'EXPORT_FPS_OPTIONS: List<Int> = listOf([^)]*)' app/src/main/java/dev/geode/data/ExportPrefs.kt | grep -o '([^)]*)')"
echo "export aspect ratios        $(awk '/^enum class ExportRatio/,/^}/' app/src/main/java/dev/geode/export/VideoExporter.kt | grep -oE '"[0-9]+:[0-9]+"' | tr -d '"' | paste -sd' ')"
echo "export destinations         $(awk '/^enum class ExportDestination/,/;/' app/src/main/java/dev/geode/data/ExportPrefs.kt | grep -oE '^\s+[A-Z]+,' | tr -d ' ,' | paste -sd' ')"
echo "free export cap             $(grep -oE 'FREE_MAX_SHORT_SIDE[^=]*= *[0-9_]+' app/src/main/java/dev/geode/pro/FreeTier.kt | grep -oE '[0-9_]+$')p, $(grep -oE 'FREE_MAX_EXPORT_MS[^=]*= *[0-9_L* ]+' app/src/main/java/dev/geode/pro/FreeTier.kt | sed -E 's/.*= *//') ms"
echo "minSdk / targetSdk          $(grep -oE 'minSdk = [0-9]+' app/build.gradle.kts | grep -oE '[0-9]+') / $(grep -oE 'targetSdk = [0-9]+' app/build.gradle.kts | grep -oE '[0-9]+')"
echo "version base / code base    $(val geode.versionBase gradle.properties) / $(val geode.versionCodeBase gradle.properties)"
echo "permissions                 $(grep -c '<uses-permission' app/src/main/AndroidManifest.xml)"
