#!/usr/bin/env python3
"""Builds the translated copies of the Pages site (docs/site/) from the English pages.

English is the only source. `extract` lists every translatable unit of the English pages in
docs/site/i18n/en.json; a translator fills docs/site/i18n/<lang>.json (English unit -> its
translation); `build` writes docs/site/<dir>/<page> for each language, with the page's links,
canonical, hreflang alternates, language switcher and localized Play badge set for it, and
refreshes the same slots in the English pages and the sitemap. `check` rebuilds in memory and
fails when a committed file differs, so CI catches pages that drifted from their English source.

A unit is the outermost element whose content is only text and inline tags (a, span, b, small,
...), taken whole with its markup so a language can reorder words around a link or a tag. A
translation must keep the unit's tags and every href/src in the same order; `build` refuses one
that does not. Attributes outside units (alt, aria-label, title, data-again) and the page's
title, description and Open Graph text are units too.

    python3 tools/site_i18n.py extract
    python3 tools/site_i18n.py build
    python3 tools/site_i18n.py check
    python3 tools/site_i18n.py lint de   # one translation: complete, markup kept
"""

import json
import re
import sys
from html.parser import HTMLParser
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SITE = ROOT / "docs" / "site"
I18N = SITE / "i18n"
BASE = "https://beatrealms3d.pages.dev/"
PAGES = ["index.html", "features.html"]
# code, name in its own language, folder, Google Play badge locale, label of the switcher
LANGS = [
    ("en", "English", "", "en", "Language"),
    ("de", "Deutsch", "de/", "de", "Sprache"),
    ("fr", "Français", "fr/", "fr", "Langue"),
    ("es", "Español", "es/", "es", "Idioma"),
    ("pt-BR", "Português (Brasil)", "pt-br/", "pt-br", "Idioma"),
    ("ja", "日本語", "ja/", "ja", "言語"),
    ("it", "Italiano", "it/", "it", "Lingua"),
]
INLINE = {"a", "span", "b", "strong", "em", "i", "br", "small", "code", "sup", "sub", "img"}
VOID = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "source", "track", "wbr"}
SKIP = {"html", "head", "body", "script", "style"}
ATTRS = {"alt", "aria-label", "title", "data-again"}
META = {("name", "description"), ("property", "og:title"), ("property", "og:description"), ("property", "og:image:alt")}
SLOT = re.compile(r"(<!-- i18n:(\w+) -->\n)(.*?)([ \t]*<!-- /i18n:\2 -->)", re.S)


def norm(s):
    return re.sub(r"\s+", " ", s).strip()


class Node:
    def __init__(self, tag, attrs, start, open_end, parent):
        self.tag, self.attrs, self.start, self.open_end, self.parent = tag, attrs, start, open_end, parent
        self.children, self.text, self.inner_end = [], False, None

    def inline_content(self):
        """Only text and inline elements below, with some text somewhere."""
        if self.tag in SKIP:
            return False
        return all(c.tag in INLINE and c.inline_content_or_void() for c in self.children) and self.has_text()

    def inline_content_or_void(self):
        return all(c.tag in INLINE and c.inline_content_or_void() for c in self.children)

    def has_text(self):
        return self.text or any(c.has_text() for c in self.children)


class Tree(HTMLParser):
    def __init__(self, src):
        super().__init__(convert_charrefs=False)
        self.src, self.root = src, Node("#root", [], 0, 0, None)
        self.cur, self.lines = self.root, [0]
        for m in re.finditer("\n", src):
            self.lines.append(m.end())
        self.feed(src)
        self.close()

    def off(self):
        line, col = self.getpos()
        return self.lines[line - 1] + col

    def handle_starttag(self, tag, attrs):
        start = self.off()
        node = Node(tag, attrs, start, start + len(self.get_starttag_text()), self.cur)
        self.cur.children.append(node)
        if tag not in VOID:
            self.cur = node

    def handle_startendtag(self, tag, attrs):
        start = self.off()
        self.cur.children.append(Node(tag, attrs, start, start + len(self.get_starttag_text()), self.cur))

    def handle_endtag(self, tag):
        node = self.cur
        while node is not self.root and node.tag != tag:
            node = node.parent
        if node is self.root:
            return
        node.inner_end = self.off()
        self.cur = node.parent

    def handle_data(self, data):
        if self.cur.tag not in ("script", "style") and re.search(r"[^\W\d_]", data):
            self.cur.text = True


def blank_slots(src):
    return SLOT.sub(lambda m: m.group(1) + " " * len(m.group(3)) + m.group(4), src)


def units(src):
    """(start, end, key) for each unit's inner HTML, and for each translatable attribute value."""
    tree = Tree(blank_slots(src))
    out = []

    def walk(node, in_unit):
        if node.tag in ("script", "style"):
            return
        unit = not in_unit and node.tag != "#root" and node.inline_content()
        if unit and node.inner_end is not None:
            inner = src[node.open_end:node.inner_end]
            out.append((node.open_end, node.inner_end, norm(inner)))
        if not in_unit:
            attrs = dict(node.attrs)
            for name, value in node.attrs:
                translatable = name in ATTRS or (node.tag == "meta" and name == "content" and any(attrs.get(k) == v for k, v in META))
                if translatable and value and re.search(r"[^\W\d_]", value):
                    tag_text = src[node.start:node.open_end]
                    m = re.search(r'\s%s="([^"]*)"' % re.escape(name), tag_text)
                    if m:
                        out.append((node.start + m.start(1), node.start + m.end(1), norm(value)))
        for c in node.children:
            walk(c, in_unit or unit)

    walk(tree.root, False)
    return sorted(out)


def shape(html):
    """The markup a translation must keep: tag names and every href/src, in order."""
    return [(m.group(1).lower(), tuple(re.findall(r'\s(?:href|src)="([^"]*)"', m.group(0))))
            for m in re.finditer(r"</?([a-zA-Z][\w-]*)[^>]*>", html)]


def translate(src, table, lang):
    pieces, last, missing, bad = [], 0, [], []
    for start, end, key in units(src):
        value = table.get(key)
        if value is None:
            missing.append(key)
            value = src[start:end]
        elif shape(value) != shape(key):
            bad.append(key)
        pieces.append(src[last:start])
        pieces.append(value)
        last = end
    pieces.append(src[last:])
    if missing or bad:
        lines = [f"{lang}: {len(missing)} missing, {len(bad)} with changed markup"]
        lines += [f"  missing: {k[:100]}" for k in missing[:20]] + [f"  markup: {k[:100]}" for k in bad[:20]]
        raise SystemExit("\n".join(lines))
    return "".join(pieces)


def relocate(html):
    """Paths in a page one folder down: assets and the policy go up a level; the two pages stay."""
    def fix(m):
        attr, url = m.group(1), m.group(2)
        if re.match(r"(https?:|mailto:|#|data:|/)", url) or url in ("./", "") or re.match(r"(index|features)\.html(#.*)?$", url):
            return m.group(0)
        return f'{attr}="../{url}"'
    return re.sub(r'\b(href|src|poster)="([^"]*)"', fix, html)


def alternates(page):
    path = "" if page == "index.html" else page
    links = [f'<link rel="alternate" hreflang="{code}" href="{BASE}{folder}{path}">' for code, _, folder, _, _ in LANGS]
    links.append(f'<link rel="alternate" hreflang="x-default" href="{BASE}{path}">')
    return "\n".join(links) + "\n"


def switcher(page, here):
    path = "" if page == "index.html" else page
    up = "" if here[2] == "" else "../"
    items = []
    for code, name, folder, _, _ in LANGS:
        href = f"{up}{folder}{path}" or "./"
        current = ' aria-current="page"' if code == here[0] else ""
        items.append(f'        <li><a href="{href}" lang="{code}" hreflang="{code}"{current}>{name}</a></li>')
    return (f'    <nav aria-label="{here[4]}">\n      <ul class="langs">\n' + "\n".join(items)
            + "\n      </ul>\n    </nav>\n")


def fill(src, page, lang):
    def slot(m):
        body = alternates(page) if m.group(2) == "alternates" else switcher(page, lang)
        return m.group(1) + body + m.group(4)
    return SLOT.sub(slot, src)


def page_for(page, lang, src, tables):
    code, _, folder, badge, _ = lang
    out = fill(src, page, lang)
    if not folder:
        return out
    out = translate(out, tables[code], code)
    out = out.replace('<html lang="en">', f'<html lang="{code}">', 1)
    path = "" if page == "index.html" else page
    out = out.replace(f'<link rel="canonical" href="{BASE}{path}">', f'<link rel="canonical" href="{BASE}{folder}{path}">', 1)
    out = out.replace("assets/google-play-badge.png", f"assets/badges/{badge}.png")
    return relocate(out)


def sitemap():
    rows = ['<?xml version="1.0" encoding="UTF-8"?>',
            '<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9" xmlns:xhtml="http://www.w3.org/1999/xhtml">']
    for page in PAGES:
        path = "" if page == "index.html" else page
        for _, _, folder, _, _ in LANGS:
            rows.append(f"  <url>\n    <loc>{BASE}{folder}{path}</loc>")
            for code, _, f2, _, _ in LANGS:
                rows.append(f'    <xhtml:link rel="alternate" hreflang="{code}" href="{BASE}{f2}{path}"/>')
            rows.append(f'    <xhtml:link rel="alternate" hreflang="x-default" href="{BASE}{path}"/>\n  </url>')
    rows.append(f"  <url>\n    <loc>{BASE}privacy-policy.html</loc>\n  </url>")
    rows.append("</urlset>")
    return "\n".join(rows) + "\n"


def outputs():
    tables = {}
    for code, _, folder, _, _ in LANGS:
        if folder:
            tables[code] = json.loads((I18N / f"{code}.json").read_text(encoding="utf-8"))["units"]
    files = {}
    for page in PAGES:
        src = (SITE / page).read_text(encoding="utf-8")
        for lang in LANGS:
            files[SITE / lang[2] / page] = page_for(page, lang, src, tables)
    files[SITE / "sitemap.xml"] = sitemap()
    return files


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd == "extract":
        keys = []
        for page in PAGES:
            for _, _, key in units((SITE / page).read_text(encoding="utf-8")):
                if key not in keys:
                    keys.append(key)
        I18N.mkdir(exist_ok=True)
        (I18N / "en.json").write_text(json.dumps({"units": {k: k for k in keys}}, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
        print(f"{len(keys)} units -> {I18N / 'en.json'}")
    elif cmd == "build":
        for path, text in outputs().items():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")
            print(f"wrote {path.relative_to(ROOT)}")
    elif cmd == "lint" and len(sys.argv) > 2:
        code = sys.argv[2]
        src = json.loads((I18N / "en.json").read_text(encoding="utf-8"))["units"]
        got = json.loads((I18N / f"{code}.json").read_text(encoding="utf-8"))["units"]
        missing = [k for k in src if k not in got]
        extra = [k for k in got if k not in src]
        bad = [k for k in src if k in got and shape(got[k]) != shape(k)]
        same = [k for k in src if got.get(k) == k and re.search(r"[^\W\d_]{4}", re.sub(r"<[^>]+>", "", k))]
        for label, keys in (("missing", missing), ("not in en.json", extra), ("markup changed", bad), ("left in English", same)):
            for k in keys:
                print(f"{label}: {k[:120]}")
        print(f"{code}: {len(got)}/{len(src)} units, {len(missing)} missing, {len(bad)} markup changed, {len(same)} identical to English")
        if missing or bad or extra:
            raise SystemExit(1)
    elif cmd == "check":
        stale = [str(p.relative_to(ROOT)) for p, t in outputs().items() if not p.exists() or p.read_text(encoding="utf-8") != t]
        if stale:
            raise SystemExit("out of date (run tools/site_i18n.py build): " + ", ".join(stale))
        print("site translations up to date")
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
