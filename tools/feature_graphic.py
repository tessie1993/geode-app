"""Writes the Play feature graphic, docs/brand/beatrealms-feature-graphic.svg (1024 x 500).

  python3 tools/feature_graphic.py

The wordmark is the one the system splash and the app draw (res/drawable/splash_wordmark.xml,
made by tools/wordmark.py), read from that file, so the graphic follows it and no font is needed. The eye is the launcher icon's art
(docs/brand/flow/icon-neon.jpg). Everything else is drawn here. Render the PNG with
docs/brand/README.md › Regenerating.

Play's rules (support.google.com/googleplay/android-developer/answer/9866151): 1024 x 500,
no alpha, key elements away from the edges, and no pure black, white or dark grey background,
so the ground is indigo, not the app's black.
"""
import math
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WORDMARK = ROOT / "app/src/main/res/drawable/splash_wordmark.xml"
OUT = ROOT / "docs/brand/beatrealms-feature-graphic.svg"
W, H = 1024, 500
A = "{http://schemas.android.com/apk/res/android}"
AAPT = "{http://schemas.android.com/aapt}"


def paint(colour):
    """An Android #RRGGBB or #AARRGGBB colour as SVG (colour, opacity)."""
    h = colour.lstrip("#")
    if len(h) == 8:
        return "#" + h[2:], int(h[:2], 16) / 255
    return "#" + h, 1.0


def wordmark_svg():
    """The vector drawable's paths as SVG: flat fills, linear gradient fills and strokes."""
    root = ET.parse(WORDMARK).getroot()
    vw, vh = float(root.get(A + "viewportWidth")), float(root.get(A + "viewportHeight"))
    defs, body = [], []
    for n, path in enumerate(root.iter("path")):
        d = path.get(A + "pathData")
        attrs = ['fill="none"']
        grad = path.find(f"{AAPT}attr/gradient")
        if grad is not None:
            gid = f"wm{n}"
            stops = "".join(
                '<stop offset="{}" stop-color="{}" stop-opacity="{:.3f}"/>'.format(
                    item.get(A + "offset"), *paint(item.get(A + "color"))
                )
                for item in grad.iter("item")
            )
            defs.append(
                f'<linearGradient id="{gid}" gradientUnits="userSpaceOnUse" '
                f'x1="{grad.get(A + "startX")}" y1="{grad.get(A + "startY")}" '
                f'x2="{grad.get(A + "endX")}" y2="{grad.get(A + "endY")}">{stops}</linearGradient>'
            )
            attrs = [f'fill="url(#{gid})"']
        elif path.get(A + "fillColor"):
            c, o = paint(path.get(A + "fillColor"))
            attrs = [f'fill="{c}" fill-opacity="{o:.3f}"']
        if path.get(A + "strokeColor"):
            c, o = paint(path.get(A + "strokeColor"))
            attrs.append(
                f'stroke="{c}" stroke-opacity="{o:.3f}" stroke-width="{path.get(A + "strokeWidth")}" '
                f'stroke-linejoin="{path.get(A + "strokeLineJoin", "miter")}"'
            )
        body.append(f'<path {" ".join(attrs)} d="{d}"/>')
    return vw, vh, "".join(defs), "".join(body)


# The eye on the left, the wordmark on the right, the pair centred and inside the middle 80 %.
vw, vh, word_defs, word = wordmark_svg()
eye = 330
word_w = 500
s = word_w / vw
word_h = vh * s
gap = 0
ex = (W - eye - gap - word_w) / 2
wx, wy = ex + eye + gap, (H - word_h) / 2 - 6
word_g = f'<g transform="translate({wx:.2f},{wy:.2f}) scale({s:.4f})">{word}</g>'

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
  {word_defs}
  <radialGradient id="ground" cx="512" cy="230" r="620" gradientUnits="userSpaceOnUse">
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
<image xlink:href="flow/icon-neon.jpg" href="flow/icon-neon.jpg" x="{ex:.1f}" y="{(H - eye) / 2 - 10:.1f}" width="{eye}" height="{eye}" mask="url(#eyeMask)" style="mix-blend-mode:screen"/>
<g filter="url(#glow)" opacity="0.45">{word_g}</g>
{word_g}
</svg>
'''
OUT.write_text(svg)
print(f"wrote {OUT.relative_to(ROOT)}; wordmark {word_w} x {word_h:.0f} at {wx:.0f},{wy:.0f}")
