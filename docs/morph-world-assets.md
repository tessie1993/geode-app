# Morph World's baked assets

`morph_world` (`lib_world`, `core/viz/world/`) draws two baked files beside its own SDF primitives:

- `app/src/main/assets/shaders/world_shapes.bin`: sixteen models as 32³ signed-distance volumes (384 KB). They are the genome's seventh primitive (`PMesh`, picked and blended by `MeshPick`).
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
| 6 | Mushroom | PBRPX | `a8f1a241-ebba-4a0e-b081-20cf8a720282` | CC0 |
| 7 | Fern 02 | Poly Haven | `b6155e0a-79eb-44da-acc2-6587f7f533da` | CC0 |
| 8 | Quiver Tree 01 | Poly Haven | `cf657993-cb54-461b-874f-9eda90e166b4` | CC0 |
| 9 | Crystalline Iceplant | Poly Haven | `6880c55f-95e5-4477-b4fe-f9276252c603` | CC0 |
| 10 | Dead Tree Trunk 02 | Poly Haven | `059bb822-bb9d-41b3-9923-6b84f9a518e4` | CC0 |
| 11 | Cheiridopsis Succulent | Poly Haven | `ff359c53-c43e-4e36-a5a8-347607e4ea77` | CC0 |
| 12 | Japanese Stone Lantern | Davide Tirindelli | `4b71dbf8-4f14-4e34-8ebf-f889f478acb4` | CC0 |
| 13 | Buddha | Eme Uve | `8ac726bb-9e5b-487c-b86c-34fa1bbab12b` | CC0 |
| 14 | Antique Ceramic Vase 01 | Poly Haven | `dec67dba-388e-4e8d-ac23-4c87c63e6249` | CC0 |
| 15 | Owl decor | abd3d | `cf2072c1-f974-44b0-a512-2b1ca634274a` | CC0 |

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

Only LOD0 of a file that stacks levels of detail is baked, a file's default scene cube is skipped, and every model wears a thin skin so parts thinner than a voxel (leaves, stems) still draw. A pinned asset that needs a login to download (some CC0 assets do) cannot be used: fetch.py stops on its 401.

Swapping an asset means changing its pin and re-running both steps. The slab and layer counts are fixed (`kShapeCount`, `kTexCount`).
