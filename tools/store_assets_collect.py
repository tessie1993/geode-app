#!/usr/bin/env python3
"""Sorts what .github/workflows/store-assets.yml pulled off the emulator into the folders the
owner uploads from, and checks every picture against Google Play's rules.

    python3 tools/store_assets_collect.py <out dir>

Reads <out>/raw (shots-<form>/, reel/, frames/, as the capture script left them) and writes
<out>/screenshots/<form>/NN-name.png, <out>/feature-graphic/<style>-1024x500.png,
<out>/promo-clips/, <out>/report.md and <out>/problems.txt (empty when every set asked for is
whole and the right size; the workflow fails on anything else).

The rules, from "Add preview assets to showcase your app"
(support.google.com/googleplay/android-developer/answer/9866151), read 2026-11:
  - screenshots: JPEG or 24-bit PNG with no alpha; each side 320 to 3840 px; the long side at
    most twice the short side; 2 to 8 per device type;
  - large screens (7-inch, 10-inch): 16:9 or 9:16, each side 1080 to 7680 px, at least 4;
  - recommendations want at least 4 screenshots of 1080 px or more (1920 x 1080 or 1080 x 1920);
  - feature graphic: 1024 x 500, JPEG or 24-bit PNG with no alpha.

Needs Pillow. Run it by hand on a downloaded raw folder to redo the checks.
"""

import os
import pathlib
import shutil
import sys

from PIL import Image, ImageStat

# The size each form's `wm size` asks for (the workflow's screen() table).
FORMS = {
    "phone": (1080, 1920),
    "phone-land": (1920, 1080),
    "tablet7": (1920, 1080),
    "tablet10": (2560, 1440),
}
LARGE_SCREENS = {"tablet7", "tablet10"}
SHOTS_PER_FORM = 8
FEATURE = (1024, 500)
# A frame darker than this on average (0..255) is listed as suspect: a black GL surface.
DARK_MEAN = 6.0


def play_problems(form, size):
    """What Play would refuse about a screenshot of this size, as words."""
    w, h = size
    short, long_ = min(w, h), max(w, h)
    found = []
    if short < 320 or long_ > 3840:
        found.append("a side is outside 320..3840 px")
    if long_ > 2 * short:
        found.append("the long side is more than twice the short side")
    if form in LARGE_SCREENS:
        if short < 1080:
            found.append("a large-screen side is under 1080 px")
        if long_ * 9 != short * 16:
            found.append("a large-screen screenshot is not 16:9 or 9:16")
    return found


def collect_shots(raw, out, form, lines, problems):
    src = raw / f"shots-{form}"
    files = sorted(src.rglob("*.png")) if src.exists() else []
    if not src.exists():
        return
    dest = out / "screenshots" / form
    dest.mkdir(parents=True, exist_ok=True)
    quality = (src / "quality.txt").read_text().strip() if (src / "quality.txt").exists() else "?"
    lines.append(f"\n## Screenshots: {form} (Performance mode {quality})\n")
    lines.append("| file | size | Play | note |")
    lines.append("|---|---|---|---|")
    for path in files:
        image = Image.open(path)
        had_alpha = image.mode != "RGB"
        rgb = image.convert("RGB")
        rgb.save(dest / path.name, format="PNG", optimize=True)
        issues = play_problems(form, rgb.size)
        if rgb.size != FORMS[form]:
            issues.append(f"expected {FORMS[form][0]}x{FORMS[form][1]}")
        notes = []
        if had_alpha:
            notes.append("alpha removed")
        if sum(ImageStat.Stat(rgb).mean) / 3 < DARK_MEAN:
            notes.append("nearly black: look at it")
        lines.append(f"| {path.name} | {rgb.size[0]}x{rgb.size[1]} | {'; '.join(issues) or 'ok'} | {'; '.join(notes)} |")
        for issue in issues:
            problems.append(f"{form}/{path.name}: {issue}")
    if len(files) < SHOTS_PER_FORM:
        problems.append(f"{form}: {len(files)} of {SHOTS_PER_FORM} screenshots")


def collect_frames(raw, out, lines, problems):
    src = raw / "frames"
    if not src.exists():
        return
    files = sorted(src.rglob("*.png"))
    dest = out / "feature-graphic"
    dest.mkdir(parents=True, exist_ok=True)
    lines.append("\n## Feature graphic frames (renders, not screenshots)\n")
    for path in files:
        style = path.stem.rsplit("-", 1)[0]
        image = Image.open(path).convert("RGB")
        if image.size[0] * FEATURE[1] != image.size[1] * FEATURE[0]:
            problems.append(f"frame {path.name}: {image.size} is not the feature graphic's shape")
            continue
        name = f"{style}-{FEATURE[0]}x{FEATURE[1]}.png"
        image.resize(FEATURE, Image.LANCZOS).save(dest / name, format="PNG", optimize=True)
        shutil.copy2(path, dest / path.name)
        lines.append(f"- {name} (from {path.name})")
    for report in src.rglob("failures.txt"):
        lines.append("\n```\n" + report.read_text().strip() + "\n```")
    if not files:
        problems.append("feature graphic: no frame rendered")


def collect_reel(raw, out, lines):
    src = raw / "reel"
    clips = sorted(src.rglob("*.mp4")) if src.exists() else []
    if not clips:
        return
    lines.append("\n## Promo clips (raw, silent, software-rendered)\n")
    for path in clips:
        form = path.parent.name
        dest = out / "promo-clips" / form
        dest.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, dest / path.name)
        lines.append(f"- promo-clips/{form}/{path.name} ({path.stat().st_size // 1024} KB)")


def main():
    out = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "store-assets-out")
    raw = out / "raw"
    out.mkdir(parents=True, exist_ok=True)
    lines = ["# Store assets", "", "What each file is for: docs/store/screenshots.md and docs/store/promo-video.md."]
    problems = []
    if not (raw / "finished").exists():
        problems.append("the capture script did not finish")
    asked = os.environ.get("STORE_FORMS", " ".join(FORMS)).split()
    for form in asked:
        if form not in FORMS:
            continue
        if not (raw / f"shots-{form}").exists():
            problems.append(f"{form}: no screenshots")
        collect_shots(raw, out, form, lines, problems)
    if os.environ.get("STORE_FRAMES", "1") == "1":
        collect_frames(raw, out, lines, problems)
    collect_reel(raw, out, lines)
    lines += ["", "## Problems", ""] + ([f"- {p}" for p in problems] or ["none"])
    (out / "report.md").write_text("\n".join(lines) + "\n")
    (out / "problems.txt").write_text("\n".join(problems))
    with open(os.environ.get("GITHUB_STEP_SUMMARY", os.devnull), "a") as summary:
        summary.write("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
