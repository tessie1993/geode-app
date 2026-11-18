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
