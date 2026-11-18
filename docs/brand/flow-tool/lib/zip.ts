import JSZip from 'jszip';
import { Flow } from 'flow-sdk';

export async function downloadZip(files: { name: string; base64: string }[], filename: string): Promise<void> {
  if (files.length === 0) return;
  const zip = new JSZip();
  for (const file of files) zip.file(file.name, file.base64, { base64: true });
  const base64 = await zip.generateAsync({ type: 'base64' });
  await Flow.download({ base64, mimeType: 'application/zip', filename });
}
