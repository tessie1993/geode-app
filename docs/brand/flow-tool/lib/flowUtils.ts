import { Flow } from 'flow-sdk';

/** Host rejects prompts longer than this for generate.image, generate.video and generate.text. */
export const MAX_PROMPT_CHARS = 4000;

/** Returns the trimmed prompt, or throws a readable error before the host rejects it. */
export function checkPrompt(prompt: string): string {
  const trimmed = prompt.trim();
  if (!trimmed) throw new Error('Prompt is empty');
  if (trimmed.length > MAX_PROMPT_CHARS) {
    throw new Error(`Prompt is ${trimmed.length} characters; Flow allows ${MAX_PROMPT_CHARS}`);
  }
  return trimmed;
}

export function toDataUrl(base64: string, mimeType: string): string {
  return `data:${mimeType};base64,${base64}`;
}

export function extensionFor(mimeType: string): string {
  const subtype = (mimeType.split('/')[1] ?? '').split(/[+;]/)[0];
  return subtype === 'jpeg' ? 'jpg' : subtype || 'bin';
}

export function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

export function slugify(text: string, fallback = 'file'): string {
  const slug = text
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '_')
    .replace(/^_+|_+$/g, '')
    .slice(0, 40);
  return slug || fallback;
}

/** Parses a model reply that should be a JSON array, tolerating Markdown code fences. */
export function parseJsonArray(raw: string): unknown[] {
  const parsed: unknown = JSON.parse(raw.replace(/`{3}(?:json)?/gi, '').trim());
  if (!Array.isArray(parsed)) throw new Error('Expected a JSON array');
  return parsed;
}

export async function downloadMedia(media: { base64: string; mimeType: string }, baseName: string): Promise<void> {
  await Flow.download({
    base64: media.base64,
    mimeType: media.mimeType,
    filename: `${slugify(baseName)}.${extensionFor(media.mimeType)}`,
  });
}
