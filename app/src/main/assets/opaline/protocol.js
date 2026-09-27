// Pure boundary between native authoritative UI and the decorative 3D scene.
// Coordinates are physical viewport pixels; they never depend on WebView DPR.
export const MAX_PARTS = 96;
const finite = value => typeof value === 'number' && Number.isFinite(value);
export const clamp01 = value => finite(value) ? Math.max(0, Math.min(1, value)) : 0.5;
export function sanitizeSnapshot(input) {
  const width = finite(input?.width) && input.width > 0 ? input.width : 1;
  const height = finite(input?.height) && input.height > 0 ? input.height : 1;
  const seen = new Set();
  const parts = [];
  for (const source of (Array.isArray(input?.parts) ? input.parts : [])) {
    if (parts.length >= MAX_PARTS) break;
    if (typeof source.id !== 'string' || source.id.length > 80 || seen.has(source.id)) continue;
    if (!/^[ABCDEFJN]\d{2}$/.test(source.element)) continue;
    if (![source.x, source.y, source.width, source.height].every(finite)) continue;
    if (source.width <= 0 || source.height <= 0 || source.x >= width || source.y >= height ||
        source.x + source.width <= 0 || source.y + source.height <= 0) continue;
    seen.add(source.id);
    parts.push({...source, value: clamp01(source.value), selected: source.selected === true,
      enabled: source.enabled !== false});
  }
  return {width, height, parts, reducedMotion: input?.reducedMotion === true,
    active: input?.active !== false, section: String(input?.section || 'player').slice(0, 40)};
}

// Camera is +Z looking at the origin, keeping native text and real geometry aligned.
export function projectBounds(part, viewport, fov = 38, cameraZ = 16, depth = 0) {
  const worldHeight = 2 * (cameraZ - depth) * Math.tan(fov * Math.PI / 360);
  const scale = worldHeight / viewport.height;
  return {x: (part.x + part.width / 2 - viewport.width / 2) * scale,
    y: (viewport.height / 2 - part.y - part.height / 2) * scale,
    width: part.width * scale, height: part.height * scale,
    thickness: Math.max(0.06, Math.min(0.24, Math.min(part.width, part.height) * scale * 0.22))};
}

export function frameBudget(dt, reducedMotion, active) {
  if (!active || reducedMotion || !finite(dt) || dt <= 0) return 0;
  // Suspension never becomes a long physics backlog on the main UI thread.
  return Math.min(dt, 1 / 30);
}
