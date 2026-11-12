#pragma once
#include <string_view>

namespace geode::viz::frame::link {

// The page "Show on a computer" serves at /<token>/: the visual full-window, with sound after one click (every
// browser blocks sound until the viewer acts). Three static files, the same for every phone and stream, with no
// user data in them, so the CSP can forbid inline script and style outright.
//
// How it plays: where Media Source Extensions take the stream's codecs (Chrome, Edge, Firefox, Safari on a Mac)
// player.js is the player: it reads index.m3u8 for the CODECS the phone's encoder really wrote, polls live.m3u8
// each second, appends the init segment and then each new fMP4 segment to one SourceBuffer, starts three
// segments back from the live edge as HLS players do, jumps to the edge when it falls more than six seconds
// behind, and drops what lies twenty seconds behind the playhead. Without MSE (Safari on an iPhone or iPad) the
// video element plays index.m3u8 itself, natively. Nothing is fetched from anywhere but the phone: no player
// library, no font, no icon.

inline constexpr std::string_view kPageHtml = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="referrer" content="no-referrer">
<title>BeatRealms 3D</title>
<link rel="stylesheet" href="player.css">
<script src="player.js" defer></script>
</head>
<body>
<video id="video" playsinline disablepictureinpicture></video>
<button id="play" type="button" disabled>Play</button>
<p id="note">Waiting for the phone&#8230;</p>
</body>
</html>
)HTML";

inline constexpr std::string_view kPageCss = R"CSS(html, body { margin: 0; height: 100%; background: #000; color: #eee; overflow: hidden;
  font: 16px system-ui, -apple-system, "Segoe UI", sans-serif; }
video { position: fixed; inset: 0; width: 100%; height: 100%; object-fit: contain; background: #000; }
#play { position: fixed; left: 50%; top: 50%; transform: translate(-50%, -50%); padding: 16px 48px; border: 0;
  border-radius: 999px; background: #fff; color: #000; font: inherit; font-size: 20px; cursor: pointer; }
#play:disabled { opacity: 0.4; cursor: default; }
#note { position: fixed; left: 0; right: 0; bottom: 24px; margin: 0; text-align: center; color: #bbb; }
[hidden] { display: none !important; }
)CSS";

inline constexpr std::string_view kPageJs = R"JS('use strict';
(() => {
  const video = document.getElementById('video');
  const play = document.getElementById('play');
  const note = document.getElementById('note');
  const MASTER = 'index.m3u8';
  const POLL_MS = 1000;
  const START_BACK = 3;
  const MAX_LAG_S = 6;
  const EDGE_S = 2;
  const KEEP_S = 20;
  const GIVE_UP_MS = 8000;
  const NAME = /^[A-Za-z0-9._-]+$/;
  const CODECS = /^[A-Za-z0-9.,]+$/;
  let stream = null;
  let lastOk = Date.now();
  let over = false;

  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  const say = (text) => { note.textContent = text; note.hidden = !text; };
  const end = () => { over = true; play.hidden = true; say('The phone stopped sharing.'); };

  async function get(uri, bytes) {
    if (!NAME.test(uri)) throw new Error('name');
    const r = await fetch(uri, { cache: 'no-store', credentials: 'omit' });
    if (!r.ok) throw new Error('status');
    lastOk = Date.now();
    return bytes ? r.arrayBuffer() : r.text();
  }

  function lines(text) { return text.split('\n').map((l) => l.trim()).filter((l) => l); }

  function parseMaster(text) {
    let codecs = null, uri = null, next = false;
    for (const line of lines(text)) {
      if (line.startsWith('#EXT-X-STREAM-INF:')) {
        const m = /CODECS="([^"]+)"/.exec(line);
        codecs = m && CODECS.test(m[1]) ? m[1] : null;
        next = true;
      } else if (next && !line.startsWith('#')) { uri = line; next = false; }
    }
    return codecs && uri ? { codecs, uri } : null;
  }

  function parseMedia(text) {
    let seq = 0, map = null;
    const segs = [];
    for (const line of lines(text)) {
      if (line.startsWith('#EXT-X-MEDIA-SEQUENCE:')) seq = parseInt(line.slice(22), 10) || 0;
      else if (line.startsWith('#EXT-X-MAP:')) { const m = /URI="([^"]+)"/.exec(line); map = m ? m[1] : null; }
      else if (!line.startsWith('#')) segs.push(line);
    }
    return { seq, map, segs };
  }

  const mime = (codecs) => 'video/mp4; codecs="' + codecs + '"';
  const mse = (codecs) => 'MediaSource' in window && MediaSource.isTypeSupported(mime(codecs));

  function whenDone(sb, act) {
    return new Promise((resolve, reject) => {
      const ok = () => { off(); resolve(); };
      const bad = () => { off(); reject(new Error('buffer')); };
      const off = () => { sb.removeEventListener('updateend', ok); sb.removeEventListener('error', bad); };
      sb.addEventListener('updateend', ok);
      sb.addEventListener('error', bad);
      try { act(); } catch (e) { off(); reject(e); }
    });
  }

  function chase() {
    const b = video.buffered;
    if (!b.length) return;
    const start = b.start(b.length - 1), edge = b.end(b.length - 1);
    if (video.currentTime < start || edge - video.currentTime > MAX_LAG_S) video.currentTime = Math.max(start, edge - EDGE_S);
  }

  async function trim(sb, keep) {
    const b = sb.buffered, upTo = video.currentTime - keep;
    if (b.length && b.start(0) < upTo) await whenDone(sb, () => sb.remove(b.start(0), upTo));
  }

  async function append(sb, data) {
    try {
      await whenDone(sb, () => sb.appendBuffer(data));
    } catch (e) {
      if (!e || e.name !== 'QuotaExceededError') throw e;
      await trim(sb, 2);
      await whenDone(sb, () => sb.appendBuffer(data));
    }
  }

  async function runMse(source, playlist, codecs) {
    await new Promise((r) => source.addEventListener('sourceopen', r, { once: true }));
    const sb = source.addSourceBuffer(mime(codecs));
    let init = null, next = -1;
    while (!over && source.readyState === 'open') {
      let list = null;
      try { list = parseMedia(await get(playlist, false)); } catch (e) {
        if (Date.now() - lastOk > GIVE_UP_MS) return end();
      }
      if (list && list.segs.length) {
        try {
          if (list.map && list.map !== init) { await append(sb, await get(list.map, true)); init = list.map; }
          const last = list.seq + list.segs.length - 1;
          if (next < list.seq || next > last + 1) next = Math.max(list.seq, last - START_BACK + 1);
          for (; next <= last; next++) await append(sb, await get(list.segs[next - list.seq], true));
          chase();
          await trim(sb, KEEP_S);
        } catch (e) { /* a segment left the window or a fetch failed: the next poll moves on */ }
      }
      await sleep(POLL_MS);
    }
  }

  // The native player only waits when the phone stops serving; the master playlist going away says so.
  async function watch() {
    while (!over) {
      await sleep(POLL_MS * 2);
      try { await get(MASTER, false); } catch (e) { if (Date.now() - lastOk > GIVE_UP_MS) return end(); }
    }
  }

  function start() {
    if (!stream || over) return;
    play.hidden = true;
    say('');
    video.muted = false;
    if (mse(stream.codecs)) {
      const source = new MediaSource();
      video.src = URL.createObjectURL(source);
      runMse(source, stream.uri, stream.codecs).catch(end);
    } else {
      video.src = MASTER;
      watch();
    }
    video.play().catch(() => { if (!over) { play.hidden = false; play.disabled = false; } });
  }

  async function wait() {
    while (!stream && !over) {
      try { stream = parseMaster(await get(MASTER, false)); } catch (e) {
        if (Date.now() - lastOk > GIVE_UP_MS) return end();
      }
      if (!stream) await sleep(POLL_MS);
    }
    if (over) return;
    const native = video.canPlayType('application/vnd.apple.mpegurl') !== '';
    if (!mse(stream.codecs) && !native) { play.hidden = true; return say('This browser cannot play the stream. Try Chrome, Edge, Firefox or Safari.'); }
    say('');
    play.disabled = false;
  }

  play.addEventListener('click', start);
  video.addEventListener('dblclick', () => {
    if (document.fullscreenElement) document.exitFullscreen().catch(() => {});
    else document.documentElement.requestFullscreen().catch(() => {});
  });
  video.addEventListener('error', () => { if (video.src && !over) end(); });
  wait();
})();
)JS";

}  // namespace geode::viz::frame::link
