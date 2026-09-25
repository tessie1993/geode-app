#!/usr/bin/env python3
"""Build the offline visual catalogue, with an optional standalone original-PNG edition.

  python tools/build-visual-catalogue.py --standalone /absolute/path/Opaline-Visual-Library.html

The package edition links local assets. The standalone edition embeds original PNG bytes,
without resizing, recolouring or recompression. It makes no network requests. Original
asset/download links and documentation links are intended for the extracted package.
"""
from __future__ import annotations
import argparse
import base64
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[1]
THEMES = ['Tidal', 'Opal', 'Moss', 'Obsidian', 'Aurora', 'Amber']
FAMILY_LABELS = {'A':'Gel bodies','B':'Precision controls','C':'Containers & surfaces','D':'Spatial assemblies','E':'Liquid structures','F':'Films & bubbles','J':'Environment','N':'Nature accents'}
TITLE_OVERRIDES = {
 'nature-dew-button':'Dew lens button','nature-leaf-tray':'Leaf compartment tray','nature-seed-button':'Sculpted seed button',
 'nature-tendril':'Liquid tendril','nature-fern-frond':'Translucent fern frond','nature-moss-pedestal':'Moss pedestal',
 'glow-seed':'Luminous seed','textarea-panel':'Multiline text container','input-capsule':'Input capsule',
 'background-tidal':'Tidal atmosphere','background-opal':'Opal atmosphere','background-moss':'Moss atmosphere',
 'background-obsidian':'Obsidian atmosphere','background-aurora':'Aurora atmosphere','background-amber':'Amber atmosphere',
}
NATURE_ART = {'nature-dew-button','nature-leaf-tray','nature-seed-button','nature-tendril','nature-fern-frond','nature-moss-pedestal','glow-seed','floating-droplet'}
NATURE_MAP = {'nature-seed-button':['N01'],'nature-leaf-tray':['N02'],'nature-dew-button':['N03'],'nature-tendril':['N04'],'nature-fern-frond':['N05'],'glow-seed':['N06'],'nature-moss-pedestal':['J08','J15'],'floating-droplet':['E08','E10']}


def read_json(relative):
    return json.loads((ROOT / relative).read_text())


def png_size(path):
    with path.open('rb') as file:
        header = file.read(24)
    if header[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError(f'Not a PNG: {path}')
    return list(struct.unpack('>II', header[16:24]))


def make_data(allow_incomplete=False):
    elements = read_json('catalogue/elements.json')
    compositions = read_json('catalogue/compositions.json')
    models = read_json('assets/models/models-manifest.json')['models']
    lookup = {item['id']:item for item in elements}
    artwork = sorted((ROOT/'assets/artwork').glob('*.png'))
    report_path = ROOT/'qa/png-render-report.json'
    report = json.loads(report_path.read_text()) if report_path.exists() else {}
    rendered_ids = {item['id'] for item in report.get('assets', [])}
    missing = [model['id'] for model in models if not (ROOT/f"assets/previews/{model['id']}.png").exists()]
    if not allow_incomplete:
        if len(artwork) != 24:
            raise SystemExit(f'Expected 24 generated PNG artworks, found {len(artwork)}.')
        if len(models) != 157:
            raise SystemExit(f'Expected 157 GLB modules, found {len(models)}.')
        if missing:
            raise SystemExit('Geometry previews still rendering: ' + ', '.join(missing))
        if len(rendered_ids) != len(models) or report.get('errors'):
            raise SystemExit(f'PNG render report is not complete: {len(rendered_ids)}/{len(models)} modules; errors={report.get("errors", [])}.')
    items = []
    for path in artwork:
        stem = path.stem
        theme = stem.split('-', 1)[1].title() if stem.startswith('background-') else 'Shared'
        family = 'Backdrops' if stem.startswith('background-') else 'Nature artwork' if stem in NATURE_ART else 'UI artwork'
        items.append({
            'id':stem,'name':TITLE_OVERRIDES.get(stem,stem.replace('-',' ').title()),'type':'artwork',
            'label':'ImageGen artwork','family':family,'familyName':family,'theme':theme,'nature':stem in NATURE_ART,
            'image':str(path.relative_to(ROOT)),'png':str(path.relative_to(ROOT)),'size':png_size(path),
            'description':'Original generated PNG artwork. A visual reference or compositing asset; geometry and interaction are supplied separately.',
            'related':NATURE_MAP.get(stem,[]),'tags':stem.replace('-',' '),'status':'Generated visual asset',
        })
    for model in models:
        id = model['id']; item = lookup.get(id,{}); impl = item.get('implementation',{})
        path = ROOT/f'assets/previews/{id}.png'
        has_preview=path.exists()
        special = 'Light-rig preview' if id=='J24' else 'Volume-domain preview' if id in ['J19','J20'] else '3D mesh preview'
        items.append({
            'id':id,'name':model['name'],'type':'geometry','label':special,'family':id[0],
            'familyName':FAMILY_LABELS.get(id[0],id[0]),'theme':'Tidal',
            'nature':id[0]=='N' or id in ['J08','J13','J14','J15','J16','J17','J18','J22','E08','E10'],
            'image':str(path.relative_to(ROOT)) if has_preview else None,'png':str(path.relative_to(ROOT)) if has_preview else None,'glb':model['path'],'size':png_size(path) if has_preview else [768,768],
            'description':item.get('description',item.get('referenceTarget',{}).get('geometry','Authored three-dimensional module.')),
            'status':impl.get('status','authored-3d-geometry').replace('-',' '),
            'limitations':impl.get('limitations',[]),'meshes':model['meshes'],'triangles':model['triangles'],
            'ports':list(model.get('ports',{})),'morphs':sum(m['count'] for m in model.get('morphTargets',[])),
            'tags':f"{id} {model['name']} {FAMILY_LABELS.get(id[0],'')} {item.get('behavior','')}",
        })
    return {'items':items,'compositions':compositions,'modelCount':len(models),'artCount':len(artwork),
            'previewCount':sum(item['type']=='geometry' and bool(item['image']) for item in items),'catalogueCount':len(elements),
            'compositionCount':len(compositions),'familyLabels':FAMILY_LABELS,'themes':THEMES,
            'complete':not missing and len(rendered_ids)==len(models) and len(artwork)==24 and not report.get('errors'),
            'edition':'package'}


HTML = r'''<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="color-scheme" content="dark"><title>Opaline · Visual element library</title>
<style>
:root{--bg:#07151f;--panel:#0b202d;--line:#c2edee1d;--ink:#eaf9f7;--muted:#98b5bd;--aqua:#a5ecdf;--blue:#94cde6}
*{box-sizing:border-box}[hidden]{display:none!important}.no-preview{display:flex;flex-direction:column;align-items:center;justify-content:center;height:100%;min-height:180px;color:#9fc8d5;font-size:16px;letter-spacing:.05em}.no-preview span{font-size:10px;color:#638797;margin-top:9px}body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.55 system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif}button,input,select{font:inherit}button,a,input,select{outline-offset:5px}a{color:inherit;text-decoration:none}button{cursor:pointer}button:disabled{cursor:default;opacity:.3}button:focus-visible,a:focus-visible,input:focus-visible,select:focus-visible{outline:2px solid var(--aqua)}
.wrap{max-width:1512px;margin:auto;padding:0 5vw}.top{display:flex;align-items:center;justify-content:space-between;gap:20px;padding-top:26px;padding-bottom:26px;border-bottom:1px solid var(--line)}.brand{font-size:19px;letter-spacing:.27em;font-weight:600}.brand i{display:inline-block;width:15px;height:22px;border-radius:50% 50% 48% 52%;transform:rotate(30deg);margin-right:16px;background:linear-gradient(145deg,#e0fff5,#5dacc5 45%,#164666);box-shadow:inset -3px -3px 6px #06172388}.edition{font-size:11px;text-transform:uppercase;letter-spacing:.15em;color:var(--muted)}.toplinks{display:flex;gap:20px;align-items:center;color:var(--muted);font-size:12px}.toplinks a:hover{color:var(--aqua)}
.hero{display:grid;grid-template-columns:1.1fr 1fr;min-height:510px;align-items:center;gap:3vw;padding-top:45px;padding-bottom:50px}.eyebrow{text-transform:uppercase;letter-spacing:.19em;font-size:10px;color:var(--aqua);margin-bottom:22px}.hero h1{font-size:clamp(44px,5.5vw,80px);line-height:1.06;letter-spacing:-.055em;font-weight:400;max-width:750px;margin:0 0 25px}.hero h1 span{color:var(--blue)}.hero p{color:var(--muted);font-size:15px;max-width:455px;margin:0 0 25px}.stats{display:flex;gap:35px;margin-top:35px}.stat b{display:block;font-weight:400;font-size:25px}.stat small{color:var(--muted);font-size:11px}.hero-art{position:relative;min-height:370px;background:radial-gradient(ellipse at center,#1d516331,transparent 70%);border-radius:50%}.hero-art img{width:100%;height:390px;object-fit:contain;filter:drop-shadow(0 20px 45px #0004)}.hero-caption{position:absolute;bottom:0;left:50%;transform:translateX(-50%);white-space:nowrap;color:var(--muted);font-size:10px;letter-spacing:.13em;text-transform:uppercase}.hero-cta{display:inline-flex;gap:22px;align-items:center;border:1px solid #80cbbf66;border-radius:30px;color:var(--aqua);font-size:12px;padding:11px 19px;background:#a5ecdf06}.hero-cta:hover{background:#a5ecdf12}
.catalogue{padding-bottom:70px}.tabs{display:flex;gap:27px;border-bottom:1px solid var(--line);overflow-x:auto;padding-bottom:0}.tab{border:0;background:none;color:var(--muted);padding:14px 0;font-size:12px;white-space:nowrap;border-bottom:2px solid transparent}.tab.active{color:var(--ink);border-color:var(--aqua)}.tab .n{font-size:9px;margin-left:6px;color:#789ca8}.toolbar{display:flex;align-items:center;gap:12px;padding:26px 0 18px}.search{flex:1;min-width:150px;background:var(--panel);border:1px solid var(--line);color:var(--ink);border-radius:9px;padding:12px 15px}.search::placeholder{color:#708f9c}.filter{background:var(--panel);border:1px solid var(--line);color:var(--muted);padding:12px 34px 12px 13px;border-radius:9px;font-size:12px;max-width:250px}.filter option{background:var(--panel)}.resultline{display:flex;justify-content:space-between;gap:20px;color:var(--muted);font-size:11px;padding-bottom:20px}.resultline .small-note{color:#698b99;max-width:620px;text-align:right}.grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:19px}.card{min-width:0;background:linear-gradient(160deg,#102b39,#0a1f2b);border:1px solid #bdebf016;border-radius:14px;overflow:hidden;transition:border-color .2s,transform .2s}.card:hover{border-color:#a5ecdf66;transform:translateY(-3px)}.picture{position:relative;display:block;border:0;background:radial-gradient(ellipse at center,#34566b18,transparent 75%);width:100%;padding:12px;aspect-ratio:1;cursor:zoom-in}.picture img{width:100%;height:100%;object-fit:contain}.card.artwork .picture{background:radial-gradient(ellipse at center,#48657622,#0b202a)}.card.backdrop .picture{padding:0}.card.backdrop .picture img{object-fit:cover}.badge{position:absolute;left:13px;top:13px;font-size:8px;letter-spacing:.1em;text-transform:uppercase;border:1px solid #b0dae124;color:#bad8df;background:#0a202cd9;padding:4px 6px;border-radius:4px}.card-body{padding:15px 16px 17px;border-top:1px solid #c2edee0c}.card-title{font-size:14px;font-weight:450;margin:0 0 4px;line-height:1.35}.card-meta{font-size:10px;color:#7599a8;display:flex;justify-content:space-between;gap:10px}.card-foot{display:flex;justify-content:space-between;gap:10px;margin-top:14px;align-items:center}.tiny{font-size:9px;color:#5e8393}.asset-links{display:flex;gap:10px}.asset-link{font-size:9px;letter-spacing:.09em;text-transform:uppercase;color:var(--aqua);border-bottom:1px solid #a5ecdf35}.empty{padding:70px 0;text-align:center;color:var(--muted)}
.recipes{width:100%;border-collapse:collapse;text-align:left;font-size:12px}.recipes th{padding:14px 13px;color:#7194a2;font-weight:500;letter-spacing:.08em;font-size:9px;text-transform:uppercase;border-bottom:1px solid var(--line)}.recipes td{padding:16px 13px;border-bottom:1px solid #b8ebee10;vertical-align:top}.recipes td:first-child{color:var(--aqua);font-size:10px;white-space:nowrap}.recipes .recipe-name{font-size:13px;color:var(--ink);font-weight:450;margin-bottom:3px}.recipe-desc{color:var(--muted);font-size:11px;max-width:500px}.code-pills{display:flex;gap:4px;flex-wrap:wrap;max-width:270px}.code-pill{color:#9dcad3;border:1px solid #b0dae11b;background:#17354466;font-size:9px;padding:2px 5px;border-radius:4px}.recipe-btn{border:1px solid #a5ecdf35;border-radius:6px;background:transparent;color:var(--aqua);font-size:10px;padding:7px 10px;white-space:nowrap}.recipe-json td{padding:0}.recipe-json pre{overflow:auto;max-height:420px;padding:20px;background:#04111a;color:#a9cad4;font:11px/1.7 ui-monospace,monospace;white-space:pre-wrap;margin:0}.docs{border-top:1px solid var(--line);padding:36px 0 45px;display:flex;gap:35px;justify-content:space-between}.docs h2{font-size:15px;font-weight:450;margin:0 0 8px}.docs p{font-size:11px;color:var(--muted);max-width:560px;margin:0}.doclinks{display:flex;flex-wrap:wrap;gap:12px 23px;font-size:11px;align-content:flex-start;max-width:550px}.doclinks a{color:var(--aqua)}footer{font-size:10px;color:#648493;padding:20px 0 35px}.standalone-note{display:none;color:#759baa;font-size:10px;margin-top:10px}body.standalone .standalone-note{display:block}
.modal{position:fixed;inset:0;background:#031019f5;backdrop-filter:blur(10px);z-index:10;display:none;padding:30px;overflow:auto}.modal.open{display:grid;grid-template-columns:minmax(0,1fr) 325px;gap:32px;align-items:center}.modal-view{height:calc(100vh - 80px);min-height:350px;display:flex;align-items:center;justify-content:center}.modal-view img{max-width:100%;max-height:100%;object-fit:contain}.modal-side{border-left:1px solid var(--line);padding:24px}.modal-side h2{font-size:27px;font-weight:400;letter-spacing:-.03em;line-height:1.2;margin:15px 0}.modal-side p{font-size:12px;color:var(--muted);line-height:1.8}.modal-label{font-size:10px;letter-spacing:.15em;text-transform:uppercase;color:var(--aqua)}.modal-close{position:fixed;right:25px;top:20px;background:#112b39;border:1px solid var(--line);width:35px;height:35px;border-radius:50%;font-size:20px;color:var(--ink)}.modal-nav{display:flex;gap:10px;margin-top:25px}.modal-nav button{background:#102b39;color:var(--aqua);border:1px solid var(--line);padding:9px 15px;border-radius:7px}.modal-facts{border-top:1px solid var(--line);padding-top:15px;margin-top:20px;font-size:11px;color:#79a0ae}.modal-facts div{margin-bottom:7px}.modal-links{display:flex;gap:20px;margin-top:25px;font-size:12px;color:var(--aqua)}
@media(min-width:1700px){.grid{grid-template-columns:repeat(5,minmax(0,1fr))}}@media(max-width:1000px){.grid{grid-template-columns:repeat(3,minmax(0,1fr))}.hero{min-height:440px}.hero-art img{height:300px}.hero-art{min-height:330px}.hero h1{font-size:57px}.modal.open{grid-template-columns:minmax(0,1fr) 280px}.modal-side{padding:15px}}
@media(max-width:720px){.wrap{padding-left:22px;padding-right:22px}.top{padding-top:22px;padding-bottom:22px}.toplinks a{display:none}.brand{font-size:15px}.hero{grid-template-columns:1fr;gap:0;padding-top:35px;padding-bottom:35px}.hero h1{font-size:51px;max-width:500px}.hero-art{min-height:280px;order:-1}.hero-art img{height:260px}.hero-caption{bottom:14px}.eyebrow{margin-bottom:16px}.hero p{font-size:13px}.stats{gap:29px;margin-top:25px}.grid{grid-template-columns:repeat(2,minmax(0,1fr));gap:12px}.toolbar{flex-wrap:wrap;padding-top:20px}.search{flex-basis:100%}.filter{flex:1;padding:10px;max-width:none}.tabs{gap:20px}.resultline .small-note{display:none}.card-body{padding:12px}.card-title{font-size:12px}.card-meta{font-size:9px}.badge{font-size:7px;left:9px;top:9px}.tiny{display:none}.docs{flex-direction:column;gap:22px}.recipes th:nth-child(3),.recipes td:nth-child(3){display:none}.recipes td{padding:13px 5px}.recipes th{padding:13px 5px}.recipes .recipe-name{font-size:12px}.recipe-desc{font-size:10px}.modal.open{display:block;padding:24px}.modal-view{height:55vh;min-height:250px}.modal-side{border-left:0;border-top:1px solid var(--line)}.modal-side h2{font-size:23px}.modal-close{top:15px;right:15px}.modal-nav{margin-bottom:30px}}
@media(prefers-reduced-motion:reduce){*{scroll-behavior:auto!important;transition:none!important}}
</style></head><body>
<div class="wrap"><header class="top"><a class="brand" href="#top"><i aria-hidden="true"></i>OPALINE</a><div class="toplinks"><span class="edition" id="edition">Offline visual catalogue</span><a href="docs/START-HERE.md">Start here</a><a href="index.html">Interactive workbench ↗</a></div></header>
<section class="hero" id="top"><div><div class="eyebrow">A shared world of liquid, light & living forms</div><h1>Nature,<br><span>made tangible.</span></h1><p>A text-free collection of sculpted controls, liquid surfaces, glass foliage and spatial UI assemblies. Browse the artwork, inspect the 3D parts, then combine them.</p><a class="hero-cta" href="#library">Explore the collection <span>↗</span></a><div class="stats"><div class="stat"><b id="stat-models">157</b><small>3D modules</small></div><div class="stat"><b id="stat-art">24</b><small>Generated artworks</small></div><div class="stat"><b id="stat-recipes">76</b><small>UI compositions</small></div></div></div><div class="hero-art"><img id="hero-image" alt="Translucent nature-inspired leaf compartment tray"><div class="hero-caption">Leaf tray · original ImageGen artwork</div></div></section>
<main class="catalogue" id="library"><nav class="tabs" aria-label="Collection views"><button class="tab active" data-mode="nature">Nature focus</button><button class="tab" data-mode="all">All assets <span class="n" id="n-all"></span></button><button class="tab" data-mode="artwork">Generated artwork <span class="n" id="n-art"></span></button><button class="tab" data-mode="geometry">3D modules <span class="n" id="n-geometry"></span></button><button class="tab" data-mode="compositions">UI compositions <span class="n" id="n-recipes"></span></button></nav>
<div class="toolbar"><input class="search" type="search" id="search" placeholder="Search forms, materials, IDs or behaviors…" aria-label="Search library"><select class="filter" id="family" aria-label="Filter by family"><option value="all">All families</option></select><select class="filter" id="theme" aria-label="Filter by theme"><option value="all">All themes</option></select><select class="filter" id="recipe-category" aria-label="Filter compositions by category" hidden><option value="all">All composition categories</option></select></div>
<div class="resultline"><span id="result-count" role="status" aria-live="polite"></span><span class="small-note" id="filter-note">Original artwork and genuine WebGL geometry renders are labelled separately.</span></div><div class="grid" id="grid"></div><div id="recipe-table" hidden></div><div class="empty" id="empty" hidden>No matching elements. Try another name or reset the filters.</div></main>
<section class="docs"><div><h2>Made to be assembled.</h2><p>The package includes authored 3D geometry, physical material code, motion systems and composition recipes. ImageGen artwork supplies visual direction; the mesh previews show the actual reusable models. Liquid simulation and interaction run in the workbench.</p><p class="standalone-note">All displayed images are embedded in this standalone file. Model, source-PNG and documentation links open within the extracted library package.</p></div><div class="doclinks"><a href="docs/START-HERE.md">Start here ↗</a><a href="docs/ASSEMBLY-API.md">Assembly API ↗</a><a href="docs/PNG-TO-MOTION.md">Artwork to motion ↗</a><a href="docs/IMPLEMENTATION-STATUS.md">Implementation status ↗</a><a href="docs/AI-HANDOFF.md">AI handoff ↗</a><a href="docs/COMPOSITION-INDEX.md">Composition index ↗</a><a href="catalogue/compositions.json">Composition JSON ↗</a><a href="assets/models/models-manifest.json">Model manifest ↗</a></div></section><footer>OPALINE / 3D ELEMENT LIBRARY · Static offline visual catalogue · No Android integration</footer></div>
<div class="modal" id="modal" role="dialog" aria-modal="true" aria-labelledby="modal-title"><button class="modal-close" id="modal-close" aria-label="Close image detail">×</button><div class="modal-view"><img id="modal-image" alt=""><div class="no-preview" id="modal-unrendered" hidden>3D model ready<span>Download the GLB to inspect its geometry.</span></div></div><aside class="modal-side"><div class="modal-label" id="modal-label"></div><h2 id="modal-title"></h2><p id="modal-description"></p><div class="modal-facts" id="modal-facts"></div><div class="modal-links" id="modal-links"></div><div class="modal-nav"><button id="previous" aria-label="Previous image">← Previous</button><button id="next" aria-label="Next image">Next →</button></div></aside></div>
<script id="catalogue-data" type="application/json">__DATA__</script>
<script>
'use strict';
const DATA=JSON.parse(document.getElementById('catalogue-data').textContent),$=id=>document.getElementById(id),state={mode:'nature',query:'',family:'all',theme:'all',category:'all',visible:[],index:0},fmt=n=>n.toLocaleString();
const esc=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
if(DATA.edition==='standalone'){document.body.classList.add('standalone');$('edition').textContent='Standalone · all images embedded';}
$('stat-models').textContent=DATA.modelCount;$('stat-art').textContent=DATA.artCount;$('stat-recipes').textContent=DATA.compositionCount;
$('n-all').textContent=DATA.items.length;$('n-art').textContent=DATA.artCount;$('n-geometry').textContent=DATA.modelCount;$('n-recipes').textContent=DATA.compositionCount;
const hero=DATA.items.find(x=>x.id==='nature-leaf-tray')||DATA.items.find(x=>x.nature);if(hero)$('hero-image').src=hero.image;
for(const family of [...new Set(DATA.items.map(x=>x.family))]){const o=document.createElement('option');o.value=family;o.textContent=DATA.familyLabels[family]||family;$('family').append(o);}
for(const theme of [...DATA.themes,'Shared']){const o=document.createElement('option');o.value=theme;o.textContent=theme==='Shared'?'Shared artwork':theme;$('theme').append(o);}
for(const category of [...new Set(DATA.compositions.map(x=>x.category))].sort()){const o=document.createElement('option');o.value=category;o.textContent=category.replace(/-/g,' ');$('recipe-category').append(o);}
function render(){const recipe=state.mode==='compositions';$('grid').hidden=recipe;$('recipe-table').hidden=!recipe;$('family').hidden=recipe;$('theme').hidden=recipe;$('recipe-category').hidden=!recipe;
 if(recipe){renderRecipes();return;}
 const q=state.query.toLowerCase();state.visible=DATA.items.filter(x=>(state.mode==='all'||state.mode==='nature'&&x.nature||x.type===state.mode)&&(state.family==='all'||x.family===state.family)&&(state.theme==='all'||x.theme===state.theme||x.theme==='Shared'&&state.theme!=='Shared')&&(`${x.name} ${x.id} ${x.tags} ${x.familyName} ${x.description}`).toLowerCase().includes(q));
 $('grid').replaceChildren();$('result-count').textContent=`${state.visible.length} ${state.mode==='nature'?'nature-inspired assets':'assets'} · click to inspect`;$('empty').hidden=state.visible.length>0;
 $('filter-note').textContent=state.mode==='geometry'?`${DATA.previewCount} PNG renders · all ${DATA.modelCount} GLB modules available · Tidal materials`:'Original artwork and genuine WebGL geometry renders are labelled separately.';
 state.visible.forEach((x,i)=>{const card=document.createElement('article');card.className=`card ${x.type} ${x.family==='Backdrops'?'backdrop':''}`;card.innerHTML=`<button class="picture" aria-label="Inspect ${esc(x.name)}">${x.image?`<img loading="lazy" decoding="async" src="${x.image}" alt="${esc(x.name)}">`:`<div class="no-preview">3D model ready<span>PNG preview not included</span></div>`}<span class="badge">${esc(x.label)}</span></button><div class="card-body"><h2 class="card-title">${esc(x.name)}</h2><div class="card-meta"><span>${esc(x.type==='geometry'?x.id+' · '+x.familyName:x.familyName)}</span><span>${esc(x.theme)}</span></div><div class="card-foot"><span class="tiny">${x.type==='geometry'?fmt(x.triangles)+' triangles':x.size.join(' × ')+' px'}</span><div class="asset-links">${x.png?`<a class="asset-link" href="${esc(x.png)}" download>PNG ↗</a>`:''}${x.glb?`<a class="asset-link" href="${esc(x.glb)}" download>GLB ↗</a>`:''}</div></div></div>`;card.querySelector('button').addEventListener('click',()=>openModal(i));$('grid').append(card);});
}
function renderRecipes(){const q=state.query.toLowerCase(),items=DATA.compositions.filter(x=>(state.category==='all'||x.category===state.category)&&(`${x.id} ${x.name} ${x.description} ${x.parts.map(p=>p.element).join(' ')} ${x.category}`).toLowerCase().includes(q));$('result-count').textContent=`${items.length} composition recipes`;$('filter-note').textContent='Text-free assemblies · content frames remain ready for application labels';$('empty').hidden=items.length>0;
 $('recipe-table').innerHTML='<table class="recipes"><thead><tr><th>ID</th><th>Composition</th><th>Parts</th><th>Recipe</th></tr></thead><tbody></tbody></table>';const tbody=$('recipe-table').querySelector('tbody');for(const item of items){const row=document.createElement('tr');row.innerHTML=`<td>${esc(item.id)}</td><td><div class="recipe-name">${esc(item.name)}</div><div class="recipe-desc">${esc(item.description)}</div></td><td><div class="code-pills">${[...new Set(item.parts.map(p=>p.element))].map(id=>`<span class="code-pill">${esc(id)}</span>`).join('')}</div></td><td><button class="recipe-btn" aria-expanded="false">View JSON</button></td>`;const detail=document.createElement('tr');detail.className='recipe-json';detail.hidden=true;const cell=document.createElement('td');cell.colSpan=4;const pre=document.createElement('pre');pre.textContent=JSON.stringify(item,null,2);cell.append(pre);detail.append(cell);row.querySelector('button').addEventListener('click',e=>{detail.hidden=!detail.hidden;e.target.setAttribute('aria-expanded',String(!detail.hidden));e.target.textContent=detail.hidden?'View JSON':'Close JSON';});tbody.append(row,detail);}}
let focusBeforeModal=null;
function openModal(index){state.index=index;const x=state.visible[index];if(!$('modal').classList.contains('open'))focusBeforeModal=document.activeElement;$('modal-image').hidden=!x.image;$('modal-unrendered').hidden=!!x.image;if(x.image)$('modal-image').src=x.image;$('modal-image').alt=x.name;$('modal-label').textContent=x.label;$('modal-title').textContent=x.name;$('modal-description').textContent=x.description;
 const facts=[x.type==='geometry'?`${x.id} · ${x.familyName}`:x.familyName,x.image?`Original PNG · ${x.size.join(' × ')} px`:`GLB included · PNG preview pending`,x.type==='geometry'?`${fmt(x.meshes)} meshes · ${fmt(x.triangles)} triangles`:`${x.theme==='Shared'?'Shared art direction':x.theme+' theme'}`];if(x.morphs)facts.push(`${x.morphs} exported morph targets`);if(x.related?.length)facts.push('Related geometry: '+x.related.join(', '));if(x.type==='geometry')facts.push(x.status);$('modal-facts').replaceChildren();for(const text of facts){const p=document.createElement('div');p.textContent=text;$('modal-facts').append(p);}
 $('modal-links').innerHTML=`${x.png?`<a href="${esc(x.png)}" download>Original PNG ↗</a>`:''}${x.glb?`<a href="${esc(x.glb)}" download>3D model ↗</a>`:''}`;$('previous').disabled=index===0;$('next').disabled=index===state.visible.length-1;$('modal').classList.add('open');document.body.style.overflow='hidden';$('modal-close').focus();}
function closeModal(){$('modal').classList.remove('open');document.body.style.overflow='';focusBeforeModal?.focus();}
$('modal-close').addEventListener('click',closeModal);$('previous').addEventListener('click',()=>{if(state.index>0)openModal(state.index-1)});$('next').addEventListener('click',()=>{if(state.index<state.visible.length-1)openModal(state.index+1)});
document.addEventListener('keydown',e=>{if(!$('modal').classList.contains('open'))return;if(e.key==='Escape')closeModal();if(e.key==='ArrowLeft'&&state.index>0)openModal(state.index-1);if(e.key==='ArrowRight'&&state.index<state.visible.length-1)openModal(state.index+1);if(e.key==='Tab'){const nodes=[...$('modal').querySelectorAll('button:not(:disabled),a[href]')],first=nodes[0],last=nodes[nodes.length-1];if(e.shiftKey&&document.activeElement===first){e.preventDefault();last.focus();}else if(!e.shiftKey&&document.activeElement===last){e.preventDefault();first.focus();}}});
for(const b of document.querySelectorAll('.tab'))b.addEventListener('click',()=>{state.mode=b.dataset.mode;for(const t of document.querySelectorAll('.tab'))t.classList.toggle('active',t===b);state.family='all';$('family').value='all';render();});
$('search').addEventListener('input',e=>{state.query=e.target.value;render();});$('family').addEventListener('change',e=>{state.family=e.target.value;render();});$('theme').addEventListener('change',e=>{state.theme=e.target.value;render();});$('recipe-category').addEventListener('change',e=>{state.category=e.target.value;render();});render();
</script></body></html>'''


def build_html(data):
    # Prevent an asset description containing a closing script tag from escaping JSON.
    encoded = json.dumps(data, separators=(',', ':'), ensure_ascii=False).replace('<', '\\u003c')
    return HTML.replace('__DATA__', encoded)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--standalone',type=Path,help='Also write a standalone HTML with all original PNG bytes embedded.')
    parser.add_argument('--allow-incomplete',action='store_true',help='Build a review draft while preview generation is still running.')
    args=parser.parse_args()
    data=make_data(args.allow_incomplete)
    package_path=ROOT/'Opaline-Visual-Library.html'
    package_path.write_text(build_html(data))
    print(f'Package catalogue: {package_path} ({package_path.stat().st_size:,} bytes)')
    if args.standalone:
        standalone=json.loads(json.dumps(data));standalone['edition']='standalone'
        for item in standalone['items']:
            if item['image']:
                raw=(ROOT/item['image']).read_bytes()
                item['image']='data:image/png;base64,'+base64.b64encode(raw).decode('ascii')
            for key in ['png','glb']:
                if item.get(key): item[key]='Opaline-3D-Library/'+item[key]
        target=args.standalone.resolve();target.parent.mkdir(parents=True,exist_ok=True);target.write_text(build_html(standalone).replace('href="docs/','href="Opaline-3D-Library/docs/').replace('href="catalogue/','href="Opaline-3D-Library/catalogue/').replace('href="assets/','href="Opaline-3D-Library/assets/').replace('href="index.html"','href="Opaline-3D-Library/index.html"'))
        print(f'Standalone catalogue: {target} ({target.stat().st_size:,} bytes)')
    print(json.dumps({'artwork':data['artCount'],'meshPreviews':data['previewCount'],'compositions':data['compositionCount'],'complete':data['complete']}))


if __name__=='__main__':
    main()
