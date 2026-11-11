"""Outlines the BeatRealms 3D wordmark (ui/theme/Wordmark) from Roboto.

  pip install uharfbuzz
  python tools/wordmark.py Roboto[wdth,wght].ttf <preview dir> app/src/main/res/drawable/splash_wordmark.xml

Writes the system splash's branding image and preview SVGs of both lockups. Roboto is the
variable font from github.com/google/fonts (ofl/roboto); only outlines ship, never the font.
"""
import sys
import uharfbuzz as hb

font_path, out_dir, drawable = sys.argv[1], sys.argv[2], sys.argv[3]
face = hb.Face(hb.Blob.from_file_path(font_path))
UPEM = face.upem
WHITE, ACCENT, VOID = "#FFFFFF", "#9CC7FF", "#06050B"
BOLD, LIGHT = 700, 300
# (text, weight, tracking em, colour)
RUNS = [("Beat", BOLD, 0.0, WHITE), ("Realms ", LIGHT, 0.0, WHITE), ("3D", BOLD, 0.02, ACCENT)]


class Pen:
    def __init__(self, sx, ox, oy):
        self.s, self.ox, self.oy, self.d = sx, ox, oy, []

    def p(self, x, y):
        return f"{self.ox + x * self.s:.2f},{self.oy - y * self.s:.2f}"

    def moveTo(self, pt): self.d.append("M" + self.p(*pt))
    def lineTo(self, pt): self.d.append("L" + self.p(*pt))
    def qCurveTo(self, a, pt): self.d.append("Q" + self.p(*a) + " " + self.p(*pt))
    def curveTo(self, a, b, pt): self.d.append("C" + self.p(*a) + " " + self.p(*b) + " " + self.p(*pt))
    def closePath(self): self.d.append("Z")


def layout(runs, size, x0, baseline):
    """Returns ({colour: path data}, end x). Tracking follows every glyph, as Compose's does."""
    paths, x, s = {}, x0, size / UPEM
    for text, wght, track, colour in runs:
        font = hb.Font(face)
        font.set_variations({"wght": wght})
        buf = hb.Buffer()
        buf.add_str(text)
        buf.guess_segment_properties()
        hb.shape(font, buf, {"kern": True})
        for info, pos in zip(buf.glyph_infos, buf.glyph_positions):
            pen = Pen(s, x + pos.x_offset * s, baseline)
            font.draw_glyph_with_pen(info.codepoint, pen)
            paths.setdefault(colour, []).append("".join(pen.d))
            x += pos.x_advance * s + track * size
    return {c: " ".join(d) for c, d in paths.items()}, x


def width(runs, size):
    return layout(runs, size, 0, 0)[1] - runs[-1][2] * size


def svg(w, h, body):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">'
            f'<rect width="{w}" height="{h}" fill="{VOID}"/>{body}</svg>\n')


def paths_svg(paths):
    return "".join(f'<path d="{d}" fill="{c}"/>' for c, d in paths.items())


def one_line(size, cx, baseline):
    w = width(RUNS, size)
    return layout(RUNS, size, cx - w / 2, baseline)[0], w


def stacked(size, cx, baseline):
    top = [RUNS[0], ("Realms", LIGHT, 0.0, WHITE)]
    w = width(top, size)
    body = paths_svg(layout(top, size, cx - w / 2, baseline)[0])
    small = size * 0.62
    tag = [("3D", BOLD, 0.08, ACCENT)]
    tw = width(tag, small)
    base2 = baseline + size * 0.92
    body += paths_svg(layout(tag, small, cx - tw / 2, base2)[0])
    rule_y, gap = base2 - small * 0.355, small * 0.45
    for a, b in ((cx - w / 2, cx - tw / 2 - gap), (cx + tw / 2 + gap, cx + w / 2)):
        body += f'<rect x="{a:.2f}" y="{rule_y - 0.5:.2f}" width="{b - a:.2f}" height="1" fill="{ACCENT}" opacity="0.55"/>'
    return body, w


# Previews.
p, w1 = one_line(72, 360, 126)
open(f"{out_dir}/wordmark-one-line.svg", "w").write(svg(720, 200, paths_svg(p)))
b, w2 = stacked(72, 240, 120)
open(f"{out_dir}/wordmark-stacked.svg", "w").write(svg(480, 260, b))
p36, _ = one_line(36, 180, 60)
b36, _ = stacked(36, 180 , 150)
both = paths_svg(p36) + b36
open(f"{out_dir}/wordmark-phone-sizes.svg", "w").write(svg(360, 220, both))

# Branding image: 200x80 dp, the one-line lockup.
size = 26
paths, bw = one_line(size, 100, 40 + size * 0.711 / 2)
xml = ['<?xml version="1.0" encoding="utf-8"?>',
       '<!-- The BeatRealms 3D wordmark (ui/theme/Wordmark, one line) outlined from Roboto by',
       '     tools/wordmark.py: the system splash can draw no text, only this 200x80dp image. -->',
       '<vector xmlns:android="http://schemas.android.com/apk/res/android"',
       '    android:width="200dp"', '    android:height="80dp"',
       '    android:viewportWidth="200"', '    android:viewportHeight="80">']
for colour, d in paths.items():
    xml += ['    <path', f'        android:fillColor="{colour}"', f'        android:pathData="{d}" />']
xml.append('</vector>')
open(drawable, "w").write("\n".join(xml) + "\n")
print(f"one-line width {w1 / 72:.3f} em, stacked {w2 / 72:.3f} em, branding {bw:.1f} dp")
