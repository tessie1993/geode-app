"""Outlines the BeatRealms 3D wordmark: "BeatRealms" in brush script over a squared "3d".

  pip install uharfbuzz fonttools skia-pathops
  python tools/wordmark.py Pacifico-Regular.ttf "Orbitron[wght].ttf" <preview dir> \
      app/src/main/res/drawable/splash_wordmark.xml

Fonts, both from github.com/google/fonts and both SIL Open Font License 1.1 (THIRD_PARTY_NOTICES):
  - Pacifico Regular, Version 3.001 (ofl/pacifico), the script: round, connected, a looped
    capital B and R, the closest open font to the owner's inspiration picture of 2026-11-11.
  - Orbitron Black (wght 900), Version 2.001 (ofl/orbitron/Orbitron[wght].ttf), the "3d":
    squared, blocky, the picture's chrome tag. "Orbitron" is a Reserved Font Name; no font is
    shipped or named as one, only the two glyphs' outlines inside the logo.
Only outlines ship, never a font. Each word's glyphs are unioned (skia-pathops) so the rim stroke
draws the outside of the joined script, not the seams where its letters overlap.

The look is the picture's glass, kept to what a VectorDrawable can draw (gradients, no blur): a
deep extrusion under each word, the script filled cyan to violet, a white sheen fading down its
top half and a light rim; the "3d" in banded chrome. Writes the system splash's branding image
(200x80dp, which the system stretches to that box, so the image keeps that shape and centres the
lockup in it); ui/theme/Wordmark draws the same drawable. Also writes its SVG twin at 1x and 3x
into the preview dir, to rasterise and check (cairosvg).
"""
import sys

import uharfbuzz as hb
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
import pathops

script_path, square_path, out_dir, drawable = sys.argv[1:5]

VOID = "#06050B"
CYAN, BLUE, VIOLET = "#4FD8FF", "#7FA6FF", "#B06CFF"
SCRIPT_DEPTH, CHROME_DEPTH = "#2A1460", "#1A1C33"
RIM = "#D8F6FF"
# (offset, colour) stops. The chrome's hard band at the middle is its horizon.
SCRIPT_FILL = [(0.0, CYAN), (0.5, BLUE), (1.0, VIOLET)]
SHEEN = [(0.0, "#A0FFFFFF"), (0.55, "#00FFFFFF")]
CHROME = [(0.0, "#FFFFFF"), (0.46, "#C7D2EE"), (0.5, "#6C7598"), (0.62, "#9AA4C6"), (1.0, "#F1EEFF")]

# Viewport 200x80. The lockup is laid out in these units and centred.
SCRIPT_WIDTH = 188.0  # "BeatRealms" is sized to this width, leaving a margin for the rim
TAG_HEIGHT = 0.58  # the "3d", against the script's height, as in the picture
GAP = 2.5  # between the script's bottom and the tag's top, in units
DEPTH = 1.4  # extrusion offset, units, straight down
RIM_WIDTH = 0.55
TAG_WGHT = 900


def outline(font_path, text, size, wght=None):
    """The text's glyphs as one unioned pathops.Path in font units scaled to `size`, y up."""
    face = hb.Face(hb.Blob.from_file_path(font_path))
    font = hb.Font(face)
    if wght is not None:
        font.set_variations({"wght": wght})
    buf = hb.Buffer()
    buf.add_str(text)
    buf.guess_segment_properties()
    hb.shape(font, buf, {"kern": True, "liga": True})
    s, x = size / face.upem, 0.0
    path = pathops.Path()
    for info, pos in zip(buf.glyph_infos, buf.glyph_positions):
        glyph = pathops.Path()
        font.draw_glyph_with_pen(info.codepoint, TransformPen(glyph.getPen(), (s, 0, 0, s, x + pos.x_offset * s, pos.y_offset * s)))
        path = pathops.op(path, glyph, pathops.PathOp.UNION)
        x += pos.x_advance * s
    return path


def bounds(path):
    return path.bounds  # xmin, ymin, xmax, ymax (y up)


def svg_d(path, dx, dy):
    """Path data flipped to y down and moved by (dx, dy)."""
    pen = SVGPathPen(None, ntos=lambda v: f"{v:.2f}".rstrip("0").rstrip("."))
    path.draw(TransformPen(pen, (1, 0, 0, -1, dx, dy)))
    return pen.getCommands()


nominal = outline(script_path, "BeatRealms", 100.0)
script = outline(script_path, "BeatRealms", 100.0 * SCRIPT_WIDTH / (nominal.bounds[2] - nominal.bounds[0]))
nominal = outline(square_path, "3d", 100.0, TAG_WGHT)
tag_size = 100.0 * TAG_HEIGHT * (script.bounds[3] - script.bounds[1]) / (nominal.bounds[3] - nominal.bounds[1])
tag = outline(square_path, "3d", tag_size, TAG_WGHT)
sx0, sy0, sx1, sy1 = bounds(script)
tx0, ty0, tx1, ty1 = bounds(tag)
s_w, s_h, t_w, t_h = sx1 - sx0, sy1 - sy0, tx1 - tx0, ty1 - ty0
# "BeatRealms" has no descender, so the script's box ends at its baseline and GAP counts from it.
total_h = s_h + GAP + t_h + DEPTH
W, H = 200.0, 80.0
top = (H - total_h) / 2
# y down: a font-space y maps to (top + sy1 - y) for the script.
s_dx, s_dy = (W - s_w) / 2 - sx0, top + sy1
t_top = top + s_h + GAP
t_dx, t_dy = (W - t_w) / 2 - tx0, t_top + ty1
script_d, tag_d = svg_d(script, s_dx, s_dy), svg_d(tag, t_dx, t_dy)
script_depth_d, tag_depth_d = svg_d(script, s_dx, s_dy + DEPTH), svg_d(tag, t_dx, t_dy + DEPTH)
s_box = (s_dx + sx0, top, s_dx + sx1, top + s_h)
t_box = (t_dx + tx0, t_top, t_dx + tx1, t_top + t_h)

# Layers: (path data, fill, stroke). A fill is a colour or ("h"|"v", box, stops).
LAYERS = [
    (script_depth_d, SCRIPT_DEPTH, None),
    (tag_depth_d, CHROME_DEPTH, None),
    (script_d, ("h", s_box, SCRIPT_FILL), None),
    (script_d, ("v", (s_box[0], s_box[1], s_box[2], s_box[1] + s_h * 0.6), SHEEN), None),
    (script_d, None, (RIM, "CC")),
    (tag_d, ("v", t_box, CHROME), None),
    (tag_d, None, ("#FFFFFF", "99")),
]


def gradient_ends(kind, box):
    x0, y0, x1, y1 = box
    return (x0, y0, x1, y0) if kind == "h" else (x0, y0, x0, y1)


def vector_xml():
    out = ['<?xml version="1.0" encoding="utf-8"?>',
           '<!-- The BeatRealms 3D wordmark: "BeatRealms" outlined from Pacifico over "3d" from Orbitron',
           '     Black (both SIL OFL 1.1), by tools/wordmark.py. Do not edit by hand. The system splash',
           '     can draw no text, only this 200x80dp image; ui/theme/Wordmark draws it too. -->',
           '<vector xmlns:android="http://schemas.android.com/apk/res/android"',
           '    xmlns:aapt="http://schemas.android.com/aapt"',
           '    android:width="200dp"', '    android:height="80dp"',
           '    android:viewportWidth="200"', '    android:viewportHeight="80">']
    for d, fill, stroke in LAYERS:
        out.append('    <path')
        if isinstance(fill, str):
            out.append(f'        android:fillColor="{fill}"')
        if stroke:
            colour, alpha = stroke
            out += [f'        android:strokeColor="#{alpha}{colour[1:]}"',
                    f'        android:strokeWidth="{RIM_WIDTH}"',
                    '        android:strokeLineJoin="round"']
        out.append(f'        android:pathData="{d}"' + (' />' if not isinstance(fill, tuple) else '>'))
        if isinstance(fill, tuple):
            kind, box, stops = fill
            ax, ay, bx, by = gradient_ends(kind, box)
            out += ['        <aapt:attr name="android:fillColor">',
                    f'            <gradient android:type="linear" android:startX="{ax:.2f}" android:startY="{ay:.2f}"'
                    f' android:endX="{bx:.2f}" android:endY="{by:.2f}">']
            out += [f'                <item android:offset="{o}" android:color="{c}" />' for o, c in stops]
            out += ['            </gradient>', '        </aapt:attr>', '    </path>']
    out.append('</vector>')
    return "\n".join(out) + "\n"


def svg_colour(c):
    """SVG colour and opacity from #RRGGBB or #AARRGGBB."""
    if len(c) == 9:
        return "#" + c[3:], int(c[1:3], 16) / 255
    return c, 1.0


def svg_doc(w, h, scale, background=True):
    defs, body = [], []
    for i, (d, fill, stroke) in enumerate(LAYERS):
        attrs = []
        if isinstance(fill, str):
            attrs.append(f'fill="{fill}"')
        elif fill:
            kind, box, stops = fill
            ax, ay, bx, by = gradient_ends(kind, box)
            items = "".join(f'<stop offset="{o}" stop-color="{svg_colour(c)[0]}" stop-opacity="{svg_colour(c)[1]:.3f}"/>' for o, c in stops)
            defs.append(f'<linearGradient id="g{i}" gradientUnits="userSpaceOnUse" x1="{ax}" y1="{ay}" x2="{bx}" y2="{by}">{items}</linearGradient>')
            attrs.append(f'fill="url(#g{i})"')
        else:
            attrs.append('fill="none"')
        if stroke:
            colour, alpha = stroke
            attrs.append(f'stroke="{colour}" stroke-opacity="{int(alpha, 16) / 255:.3f}" stroke-width="{RIM_WIDTH}" stroke-linejoin="round"')
        body.append(f'<path d="{d}" {" ".join(attrs)}/>')
    bg = f'<rect width="{W}" height="{H}" fill="{VOID}"/>' if background else ""
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w * scale}" height="{h * scale}" viewBox="0 0 {w} {h}">'
            f'<defs>{"".join(defs)}</defs>{bg}{"".join(body)}</svg>\n')


open(drawable, "w").write(vector_xml())
open(f"{out_dir}/wordmark.svg", "w").write(svg_doc(W, H, 1))
open(f"{out_dir}/wordmark@3x.svg", "w").write(svg_doc(W, H, 3))
print(f"script {s_w:.1f}x{s_h:.1f}, tag {t_w:.1f}x{t_h:.1f}, lockup height {total_h:.1f} of {H:.0f}")
