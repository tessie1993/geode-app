Build this tool from the files below. Create each file at exactly the given path with exactly the given content. Do not rename, reformat, merge or add files, and do not change prompts, model names or class names. When you are done, list the files you created.

--- FILE: App.tsx ---
import { useState } from 'react';
import { Flow } from 'flow-sdk';
import { ResultCard } from './components/ResultCard';
import { Icon, IconButton, PillButton } from './components/Primitives';
import { SettingsPanel } from './components/SettingsPanel';
import { FEATURE_LABELS, IMAGE_MODELS, VIDEO_MODELS } from './constants';
import { downloadMedia, errorMessage, extensionFor, slugify } from './lib/flowUtils';
import { useGlobalStyles } from './lib/globalStyles';
import { isVideoKind, makeJob, runJob } from './lib/jobs';
import { buildPrompt, taskLabel } from './lib/prompts';
import { downloadZip } from './lib/zip';
import type { MediaResult, ProjectMedia, Settings, Task } from './types';

type FinishedTask = Task & { result: MediaResult };

const isFinished = (task: Task): task is FinishedTask => Boolean(task.result);

const DEFAULT_SETTINGS: Settings = {
  kind: 'Hero loop',
  feature: FEATURE_LABELS[0],
  extra: '',
  imageModel: IMAGE_MODELS[0],
  videoModel: VIDEO_MODELS[0],
  imageAspect: '16:9',
  videoAspect: '16:9',
  duration: '8',
  count: '2',
};

export default function App() {
  useGlobalStyles();

  const [settings, setSettings] = useState<Settings>(DEFAULT_SETTINGS);
  const [logo, setLogo] = useState<ProjectMedia | null>(null);
  const [tasks, setTasks] = useState<Task[]>([]);
  const [isGenerating, setIsGenerating] = useState(false);
  const [notice, setNotice] = useState<string | null>(null);

  const prompt = buildPrompt(settings);
  const runs = isVideoKind(settings.kind) ? 1 : Number(settings.count);
  const runLabel = isVideoKind(settings.kind) ? 'Generate 1 video' : `Generate ${runs} image${runs === 1 ? '' : 's'}`;
  const finishedTasks = tasks.filter(isFinished);

  const updateSettings = (patch: Partial<Settings>) => setSettings(previous => ({ ...previous, ...patch }));

  const updateTask = (id: string, patch: Partial<Task>) => {
    setTasks(previous => previous.map(task => (task.id === id ? { ...task, ...patch } : task)));
  };

  const chooseLogo = async () => {
    try {
      const media = await Flow.media.select({ filter: 'image' });
      if (media) setLogo(media);
    } catch (error) {
      setNotice(errorMessage(error));
    }
  };

  const runTask = async (task: Task) => {
    updateTask(task.id, { status: 'generating', error: undefined });
    try {
      const result = await runJob(task.job);
      updateTask(task.id, { status: 'done', result });
    } catch (error) {
      updateTask(task.id, { status: 'error', error: errorMessage(error) });
    }
  };

  const generate = async () => {
    if (isGenerating) return;
    setNotice(null);
    setIsGenerating(true);
    try {
      const job = await makeJob(settings, prompt, logo?.mediaId);
      const label = taskLabel(settings);
      const batch: Task[] = Array.from({ length: runs }, (_, index) => ({
        id: `${Date.now()}-${index}`,
        label,
        job,
        status: 'generating',
      }));
      setTasks(previous => [...batch, ...previous]);
      for (const task of batch) await runTask(task); // one request at a time
    } catch (error) {
      setNotice(errorMessage(error));
    } finally {
      setIsGenerating(false);
    }
  };

  const downloadTask = async (task: Task) => {
    if (!task.result) return;
    try {
      await downloadMedia(task.result, `beatrealms_${task.label}_${task.id}`);
    } catch (error) {
      setNotice(errorMessage(error));
    }
  };

  const exportAll = async () => {
    const files = finishedTasks.map((task, index) => ({
      name: `${String(index + 1).padStart(2, '0')}_${slugify(task.label)}.${extensionFor(task.result.mimeType)}`,
      base64: task.result.base64,
    }));
    try {
      await downloadZip(files, 'beatrealms_flow_assets.zip');
    } catch (error) {
      setNotice(errorMessage(error));
    }
  };

  return (
    <div className="flex h-screen w-screen overflow-hidden bg-[#0e0e0e] text-white">
      <aside className="flex h-full w-[300px] shrink-0 flex-col justify-between gap-4 border-r border-[rgba(218,220,224,0.15)] px-[10px] py-[12px]">
        <div className="dark-scrollbar flex min-h-0 flex-col gap-6 overflow-y-auto">
          <SettingsPanel
            settings={settings}
            onChange={updateSettings}
            logo={logo}
            onChooseLogo={chooseLogo}
            onClearLogo={() => setLogo(null)}
            prompt={prompt}
            disabled={isGenerating}
          />
          {notice && <p className="px-2 text-[11px] font-medium text-red-400/80">{notice}</p>}
        </div>

        <div className="flex flex-col gap-2 border-t border-white/5 pt-4">
          <PillButton variant="solid" icon={<Icon name="arrow_forward" />} onClick={generate} disabled={isGenerating}>
            {isGenerating ? 'Generating…' : runLabel}
          </PillButton>
          <div className="flex gap-2">
            <PillButton variant="outline" icon={<Icon name="download" />} onClick={exportAll} disabled={finishedTasks.length === 0} className="flex-1">
              Export all
            </PillButton>
            <IconButton icon="refresh" label="Clear results" onClick={() => setTasks([])} disabled={isGenerating} />
          </div>
        </div>
      </aside>

      <main className="relative min-w-0 flex-1">
        <div className="dark-scrollbar absolute inset-0 overflow-y-auto p-8">
          {tasks.length === 0 ? (
            <div className="pointer-events-none flex h-full flex-col items-center justify-center opacity-10">
              <Icon name="visibility" size={120} />
              <h2 className="mt-4 text-2xl font-light uppercase tracking-[8px]">BeatRealms Studio</h2>
            </div>
          ) : (
            <div className="mx-auto grid max-w-[1600px] grid-cols-1 gap-6 pb-12 sm:grid-cols-2 lg:grid-cols-3">
              {tasks.map(task => (
                <ResultCard key={task.id} task={task} onDownload={() => downloadTask(task)} onRetry={() => runTask(task)} />
              ))}
            </div>
          )}
        </div>
      </main>
    </div>
  );
}
--- END FILE ---

--- FILE: types.ts ---
import type {
  AssetKind,
  FeatureLabel,
  ImageAspectRatio,
  ImageCount,
  ImageModel,
  VideoAspectRatio,
  VideoDuration,
  VideoModel,
} from './constants';

export type TaskStatus = 'generating' | 'done' | 'error';

export interface MediaResult {
  mediaId: string;
  base64: string;
  mimeType: string;
}

export interface ProjectMedia extends MediaResult {
  type: 'image' | 'video' | 'audio';
  name: string;
}

export interface Settings {
  kind: AssetKind;
  feature: FeatureLabel;
  extra: string;
  imageModel: ImageModel;
  videoModel: VideoModel;
  imageAspect: ImageAspectRatio;
  videoAspect: VideoAspectRatio;
  duration: VideoDuration;
  count: ImageCount;
}

export interface ImageJob {
  type: 'image';
  prompt: string;
  model: ImageModel;
  aspectRatio: ImageAspectRatio;
  referenceMediaIds: string[];
}

export interface VideoJob {
  type: 'video';
  prompt: string;
  model: VideoModel;
  aspectRatio: VideoAspectRatio;
  durationSeconds: number;
  firstFrameMediaId: string;
  lastFrameMediaId: string;
}

export type Job = ImageJob | VideoJob;

/** Everything a run needs is on the task, so Retry repeats it exactly. */
export interface Task {
  id: string;
  label: string;
  job: Job;
  status: TaskStatus;
  result?: MediaResult;
  error?: string;
}
--- END FILE ---

--- FILE: constants.ts ---
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
--- END FILE ---

--- FILE: components/Primitives.tsx ---
import type { ReactNode } from 'react';

const LABEL_CLASS = 'text-[11px] font-medium tracking-[0.1px] text-[rgba(255,255,255,0.35)]';
const FIELD_CLASS =
  'flex flex-col gap-0.5 rounded-xl border border-[#595959] pb-2 pl-2.5 pr-2 pt-[5px] focus-within:border-[#7a7a7a]';
const VALUE_CLASS = 'bg-transparent text-[11px] font-medium tracking-[0.1px] text-white outline-none';

export function Icon({ name, size = 18, className = '' }: { name: string; size?: number; className?: string }) {
  return (
    <span aria-hidden="true" className={`material-symbols-outlined leading-none ${className}`} style={{ fontSize: size }}>
      {name}
    </span>
  );
}

export function SectionLabel({ children }: { children: ReactNode }) {
  return (
    <div className="flex items-center px-2">
      <span className="text-[11px] font-medium normal-case tracking-[0.1px] text-[rgba(218,220,224,0.9)]">{children}</span>
    </div>
  );
}

type ButtonVariant = 'filled' | 'outline' | 'solid';

const BUTTON_BASE =
  'flex h-[34px] w-full cursor-pointer select-none items-center justify-center gap-[2px] rounded-xl font-medium tracking-[0.1px] transition-all disabled:cursor-not-allowed disabled:opacity-50';

const BUTTON_VARIANTS: Record<ButtonVariant, string> = {
  filled: 'bg-[#969696] py-1 pl-[8px] pr-[24px] text-[11px] text-black hover:bg-[#a6a6a6] active:bg-[#868686]',
  outline:
    'border border-[#595959] py-2 pl-[8px] pr-[16px] text-[12px] text-white backdrop-blur-[40px] hover:bg-white/5 active:bg-white/10',
  solid: 'bg-white py-2 pl-[8px] pr-[16px] text-[12px] text-black hover:bg-gray-200 active:bg-gray-300',
};

interface PillButtonProps {
  children: ReactNode;
  icon?: ReactNode;
  variant?: ButtonVariant;
  onClick?: () => void;
  disabled?: boolean;
  className?: string;
}

export function PillButton({ children, icon, variant = 'filled', onClick, disabled, className = '' }: PillButtonProps) {
  return (
    <button
      type="button"
      className={`${BUTTON_BASE} ${BUTTON_VARIANTS[variant]} ${className}`}
      onClick={onClick}
      disabled={disabled}
    >
      {icon && <span className="flex h-6 w-6 items-center justify-center">{icon}</span>}
      <span>{children}</span>
    </button>
  );
}

interface IconButtonProps {
  icon: string;
  label: string;
  onClick?: () => void;
  disabled?: boolean;
}

export function IconButton({ icon, label, onClick, disabled }: IconButtonProps) {
  return (
    <button
      type="button"
      aria-label={label}
      title={label}
      onClick={onClick}
      disabled={disabled}
      className="flex h-[34px] w-12 shrink-0 cursor-pointer items-center justify-center rounded-xl border border-[#595959] text-white transition-all hover:bg-white/5 active:bg-white/10 disabled:cursor-not-allowed disabled:opacity-50"
    >
      <Icon name={icon} />
    </button>
  );
}

interface TextAreaFieldProps {
  label: string;
  value: string;
  onChange: (value: string) => void;
  placeholder?: string;
  rows?: number;
  maxLength?: number;
  disabled?: boolean;
}

export function TextAreaField({ label, value, onChange, placeholder, rows = 3, maxLength, disabled }: TextAreaFieldProps) {
  return (
    <label className={FIELD_CLASS}>
      <span className={LABEL_CLASS}>{label}</span>
      <textarea
        className={`${VALUE_CLASS} dark-scrollbar resize-none placeholder:text-white/30`}
        value={value}
        rows={rows}
        maxLength={maxLength}
        placeholder={placeholder}
        disabled={disabled}
        onChange={event => onChange(event.target.value)}
      />
    </label>
  );
}

interface ChoiceProps<T extends string> {
  label: string;
  value: T;
  options: readonly T[];
  onChange: (value: T) => void;
  disabled?: boolean;
}

export function SelectField<T extends string>({ label, value, options, onChange, disabled }: ChoiceProps<T>) {
  return (
    <label className={FIELD_CLASS}>
      <span className={LABEL_CLASS}>{label}</span>
      <select
        className={`${VALUE_CLASS} cursor-pointer`}
        value={value}
        disabled={disabled}
        onChange={event => {
          const next = options.find(option => option === event.target.value);
          if (next) onChange(next);
        }}
      >
        {options.map(option => (
          <option key={option} value={option} className="bg-[#1a1a1a]">
            {option}
          </option>
        ))}
      </select>
    </label>
  );
}

export function SegmentedControl<T extends string>({ label, value, options, onChange, disabled }: ChoiceProps<T>) {
  return (
    <div className="flex flex-col gap-1 rounded-xl border border-[#595959] p-1.5 pt-[5px]">
      <span className={`${LABEL_CLASS} px-1`}>{label}</span>
      <div role="radiogroup" aria-label={label} className="flex gap-1">
        {options.map(option => (
          <button
            key={option}
            type="button"
            role="radio"
            aria-checked={option === value}
            disabled={disabled}
            onClick={() => onChange(option)}
            className={`h-7 flex-1 rounded-lg text-[11px] font-medium transition-colors disabled:opacity-50 ${
              option === value ? 'bg-white text-black' : 'text-white/60 hover:bg-white/5'
            }`}
          >
            {option}
          </button>
        ))}
      </div>
    </div>
  );
}
--- END FILE ---

--- FILE: components/SettingsPanel.tsx ---
import {
  ASSET_KINDS,
  FEATURE_LABELS,
  IMAGE_ASPECT_RATIOS,
  IMAGE_COUNTS,
  IMAGE_MODELS,
  KIND_HINTS,
  VIDEO_ASPECT_RATIOS,
  VIDEO_DURATIONS,
  VIDEO_MODELS,
} from '../constants';
import { isVideoKind, needsLogo } from '../lib/jobs';
import type { ProjectMedia, Settings } from '../types';
import { Icon, IconButton, PillButton, SectionLabel, SegmentedControl, SelectField, TextAreaField } from './Primitives';

const EXTRA_MAX_CHARS = 600;

interface SettingsPanelProps {
  settings: Settings;
  onChange: (patch: Partial<Settings>) => void;
  logo: ProjectMedia | null;
  onChooseLogo: () => void;
  onClearLogo: () => void;
  prompt: string;
  disabled: boolean;
}

export function SettingsPanel({ settings, onChange, logo, onChooseLogo, onClearLogo, prompt, disabled }: SettingsPanelProps) {
  const video = isVideoKind(settings.kind);

  return (
    <div className="flex flex-col gap-6">
      <section className="flex flex-col gap-2">
        <SectionLabel>Asset</SectionLabel>
        <SelectField label="What to make" value={settings.kind} options={ASSET_KINDS} onChange={kind => onChange({ kind })} disabled={disabled} />
        <p className="px-2 text-[11px] leading-snug text-white/45">{KIND_HINTS[settings.kind]}</p>
        {settings.kind === 'Feature art' && (
          <SelectField label="Website section" value={settings.feature} options={FEATURE_LABELS} onChange={feature => onChange({ feature })} disabled={disabled} />
        )}
      </section>

      <section className="flex flex-col gap-2">
        <SectionLabel>{needsLogo(settings.kind) ? 'Logo (required)' : 'Logo, for the look (optional)'}</SectionLabel>
        <div className="flex gap-2">
          <PillButton variant="outline" icon={<Icon name="image" />} onClick={onChooseLogo} disabled={disabled} className="flex-1">
            {logo ? logo.name || 'Logo selected' : 'Choose from project'}
          </PillButton>
          {logo && <IconButton icon="close" label="Remove logo" onClick={onClearLogo} disabled={disabled} />}
        </div>
      </section>

      <section className="flex flex-col gap-2">
        <SectionLabel>Settings</SectionLabel>
        {video ? (
          <>
            <SelectField label="Video model" value={settings.videoModel} options={VIDEO_MODELS} onChange={videoModel => onChange({ videoModel })} disabled={disabled} />
            <SegmentedControl label="Aspect ratio" value={settings.videoAspect} options={VIDEO_ASPECT_RATIOS} onChange={videoAspect => onChange({ videoAspect })} disabled={disabled} />
            <SegmentedControl label="Seconds" value={settings.duration} options={VIDEO_DURATIONS} onChange={duration => onChange({ duration })} disabled={disabled} />
          </>
        ) : (
          <>
            <SelectField label="Image model" value={settings.imageModel} options={IMAGE_MODELS} onChange={imageModel => onChange({ imageModel })} disabled={disabled} />
            {settings.kind === 'Feature art' && (
              <SegmentedControl label="Aspect ratio" value={settings.imageAspect} options={IMAGE_ASPECT_RATIOS} onChange={imageAspect => onChange({ imageAspect })} disabled={disabled} />
            )}
            <SegmentedControl label="Images per run" value={settings.count} options={IMAGE_COUNTS} onChange={count => onChange({ count })} disabled={disabled} />
          </>
        )}
      </section>

      <section className="flex flex-col gap-2">
        <SectionLabel>Direction</SectionLabel>
        <TextAreaField
          label={`Extra direction (optional) · ${settings.extra.length}/${EXTRA_MAX_CHARS}`}
          value={settings.extra}
          onChange={extra => onChange({ extra })}
          maxLength={EXTRA_MAX_CHARS}
          rows={3}
          placeholder="e.g. more cyan, slower turn"
          disabled={disabled}
        />
        <details className="rounded-xl border border-[#595959] px-2.5 py-2">
          <summary className="cursor-pointer text-[11px] font-medium text-white/60">Prompt that will be sent ({prompt.length} chars)</summary>
          <p className="dark-scrollbar mt-2 max-h-48 overflow-y-auto whitespace-pre-wrap text-[11px] leading-snug text-white/50">{prompt}</p>
        </details>
      </section>
    </div>
  );
}
--- END FILE ---

--- FILE: components/ResultCard.tsx ---
import { toDataUrl } from '../lib/flowUtils';
import type { Task } from '../types';
import { Icon } from './Primitives';

interface ResultCardProps {
  task: Task;
  onDownload: () => void;
  onRetry: () => void;
}

export function ResultCard({ task, onDownload, onRetry }: ResultCardProps) {
  const url = task.result ? toDataUrl(task.result.base64, task.result.mimeType) : '';
  const isVideo = task.result?.mimeType.startsWith('video/') ?? task.job.type === 'video';

  return (
    <figure className="animate-fade-in m-0 flex flex-col gap-2">
      <div
        className="group relative flex items-center justify-center overflow-hidden rounded-2xl border border-[#595959] bg-[#1a1a1a] transition-colors hover:border-[#7a7a7a]"
        style={{ aspectRatio: task.job.aspectRatio.replace(':', ' / ') }}
      >
        {task.status === 'generating' && (
          <div className="flex flex-col items-center gap-4">
            <div className="h-12 w-12 animate-spin rounded-full border-4 border-[#969696]/20 border-t-[#969696]" />
            <span className="text-[11px] font-medium uppercase tracking-wider text-white/50">
              {task.job.type === 'video' ? 'Generating video…' : 'Generating…'}
            </span>
          </div>
        )}

        {task.status === 'error' && (
          <div className="flex flex-col items-center gap-2 p-4 text-center">
            <Icon name="error" size={32} className="text-red-400" />
            <span className="text-[11px] font-medium text-red-400/80">{task.error ?? 'Generation failed'}</span>
            <button type="button" onClick={onRetry} className="text-[11px] font-medium text-white/70 underline hover:text-white">
              Retry
            </button>
          </div>
        )}

        {task.status === 'done' && task.result && (
          <>
            {isVideo ? (
              <video src={url} className="h-full w-full object-cover" autoPlay muted loop playsInline controls />
            ) : (
              <img src={url} alt={task.label} className="h-full w-full object-cover" />
            )}
            <button
              type="button"
              onClick={onDownload}
              title="Download"
              aria-label="Download"
              className="absolute right-3 top-3 rounded-full bg-white p-2 text-black opacity-0 shadow-lg transition-opacity hover:bg-gray-200 focus:opacity-100 group-hover:opacity-100"
            >
              <Icon name="download" size={20} />
            </button>
          </>
        )}
      </div>
      <figcaption className="px-1 text-[11px] font-medium tracking-[0.1px] text-white/50">{task.label}</figcaption>
    </figure>
  );
}
--- END FILE ---

--- FILE: lib/flowUtils.ts ---
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
--- END FILE ---

--- FILE: lib/globalStyles.ts ---
import { useEffect } from 'react';

const STYLE_ID = 'flow-tool-global-styles';

const GLOBAL_CSS = `
  html, body, #root { margin: 0; padding: 0; width: 100%; height: 100%; background: #0e0e0e; color: white; overflow: hidden; font-family: 'Google Sans Text', sans-serif; }
  .dark-scrollbar { scrollbar-width: thin; scrollbar-color: #595959 transparent; }
  .dark-scrollbar::-webkit-scrollbar { width: 6px; }
  .dark-scrollbar::-webkit-scrollbar-track { background: transparent; }
  .dark-scrollbar::-webkit-scrollbar-thumb { background: #595959; border-radius: 9999px; }
  @keyframes fade-in { from { opacity: 0; transform: translateY(10px); } to { opacity: 1; transform: translateY(0); } }
  .animate-fade-in { animation: fade-in 0.4s ease-out forwards; }
`;

export function useGlobalStyles(): void {
  useEffect(() => {
    if (document.getElementById(STYLE_ID)) return;
    const style = document.createElement('style');
    style.id = STYLE_ID;
    style.textContent = GLOBAL_CSS;
    document.head.appendChild(style);
  }, []);
}
--- END FILE ---

--- FILE: lib/jobs.ts ---
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
--- END FILE ---

--- FILE: lib/prompts.ts ---
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
--- END FILE ---

--- FILE: lib/zip.ts ---
import JSZip from 'jszip';
import { Flow } from 'flow-sdk';

export async function downloadZip(files: { name: string; base64: string }[], filename: string): Promise<void> {
  if (files.length === 0) return;
  const zip = new JSZip();
  for (const file of files) zip.file(file.name, file.base64, { base64: true });
  const base64 = await zip.generateAsync({ type: 'base64' });
  await Flow.download({ base64, mimeType: 'application/zip', filename });
}
--- END FILE ---
