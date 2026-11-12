import { BRAND_STYLE, FEATURES, NO_TEXT, SAFE_MOTION } from '../constants';
import type { Settings } from '../types';

const LOGO = 'this neon eye logo (an eye made of sound: rippling oscilloscope eyelids, spectrum bars, a twisted-hexagon vortex iris and an amber hexagon pupil)';

function withExtra(prompt: string, extra: string): string {
  const direction = extra.trim();
  return direction ? `${prompt}\nAdditional direction: ${direction}` : prompt;
}

/** The exact prompt a run sends, shown in the panel before anything is spent. */
export function buildPrompt(settings: Settings): string {
  switch (settings.kind) {
    case 'Hero loop':
      return withExtra(
        `Animate ${LOGO}. The iris turns slowly, the eyelids ripple gently like a sound wave, the spectrum bars rise and fall softly ` +
          `and violet light breathes around the eye. The camera stays still and the eye stays centred. The last frame matches the first ` +
          `so the clip loops seamlessly.\n${BRAND_STYLE}\n${SAFE_MOTION}\n${NO_TEXT}`,
        settings.extra,
      );
    case 'Logo reveal':
      return withExtra(
        `From darkness, violet smoke drifts in and threads of cyan and violet light converge from the edges, assembling into ${LOGO}. ` +
          `The amber pupil lights up softly last, and the shot settles on the logo, still and centred.\n${BRAND_STYLE}\n${SAFE_MOTION}\n${NO_TEXT}`,
        settings.extra,
      );
    case 'Feature art': {
      const feature = FEATURES.find(item => item.label === settings.feature) ?? FEATURES[0];
      return withExtra(
        `Website illustration for a music visualizer app. ${feature.scene} Match the glow and palette of the reference logo; do not ` +
          `draw the logo itself.\n${BRAND_STYLE}\n${NO_TEXT}`,
        settings.extra,
      );
    }
    case 'Logo ideas':
      return withExtra(
        `A fresh rendering of ${LOGO}, as square app icon artwork. Keep every element of the reference and its symmetry; explore the ` +
          `lighting, glass and depth. Centred, front view, generous empty margin, no frame or border.\n${BRAND_STYLE}\n${NO_TEXT}`,
        settings.extra,
      );
  }
}

export function taskLabel(settings: Settings): string {
  return settings.kind === 'Feature art' ? `${settings.kind} · ${settings.feature}` : settings.kind;
}
