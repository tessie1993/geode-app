#pragma once
#include <string_view>

namespace geode::viz::frame::link {

// The page "Show on a computer" serves at /<token>/: the visual full-window, with sound after one click (every
// browser blocks sound until the viewer acts). Three static files, the same for every phone and stream, with no
// user data in them, so the CSP can forbid inline script and style outright.
//
// How it plays: WebRTC first (owner, 2026-11-12: "Use webrtc protocol. To have smooth play", "Let browser buffer
// for 0.5 sec"). player.js offers to receive one video and one audio track, with no ICE server (nothing is asked
// of the internet), POSTs the offer to rtc and plays the phone's answer (core/cast/WebRtcLink): the encoder's own
// H.264 and the same sound as Opus, encrypted in transit (DTLS-SRTP). Each receiver is asked to hold half a
// second (jitterBufferTarget, 500 ms; playoutDelayHint, in seconds, where only that exists). The offer and answer
// themselves travel over this plain-http page. If WebRTC is missing, refused (503 or 400) or does not connect
// within six seconds, or fails later, the page falls back to the feed below, which is not encrypted.
//
// The fallback: where Media Source Extensions take the stream's codecs (Chrome, Edge, Firefox, Safari on a Mac)
// player.js is the player: it reads index.m3u8 for the CODECS the phone's encoder really wrote, then opens one
// long-lived fetch of stream.mp4, which the phone writes the init segment into and then every fMP4 fragment the
// moment the encoder cuts it (no playlist polling, no request per segment). Each chunk read is appended to one
// SourceBuffer as it arrives. The playhead is held one fragment plus CUSHION_S (0.5 s, owner) behind the newest
// frame, so the browser always keeps about half a second in hand however the fragments land: more than the
// target plus 0.75 s behind and it plays 4 % fast until back, more than 3 s and it jumps; a gap left by a
// dropped frame or a reconnect is jumped over. A feed that ends is opened again (the phone starts it at its
// newest fragment, after the init segment). What lies ten seconds behind the playhead is dropped. Without MSE
// (Safari on an iPhone or iPad) the video element plays index.m3u8 itself, natively, with HLS's own few seconds
// of delay. Nothing is fetched from anywhere but the phone: no player library, no font, no icon.

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
#pair { position: fixed; left: 50%; top: 50%; transform: translate(-50%, -50%); width: min(90vw, 360px);
  display: flex; flex-direction: column; align-items: center; gap: 16px; }
#code { width: 100%; box-sizing: border-box; padding: 12px; border: 1px solid #555; border-radius: 12px;
  background: #111; color: #fff; font: inherit; font-size: 28px; letter-spacing: 4px; text-align: center;
  text-transform: uppercase; }
#go { padding: 12px 40px; border: 0; border-radius: 999px; background: #fff; color: #000; font: inherit;
  font-size: 18px; cursor: pointer; }
#go:disabled { opacity: 0.4; cursor: default; }
)CSS";

inline constexpr std::string_view kPageJs = R"JS('use strict';
(() => {
  const video = document.getElementById('video');
  const play = document.getElementById('play');
  const note = document.getElementById('note');
  const MASTER = 'index.m3u8';
  const FEED = 'stream.mp4';
  const POLL_MS = 1000;
  // Half a second always in hand (owner). The phone sends a whole fragment at a time, so the playhead sits one
  // fragment plus this behind the newest frame.
  const CUSHION_S = 0.5;
  const CATCH_UP_S = 0.75;
  const JUMP_S = 3;
  const FAST = 1.04;
  const KEEP_S = 10;
  const GIVE_UP_MS = 8000;
  const RTC = 'rtc';
  // The browser's own receive buffer on WebRTC: half a second (owner), in ms.
  const JITTER_MS = 500;
  const GATHER_MS = 1500;
  const RTC_CONNECT_MS = 6000;
  const CODECS = /^[A-Za-z0-9.,]+$/;
  let stream = null;
  let lastOk = Date.now();
  let over = false;
  let begun = false;
  let placed = false;
  let fragment = 1;
  let lastEdge = -1;
  let pc = null;

  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  const say = (text) => { note.textContent = text; note.hidden = !text; };
  const end = () => { over = true; play.hidden = true; say('The phone stopped sharing.'); };

  async function get(uri) {
    const r = await fetch(uri, { cache: 'no-store', credentials: 'omit' });
    if (!r.ok) throw new Error('status');
    lastOk = Date.now();
    return r.text();
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

  const mime = (codecs) => 'video/mp4; codecs="' + codecs + '"';
  const mse = (codecs) => 'MediaSource' in window && MediaSource.isTypeSupported(mime(codecs));
  const target = () => fragment + CUSHION_S;

  // Holds the playhead target seconds behind the newest frame, inside the newest buffered range.
  function steer() {
    const b = video.buffered;
    if (!b.length) return;
    const last = b.length - 1;
    const start = b.start(last), edge = b.end(last);
    const grew = edge - lastEdge;
    if (lastEdge >= 0 && grew > 0.2 && grew < 3) fragment = 0.8 * fragment + 0.2 * grew;
    lastEdge = edge;
    const want = Math.max(start, edge - target());
    if (!placed) {
      if (edge - start < target()) return;
      placed = true;
      video.currentTime = want;
      return;
    }
    const lag = edge - video.currentTime;
    if (video.currentTime < start || lag > target() + JUMP_S) {
      video.currentTime = want;
      video.playbackRate = 1;
    } else if (lag > target() + CATCH_UP_S) {
      video.playbackRate = FAST;
    } else if (lag <= target()) {
      video.playbackRate = 1;
    }
  }

  // Appends what the feed delivered, a batch at a time (SourceBuffer takes one append at once), and drops what
  // lies KEEP_S behind the playhead first.
  function feeder(source, sb) {
    let queue = [];
    function next() {
      if (sb.updating || source.readyState !== 'open') return;
      const b = sb.buffered;
      const upTo = video.currentTime - KEEP_S;
      if (b.length && b.start(0) < upTo - 1) { sb.remove(b.start(0), upTo); return; }
      if (!queue.length) return;
      let n = 0;
      for (const c of queue) n += c.byteLength;
      const all = new Uint8Array(n);
      let at = 0;
      for (const c of queue) { all.set(c, at); at += c.byteLength; }
      queue = [];
      try {
        sb.appendBuffer(all);
      } catch (e) {
        if (!e || e.name !== 'QuotaExceededError') throw e;
        queue.unshift(all);
        if (b.length && b.start(0) < video.currentTime - 1) sb.remove(b.start(0), video.currentTime - 1);
      }
    }
    sb.addEventListener('updateend', () => { steer(); try { next(); } catch (e) { end(); } });
    return {
      push: (chunk) => { queue.push(chunk); next(); },
      // A new connection starts with the init segment: drop the cut-off tail of the last one and the parser's state.
      reset: () => { queue = []; if (source.readyState === 'open') sb.abort(); },
    };
  }

  async function feed(f) {
    while (!over) {
      try {
        const r = await fetch(FEED, { cache: 'no-store', credentials: 'omit' });
        if (!r.ok || !r.body) throw new Error('status');
        f.reset();
        const reader = r.body.getReader();
        for (;;) {
          const { done, value } = await reader.read();
          if (done || over) break;
          lastOk = Date.now();
          f.push(value);
        }
      } catch (e) { /* the phone closed the feed, or is feeding enough browsers: open it again */ }
      if (over) return;
      if (Date.now() - lastOk > GIVE_UP_MS) return end();
      await sleep(POLL_MS);
    }
  }

  // The native player only waits when the phone stops serving; the master playlist going away says so.
  async function watch() {
    while (!over) {
      await sleep(POLL_MS * 2);
      try { await get(MASTER); } catch (e) { if (Date.now() - lastOk > GIVE_UP_MS) return end(); }
    }
  }

  function hold(receiver) {
    try {
      if ('jitterBufferTarget' in receiver) receiver.jitterBufferTarget = JITTER_MS;
      else if ('playoutDelayHint' in receiver) receiver.playoutDelayHint = JITTER_MS / 1000;
    } catch (e) { /* the browser keeps its own buffer */ }
  }

  // Resolves once the browser has its candidates (or after GATHER_MS): one offer carries them all.
  function gathered(conn) {
    if (conn.iceGatheringState === 'complete') return Promise.resolve();
    return new Promise((resolve) => {
      conn.addEventListener('icegatheringstatechange', () => { if (conn.iceGatheringState === 'complete') resolve(); });
      setTimeout(resolve, GATHER_MS);
    });
  }

  function connected(conn) {
    return new Promise((resolve, reject) => {
      const check = () => {
        if (conn.connectionState === 'connected') resolve();
        else if (conn.connectionState === 'failed' || conn.connectionState === 'closed') reject(new Error('rtc'));
      };
      conn.addEventListener('connectionstatechange', check);
      setTimeout(() => reject(new Error('timeout')), RTC_CONNECT_MS);
      check();
    });
  }

  // WebRTC: true once the phone's picture is connected into media; false (and nothing left open) otherwise.
  async function viaRtc(media) {
    if (!('RTCPeerConnection' in window)) return false;
    let conn = null;
    try {
      conn = new RTCPeerConnection({ iceServers: [], bundlePolicy: 'max-bundle' });
      conn.addTransceiver('video', { direction: 'recvonly' });
      conn.addTransceiver('audio', { direction: 'recvonly' });
      conn.addEventListener('track', (e) => { hold(e.receiver); media.addTrack(e.track); });
      await conn.setLocalDescription(await conn.createOffer());
      await gathered(conn);
      const r = await fetch(RTC, {
        method: 'POST', cache: 'no-store', credentials: 'omit',
        headers: { 'Content-Type': 'application/sdp' }, body: conn.localDescription.sdp,
      });
      if (!r.ok) throw new Error('status');
      await conn.setRemoteDescription({ type: 'answer', sdp: await r.text() });
      await connected(conn);
      conn.getReceivers().forEach(hold);
    } catch (e) {
      if (conn) conn.close();
      return false;
    }
    pc = conn;
    lastOk = Date.now();
    conn.addEventListener('connectionstatechange', () => {
      if (pc !== conn || over) return;
      if (conn.connectionState === 'failed' || conn.connectionState === 'closed') {
        pc = null;
        conn.close();
        useFeed();
      }
    });
    return true;
  }

  // A play() cut short by a new source (AbortError) is not a refusal; anything else asks for a click.
  function resume() {
    video.play().catch((e) => {
      if (!over && (!e || e.name !== 'AbortError')) { play.hidden = false; play.disabled = false; }
    });
  }

  // The fallback: the fMP4 feed through MSE, or the browser's own HLS.
  function useFeed() {
    if (over) return;
    video.srcObject = null;
    if (mse(stream.codecs)) {
      const source = new MediaSource();
      source.addEventListener('sourceopen', () => {
        try { feed(feeder(source, source.addSourceBuffer(mime(stream.codecs)))).catch(end); } catch (e) { end(); }
      }, { once: true });
      video.src = URL.createObjectURL(source);
    } else if (video.canPlayType('application/vnd.apple.mpegurl') !== '') {
      video.src = MASTER;
      watch();
    } else {
      play.hidden = true;
      return say('This browser cannot play the stream. Try Chrome, Edge, Firefox or Safari.');
    }
    resume();
  }

  async function begin() {
    // The element starts playing inside the click, on an empty stream the tracks join, so sound is allowed.
    const media = 'MediaStream' in window ? new MediaStream() : null;
    if (media) {
      video.srcObject = media;
      resume();
      if (await viaRtc(media)) { watch(); return; }
    }
    useFeed();
  }

  function start() {
    if (!stream || over) return;
    play.hidden = true;
    say('');
    video.muted = false;
    if (!begun) {
      begun = true;
      begin();
    } else {
      resume();
    }
  }

  async function wait() {
    while (!stream && !over) {
      try { stream = parseMaster(await get(MASTER)); } catch (e) {
        if (Date.now() - lastOk > GIVE_UP_MS) return end();
      }
      if (!stream) await sleep(POLL_MS);
    }
    if (over) return;
    const native = video.canPlayType('application/vnd.apple.mpegurl') !== '';
    if (!mse(stream.codecs) && !native && !('RTCPeerConnection' in window)) { play.hidden = true; return say('This browser cannot play the stream. Try Chrome, Edge, Firefox or Safari.'); }
    say('');
    play.disabled = false;
  }

  play.addEventListener('click', start);
  video.addEventListener('playing', () => { play.hidden = true; });
  video.addEventListener('waiting', steer);
  video.addEventListener('dblclick', () => {
    if (document.fullscreenElement) document.exitFullscreen().catch(() => {});
    else document.documentElement.requestFullscreen().catch(() => {});
  });
  video.addEventListener('error', () => { if (video.src && !video.srcObject && !over) end(); });
  wait();
})();
)JS";

// The typed path's page at "/" (LinkPair.hpp): a computer that cannot scan the QR opens the short link and types
// the code the phone shows; a right code is answered with the token's path, which the page opens in its place.
inline constexpr std::string_view kPairHtml = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="referrer" content="no-referrer">
<title>BeatRealms 3D</title>
<link rel="stylesheet" href="player.css">
<script src="pair.js" defer></script>
</head>
<body>
<form id="pair" autocomplete="off">
<label for="code">Type the code the phone shows</label>
<input id="code" type="text" maxlength="16" autocapitalize="characters" spellcheck="false" placeholder="XXXX-XXXX" autofocus>
<button id="go" type="submit">Watch</button>
</form>
<p id="note"></p>
</body>
</html>
)HTML";

inline constexpr std::string_view kPairJs = R"JS('use strict';
(() => {
  const form = document.getElementById('pair');
  const code = document.getElementById('code');
  const go = document.getElementById('go');
  const note = document.getElementById('note');
  const PATH = /^\/[0-9a-f]{32}\/$/;
  const say = (text) => { note.textContent = text; };

  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    const typed = code.value.trim();
    if (!typed) return;
    go.disabled = true;
    try {
      const r = await fetch('pair', { method: 'POST', body: typed, cache: 'no-store', credentials: 'omit',
        headers: { 'Content-Type': 'text/plain' } });
      if (r.ok) {
        const path = (await r.text()).trim();
        if (PATH.test(path)) { location.replace(path); return; }
        say('The phone sent an answer this page cannot use.');
      } else if (r.status === 403) {
        say('That code is not right. Type the code the phone shows now.');
      } else if (r.status === 429) {
        say('Too many tries. Wait a minute, then type the code the phone shows now.');
      } else {
        say('The phone is not sharing right now.');
      }
    } catch (err) {
      say('The phone cannot be reached.');
    }
    go.disabled = false;
    code.select();
  });
})();
)JS";

}  // namespace geode::viz::frame::link
