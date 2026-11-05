# Morph World's baked assets

`morph_world` (`lib_world`, `core/viz/world/`) draws two baked files beside its own SDF primitives:

- `app/src/main/assets/shaders/world_shapes.bin`: six models as 32³ signed-distance volumes (192 KB). They are the genome's seventh primitive (`PMesh`, picked and blended by `MeshPick`).
- `app/src/main/assets/shaders/world_textures.bin`: eight material maps as 128² grey detail layers (128 KB). They vary surface albedo (`TexAmount`) and pattern the light (`TexEmit`). They are grey on purpose: colour still comes only from `pal()`.

Every source asset is **CC0** (public domain dedication) from [BlenderKit](https://www.blenderkit.com), pinned by asset id in `tools/morph_world_assets/assets.json`. `fetch.py` refuses any asset the API does not report as `cc_zero`. CC0 needs no attribution; the authors are credited here anyway.

## Models (`world_shapes.bin`, slab order)

| # | Asset | Author | BlenderKit asset id | Licence |
|---|---|---|---|---|
| 0 | Lambis Shell | Poly Haven | `19b1ef54-c54a-4c0c-b309-28f1b266686f` | CC0 |
| 1 | Gothic Statue | Poly Haven | `46610f9c-5817-439e-be91-dd0e2c4ad1a9` | CC0 |
| 2 | Bull Head | Poly Haven | `b5292b3a-ce6d-4721-8cd7-f214f62e7889` | CC0 |
| 3 | Cow Skull | Alexander Handjiev | `a37f703c-8986-4488-8053-b47422a87175` | CC0 |
| 4 | Horse Statue 01 | Poly Haven | `953a5b75-6bef-4c80-9371-9d99a4aa9122` | CC0 |
| 5 | Chandelier 03 | Poly Haven | `9bcdea77-1798-4d4a-a69e-bab56a4ea261` | CC0 |

## Materials (`world_textures.bin`, layer order)

| # | Asset | Author | BlenderKit asset id | Licence |
|---|---|---|---|---|
| 0 | Marble Tiles | Poly Haven | `cb2ba137-0fcb-4c4c-913c-4e22b1735605` | CC0 |
| 1 | Corrugated Iron | Poly Haven | `0faa32f0-80ce-4b4f-94a4-51047252020d` | CC0 |
| 2 | Volcanic Rock Tiles | Poly Haven | `b73e2a0e-7967-4d27-8eca-b5208c4a4916` | CC0 |
| 3 | Lichen Rock | Poly Haven | `7372ddd4-5513-469e-a942-970d5fa50fd5` | CC0 |
| 4 | Ice 02 | Julio Sillet | `7805955b-1f11-40e5-bbed-fa2ad44601d8` | CC0 |
| 5 | Chinese Cedar Bark | Poly Haven | `785ae9c1-5c95-4345-89ef-728b4b12738a` | CC0 |
| 6 | Clay Roof Tiles | Poly Haven | `e8feb6dc-2e14-4b9c-8aa1-b43bb881bb33` | CC0 |
| 7 | Computer Texture | Cyberking 42 | `d2985100-fe7a-481d-902b-02d5965a446d` | CC0 |

## Rebuilding

Use the official Blender build (4.5 LTS was used). Ubuntu's `blender` 4.0 package bakes black and cannot open some of these files.

```sh
python3 tools/morph_world_assets/fetch.py /tmp/bk           # CC0 only; no API key needed for free assets
blender -b --factory-startup --disable-autoexec -noaudio \
    --python tools/morph_world_assets/bake.py -- /tmp/bk app/src/main/assets/shaders
```

Autoexec stays off: the downloaded `.blend` files are data, never code. `bake.py` describes both file formats; `core/viz/world/WorldAssets.hpp` reads them, and `world_genome_test` checks their sizes and that every model has an inside and an outside.

Swapping an asset means changing its pin and re-running both steps. The slab and layer counts are fixed (`kShapeCount`, `kTexCount`).
