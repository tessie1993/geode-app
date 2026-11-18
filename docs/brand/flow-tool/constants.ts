export const IMAGE_MODELS = ['🍌 Nano Banana Pro', '🍌 Nano Banana 2', '🍌 Nano Banana 2 Lite'] as const;
export const VIDEO_MODELS = ['Veo 3.1 - Fast', 'Veo 3.1 - Quality', 'Veo 3.1 - Lite', 'Omni 1.1 Flash'] as const;
export const IMAGE_ASPECT_RATIOS = ['16:9', '4:3', '1:1', '3:4', '9:16'] as const;
export const VIDEO_ASPECT_RATIOS = ['16:9', '9:16'] as const;
/** The durations every video model above accepts. */
export const VIDEO_DURATIONS = ['4', '6', '8'] as const;
export const IMAGE_COUNTS = ['1', '2', '4'] as const;

export const ASSET_KINDS = ['Hero loop', 'Logo reveal', 'Feature art', 'Logo ideas'] as const;

export const KIND_HINTS: Record<(typeof ASSET_KINDS)[number], string> = {
  'Hero loop': 'Video. The logo is the first and the last frame, so the clip loops without a seam.',
  'Logo reveal': 'Video. Starts in darkness and ends on the logo, like the app’s splash.',
  'Feature art': 'Images for one section of the website. Abstract art, never a fake app screen.',
  'Logo ideas': 'Square images that explore the logo. For comparing only; nothing replaces it.',
};

/** Brand night, the splash's ground: the void frame a Logo reveal starts from. */
export const VOID_HEX = '#06050B';
export const VOID_FRAME_SIZE: Record<(typeof VIDEO_ASPECT_RATIOS)[number], readonly [number, number]> = {
  '16:9': [1280, 720],
  '9:16': [720, 1280],
};

/** docs/brand/README.md: the palette and the neon-glass look of the logo. */
export const BRAND_STYLE =
  'Style: luminous neon light tubes with a glassy glow, premium and crisp. Palette: deep indigo-black background (#0E0620), ' +
  'violet (#7C4DFF), lilac (#C792EA), cyan (#89DDFF) and blue (#82AAFF), with one small amber (#FFCB6B) accent at the focal point. ' +
  'Soft violet bloom and faint particle dust. Generous dark margin around the subject.';

export const NO_TEXT = 'No text, letters or numbers. No watermark. No people, faces or hands. No phones, devices or app screens.';

/** The visuals carry a photosensitivity warning; the brand's own motion must never flash. */
export const SAFE_MOTION =
  'Motion is slow, smooth and continuous, one unbroken shot. No strobing, no flashing, no sudden changes of brightness, no fast cuts.';

export const FEATURES = [
  { label: 'Visuals that listen', scene: 'Sound made visible: neon waveforms and spectrum bars rise out of darkness and bend into a deep 3D tunnel of light.' },
  { label: '334 styles', scene: 'Glowing portals floating in a dark void, each one opening onto a different luminous world: a fractal cathedral, a nebula, a glass sphere, a field of light.' },
  { label: 'Auto mode', scene: 'A slow glide through a landscape of neon light whose form changes from one region to the next, like the sections of a song.' },
  { label: 'Customize', scene: 'Four round control knobs made of neon glass floating above a pool of swirling liquid light that bends where they turn.' },
  { label: 'Music player', scene: 'Floating rounded squares of glowing glass like album covers, orbiting a soft circle of light, over a dark indigo void.' },
  { label: 'Make a video', scene: 'A ribbon of glowing frames unspooling through space, each frame holding a different abstract visual of light.' },
  { label: 'Record', scene: 'A single small amber dot of light at the heart of a slowly turning violet and cyan vortex.' },
  { label: 'Beyond the phone', scene: 'A beam of neon light spreading from a small glowing pane onto a vast wall of light in a dark room.' },
] as const;

export const FEATURE_LABELS = FEATURES.map(feature => feature.label);

export type ImageModel = (typeof IMAGE_MODELS)[number];
export type VideoModel = (typeof VIDEO_MODELS)[number];
export type ImageAspectRatio = (typeof IMAGE_ASPECT_RATIOS)[number];
export type VideoAspectRatio = (typeof VIDEO_ASPECT_RATIOS)[number];
export type VideoDuration = (typeof VIDEO_DURATIONS)[number];
export type ImageCount = (typeof IMAGE_COUNTS)[number];
export type AssetKind = (typeof ASSET_KINDS)[number];
export type FeatureLabel = (typeof FEATURES)[number]['label'];
