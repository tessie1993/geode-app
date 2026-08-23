#!/usr/bin/env python3
"""Dependency-free source checks for the composite style collection."""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"style collection check failed: {message}")


def declared_uniforms(shader: str) -> set[str]:
    return set(re.findall(r"uniform\s+(?:highp\s+|mediump\s+|lowp\s+)?\w+\s+(\w+)", shader))


def uploaded_uniforms(scene: str) -> set[str]:
    return set(re.findall(r'loc\("(\w+)"\)', scene))


def main() -> None:
    catalog = read("app/src/main/java/dev/musicviz/render/scene/VisualStyleCatalog.kt")
    cym_shader = read("app/src/main/res/raw/cymatics_field_frag.glsl")
    cym_scene = read("app/src/main/java/dev/musicviz/render/scene/CymaticsScene.kt")
    preview = read("tools/shaderpreview/lib/scenes.mjs")
    crystal = read("app/src/main/java/dev/musicviz/ui/Crystal.kt")
    renderer = read("app/src/main/java/dev/musicviz/render/VisualizerRenderer.kt")
    hub = read("app/src/main/java/dev/musicviz/ui/VisualsHub.kt")
    shell = read("app/src/main/java/dev/musicviz/ui/AppShell.kt")

    # \s* after the paren: ktlint wraps long constructor calls onto their own
    # lines, so the id literal is not necessarily on the same line as the name.
    cym_ids = re.findall(r'CymaticsStyle\(\s*"([^" ]+)"', catalog)
    require(len(cym_ids) == 10, f"expected 10 Cymatics substyles, got {len(cym_ids)}")
    require(len(set(cym_ids)) == 10, "style IDs collide")

    for family, shader in (("Cymatics", cym_shader),):
        require("uniform int uStyle;" in shader, f"{family} shader lacks uStyle")
        for style in range(1, 11):
            require(f"uStyle == {style}" in shader, f"{family} shader lacks branch {style}")

    require(declared_uniforms(cym_shader) == uploaded_uniforms(cym_scene), "Cymatics uniform parity drift")
    require("'uStyle'" in preview and "uStyle: { t: '1i'" in preview, "preview driver does not supply uStyle")
    require("addAll(VisualStyleCatalog.cymaticsIds)" in renderer, "renderer does not offer Cymatics variants")
    require("VisualStyleCatalog.cymatics(id)?.let" in renderer, "live factory does not resolve Cymatics variants")
    # The export factory used to be a second hand-maintained switch (pinned
    # builds through the same createScene the live registry uses, so the
    # variant resolution pinned above covers export too - pin the routing.
    require("createScene(sceneId" in renderer, "export factory no longer routes through createScene")
    require("SceneList(VisualStyleCatalog.cymaticsIds" in hub, "Cymatics variants are absent from the picker")

    named_crystals = [
        "LAPIS",
        "MALACHITE",
        "CLEAR_QUARTZ",
        "ROSE_QUARTZ",
        "SUGILITE",
        "AMETHYST",
        "KYANITE",
        "ONYX",
    ]
    for name in named_crystals:
        require(f"CrystalTextureKind.{name}" in crystal, f"missing mineral texture for {name}")
    require("CrystalMaterialTheme(" in shell, "shell does not propagate the active mineral theme")
    require("LocalCrystalTheme provides appTheme" in crystal, "crystal panels do not receive the active theme")

    print("style collection checks passed")
    print(f"  Cymatics substyles:   {len(cym_ids)}")
    print(f"  Crystal materials:    {len(named_crystals)}")


if __name__ == "__main__":
    main()
