"""Downloads Morph World's pinned CC0 BlenderKit assets (assets.json) into a cache folder.

Each asset is looked up by its assetBaseId through BlenderKit's public search API; the script
refuses any whose license is not cc_zero, so nothing but CC0 can reach the bake. It fetches the
smallest .blend the asset offers (resolution_0_5K, else the full blend) and writes provenance.json
beside the downloads: name, author, license and asset id of each, for docs/morph-world-assets.md.

Free CC0 assets download without an API key. Set BLENDERKIT_API_KEY to send one anyway.

Usage: python3 tools/morph_world_assets/fetch.py <cache-dir>
Then:  blender -b --factory-startup --python tools/morph_world_assets/bake.py -- <cache-dir> <assets-out-dir>
"""
import json
import os
import pathlib
import sys
import urllib.parse
import urllib.request
import uuid

API = "https://www.blenderkit.com/api/v1"
HERE = pathlib.Path(__file__).resolve().parent


def get(url, as_json=True):
    req = urllib.request.Request(url, headers={"User-Agent": "geode-morph-world-assets"})
    key = os.environ.get("BLENDERKIT_API_KEY")
    if key:
        req.add_header("Authorization", f"Bearer {key}")
    with urllib.request.urlopen(req, timeout=120) as r:
        data = r.read()
    return json.loads(data) if as_json else data


def lookup(base_id):
    q = urllib.parse.quote(f"asset_base_id:{base_id}")
    results = get(f"{API}/search/?query={q}&page_size=1")["results"]
    if not results or results[0]["assetBaseId"] != base_id:
        raise SystemExit(f"asset {base_id} not found")
    return results[0]


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    cache = pathlib.Path(sys.argv[1])
    cache.mkdir(parents=True, exist_ok=True)
    pins = json.loads((HERE / "assets.json").read_text())
    provenance = []
    for kind in ("materials", "models"):
        for pin in pins[kind]:
            asset = lookup(pin["id"])
            if asset.get("license") != "cc_zero":
                raise SystemExit(f"{pin['name']}: license {asset.get('license')}, not cc_zero; refusing")
            files = {f["fileType"]: f for f in asset["files"]}
            chosen = files.get("resolution_0_5K") or files["blend"]
            target = cache / f"{pin['id']}.blend"
            if not target.exists():
                link = get(f"{chosen['downloadUrl']}?scene_uuid={uuid.uuid4()}")
                target.write_bytes(get(link["filePath"], as_json=False))
            provenance.append({
                "kind": kind[:-1],
                "id": pin["id"],
                "name": asset["name"],
                "author": (asset.get("author") or {}).get("fullName", "").strip(),
                "license": asset["license"],
                "file": chosen["fileType"],
            })
            print(f"ok  {kind[:-1]:8} {asset['name']} ({provenance[-1]['author']}, {asset['license']})")
    (cache / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")


if __name__ == "__main__":
    main()
