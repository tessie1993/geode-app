// Minimal Chrome DevTools Protocol driver.
//
// Deliberately dependency-free. Playwright is NOT resolvable in this
// environment (`node -e "require('playwright')"` -> MODULE_NOT_FOUND) and
// installing it would pull ~100 MB of node_modules into a repo whose only
// other tooling is a handful of scripts. Node 22 ships a global `WebSocket`
// and `fetch`, which is everything CDP needs, so the whole browser driver is
// the ~120 lines below.
//
// The launch flags are the ones that give a real WebGL2 context with no GPU:
// ANGLE over SwiftShader's Vulkan backend. `--enable-unsafe-swiftshader`
// is required or Chrome refuses the software path for WebGL.

import { spawn } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const DEFAULT_CHROMIUM = process.env.MUSICVIZ_CHROMIUM || '/opt/pw-browsers/chromium';

export class Browser {
  constructor(proc, ws, userDataDir) {
    this.proc = proc;
    this.ws = ws;
    this.userDataDir = userDataDir;
    this.nextId = 0;
    this.waiters = new Map();
    this.consoleLines = [];
    ws.addEventListener('message', (e) => {
      const msg = JSON.parse(e.data);
      if (msg.id != null && this.waiters.has(msg.id)) {
        const { resolve, reject } = this.waiters.get(msg.id);
        this.waiters.delete(msg.id);
        if (msg.error) reject(new Error(`${msg.error.message} (${msg.error.code})`));
        else resolve(msg.result);
        return;
      }
      if (msg.method === 'Runtime.consoleAPICalled') {
        const text = (msg.params.args || [])
          .map((a) => (a.value !== undefined ? a.value : a.description || a.type))
          .join(' ');
        this.consoleLines.push(`${msg.params.type}: ${text}`);
      } else if (msg.method === 'Runtime.exceptionThrown') {
        this.consoleLines.push(`exception: ${msg.params.exceptionDetails.text} ${
          msg.params.exceptionDetails.exception?.description || ''}`);
      }
    });
  }

  send(method, params = {}, sessionId = undefined) {
    const id = ++this.nextId;
    return new Promise((resolve, reject) => {
      this.waiters.set(id, { resolve, reject });
      this.ws.send(JSON.stringify({ id, method, params, sessionId }));
    });
  }

  /** Opens a tab on `fileUrl` and waits for `window.__harnessReady`. */
  async openPage(fileUrl, timeoutMs = 30000) {
    const { targetId } = await this.send('Target.createTarget', { url: 'about:blank' });
    const { sessionId } = await this.send('Target.attachToTarget', { targetId, flatten: true });
    await this.send('Runtime.enable', {}, sessionId);
    await this.send('Page.enable', {}, sessionId);
    await this.send('Page.navigate', { url: fileUrl }, sessionId);
    const deadline = Date.now() + timeoutMs;
    for (;;) {
      const r = await this.send(
        'Runtime.evaluate',
        { expression: 'window.__harnessReady === true', returnByValue: true },
        sessionId,
      );
      if (r.result?.value === true) break;
      if (Date.now() > deadline) throw new Error('harness page never became ready');
      await new Promise((res) => setTimeout(res, 50));
    }
    return new Page(this, sessionId);
  }

  async close() {
    try { this.ws.close(); } catch { /* already gone */ }
    try { this.proc.kill('SIGKILL'); } catch { /* already gone */ }
    try { fs.rmSync(this.userDataDir, { recursive: true, force: true }); } catch { /* best effort */ }
  }
}

export class Page {
  constructor(browser, sessionId) {
    this.browser = browser;
    this.sessionId = sessionId;
  }

  /** Calls `window.<fn>(arg)` in the page and returns its (awaited) result. */
  async call(fn, arg) {
    const expr = `window[${JSON.stringify(fn)}](${JSON.stringify(arg ?? null)})`;
    const r = await this.browser.send(
      'Runtime.evaluate',
      { expression: expr, returnByValue: true, awaitPromise: true },
      this.sessionId,
    );
    if (r.exceptionDetails) {
      const d = r.exceptionDetails;
      throw new Error(`page threw: ${d.exception?.description || d.text}`);
    }
    return r.result.value;
  }

  drainConsole() {
    const lines = this.browser.consoleLines;
    this.browser.consoleLines = [];
    return lines;
  }
}

export async function launch({ chromium = DEFAULT_CHROMIUM, timeoutMs = 30000 } = {}) {
  if (!fs.existsSync(chromium)) {
    throw new Error(
      `chromium not found at ${chromium}. Set MUSICVIZ_CHROMIUM to a Chrome/Chromium binary.`,
    );
  }
  const userDataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'shaderpreview-'));
  const proc = spawn(
    chromium,
    [
      '--headless=new',
      '--no-sandbox',
      '--disable-dev-shm-usage',
      // Software GL. Without --enable-unsafe-swiftshader Chrome blocks WebGL
      // on the software path entirely and getContext('webgl2') returns null.
      '--use-gl=angle',
      '--use-angle=swiftshader',
      '--enable-unsafe-swiftshader',
      '--disable-dbus',
      '--mute-audio',
      '--remote-debugging-port=0',
      `--user-data-dir=${userDataDir}`,
      'about:blank',
    ],
    { stdio: ['ignore', 'ignore', 'pipe'] },
  );
  const stderr = [];
  proc.stderr.on('data', (d) => stderr.push(String(d)));

  const portFile = path.join(userDataDir, 'DevToolsActivePort');
  const deadline = Date.now() + timeoutMs;
  let port = null;
  while (Date.now() < deadline) {
    if (fs.existsSync(portFile)) {
      const first = fs.readFileSync(portFile, 'utf8').split('\n')[0].trim();
      if (first) { port = first; break; }
    }
    await new Promise((r) => setTimeout(r, 50));
  }
  if (!port) {
    proc.kill('SIGKILL');
    throw new Error(`chromium never reported a debugging port.\n${stderr.join('')}`);
  }
  const version = await (await fetch(`http://127.0.0.1:${port}/json/version`)).json();
  const ws = new WebSocket(version.webSocketDebuggerUrl);
  await new Promise((resolve, reject) => {
    ws.addEventListener('open', resolve, { once: true });
    ws.addEventListener('error', () => reject(new Error('CDP websocket failed')), { once: true });
  });
  const browser = new Browser(proc, ws, userDataDir);
  browser.version = version.Browser;
  return browser;
}
