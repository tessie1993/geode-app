#!/usr/bin/env python3
"""Vendors the gl-transitions corpus into app/src/main/assets/gl_transitions.json.

Run:  python3 tools/vendor_gl_transitions.py <path-to-gl-transitions.json>

The input is the `gl-transitions.json` shipped inside the npm package
(`npm pack gl-transitions`), which is the canonical published artefact: one
JSON holding every transition's name, parameter types, defaults and GLSL. That
is a better provenance anchor than a git checkout - a published version number
pins exactly what was audited.

WHAT THIS SCRIPT DOES, AND WHY EACH PART EXISTS
-----------------------------------------------

1. LICENCE AUDIT. Every transition must carry a `// License:` header. Anything
   without one is dropped rather than assumed - and as of 1.71.0 nothing is
   dropped for this reason: 123 of the 125 are MIT, one is BSD 3-Clause
   (InvertedPageCurl, Hewlett-Packard) and one BSD 2-Clause (StereoViewer, Ted
   Schundler). The two BSD notices go into THIRD_PARTY_NOTICES separately from
   the MIT bulk. The counts are printed so a future re-vendor notices a change.

2. SAMPLER EXCLUSION. `displacement` and `luma` declare a `sampler2D` the
   engine has no image to bind. They are excluded rather than shipped broken.

3. GLSL ES 3.00 FIXES. The corpus targets WebGL 1 / desktop GLSL, which allows
   things ES 3.00 does not. Two mechanical, provably-equivalent rewrites:

   a) `texture2D(` -> `texture(`. The old name does not exist in ES 3.00.

   b) File-scope variables initialised from a uniform, e.g.
          float nQuick = clamp(zoom_quickness, 0.2, 1.0);
      ES 3.00 requires global initialisers to be constant expressions, and a
      uniform is not one. Each such declaration becomes a `#define`, which is
      exactly equivalent HERE because these are all read-only aliases - the
      pass asserts the name is never assigned again, and refuses to convert it
      if it is. Depth-tracked so identical-looking declarations *inside*
      functions (which are perfectly legal) are left alone.

Anything that still fails to compile after this is left in the asset with
`"broken": true` so the app can skip it while the reason stays visible; the
compile check itself lives outside this script (a real GLSL ES 3.00 compiler).
"""

import json
import re
import sys
from collections import Counter

# Transitions needing a sampler the engine cannot supply.
NEEDS_SAMPLER = {"displacement", "luma"}

# Names that would collide with a built-in transition id. `fade` is a plain
# crossfade, which the base composite shader already implements as
# TransitionStyle.FADE - shipping both would put two identically-named entries
# in one flat picker and make the id ambiguous to the renderer.
COLLIDES_WITH_BUILT_IN = {"cut", "fade", "melt", "slide", "zoom"}

DECL = re.compile(
    r"^[ \t]*(float|vec2|vec3|vec4|int|bool|ivec2|ivec3|ivec4|mat2|mat3|mat4)"
    r"[ \t]+([A-Za-z_]\w*)[ \t]*=[ \t]*(.+?);[ \t]*$",
    re.S,
)


def strip_comments(src: str) -> str:
    """Blanks comments while preserving offsets, so depth tracking is honest."""
    out = []
    i = 0
    n = len(src)
    while i < n:
        if src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join(c if c == "\n" else " " for c in src[i:j]))
            i = j
        else:
            out.append(src[i])
            i += 1
    return "".join(out)


def statements_at_top_level(src: str):
    """Yields (start, end) of every `;`-terminated statement at brace depth 0."""
    bare = strip_comments(src)
    depth = 0
    start = 0
    for i, ch in enumerate(bare):
        if ch == "{":
            if depth == 0:
                start = i + 1
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                start = i + 1
        elif ch == ";" and depth == 0:
            yield start, i + 1
            start = i + 1


def hoist_global_initialisers(name: str, src: str) -> tuple[str, list[str]]:
    """Rewrites non-constant file-scope initialisers as #defines."""
    notes = []
    bare = strip_comments(src)
    edits = []
    for start, end in statements_at_top_level(src):
        text = bare[start:end].strip()
        if not text or text.startswith(("uniform", "const", "in ", "out ", "#", "precision", "varying", "attribute")):
            continue
        m = DECL.match(text)
        if not m:
            continue
        vtype, var, expr = m.group(1), m.group(2), m.group(3)
        # Constant initialisers are already legal - leave them exactly as they are.
        if re.fullmatch(r"[-+0-9.eEf()*/,\s]+|vec[234]\([-+0-9.eEf,\s]*\)", expr.strip()):
            continue
        # Only safe as a macro if the name is never written to again.
        writes = len(re.findall(r"\b" + re.escape(var) + r"\b\s*(?:=[^=]|\+=|-=|\*=|/=|\+\+|--)", bare))
        if writes > 1:
            notes.append(f"{name}: '{var}' is reassigned; left alone")
            continue
        flat = " ".join(expr.split())
        edits.append((start, end, f"#define {var} ({flat})\n"))
        notes.append(f"{name}: hoisted global '{vtype} {var}' to a #define")
    for start, end, replacement in sorted(edits, reverse=True):
        # A #define must own its line, hence the leading newline.
        src = src[:start] + "\n" + replacement + src[end:]
    return src, notes


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    source = json.load(open(sys.argv[1]))
    licences = Counter()
    notes = []
    out = []
    for t in sorted(source, key=lambda x: x["name"].lower()):
        name = t["name"]
        glsl = t["glsl"]
        licence = (re.search(r"^//\s*[Ll]icense:\s*(.+)$", glsl, re.M) or [None, ""])[1].strip()
        if not licence:
            notes.append(f"{name}: DROPPED - no licence header")
            continue
        if name in NEEDS_SAMPLER:
            notes.append(f"{name}: DROPPED - needs a sampler2D the engine cannot supply")
            continue
        if name.lower() in COLLIDES_WITH_BUILT_IN:
            notes.append(f"{name}: DROPPED - the id collides with a built-in transition")
            continue
        author = (re.search(r"^//\s*[Aa]uthor:\s*(.+)$", glsl, re.M) or [None, ""])[1].strip()
        if "texture2D(" in glsl:
            glsl = glsl.replace("texture2D(", "texture(")
            notes.append(f"{name}: texture2D -> texture")
        glsl, hoisted = hoist_global_initialisers(name, glsl)
        notes.extend(hoisted)
        licences[licence] += 1
        out.append(
            {
                "name": name,
                "author": author,
                "license": licence,
                "paramsTypes": t.get("paramsTypes") or {},
                "defaultParams": t.get("defaultParams") or {},
                "glsl": glsl,
            }
        )
    dest = "app/src/main/assets/gl_transitions.json"
    json.dump(out, open(dest, "w"), separators=(",", ":"))
    print(f"wrote {dest}: {len(out)} transitions")
    print("licences:", dict(licences))
    print(f"with tunable parameters: {sum(1 for x in out if x['paramsTypes'])}")
    print("\n".join(f"  {n}" for n in notes))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
