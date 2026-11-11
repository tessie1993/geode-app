#!/usr/bin/env bash
# Builds the raster brand assets from the neon render and the splash video (2026-11-01).
#
#   tools/brand_raster.sh [path/to/splash-source.mp4]
#
# Needs ImageMagick (convert) and ffmpeg with libx264. Without a video argument only the images
# are rebuilt; the source video (10 s, 720x1280, 24 fps, Gemini) is not kept in the repo, so the
# cut below is the record of how res/raw/splash_intro.mp4 was made.
#
# Icon geometry. The eye spans 87% of the render's width. It is drawn at 75% of the canvas, so
# it is 65% of the canvas wide. That keeps it inside the adaptive icon's 72dp visible area
# (72/108 = 67%) and inside the system splash's 192dp icon circle (192/288 = 67%). The rest of
# the canvas is VOID, the render's own background colour. Splash and BootIntro use VOID too,
# so the icon's edge never shows.
set -euo pipefail
cd "$(dirname "$0")/.."

SRC=docs/brand/flow/icon-neon.jpg
VOID='#06050B'   # @color/brand_void: the render's background, sampled from its edges
RES=app/src/main/res

art() { # art <canvas px> <out>
  local size=$1 out=$2 inner feather
  inner=$(( size * 3 / 4 ))
  feather=$(( inner / 9 + 1 ))
  convert "$SRC" -resize "${inner}x${inner}" -alpha set -virtual-pixel transparent \
    -channel A -blur "0x${feather}" -level 50%,100% -evaluate pow 1.6 +channel \
    \( -size "${size}x${size}" "xc:${VOID}" \) +swap -gravity center -composite \
    -alpha off -define webp:lossless=false -quality 90 "$out"
}

# Launcher foreground (108dp) at every density. It is opaque; the background layer sits under it.
for d in mdpi:108 hdpi:162 xhdpi:216 xxhdpi:324 xxxhdpi:432; do
  mkdir -p "$RES/drawable-${d%%:*}"
  art "${d##*:}" "$RES/drawable-${d%%:*}/ic_launcher_foreground.webp"
done

# System splash and BootIntro: drawn at 288dp, so 1152 px covers 4x screens.
mkdir -p "$RES/drawable-nodpi"
art 1152 "$RES/drawable-nodpi/splash_mark.webp"

# Play Store icon: 512x512 32-bit PNG, full bleed. Play rounds the corners itself.
convert "$SRC" -resize 512x512 -alpha set PNG32:docs/brand/beatrealms-icon-512.png

# Splash video: silent, 4.54 s, with every key moment kept and the slow stretches sped up. In
# 24 fps source frames:
#   29-148  spark, bloom, burst, lids forming  2x
#   149-201 swirl iris, amber hexagon          3x
#   202-215 the white flash                    2x (one flash, inside WCAG 2.3.1)
#   216-239 the finished eye                   1x (the wordmark fades in here)
# Whole-frame steps (2x, 3x) keep the motion even.
if [ $# -ge 1 ]; then
  sel='between(n,29,148)*not(mod(n,2))+between(n,149,201)*not(mod(n-149,3))'
  sel+='+between(n,202,215)*not(mod(n-202,2))+between(n,216,239)'
  mkdir -p "$RES/raw"
  ffmpeg -v error -y -i "$1" -an -vf "select='$sel',setpts=N/24/TB" -r 24 \
    -c:v libx264 -preset veryslow -crf 25 -profile:v high -level 3.1 -pix_fmt yuv420p \
    -x264-params keyint=48 -movflags +faststart "$RES/raw/splash_intro.mp4"
fi
