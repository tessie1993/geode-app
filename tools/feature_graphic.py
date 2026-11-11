"""Writes the Play feature graphic, docs/brand/beatrealms-feature-graphic.svg (1024 x 500).

  python3 tools/feature_graphic.py

The wordmark is the outlined one the system splash uses (res/drawable/splash_wordmark.xml,
made by tools/wordmark.py), so no font is needed. The eye is the launcher icon's art
(docs/brand/flow/icon-neon.jpg). Everything else is drawn here. Render the PNG with
docs/brand/README.md › Regenerating.

Play's rules (support.google.com/googleplay/android-developer/answer/9866151): 1024 x 500,
no alpha, key elements away from the edges, and no pure black, white or dark grey background,
so the ground is indigo, not the app's black.
"""
import math
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WORDMARK = ROOT / "app/src/main/res/drawable/splash_wordmark.xml"
OUT = ROOT / "docs/brand/beatrealms-feature-graphic.svg"
W, H = 1024, 500

xml = WORDMARK.read_text()
paths = re.findall(r'android:fillColor="(#[0-9A-Fa-f]{6})"\s+android:pathData="([^"]+)"', xml)
nums = [float(n) for _, d in paths for n in re.findall(r"-?\d+(?:\.\d+)?", d)]
xs, ys = nums[0::2], nums[1::2]
x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)

# The wordmark, 600 px wide, centred, under the eye.
word_w = 600
s = word_w / (x1 - x0)
word_h = (y1 - y0) * s
wx, wy = (W - word_w) / 2, 318
word = "".join(f'<path fill="{c}" d="{d}"/>' for c, d in paths)
word_g = f'<g transform="translate({wx - x0 * s:.2f},{wy - y0 * s:.2f}) scale({s:.4f})">{word}</g>'

# A spectrum across the whole width, quiet, symmetric about the centre: background only.
bars = []
n = 64
for i in range(n):
    t = abs((i + 0.5) / n - 0.5) * 2
    h = 30 + 150 * (0.5 + 0.5 * math.cos(i * 0.9)) * (0.35 + 0.65 * t) * (0.6 + 0.4 * math.sin(i * 0.37) ** 2)
    x = i * W / n + 3
    bars.append(f'<rect x="{x:.1f}" y="{H - h:.1f}" width="{W / n - 6:.1f}" height="{h:.1f}" rx="4"/>')

# Two sine traces, like the eye's lids, running the width.
def wave(amp, freq, phase, yc):
    pts = [f"{x},{yc + amp * math.sin(x / W * freq * 2 * math.pi + phase):.1f}" for x in range(0, W + 1, 8)]
    return "M" + " L".join(pts)

svg = f'''<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="{W}" height="{H}" viewBox="0 0 {W} {H}">
<defs>
  <radialGradient id="ground" cx="512" cy="190" r="620" gradientUnits="userSpaceOnUse">
    <stop offset="0" stop-color="#3B2183"/>
    <stop offset="0.45" stop-color="#22125A"/>
    <stop offset="1" stop-color="#160C3C"/>
  </radialGradient>
  <linearGradient id="bars" x1="0" y1="0" x2="1" y2="0">
    <stop offset="0" stop-color="#9CC7FF"/>
    <stop offset="0.5" stop-color="#7C4DFF"/>
    <stop offset="1" stop-color="#9CC7FF"/>
  </linearGradient>
  <linearGradient id="fade" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#fff" stop-opacity="0"/>
    <stop offset="1" stop-color="#fff" stop-opacity="1"/>
  </linearGradient>
  <mask id="barsMask"><rect width="{W}" height="{H}" fill="url(#fade)"/></mask>
  <radialGradient id="feather" cx="0.5" cy="0.5" r="0.5">
    <stop offset="0.62" stop-color="#fff"/>
    <stop offset="1" stop-color="#000"/>
  </radialGradient>
  <mask id="eyeMask" maskContentUnits="objectBoundingBox"><rect width="1" height="1" fill="url(#feather)"/></mask>
  <filter id="glow" x="-20%" y="-50%" width="140%" height="200%"><feGaussianBlur stdDeviation="9"/></filter>
</defs>
<rect width="{W}" height="{H}" fill="url(#ground)"/>
<g fill="url(#bars)" opacity="0.22" mask="url(#barsMask)">{"".join(bars)}</g>
<g fill="none" stroke-linecap="round">
  <path d="{wave(18, 2.5, 0.4, 150)}" stroke="#C792EA" stroke-opacity="0.18" stroke-width="2"/>
  <path d="{wave(14, 3.5, 2.1, 168)}" stroke="#89DDFF" stroke-opacity="0.14" stroke-width="2"/>
</g>
<image xlink:href="flow/icon-neon.jpg" href="flow/icon-neon.jpg" x="{W / 2 - 160}" y="-14" width="320" height="320" mask="url(#eyeMask)" style="mix-blend-mode:screen"/>
<g filter="url(#glow)" opacity="0.55">{word_g}</g>
{word_g}
</svg>
'''
OUT.write_text(svg)
print(f"wrote {OUT.relative_to(ROOT)}; wordmark {word_w} x {word_h:.0f} at y {wy}")
