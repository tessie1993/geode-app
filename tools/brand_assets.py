#!/usr/bin/env python3
"""Generates the BeatRealms 3D brand mark (the eye): SVG masters in docs/brand/ and the launcher background,
themed-icon and status-bar drawables in app/src/main/res/drawable/, all from one geometry.

The launcher foreground, the splash art and the splash video are raster since 2026-11-01:
tools/brand_raster.sh builds them from the neon render.

The mark is the "all three" direction chosen on 2026-10-30: a spectrum of bars (waveform) inside a
rippling oscilloscope lid (eye) around a twisted-hexagon vortex iris (fractal), in the old neon
purple palette. Edit the geometry here, never the generated files, then run:

    python tools/brand_assets.py
    # then the PNG exports in docs/brand/README.md (headless Chrome)

The raster store art (docs/brand/flow/) came from Google Flow; its prompts are in the README.

The wordmark is not drawn here: tools/wordmark.py outlines it, and tools/feature_graphic.py
draws the Play feature graphic.
"""
import math
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
BRAND = ROOT / "docs" / "brand"
DRAWABLE = ROOT / "app" / "src" / "main" / "res" / "drawable"

NIGHT, NIGHT_LIFT = "#0E0620", "#1F1145"  # brand background, its lit centre
VIOLET, VIOLET_LIGHT, LILAC = "#7C4DFF", "#9C6BFF", "#C792EA"
CYAN, BLUE, AMBER = "#89DDFF", "#82AAFF", "#FFCB6B"
C = 54.0  # centre of the 108x108 adaptive-icon viewport; the mark stays within r=33 of it

BAR_GRADIENT = ("linear", (22, 0, 86, 0), [(0, VIOLET), (0.5, CYAN), (1, VIOLET)])


def num(v):
    return f"{v:.2f}".rstrip("0").rstrip(".")


def polyline(pts, close=True):
    return "M" + " L".join(f"{num(x)},{num(y)}" for x, y in pts) + (" Z" if close else "")


def ngon(n, r, rot, cx=C, cy=C):
    return [(cx + r * math.cos(rot + 2 * math.pi * i / n), cy + r * math.sin(rot + 2 * math.pi * i / n)) for i in range(n)]


def circle(r, cx=C, cy=C):
    return f"M{num(cx - r)},{num(cy)} a{num(r)},{num(r)} 0 1,0 {num(2 * r)},0 a{num(r)},{num(r)} 0 1,0 {num(-2 * r)},0 Z"


def lid_env(x):
    """Half-height of the eye at x: an almond, pointed at x=22 and x=86."""
    t = (x - C) / 32
    return 19 * max(0.0, 1 - t * t) ** 1.15


RIPPLE = 0.13  # lid ripple depth, as a fraction of the half-height


def lid(sign):
    pts = []
    for i in range(121):
        x = 22 + 64 * i / 120
        pts.append((x, C + sign * lid_env(x) * (1 + RIPPLE * math.sin(i / 120 * 2 * math.pi * 7 + sign))))
    return polyline(pts, close=False)


# One path: d, plus fill/stroke (a colour or a gradient tuple), and an optional name to animate.
def path(d, fill=None, stroke=None, width=None, name=None):
    return {"d": d, "fill": fill, "stroke": stroke, "width": width, "name": name}


def group(name, children, pivot=(C, C)):
    return {"group": name, "pivot": pivot, "children": children}


AMPS = [0.55, 0.8, 0.62, 1.0, 0.7, 0.92, 0.66, 0.85, 0.75, 0.95, 0.6, 0.88, 0.7, 1.0, 0.58, 0.82, 0.5]


def bar_groups(colour, width):
    """Spectrum bars, paired by distance from the centre so each pair can rise together."""
    pairs = {}
    for i, a in enumerate(AMPS):
        x = 23 + 62 * i / (len(AMPS) - 1)
        if abs(x - C) < 15:
            continue  # behind the iris
        t = (x - C) / 32
        room = lid_env(x) * (1 - RIPPLE) - 2.6  # keep a gap inside the lid
        h = max(1.2, min(21 * (1 - t * t) * a, room))
        k = round(abs(x - C) / (62 / (len(AMPS) - 1))) - 4
        pairs.setdefault(k, []).append(path(f"M{num(x)},{num(C - h)} L{num(x)},{num(C + h)}", stroke=colour, width=width))
    return [group(f"bars_{k}", pairs[k]) for k in sorted(pairs)]


def vortex(rings, r0=13.2, shrink=0.84, twist=12):
    """Twisted-hexagon vortex: each ring further round and smaller, a self-similar spiral."""
    out, r, rot = [], r0, -math.pi / 2
    for colour in rings:
        if colour:
            out.append(path(polyline(ngon(6, r, rot)), stroke=colour, width=max(0.6, r / 11)))
        r, rot = r * shrink, rot + math.radians(twist)
    return out


def mark():
    return [
        group("lids", [path(lid(-1), stroke=LILAC, width=2.4, name="lid_top"), path(lid(1), stroke=LILAC, width=2.4, name="lid_bottom")]),
        *bar_groups(BAR_GRADIENT, 2.4),
        group("iris", [path(circle(15), fill=NIGHT, stroke=VIOLET_LIGHT, width=1.6),
                       *vortex([CYAN, CYAN, BLUE, BLUE, VIOLET_LIGHT, VIOLET_LIGHT, LILAC, LILAC, AMBER])]),
    ]


def monochrome():
    """Themed icon: one tint, so no fills that would merge, and strokes thick enough to survive 48dp."""
    w = "#FFFFFF"
    r0, rot = 11.0, -math.pi / 2
    return [
        path(lid(-1), stroke=w, width=2.6), path(lid(1), stroke=w, width=2.6),
        *bar_groups(w, 2.6),
        path(circle(15), stroke=w, width=1.8),
        path(polyline(ngon(6, r0, rot)), stroke=w, width=1.6),
        path(polyline(ngon(6, r0 * 0.68, rot + math.radians(24))), stroke=w, width=1.6),
        path(polyline(ngon(6, r0 * 0.36, rot + math.radians(48))), fill=w),
    ]


def status_icon():
    """24dp status-bar silhouette: smooth lids, one bar a side, a two-ring vortex."""
    w, cx = "#FFFFFF", 12.0

    def env(x):
        t = (x - cx) / 10.5
        return 7.2 * max(0.0, 1 - t * t) ** 1.15

    lids = [polyline([(1.5 + 21 * i / 60, cx + s * env(1.5 + 21 * i / 60)) for i in range(61)], close=False) for s in (-1, 1)]
    return [
        *(path(d, stroke=w, width=1.5) for d in lids),
        path("M5.2,10 L5.2,14 M18.8,10 L18.8,14", stroke=w, width=1.5),
        path(polyline(ngon(6, 3.9, -math.pi / 2, cx, cx)), stroke=w, width=1.4),
        path(polyline(ngon(6, 1.7, -math.pi / 2 + math.radians(30), cx, cx)), fill=w),
    ]


# ---------------------------------------------------------------- SVG


class Svg:
    def __init__(self):
        self.defs, self.n = [], 0

    def paint(self, p):
        if p is None:
            return "none"
        if isinstance(p, str):
            return p
        kind, (x1, y1, x2, y2), stops = p
        self.n += 1
        gid = f"g{self.n}"
        units = "" if (x1, y1, x2, y2) == (0, 0, 1, 0) else ' gradientUnits="userSpaceOnUse"'
        s = "".join(f'<stop offset="{o}" stop-color="{c}"/>' for o, c in stops)
        self.defs.append(f'<linearGradient id="{gid}" x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}"{units}>{s}</linearGradient>')
        return f"url(#{gid})"

    def node(self, n):
        if "group" in n:
            return "<g>" + "".join(self.node(c) for c in n["children"]) + "</g>"
        a = f'd="{n["d"]}" fill="{self.paint(n["fill"])}"'
        if n["stroke"]:
            a += f' stroke="{self.paint(n["stroke"])}" stroke-width="{num(n["width"])}" stroke-linecap="round" stroke-linejoin="round"'
        return f"<path {a}/>"

    def body(self, nodes):
        return "".join(self.node(n) for n in nodes)

    def doc(self, w, h, inner, view=None):
        v = view or f"0 0 {w} {h}"
        return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="{v}">'
                f'<defs>{"".join(self.defs)}</defs>{inner}</svg>\n')


def background_svg(cx=C, cy=C, r=76, w=108, h=108):
    return (f'<radialGradient id="bg" cx="{cx}" cy="{cy}" r="{r}" gradientUnits="userSpaceOnUse">'
            f'<stop offset="0" stop-color="{NIGHT_LIFT}"/><stop offset="1" stop-color="{NIGHT}"/></radialGradient>',
            f'<rect width="{w}" height="{h}" fill="url(#bg)"/>')


def write_svgs():
    BRAND.mkdir(parents=True, exist_ok=True)
    grad, rect = background_svg()

    s = Svg()
    s.defs.append(grad)
    (BRAND / "beatrealms-icon.svg").write_text(s.doc(108, 108, rect + s.body(mark())))
    s = Svg()
    (BRAND / "beatrealms-mark.svg").write_text(s.doc(108, 108, s.body(mark())))
    s = Svg()
    (BRAND / "beatrealms-monochrome.svg").write_text(s.doc(108, 108, s.body(monochrome())))



# ---------------------------------------------------------------- VectorDrawable


def argb(c):
    return "#FF" + c[1:].upper()


class Vd:
    def paint_attr(self, attr, p, pad):
        if isinstance(p, str):
            return f' android:{attr}="{argb(p)}"', ""
        _, (x1, y1, x2, y2), stops = p
        items = "".join(f'\n{pad}            <item android:offset="{o}" android:color="{argb(c)}" />' for o, c in stops)
        return "", (f'\n{pad}    <aapt:attr name="android:{attr}">'
                    f'\n{pad}        <gradient android:type="linear" android:startX="{x1}" android:startY="{y1}" '
                    f'android:endX="{x2}" android:endY="{y2}">{items}'
                    f'\n{pad}        </gradient>\n{pad}    </aapt:attr>')

    def node(self, n, depth=1):
        pad = "    " * depth
        if "group" in n:
            px, py = n["pivot"]
            kids = "\n".join(self.node(c, depth + 1) for c in n["children"])
            return (f'{pad}<group android:name="{n["group"]}" android:pivotX="{num(px)}" android:pivotY="{num(py)}">\n'
                    f"{kids}\n{pad}</group>")
        attrs, children = "", ""
        if n["name"]:
            attrs += f' android:name="{n["name"]}"'
        attrs += f' android:pathData="{n["d"]}"'
        for attr, p in (("fillColor", n["fill"]), ("strokeColor", n["stroke"])):
            if p:
                a, c = self.paint_attr(attr, p, pad)
                attrs, children = attrs + a, children + c
        if n["stroke"]:
            attrs += f' android:strokeWidth="{num(n["width"])}" android:strokeLineCap="round" android:strokeLineJoin="round"'
        return f"{pad}<path{attrs}>{children}\n{pad}</path>" if children else f"{pad}<path{attrs} />"

    def doc(self, comment, nodes, size=108, extra=""):
        body = "\n".join(self.node(n) for n in nodes)
        return (f'<?xml version="1.0" encoding="utf-8"?>\n<!--\n{comment}\n  Generated by tools/brand_assets.py: edit that, not this file.\n-->\n'
                f'<vector xmlns:android="http://schemas.android.com/apk/res/android"\n'
                f'    xmlns:aapt="http://schemas.android.com/aapt"\n'
                f'    android:width="{size}dp"\n    android:height="{size}dp"\n'
                f'    android:viewportWidth="{size}"\n    android:viewportHeight="{size}"{extra}>\n{body}\n</vector>\n')


def write_drawables():
    vd = Vd()
    (DRAWABLE / "ic_launcher_background.xml").write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n<!--\n  Brand night (@color/brand_night) lifting to violet at the centre, where the iris sits.\n'
        "  Generated by tools/brand_assets.py: edit that, not this file.\n-->\n"
        '<vector xmlns:android="http://schemas.android.com/apk/res/android"\n'
        '    xmlns:aapt="http://schemas.android.com/aapt"\n'
        '    android:width="108dp"\n    android:height="108dp"\n    android:viewportWidth="108"\n    android:viewportHeight="108">\n'
        '    <path android:pathData="M0,0h108v108h-108z">\n        <aapt:attr name="android:fillColor">\n'
        f'            <gradient android:type="radial" android:centerX="54" android:centerY="54" android:gradientRadius="76">\n'
        f'                <item android:offset="0" android:color="{argb(NIGHT_LIFT)}" />\n'
        f'                <item android:offset="1" android:color="{argb(NIGHT)}" />\n'
        "            </gradient>\n        </aapt:attr>\n    </path>\n</vector>\n")
    (DRAWABLE / "ic_launcher_monochrome.xml").write_text(vd.doc(
        "  Themed-icon silhouette (Android 13+). Its own drawing, not the foreground: the system\n"
        "  tints every opaque pixel one colour, so the iris disc would swallow the vortex. Strokes\n"
        "  only, thick enough to read at launcher size.", monochrome()))
    (DRAWABLE / "ic_stat_muzviz.xml").write_text(vd.doc(
        "  Status-bar icon for the playback notification: the mark reduced to a flat white\n"
        "  silhouette that reads at 24dp.", status_icon(), size=24, extra='\n    android:tint="#FFFFFF"'))


def main():
    write_svgs()
    write_drawables()
    print("brand assets written")


if __name__ == "__main__":
    main()
