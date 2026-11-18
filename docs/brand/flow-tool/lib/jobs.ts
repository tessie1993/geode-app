import { Flow, type GenerateResult } from 'flow-sdk';
import { VOID_FRAME_SIZE, VOID_HEX, type VideoAspectRatio } from '../constants';
import type { Job, Settings } from '../types';
import { checkPrompt } from './flowUtils';

export function isVideoKind(kind: Settings['kind']): boolean {
  return kind === 'Hero loop' || kind === 'Logo reveal';
}

/** Everything but Feature art is built from the logo; Feature art only borrows its look. */
export function needsLogo(kind: Settings['kind']): boolean {
  return kind !== 'Feature art';
}

/** A frame of brand night, uploaded silently: the darkness a Logo reveal starts from. */
async function uploadVoidFrame(aspect: VideoAspectRatio): Promise<string> {
  const [width, height] = VOID_FRAME_SIZE[aspect];
  const canvas = document.createElement('canvas');
  canvas.width = width;
  canvas.height = height;
  const context = canvas.getContext('2d');
  if (!context) throw new Error('Canvas is not available in this browser');
  context.fillStyle = VOID_HEX;
  context.fillRect(0, 0, width, height);
  const base64 = canvas.toDataURL('image/png').split(',')[1] ?? '';
  const { mediaId } = await Flow.upload({ base64, mimeType: 'image/png', name: 'BeatRealms void frame' });
  return mediaId;
}

/** Turns the panel's settings into one run's inputs. A Logo reveal uploads its dark first frame here. */
export async function makeJob(settings: Settings, prompt: string, logoMediaId: string | undefined): Promise<Job> {
  const checked = checkPrompt(prompt);
  if (needsLogo(settings.kind) && !logoMediaId) throw new Error('Choose the logo from your project first');

  if (settings.kind === 'Hero loop' || settings.kind === 'Logo reveal') {
    const logo = logoMediaId ?? '';
    const first = settings.kind === 'Hero loop' ? logo : await uploadVoidFrame(settings.videoAspect);
    return {
      type: 'video',
      prompt: checked,
      model: settings.videoModel,
      aspectRatio: settings.videoAspect,
      durationSeconds: Number(settings.duration),
      firstFrameMediaId: first,
      lastFrameMediaId: logo,
    };
  }

  return {
    type: 'image',
    prompt: checked,
    model: settings.imageModel,
    aspectRatio: settings.kind === 'Logo ideas' ? '1:1' : settings.imageAspect,
    referenceMediaIds: logoMediaId ? [logoMediaId] : [],
  };
}

export function runJob(job: Job): Promise<GenerateResult> {
  if (job.type === 'image') {
    return Flow.generate.image({
      prompt: job.prompt,
      modelDisplayName: job.model,
      aspectRatio: job.aspectRatio,
      referenceImageMediaIds: job.referenceMediaIds.length > 0 ? job.referenceMediaIds : undefined,
    });
  }
  return Flow.generate.video({
    prompt: job.prompt,
    modelDisplayName: job.model,
    aspectRatio: job.aspectRatio,
    durationSeconds: job.durationSeconds,
    firstFrameImageMediaId: job.firstFrameMediaId,
    lastFrameImageMediaId: job.lastFrameMediaId,
  });
}
